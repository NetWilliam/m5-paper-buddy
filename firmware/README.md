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
│   │   └── buddy_display.h/.cc # LVGL UI (仪表盘 / 审批卡片 / 启动画面)
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
- **字体**: 使用 LVGL 内置 Montserrat (14/16/20/36pt)，暂不支持 CJK。

## 与 M5Paper 版本的区别

| | M5Paper V1.1 (原版) | ESP32-S3-RLCD-4.2 (本固件) |
| --- | --- | --- |
| 框架 | Arduino (PlatformIO) | ESP-IDF (CMake) |
| 显示 | 540×960 e-ink, 16 灰度, IT8951 | 400×300 RLCD, 1-bit, ST7305 |
| 触屏 | GT911 电容触屏 | 无 |
| 按键 | 3 (UP/PUSH/DOWN) | 2 (KEY/BOOT) |
| CJK | TTF 字体 + LittleFS | 暂不支持 |
| 设置页 | DND / 语言切换 | 无 (2 键最小化) |
| BLE | 完整 (NimBLE) | WIP |
