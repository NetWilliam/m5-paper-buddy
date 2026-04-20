# 空闲图片更新指南

## 硬件背景

- **屏幕**: ST7305 反射式 LCD, 400×300 像素, 1-bit 黑白
- **存储**: SPIFFS 分区, 起始 0x410000, 大小 ~12MB
- **图片槽位**: `/spiffs/slot0.raw` ~ `/spiffs/slot3.raw` (最多 4 张)
- **单张大小**: 固定 15000 字节 (400 × 300 / 8)

## 图片格式

线性 1-bit raw, MSB first, 行优先 (row-major):

```
像素 (x, y) → 字节偏移 (y * 400 + x) / 8, 位偏移 7 - (x % 8)
bit = 1 → 白色, bit = 0 → 黑色
```

## 从任意图片生成 raw 文件

需要 ImageMagick (`convert` 命令):

```bash
# 基本转换
convert input.png -resize 400x300! -threshold 50% -depth 1 gray:output.raw

# 反色 (白底黑图 → 黑底白图)
convert input.png -resize 400x300! -threshold 50% -depth 1 -negate gray:output.raw
```

参数说明:
- `-resize 400x300!` — 强制拉伸到 400×300, 忽略比例 (`!` 很重要)
- `-threshold 50%` — 灰度二值化阈值, 可调 (如 `40%` 偏暗, `60%` 偏亮)
- `-depth 1` — 输出 1-bit
- `gray:output.raw` — 输出为灰度 raw 格式
- `-negate` — 反色

验证文件大小:

```bash
ls -la output.raw
# 应该恰好 15000 字节
```

## 烧录到设备

### 方法 1: 重新生成 SPIFFS 镜像并烧录

```bash
source ~/esp/v5.5.4/esp-idf/export.sh

# 1. 把 raw 文件放到 spiffs_image_data/ 目录
cp my_image.raw firmware/main/spiffs_image_data/slot0.raw
cp my_image2.raw firmware/main/spiffs_image_data/slot1.raw

# 2. 生成 SPIFFS 镜像 (大小 0xBF0000 = 分区大小)
python3 ~/esp/v5.5.4/esp-idf/components/spiffs/spiffsgen.py \
    0xBF0000 \
    firmware/main/spiffs_image_data/ \
    firmware/build/spiffs_image.bin

# 3. 烧录 SPIFFS 分区 (不擦固件)
python3 -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
    write_flash 0x410000 firmware/build/spiffs_image.bin
```

### 方法 2: 运行时通过 USB 命令更新 (需要先实现固件侧命令)

暂未实现。计划: 通过 JSON 命令 `{"cmd":"image","slot":0}` + 二进制传输写入 SPIFFS。

## 空闲图片轮换行为

- 设备断开连接 5 秒后 (正式版改为 5 分钟) 进入空闲图片模式
- 每 15 秒切换一张图片, 跳过空槽位
- 任意活动 (USB 连接、BLE 连接、按键) 退出空闲模式回到仪表盘

## 修改轮换参数

编辑 `firmware/main/main.cc`:

```cpp
static constexpr uint32_t IDLE_IMAGE_TIMEOUT_MS = 5000;   // 进入空闲的等待时间
static constexpr uint32_t IDLE_IMAGE_CYCLE_MS   = 15000;  // 每张图片显示时长
```

修改后需重新编译烧录固件。

## 槽位对应关系

| 文件 | 槽位 | 说明 |
| --- | --- | --- |
| `slot0.raw` | 0 | 第一张 |
| `slot1.raw` | 1 | 第二张 |
| `slot2.raw` | 2 | 第三张 (可空) |
| `slot3.raw` | 3 | 第四张 (可空) |

空槽位 (文件不存在或大小不是 15000 字节) 会被自动跳过。
