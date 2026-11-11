"""CPU selection for native Linux ARM64 and x86-64 builds. RX_MARCH overrides the CPU level in build.py."""
import os
import platform


def host_march():
    if platform.machine().lower() in ("aarch64", "arm64"):
        if os.name == "nt":
            raise SystemExit("Native Windows ARM64 is not supported; use ARM64 Linux.")
        return "armv8-a"
    want = ("avx2", "fma", "bmi2", "movbe")
    try:
        if os.name == "nt":
            import ctypes
            pf = ctypes.windll.kernel32.IsProcessorFeaturePresent
            # (PF_AVX2_INSTRUCTIONS_AVAILABLE 40; PF_SSE4_2_INSTRUCTIONS_AVAILABLE 38. Windows reports no FMA/BMI2 bit, but
            # every AVX2 CPU has them, as x86-64-v3 counts them together)
            if pf(40):
                return "x86-64-v3"
            return "x86-64-v2" if pf(38) else "x86-64"
        with open("/proc/cpuinfo") as source:
            flags = source.read().split("flags", 1)[1].split(chr(10), 1)[0].split()
        if all(f in flags for f in want):
            return "x86-64-v3"
        return "x86-64-v2" if all(f in flags for f in ("sse4_2", "popcnt", "ssse3")) else "x86-64"
    except Exception:
        return "x86-64-v3"

def host_is_arm64():
    """Architecture of a native build. Cross compilation needs a matching sysroot and is not configured here."""
    return platform.machine().lower() in ("aarch64", "arm64")
