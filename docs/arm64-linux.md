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
FFmpeg development headers (`libavcodec`, `libswscale`, `libswresample`, `libavutil`). Then run:

```sh
python3 tests/test_arm64_platform.py
python3 nx2/check_platform.py
```

The first command runs nested nonlocal jumps, checks floating-point rounding restoration, exercises real
read/write/execute faults through the POSIX exception layer, and checks CPU detection. It runs at `-O0` and `-O2`.
The second compiles the host and generated runtime into objects under `work/platform-check`; it does not link a game.
`--core-only` checks the six architecture-dependent/runtime units without media development packages.

The **ARM64 Linux platform checks** workflow also builds and links `hookshot_vk.so`, using Hookshot's existing
`RX_VK_ONLY=1 python3 nx2/build.py --standalone --jobs 2` path and pinned Vulkan/SPIR-V headers. This checks CPU
compilation and link symbols. It does not run Vulkan or establish GPU compatibility.

## Remaining validation

- Build the supported Zelda game version from the user's own files on ARM64 Linux, with a native Clang toolchain
  and Python dependencies from Hookshot's source-build instructions.
- Check generated game-code compilation, thread synchronization, fibers, saves, movies and gameplay on the device.
- Confirm that the device's Vulkan driver exposes the Vulkan 1.3 and mesh-shader features Hookshot requires.
  TotK's renderer working on Turnip does not validate Hookshot's Switch 2 renderer.
- The fixed guest base at 16 TiB plus the 512 GiB reservation must fit the device's userspace virtual address range.
- Build and package an ARM64 builder/tool bundle separately; the x86 AppImage packaging scripts are not ported here.

Cross compilation, a Windows ARM64 target, performance tuning and a mesh-shader fallback are outside this first milestone.
