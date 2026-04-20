# 从 M5Paper 移植到 ESP32-S3-RLCD-4.2 的调试记录

> 2026-04-21 · 记录将 Paper Buddy 从 M5Paper V1.1 (Arduino/PlatformIO) 移植到 Waveshare ESP32-S3-RLCD-4.2 (ESP-IDF/CMake) 的全过程，重点是显示驱动的调试。

---

## 1. 起点

M5Paper V1.1 的固件写好了，功能完整。目标是移植到一块新硬件：Waveshare ESP32-S3-RLCD-4.2，核心差异：

- **框架**：Arduino/PlatformIO → ESP-IDF/CMake
- **显示**：540×960 16灰阶 e-ink (IT8951) → 400×300 1-bit RLCD (ST7305)
- **输入**：3 键 + 电容触屏 → 2 键 (KEY + BOOT)
- **连接**：UART serial → USB-serial-jtag

bridge daemon (Python) 不需要改动，它只管发 JSON 行。

---

## 2. 代码架构

复用了 M5Paper 的状态结构 (`TamaState`)、协议解析 (`state_parser`)、命令处理 (`xfer_commands`)、统计 (`stats`)、ASCII 猫帧 (`buddy_frames`)。新写的部分：

- **显示驱动** (`display/lcd_driver.h/.cc`)：SPI 总线初始化、ST7305 初始化序列、LVGL flush callback、1-bit 像素地址计算
- **UI** (`display/buddy_display.h/.cc`)：LVGL 9 控件搭建仪表盘和审批卡片
- **按键** (`button/button_handler.h/.cc`)：ESP-IDF button 组件 + FreeRTOS event group
- **传输** (`transport/`)：USB-serial-jtag 驱动
- **协议** (`protocol/line_protocol.h/.cc`)：行缓冲 JSON 解析器

代码量约 2000 行，分散在 21 个源文件中。

---

## 3. 踩坑记录

### 3.1 主任务栈溢出（致命，白屏根因）

**现象**：烧录后屏幕全白。串口日志显示 `Initializing LVGL` 后立即 stack overflow，然后无限重启。

**根因**：ESP-IDF 默认主任务栈约 3.5KB。`lv_init()` + `lvgl_port_init()` 需要远超这个大小的栈空间。

**修复**：

```
# sdkconfig.defaults
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
```

**教训**：ESP-IDF 和 Arduino 的内存模型完全不同。Arduino 框架下 `setup()/loop()` 的栈空间通常不是问题，但 ESP-IDF 的 `app_main()` 运行在 FreeRTOS main task 上，默认栈很小。任何重度初始化（LVGL、BLE、WiFi）都要显式增大栈。

**发现过程**：之前一直以为是显示驱动的问题（像素地址计算错误、SPI 时序不对、ST7305 初始化序列不对），反复调试显示管线。实际上固件在 LVGL 初始化阶段就 crash 了，根本没走到显示刷新那一步。crash 发生在 USB-serial-jtag 被接管之前，所以串口日志只能看到早期的几行。后来读到串口输出才定位到 stack overflow。

### 3.2 ST7305 像素地址计算

**背景**：ST7305 是 1-bit 面板，但不是简单的行优先 1-bit 打包。每个字节包含一个 2-wide × 4-tall 像素块中的 8 个像素。

**公式**（400×300 landscape 模式）：

```
byte_index = (x/2) * (H/4) + ((H-1-y)/4)
bit_pos    = 7 - (((H-1-y)%4)*2 + x%2)
```

Waveshare 官方驱动用 360KB 查找表（LUT）做这个映射。为了节省 PSRAM，改为 inline 函数直接计算。

**验证**：在 LVGL 接管之前，手动画三条水平黑条到 frame buffer，调用 `RLCD_Display()`，屏幕上确实出现三条黑条。证明 SPI 通信、ST7305 初始化序列、像素地址计算全部正确。

### 3.3 LVGL 颜色格式选择

