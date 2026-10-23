# iOS 投屏：独立 ESP-IDF 工程

工程位于 [`projects/ios_screen_mirror`](../projects/ios_screen_mirror/)。
这是按开发者明确要求创建的独立工程，采用普通 ESP-IDF 引导和分区布局。
硬件由当前 BSP 初始化，应用没有 ESP-Iris、Vibe Mode 集成、GSP 或 LVGL 依赖。
可以单独复制工程，通过 `MOSAICO_BSP_ROOT` 指向 BSP checkout。

## 组成

- ESP32-S31，ESP-IDF 固定为 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。
- 沿用工作区 `dd63f3a` 固定的 BSP `a2c985a`。`bsp_display_new()`
  读取 eFuse USER_DATA，识别 v1.0 / v1.1 / v1.2；v1.1 使用 v1.2 的引脚配置。
  工程只读 eFuse。
- CO5300 LCD，480×480，QSPI，RGB565。初始等待和停止投屏后为黑屏，状态通过日志输出。
- 原工程固定提交的 RPiPlay，适配 ESP-IDF pthread/lwIP、mDNS、ESP AES、PSA SHA-512。
  音频接收后丢弃。
- Cisco OpenH264 v2.7.0 便携解码源码，支持 8-bit、4:2:0、逐行的
  Baseline/Main/High profile，包含 CABAC、8×8 变换和 B 帧解码。
  解码包装层为 C，输出打包 I420；上游 C++ 实现保留原有约定。
- `espressif/esp_tinyusb ==2.4.0`、`espressif/tinyusb ==0.19.0~3`，
  在板载 USB-HS 口提供一个 CDC 串口，用于应用日志与标准输入输出。

数据路径：Wi-Fi → AirPlay 解密和 Annex B → 有界队列 → H.264 解码 →
按 SPS 裁剪、等比缩放和颜色转换 → RGB565 → LCD DMA。
CPU1 上的单个视频任务拥有解码器和 LCD，栈预算 32 KiB；关闭 OpenH264 内部多线程。
CPU0 处理 Wi-Fi、USB 和协议。协议回调只更新配置或复制到预分配包池。
DMA 完成前不复用帧缓冲。拥塞时优先显示关键图像，省略积压的普通图像显示；丢失
压缩帧后重建解码器并等待 IDR，同时保留最后一张有效图像，避免持续黑屏。

## 配置与构建

在工作区根目录运行：

```sh
git -C /home/yanke/esp/esp-idf-mosaico-7b9cc1a rev-parse HEAD
source /home/yanke/esp/esp-idf-mosaico-7b9cc1a/export.sh
idf.py -C projects/ios_screen_mirror menuconfig
```

在 **iOS screen mirror** 中设置 `MIRROR_WIFI_SSID` 和 `MIRROR_WIFI_PASSWORD`。
SSID 留空时读取普通 ESP-IDF Wi-Fi NVS 的 STA 配置，不读取其他产品自定义的配置格式。
Kconfig 指定的凭据只写驱动 RAM，不覆盖原 NVS 凭据。设备和 iPhone 需要在同一个
允许 mDNS 和设备互访的局域网中。

默认接收端名称为 **Mosaico Mirror**，向手机声明 **320×320、15 Hz**。
实际编码尺寸和帧率由手机决定。解码上限按宏块对齐尺寸计算，默认为 **307200 像素**，
同时限制参考图像内存估算不超过 8 MiB。

```sh
python3 submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py \
  --project projects/ios_screen_mirror \
  --idf-path /home/yanke/esp/esp-idf-mosaico-7b9cc1a doctor
python3 submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py \
  --project projects/ios_screen_mirror \
  --idf-path /home/yanke/esp/esp-idf-mosaico-7b9cc1a build
```

输出为 `build/ios_screen_mirror.bin`，日志在工程的 `.codex-runs/idf-low-noise-build/`。
单独复制工程后使用原生 `idf.py build`；可通过
`idf.py -DMOSAICO_BSP_ROOT=/absolute/path/to/esp-mosaico-bsp build` 指定 BSP。
构建脚本仅是主机工具，不构成固件依赖。

## USB-HS 日志串口

应用首先初始化 TinyUSB CDC，再启动 LCD、解码器和 Wi-Fi。
`ESP_LOGx`、`printf()` 和 `stderr` 均经 CDC 输出。固件将普通 UART 控制台和
USB Serial/JTAG 副控制台关闭，由 TinyUSB 独占应用的 USB-HS 日志通路。

电脑通过数据线连接板载 USB-HS 口，设备会枚举为 **Mosaico Mirror USB Console**，
串号取自芯片 eFuse MAC。Linux 通常出现 `/dev/ttyACM*`，Windows 为 `COM*`。
打开对应串口即可看日志，例如 Linux：

