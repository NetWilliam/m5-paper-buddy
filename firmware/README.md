# Paper Buddy — ESP32-S3-RLCD-4.2 固件

ESP-IDF (CMake) 固件，运行在 Waveshare ESP32-S3-RLCD-4.2 开发板上。通过 USB-serial-jtag 与 bridge daemon 通信，在 4.2 寸 ST7305 反射式 LCD 上渲染 Claude Code 状态仪表盘和权限审批卡片。

## 硬件

| 项目 | 值 |
| --- | --- |
| MCU | ESP32-S3 双核 LX7, 240 MHz |
| Flash / PSRAM | 16 MB / 8 MB (Octal) |
| 显示屏 | ST7305, 400×300, 1-bit 单色反射式 LCD |
| SPI 总线 | GPIO11(SCK), GPIO12(MOSI), GPIO5(DC), GPIO40(CS), GPIO41(RST), 40 MHz |
| 按键 | KEY (GPIO18) = 批准, BOOT (GPIO0) = 拒绝 |
| USB | USB-serial-jtag CDC (/dev/ttyACM0) |

## 目录结构

```
firmware/
├── CMakeLists.txt            # 顶层项目
├── sdkconfig.defaults        # ESP-IDF 编译选项
├── partitions.csv            # NVS + app + SPIFFS 分区
├── main/
│   ├── main.cc               # 入口 + 主循环
│   ├── config.h              # 引脚定义 + 显示常量
│   ├── buddy_frames.h        # ASCII 猫 6 个状态
│   ├── idf_component.yml     # 托管依赖: LVGL, cJSON, button, esp_lvgl_port
│   ├── display/
│   │   ├── lcd_driver.h/.cc  # SPI + ST7305 驱动 + LVGL flush callback
│   │   ├── buddy_display.h/.cc # LVGL UI (仪表盘 / 审批卡片 / 启动画面)
│   │   └── lv_font_cjk_ext_14.c # CJK 扩展字体 (20K+ 字, 编译内嵌)
│   ├── button/
│   │   └── button_handler.h/.cc # iot_button + FreeRTOS event group
│   ├── transport/
│   │   ├── transport.h        # Transport 抽象接口
│   │   ├── serial_jtag_transport.h/.cc  # USB-serial-jtag
│   │   └── ble_transport.h/.cc          # BLE Nordic UART Service (WIP)
│   ├── protocol/
│   │   └── line_protocol.h/.cc # 行缓冲 JSON 解析器
│   └── state/
│       ├── tama_state.h       # TamaState 结构体
│       ├── state_parser.cc    # cJSON → TamaState
│       ├── xfer_commands.h    # name/owner/status 命令处理
│       └── stats.h            # NVS 统计持久化
```

## 构建 & 烧录

### 前置条件

- ESP-IDF v5.5+（已验证 v5.5.4）
- Python 3.10+
- USB 数据线（开发板通过 USB-C 供电 + 烧录）

### 构建

```bash
# 加载 ESP-IDF 环境
source ~/esp/v5.5.4/esp-idf/export.sh

# 构建
cd firmware/
idf.py build
```

### 烧录

```bash
idf.py -p /dev/ttyACM0 flash
```

> 如果端口不同，用 `-p` 指定。macOS 上可能是 `/dev/cu.usbmodem*`。
>
> **大固件烧录提示**: 含 CJK 字体的固件约 1.5MB。USB-serial-jtag 偶尔在写大文件时断连。如果烧录失败:
> 1. 确认设备端口 (`ls /dev/ttyACM*`)，可能不是 `ttyACM0`
> 2. 降低波特率: `idf.py -p /dev/ttyACM1 -b 115200 flash`
> 3. 重新插拔 USB 线后重试

### 查看日志

固件默认禁用了 USB-serial-jtag 控制台（`CONFIG_ESP_CONSOLE_NONE=y`），以便 transport 层独占 CDC 端点。调试时需要外接 UART 或临时修改 `sdkconfig.defaults`。

## 使用

### 1. 启动 bridge daemon

```bash
python3 tools/claude_code_bridge.py --port /dev/ttyACM0
```

daemon 会自动向设备发送 owner name（取自主机名），设备 NVS 里记住后显示为 `"xxx's Paper Buddy"`。

