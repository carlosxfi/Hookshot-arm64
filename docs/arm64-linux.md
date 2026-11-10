# Experimental ARM64 Linux port

This branch starts a native ARM64 Linux port of Hookshot's Zelda recompilation. It is not yet a tested playable
ARM64 release. The existing Linux AppImage and bundled tools are x86-64 and cannot be used as the ARM64 builder.

## First milestone

- Select `armv8-a` on an ARM64 Linux host and remove the x86-only `-mcx16` flag.
- Keep signed `char` semantics and acquire/release semantics for the host's Win32-style volatile synchronization.
- Preserve AAPCS64 registers, FPCR and FPSR in the lifted setjmp/longjmp bridge.
- Translate ARM64 Linux signal contexts and ESR read/write/execute faults; sample native PCs in the profiler.
- Compile the host runtime and compile/link the Vulkan renderer on native ARM64 and x86-64 CI runners, without
  reading or uploading any game data.

The code adapted from TotK Recomp is documented, with its MIT license, in [host/ARM64_PROVENANCE.md](../host/ARM64_PROVENANCE.md).
Hookshot's game translator, guest context, 512 GiB address space, scheduler and NVN2 renderer remain its own.

## Checks without a game

On native ARM64 Linux, install Clang, LLD, a C++ standard library, Python 3, pkg-config, SDL2 development headers and
FFmpeg development headers (`libavcodec`, `libswscale`, `libswresample`, `libavutil`). Fedora also needs
`libstdc++-static` for the renderer's C++ runtime link. Then run:

```sh
python3 tests/test_arm64_platform.py
python3 tests/test_dma_memory_init.py
python3 tests/test_guest_copy.py
python3 nx2/check_platform.py
```

The first command runs nested nonlocal jumps, checks floating-point rounding restoration, exercises real
read/write/execute faults through the POSIX exception layer, and checks CPU detection. It runs at `-O0` and `-O2`.
The memory tests compile actual startup/allocation/submission functions with a deterministic Vulkan device and test
CPU/GPU writes, unchanged-value GPU stores in known ranges, allocation failures, and fence publication order.
`nx2/check_platform.py` compiles the host and generated runtime into objects under `work/platform-check`; it does not link a game.
`--core-only` checks the six architecture-dependent/runtime units without media development packages.

The **ARM64 Linux platform checks** workflow also builds and links `hookshot_vk.so`, using Hookshot's existing
`RX_VK_ONLY=1 python3 nx2/build.py --standalone --jobs 2` path and pinned Vulkan/SPIR-V headers. This checks CPU
compilation and link symbols. It does not run Vulkan or establish GPU compatibility.

## Remaining validation

- Build the supported Zelda game version from the user's own files on ARM64 Linux, with a native Clang toolchain
  and Python dependencies from Hookshot's source-build instructions.
- Check generated game-code compilation, thread synchronization, fibers, saves, movies and gameplay on the device.
- Check the Vulkan 1.3 features and either native mesh shaders or the existing experimental compute fallback.
  Turnip on the tested Odin exposes buffer device addresses for that fallback, but graphics remain unvalidated.
  TotK's renderer working on Turnip does not validate Hookshot's Switch 2 renderer.
- The fixed guest base at 16 TiB plus the 512 GiB reservation must fit the device's userspace virtual address range.
- Build and package an ARM64 builder/tool bundle separately; the x86 AppImage packaging scripts are not ported here.

Cross compilation, a Windows ARM64 target, performance tuning and changes to the mesh-shader fallback are outside this first milestone.

## GPU memory on Turnip

The renderer accepts host-pointer imports (`VK_EXT_external_memory_host`), its Linux DMA-buffer path, or a separate
buffer copy fallback. Turnip on the Odin does not expose host-pointer imports. The DMA path additionally requires
`VK_EXT_external_memory_dma_buf`, the runtime's memfd guest window, and user access to `/dev/udmabuf`.

The tested Armada 7.2.6 kernel reports `# CONFIG_UDMABUF is not set`. With no usable import mechanism at startup,
the renderer automatically selects copies. `RX_VK_GUEST_COPY=1` forces copies for testing on other devices;
`RX_VK_GUEST_COPY=0` disables the fallback and fails startup if neither import path is usable.

The copy path allocates coherent, CPU-visible buffers with device addresses. It coalesces overlapping game pools
before drawing, uploads changed pages before each recording is submitted, waits for its GPU fence, then copies
results back before publishing either timeline semaphore. Known write ranges are copied even when a GPU store
kept the previous value. A page diff also collects changes made by shaders following guest pointers outside those
ranges, preserving CPU edits in bytes the GPU left unchanged. Identical-value stores outside reported write ranges
cannot be detected by that diff. This path needs on-device validation, particularly GPU atomics and pointer-following
compute shaders.

While copies are active, VRAM mirrors, asynchronous replay, parallel recording, recording chunks, queue bursts and
batched submissions are disabled. Event waits between game queues still keep their normal ordering.
Whole game pools are allocated ahead of recording to keep one GPU buffer per guest page. The GPU allocation and
its CPU comparison baseline add roughly twice the copied pool bytes to the game's memory use. Full page scans and
GPU waits can substantially reduce frame rate; this is an experimental compatibility path, not a performance result.
An available DMA import that subsequently fails for a specific pool is still reported, not mixed with copy storage.

### Updating only the renderer after an existing native build

The copy path does not change the host/renderer ABI. Keep the existing game executable, `image.bin`, and `romfs`.
After updating this branch in the `hookshot-build` container:

```sh
cd "$HOME/Hookshot-arm64"
source .venv/bin/activate
OOT_WORK="$HOME/.cache/hookshot-builder" RX_CPM="$PWD/cpm" RX_RELEASE=1 RX_VK_ONLY=1 \
  python -I nx2/build.py --standalone --jobs 2
```

Once that succeeds and the game is closed, copy
`$HOME/.cache/hookshot-builder/sa/build/hookshot_vk.so` into `$HOME/Games/Hookshot/`.
The log should contain `vk: guest memory copy fallback ON`. A successful build does not establish that the game
renders correctly; collect the startup log and validate visible graphics on the Odin.