```sh
python -m serial.tools.miniterm /dev/ttyACM0 115200 --raw
```

这里的 `115200` 是串口工具的参数，CDC 的数据通过 USB 传输。
`menuconfig → iOS screen mirror → Startup wait for USB CDC terminal` 默认等待
3000 ms，方便电脑在 LCD/Wi-Fi 初始化前打开串口；超时后继续运行。
日志写入采用非阻塞方式，电脑断开或发送 FIFO 满时可能丢失日志，不会等待串口而
阻塞投屏。此 CDC 在 `app_main()` 中启动，不承载 ROM 和 bootloader 阶段日志。

## 性能与兼容性

小屏只有在手机实际发送低分辨率码流时才会减少解码量；收到 720p 再缩到 480×480，
仍然需要解码完整的 720p。

实际 iPhone 已发送 `profile=100 constraints=0x00 level=31` 的 High profile 码流。
`esp_h264 1.4.1` 的 constrained-baseline 解码器无法处理该输入，所以本工程使用
[OpenH264](https://github.com/cisco/openh264/tree/v2.7.0) 便携实现。其实际速度需要在
S31 上测量，不能套用 esp_h264 的汇编解码性能数据；投屏还需承担 Wi-Fi、解密、
颜色转换、图像拷贝与刷屏开销。

当前实现解析 High profile SPS 的色度、位深和缩放矩阵，再检查编码尺寸、参考帧
数量和裁剪；不会将 High 码流改标为 Baseline。10-bit、非 4:2:0、隔行、
QP-prime transform bypass，以及超过像素/参考图像内存限制的输入会被拒绝。

`SPS:` 日志给出实际 profile、可见/编码尺寸和参考帧数量。约每 5 秒记录解码帧数、
显示帧数、区间长度、解码耗时与转换/刷屏耗时；统计从本次视频开始计算，不包含
等待手机连接的空闲时间。

### PSRAM 与 DMA 内存

OpenH264 CPU 工作区、打包后的 I420 输出、视频包池和 LCD 帧缓冲明确放入 PSRAM。
解码器工作区分配失败会报错，不回退消耗内部 SRAM。小型控制对象、任务栈以及
驱动要求的内部/DMA 资源仍有独立的内存需求，不能用 PSRAM 空闲量替代判断。

固定 S31 IDF 的 AES 大数据 DMA 路径可能申请内部临时缓冲和描述符。本工程将
CTR/CBC 硬件调用限制为每次最多 128 字节，并显式启用
`CONFIG_MBEDTLS_AES_HW_SMALL_DATA_LEN_OPTIM`，使用寄存器路径避免每帧 DMA 临时分配。
计数器与不足 16 字节的余量跨包保留，解密热路径不分配堆内存。若硬件操作失败，
错误输出不会送往 NAL 解析或解码器；当前视频流关闭后需要重新连接，不能沿用
已失步的计数器状态。

解码器创建时和运行统计后的 `heap:` 日志提供 `internal`、`dma_largest` 与 `psram`：
内部可用字节、内部 DMA 堆最大连续空闲块、PSRAM 可用字节。应一起观察这些值。

## 验证边界

```sh
python3 -m unittest discover -s projects/ios_screen_mirror/tests -v
```

主机测试编译实际 C 包装层与 OpenH264 上游解码源码，以包含 CABAC、B 帧及裁剪的
High profile 流验证 12 帧输出和显示顺序，并与 FFmpeg 解码结果比较，允许最多
一个 8-bit 色阶的舍入差异。测试还覆盖解码器重建、Baseline/High SPS、AVC 包/配置
边界、I420 颜色范围、RGB565 字节序、等比留黑边和 plist 互操作。
H.264 夹具由 FFmpeg 的 `testsrc2` 和 libx264 生成，无外部视频素材；运行完整
解码测试需要主机具有 CMake、C++ 编译器和 FFmpeg。
AES 测试使用实际适配层和流解密实现、OpenSSL 主机后端，验证 NIST CTR/CBC 向量、
大包/非对齐/短包跨块、无热路径分配以及故障注入；需要 OpenSSL 开发头文件与 libcrypto。

主机测试不验证 S31 上的实际速度/内存峰值、真实 iPhone 码流的完整解码与显示、
LCD 颜色/方向、长时间重连和实际帧率。新版仍需烧录后用原 iPhone 复测。
此工程布局不同于 retained Vibe Mode 应用，不能作为 Iris `app-update` 输入。

版权与来源见 [ORIGIN.md](../projects/ios_screen_mirror/components/airplay_receiver/ORIGIN.md)。