### 2. 在 Claude Code 项目中安装 hooks

daemon 启动后会输出配置指引。或者手动将 `plugin/settings/hooks.json` 的 hooks 块合并到项目的 `.claude/settings.json`：

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          {"type": "command", "command": "curl -sS --max-time 3 -X POST --data-binary @- http://127.0.0.1:9876/hook 2>/dev/null || echo '{}'"}
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": ".*",
        "hooks": [
          {"type": "command", "command": "curl -sS --max-time 40 -X POST --data-binary @- http://127.0.0.1:9876/hook 2>/dev/null || echo '{}'"}
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": ".*",
        "hooks": [
          {"type": "command", "command": "curl -sS --max-time 3 -X POST --data-binary @- http://127.0.0.1:9876/hook 2>/dev/null || echo '{}'"}
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {"type": "command", "command": "curl -sS --max-time 3 -X POST --data-binary @- http://127.0.0.1:9876/hook 2>/dev/null || echo '{}'"}
        ]
      }
    ]
  }
}
```

> **注意**：Claude Code 通过 stdin 将 hook payload 传递给 command。`--data-binary @-` 让 curl 从 stdin 读取 JSON 并作为 POST body 发送。`|| echo '{}'` 确保 daemon 未运行时 hook 不阻塞 Claude Code。

### 3. 正常使用

- Claude Code 运行时，RLCD 显示**仪表盘**：项目名、分支、模型、会话数、最近回复、活动日志
- Claude Code 请求权限时（PreToolUse hook），RLCD 切换到**审批卡片**：显示工具名、项目/会话、完整命令或 diff
- 按 **KEY** (GPIO18) 批准，按 **BOOT** (GPIO0) 拒绝
- 30 秒无操作自动超时（bridge daemon 侧控制）

### 设备命令

通过 USB serial 发送单行 JSON：

| 命令 | 格式 | 说明 |
| --- | --- | --- |
| 设置宠物名 | `{"cmd":"name","name":"Whiskers"}` | 返回 `{"ack":"name","ok":true}` |
| 设置主人名 | `{"cmd":"owner","name":"Alice"}` | 返回 `{"ack":"owner","ok":true}` |
| 查询状态 | `{"cmd":"status"}` | 返回 name/owner/stats |

## UI 布局

### 仪表盘 (400×300)

```
┌────────────────────────────────────┐
│ Alice's Whiskers     claude-opus-4 │
├────────────────────────────────────┤
│ m5-paper-buddy  main               │
│ 2 run / 1 wait                     │
├────────────────────────────────────┤
│ LATEST REPLY                       │
│ Fixed the stack overflow!          │
├────────────────────────────────────┤
│ ACTIVITY                           │
│ 14:30 Edit lcd_driver.cc           │
│ 14:29 Build OK                     │
├────────────────────────────────────┤
│  ^_^                  LINKED       │
└────────────────────────────────────┘
```

### 审批卡片 (全屏覆盖)

```
┌────────────────────────────────────┐
│ PERMISSION REQUESTED               │
│ Bash                               │
│ m5-paper-buddy [a1b2c3]           │
│ ─────────────────────────────────  │
│ $ rm -rf /tmp/test_build           │
│ (可滚动)                           │
│ ─────────────────────────────────  │
│ waiting 5s                         │
│    KEY=approve    BOOT=deny        │
└────────────────────────────────────┘
```

## 注意事项

- **BOOT 键 (GPIO0)** 是 ESP32-S3 strapping pin。按住 BOOT 再上电/复位会进入下载模式。正常使用时先松开 BOOT 再复位。
- **RLCD 刷新**约 450ms，idle 状态下 5 秒刷新一次，审批模式下 1 秒一次。
- **字体**: Montserrat (14/16/20/36pt) + 自定义 CJK 扩展字体 (14pt, 20000+ 字符)。CJK 字体编译进固件，无需额外文件。

## 与 M5Paper 版本的区别

| | M5Paper V1.1 (原版) | ESP32-S3-RLCD-4.2 (本固件) |
| --- | --- | --- |
| 框架 | Arduino (PlatformIO) | ESP-IDF (CMake) |
| 显示 | 540×960 e-ink, 16 灰度, IT8951 | 400×300 RLCD, 1-bit, ST7305 |
| 触屏 | GT911 电容触屏 | 无 |
| 按键 | 3 (UP/PUSH/DOWN) | 2 (KEY/BOOT) |
| CJK | TTF 字体 + LittleFS | 编译内嵌 C 字体 (20K+ 字) |
| 设置页 | DND / 语言切换 | 无 (2 键最小化) |
| BLE | 完整 (NimBLE) | WIP |

## CJK 字体

### 工作原理

CJK 字体以 **C 源码**形式编译进固件，不依赖 SPIFFS 或外部文件。固件启动时 `BuddyDisplay::LoadCjkFonts()` 通过 `extern` 引用链接好的字体对象，直接用于所有 LVGL label。

**字体规格**:
- 字体: NotoSansCJKsc-Regular (思源黑体 SC)
- 大小: 14px, 1-bit (单色)
- 覆盖范围:
  - ASCII: U+0020-U+007E (95 字符)
  - CJK 符号: U+3000-U+303F (标点、括号等)
  - CJK 统一汉字: U+4E00-U+9FFF (20992 字符)
  - 全角字符: U+FF00-U+FFEF (全角 ASCII、标点)
- 源文件: `main/display/lv_font_cjk_ext_14.c` (~5.4MB)
- 编译后二进制增量: ~380KB (固件总大小 ~1.5MB)

### 新板子烧录

不需要任何特殊操作。CJK 字体已编译进固件二进制:

```bash
source ~/esp/v5.5.4/esp-idf/export.sh
cd firmware/
idf.py build
idf.py -p /dev/ttyACM0 flash
```

烧录完成后 CJK 自动生效，无需额外文件或配置。

### 重新生成字体 (按需)

如果需要修改字体大小、范围或换字体文件:

```bash
# 1. 安装 lv_font_conv
npm install -g lv_font_conv

