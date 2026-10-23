# OpenH264 decoder

Cisco OpenH264 v2.7.0, commit `1bca62311e447fd9c39fc93de2c4b96cd5ce971c`,
from https://github.com/cisco/openh264. The source snapshot includes API,
common, and decoder code; the BSD license is retained in `vendor/openh264/LICENSE`.
The component builds portable decoder/common C++ and exposes a small C API.
Application modules remain C. The encoder and platform assembly are not built.

ESP adaptation is limited to `WelsThreadLib.cpp`: avoid desktop sysctl headers,
skip unsupported pthread scheduler settings, and report one decode processor.
The wrapper explicitly disables internal decoding threads. ESP-IDF malloc owns
allocations. On ESP, `memory_align.cpp` explicitly allocates decoder CPU
workspaces in PSRAM; the wrapper's packed output also uses PSRAM. These buffers
have no DMA ownership and do not fall back to internal SRAM when PSRAM is full.
Small object/control allocations still use the normal allocator. Normal `free`
releases the original heap-capability allocations using the upstream owner/lifetime.

`decoder_core.cpp` uses `int32_t` for two bitstream-buffer helper definitions
to match their existing header declarations. On the pinned RISC-V toolchain,
`int32_t` is `long`, so the upstream `int` definitions otherwise create
different C++ overloads and fail to link.
