# redroid-hwenc-intel-iHD

- 编译 `gralloc_mega_test.c` 的示例命令
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
