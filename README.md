# redroid-hwenc-intel-iHD

## 代码声明
- **AI 生成声明**：本项目代码完全由 **DeepSeek V4.1 Flash** 生成。
- 项目围绕LLM生成的 `gralloc_mega_test.c` 进行接口测试后，由LLM进行逻辑代码编写。
---

## 宿主机要求

### GPU 与驱动
- GPU 必须暴露 VA-API 的 H.264 编码入口点 `VAEntrypointEncSlice`，用 `vainfo | grep -A5 H264` 确认
- Intel 平台需 `intel-media-driver`（iHD），**不是** `intel-vaapi-driver`

### 内核特性（必须）

| 特性 | 替代方案 | 说明 |
|---|---|---|
| `binderfs` | `binder_linux` 模块 | Android IPC |
| `ashmem` | **`memfd`**（redroid ≥11 推荐） | 匿名共享内存 |
| DMA-BUF Heaps（`/dev/dma_heap/*`） | **无替代** | 硬件编码 YUV buffer 分配 (项目只实现了 `DMA-BUF Heaps`) |

### 内核版本
- **推荐 5.10+ LTS**：`DMA-BUF Heaps` 成熟，`CONFIG_DMABUF_HEAPS` 可直接启用，硬件编码路径所需的内核设施完整

## 编译 `gralloc_mega_test.c` 的示例命令
```bash
/src/toolchain/linux-x86/clang-r487747c/bin/clang \
  -O2 -std=gnu11 \
  -target x86_64-linux-android34 \
  -D__ANDROID__ -D__BIONIC__ \
  --sysroot=/src/vndk-sysroot -B/src/vndk-sysroot/usr/lib64/ \
  -I/src/bionic-src/libc/include \
  -I/src/bionic-src/libc/kernel/uapi \
  -I/src/bionic-src/libc/kernel/uapi/asm-x86 \
  -I/src/bionic-src/libc/kernel/android/uapi \
  -I/src/bionic-src/system-core-src/libcutils/include \
  -I/src/bionic-src/system-core-src/libutils/include \
  -I/src/bionic-src/system-core-src/libsystem/include \
  -I/src/bionic-src/system-core-src/liblog/include \
  -I/src/libhardware-src/include \
  -I/src/frameworks-native-src/libs/nativewindow/include \
  -I/src/frameworks-native-src/libs/arect/include \
  -L/src/vndk-sysroot/usr/lib64 \
  -lcutils -llog -ldl -lc \
  gralloc_mega_test.c -o gralloc_mega_test
```
