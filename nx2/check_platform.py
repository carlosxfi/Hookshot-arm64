"""Compile Hookshot's Linux host runtime without a game. Objects are not a playable executable.

Run on the target architecture: python3 nx2/check_platform.py
--core-only checks the architecture-dependent files and a64 runtime without SDL/FFmpeg/Opus headers.
The Vulkan renderer is checked separately with RX_VK_ONLY=1 python3 nx2/build.py --standalone.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kit
import rtgen
from host_cpu import host_is_arm64, host_march


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--core-only", action="store_true")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("This compile check is for native Linux builds")
    for name in (args.cc, args.cxx):
        if not shutil.which(name):
            parser.error("Compiler not found: " + name)
    root = Path(kit.ROOT)
    out = Path(kit.WORK) / "platform-check"
    gen, rt, objects = out / "gen", out / "rt64", out / "obj"
    for folder in (gen, rt, objects):
        folder.mkdir(parents=True, exist_ok=True)

    # Only translator/runtime source is used; no NSO, game image or game-derived C is read.
    emitter = out / "liba64emit.so"
    subprocess.run([args.cxx, "-std=c++20", "-O1", "-shared", "-fPIC",
                    str(root / "third_party/a64-kit/lift/a64emit.cpp"), "-o", str(emitter)], check=True)
    import build_emit
    build_emit.build = lambda force=False: str(emitter.resolve())
    import a64_lift
    import a64_rtgen
    a64_rtgen.write(str(gen))
    (gen / "a64_fpx.h").write_text(a64_lift.fpx_header(set()), encoding="utf-8")
    rtgen.write(str(rt))

    host = root / "host"
    if args.core_only:
        sources = [host / name for name in ("hos_setjmp.c", "hos_thread.c", "posix/prof.c", "posix/win32_posix.c")]
    else:
        excluded = {"pgo_rt.c", "display.c", "mf_av1.c", "rx_nowgpu.c"}
        sources = sorted(p for p in host.glob("*.c") if p.name not in excluded)
        sources += [host / "posix" / f for f in ("win32_posix.c", "display_sdl.c", "linux_stubs.c", "prof.c")]
    sources += [rt / "a64_rt.c", gen / "a64_suyu.c"]
    flags = ["-O2", "-g0", "-Wno-attributes", "-D_GNU_SOURCE", "-DA64_POLLING", "-DA64_WRITE_TRACK",
             "-ffp-contract=off", "-fno-strict-aliasing", "-march=" + (os.environ.get("RX_MARCH") or host_march())]
    if host_is_arm64():
        flags += ["-fsigned-char", "-fms-volatile"]
    else:
        flags += ["-mcx16"]
    flags += ["-I" + str(folder) for folder in (host / "posix", host, rt, gen,
                                               root / "third_party/opus/include")]
    if not args.core_only:
        flags += subprocess.check_output(["pkg-config", "--cflags", "sdl2", "libavcodec", "libswscale",
                                           "libswresample", "libavutil"], text=True).split()
    for i, source in enumerate(sources):
        print("[%d/%d] %s" % (i + 1, len(sources), source.name), flush=True)
        subprocess.run([args.cc, *flags, "-c", str(source), "-o", str(objects / (str(i) + ".o"))], check=True)
    report = {"architecture": platform.machine(), "core_only": args.core_only, "compiled": len(sources),
              "game_files_used": False, "linked_game": False}
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