# 2. 准备 TTF/OTF 字体文件 (需要支持 CJK)
#    例如: NotoSansCJKsc-Regular.otf

# 3. 生成 C 源码
npx lv_font_conv \
  --font /path/to/NotoSansCJKsc-Regular.otf \
  --bpp 1 \
  --size 14 \
  --range 0x20-0x7e,0x3000-0x303f,0x4e00-0x9fff,0xff00-0xffef \
  --format lvgl \
  -o main/display/lv_font_cjk_ext_14.c

# 4. 修复 include (lv_font_conv 生成的条件编译与 ESP-IDF 不兼容)
#    将文件头部的:
#      #ifdef LV_LVGL_H_INCLUDE_SIMPLE
#    改为:
#      #if 1
sed -i 's/#ifdef LV_LVGL_H_INCLUDE_SIMPLE/#if 1/' \
  main/display/lv_font_cjk_ext_14.c

# 5. 编译 & 烧录
idf.py build
idf.py -p /dev/ttyACM0 flash
```

**减小字体体积**: 如果不需要完整 CJK 范围，缩小 `--range` 参数。例如仅覆盖常用汉字 (GB2312 级别):
```
--range 0x20-0x7e,0x3000-0x303f,0x4e00-0x77ff,0xff00-0xffef
```
这会将覆盖字符从 20992 减至约 12000，固件从 1.5MB 降至 1.15MB。

### 关键 sdkconfig 选项

| 选项 | 值 | 作用 |
| --- | --- | --- |
| `CONFIG_LV_FONT_FMT_TXT_LARGE` | y | 支持大字体 (>65536 glyph) |
| `CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK` | y | LVGL 内置 CJK 基础字体 (备用) |
| `CONFIG_LV_FONT_MONTSERRAT_14` | y | Latin 主字体 |

### 相关源码

| 文件 | 作用 |
| --- | --- |
| `main/display/lv_font_cjk_ext_14.c` | CJK 字体数据 (lv_font_conv 生成) |
| `main/display/buddy_display.cc` | `LoadCjkFonts()` 加载字体，`CreateScreens()` 应用到所有 label |
| `main/CMakeLists.txt` | SRCS 列表包含字体文件 |
| `sdkconfig.defaults` | `LV_FONT_FMT_TXT_LARGE=y` 等编译选项 |