**尝试 1 — I1 (1-bit)**：LVGL v9 的 `LV_COLOR_FORMAT_I1` 看起来和 ST7305 天然匹配。但实际上 LVGL 的 I1 布局和 ST7305 的布局完全不同，`FlushCb` 收到的像素数据无法直接发送到屏幕。

**尝试 2 — RGB565（最终方案）**：使用 `LV_COLOR_FORMAT_RGB565`，在 `FlushCb` 中逐像素转换为 1-bit（阈值 0x7FFF）。这和 Waveshare 官方驱动的做法完全一致。

```cpp
void FlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p) {
    uint16_t* buffer = reinterpret_cast<uint16_t*>(color_p);
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint8_t color = (*buffer < 0x7FFF) ? 0 : 1;
            SetPixel(disp_buffer_, x, y, color);
            buffer++;
        }
    }
    RLCD_Display();           // 发送整个 15000 字节 frame buffer
    lv_disp_flush_ready(disp);
}
```

**代价**：RGB565 buffer 需要 400×300×2 = 240KB (PSRAM)。1-bit buffer 15KB。总计约 255KB。8MB PSRAM 下完全可接受。

**性能**：每次 flush 都遍历脏区域所有像素做 RGB565→1-bit 转换，然后发送整个 15000 字节 buffer。PARTIAL 模式下 LVGL 只刷新变化的区域，实际转换的像素量不大。RLCD 物理刷新约 450ms，这是瓶颈所在。

### 3.4 ESP-IDF button 组件 API

**问题**：ESP-IDF button 组件 (v3.5.0) 的 API 和文档示例有差异。

**踩坑**：
- `iot_button_new_gpio_device()` 不存在，要用 `iot_button_create()` + `BUTTON_TYPE_GPIO`
- `button_config_t` 是 union-based 结构体，不是嵌套 struct
- `iot_button_register_cb()` 在 v3.5.0 中是 4 个参数（少了 `user_data` 那个... 实际上第 4 个参数就是 user_data）

**正确写法**：

```cpp
button_config_t cfg = {
    .type = BUTTON_TYPE_GPIO,
    .long_press_time = 0,
    .short_press_time = BUTTON_DEBOUNCE_MS,
    .gpio_button_config = {
        .gpio_num = GPIO_NUM_18,
        .active_level = 0,
        .disable_pull = false,
    },
};
button_handle_t btn = iot_button_create(&cfg);
iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, OnPressed, event_group);
```

### 3.5 `esp_lcd_panel_io_tx_param` 的 data 参数

**细节**：发送数据字节时，register 地址传 `-1`（即 `0xFF`，因为 DC 引脚控制命令/数据切换）：

```cpp
// 命令
esp_lcd_panel_io_tx_param(io_handle_, 0x2A, nullptr, 0);

// 数据
uint8_t data = 0x12;
esp_lcd_panel_io_tx_param(io_handle_, -1, &data, 1);

// Buffer
esp_lcd_panel_io_tx_color(io_handle_, -1, buf, len);
```

Waveshare 官方驱动用的也是 `-1` 作为 data 标记。

### 3.6 USB-serial-jtag 控制台冲突

**问题**：ESP-IDF 默认把 USB-serial-jtag 用作控制台输出（`printf` / `ESP_LOGI`）。我们的 transport 也需要用这个接口。

**修复**：

```
# sdkconfig.defaults
CONFIG_ESP_CONSOLE_NONE=y
```

禁用控制台后，`ESP_LOGI` 的输出无处可去（不是重定向到 transport，而是直接丢弃）。这意味着调试时看不到日志，但 transport 层能正常工作。

**替代方案**：如果需要日志，可以外接 UART0（GPIO43=TX, GPIO44=RX），或者用 `esp_log_set_vprintf` 把日志重定向到 transport 的发送缓冲区。

### 3.7 GPIO0 (BOOT) strapping pin

