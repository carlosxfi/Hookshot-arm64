# ARM64 runtime sources

The ARM64 Linux port adapts register-preservation and signal-frame handling from
[seregasmith/ARM_Linux_totk_recomp](https://github.com/seregasmith/ARM_Linux_totk_recomp),
commit `d8f1229fe530fc8b668c7d0e6b55c0072e3ea140`, `runtime/platform/linux.c`:

- `host/hos_setjmp_arch.h`: AAPCS64 register set and floating-point control preservation, adapted to Hookshot's
  nonlocal-jump slots. This is not TotK's fiber scheduler. Hookshot also preserves FPSR.
- `host/posix/signal_context.h`: ARM64 PC access and ESR access-type decoding, with bounds checks on signal records.

Hookshot keeps its own a64-kit context, lifter, scheduler, 512 GiB guest window and NVN2 renderer.
The TotK SIMD lowering, game hooks and renderer are not imported. The existing a64-kit floating-point helpers
already have an AArch64 implementation.

The adapted code retains the following license:

MIT License

Copyright (c) 2026 Valiantsin Dzerakh

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