BOOT 键连接 GPIO0，这是 ESP32-S3 的 strapping pin。按住 BOOT 再上电会进入下载模式。

这不是 bug，是硬件设计如此。使用时注意：先松开 BOOT 再按复位键。

---

## 4. 调试方法论

### 4.1 分层验证

显示问题最难调试，因为"屏幕没反应"有太多可能的根因。有效的方法是**自底向上**验证：

1. **SPI 通信层**：确认 `spi_bus_initialize()` 成功
2. **面板 IO 层**：确认 `esp_lcd_new_panel_io_spi()` 成功
3. **ST7305 初始化**：用逻辑分析仪或示波器看 SPI 波形，或者直接验证 init sequence 和官方一致
4. **像素写入**：手动写 test pattern 到 frame buffer，调 `RLCD_Display()`
5. **LVGL 渲染**：确认 FlushCb 被调用，像素正确转换
6. **UI 层**：确认控件创建正确、样式正确、visible flag 正确

这次调试中，步骤 4 成功（test pattern 显示），但步骤 5 失败（FlushCb 从未被调用）。根因不是显示问题，而是 LVGL 初始化 crash（步骤 5 之上的步骤 3.1）。

### 4.2 摄像头调试

用 webcam 对准 LCD 屏幕，通过 `ffmpeg` 抓拍静态图片验证：

```bash
ffmpeg -y -f v4l2 -i /dev/video0 -frames:v 1 /tmp/lcd.jpg
```

优点：
- 不需要物理接触设备
- 可以反复抓拍对比
- AI 视觉模型可以解读图片内容

局限：
- 反射式 LCD 在强光下反光严重，图片可能不清晰
- 小字体在 webcam 分辨率下难以辨认
- 需要稳定的摄像头支架

### 4.3 串口日志

stack overflow 发生在 USB-serial-jtag 被接管之前，所以早期日志可以通过串口看到。一旦 transport 初始化完成，日志就消失了。

用 Python pyserial 直接读取：

```python
import serial
ser = serial.Serial('/dev/ttyACM0', 115200, timeout=2)
# ... 读取并打印
```

---

## 5. Waveshare 官方驱动作为参考

Waveshare 的 ESP32-S3-RLCD-4.2 仓库有完整的 ST7305 驱动代码。移植时参考了：

- SPI 初始化参数
- ST7305 init sequence（逐字节对照）
- RGB565 → 1-bit 的转换方式
- `RLCD_Display()` 的列/行地址设置命令
- `esp_lcd_panel_io_tx_param` 和 `esp_lcd_panel_io_tx_color` 的用法

关键发现：**官方驱动也用 RGB565 格式 + LVGL PARTIAL 模式**，不是 I1。这直接决定了我们的方案选择。

---

## 6. 当前状态

| 功能 | 状态 |
| --- | --- |
| ST7305 显示驱动 | ✅ 工作正常 |
| LVGL 仪表盘 UI | ✅ 工作正常 |
| LVGL 审批卡片 UI | ✅ 工作正常 |
| USB-serial-jtag 传输 | ✅ 双向 JSON 协议验证 |
| 按键 (KEY=批准, BOOT=拒绝) | ✅ 物理验证 |
| Bridge daemon 对接 | ✅ 连接、状态同步、权限审批 |
| 设备命令 (name/owner/status) | ✅ NVS 持久化 |
| BLE 传输 | ⬜ WIP (NimBLE 框架已搭建) |
| CJK 字体 | ⬜ 未来 (需要 SPIFFS + TTF) |

---

## 7. 关键数据

- 固件大小：~700KB（app partition 4MB，用了 17%）
- PSRAM 占用：~255KB（RGB565 buffer 240KB + 1-bit buffer 15KB）
- RLCD 刷新时间：~450ms（全屏）
- 刷新策略：idle 5s，审批 1s
- 串口波特率：115200（USB-serial-jtag 不严格需要，但保持兼容）
