#!/usr/bin/env python3
"""Build/run the game-free Linux dma-buf export probe using existing headers."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main():
    if not sys.platform.startswith("linux"):
        raise SystemExit("Run this on the Odin's Linux system, inside hookshot-build.")
    root = Path(__file__).resolve().parents[2]
    compiler = shlex.split(os.environ.get("CXX", ""))
    if not compiler:
        compiler = [shutil.which("clang++") or shutil.which("g++") or "c++"]
    includes = []
    override = os.environ.get("VULKAN_HEADERS")
    if override:
        includes.append(Path(override))
        includes.append(Path(override) / "include")
    includes.append(Path("/usr/include"))
    for cache in (root / "cpm", root / "_deps/vulkan-headers"):
        if cache.is_dir():
            includes.extend(p.parent.parent for p in cache.rglob("vulkan_core.h"))
    header = next((p for p in includes if (p / "vulkan/vulkan.h").is_file()), None)
    if header is None:
        raise SystemExit("Vulkan headers not found. Set VULKAN_HEADERS to the include directory containing vulkan/vulkan.h.")
    with tempfile.TemporaryDirectory(prefix="hookshot-shared-probe-") as folder:
        binary = Path(folder) / "probe"
        subprocess.run(compiler + ["-std=c++17", "-O2", "-Wall", "-Wextra",
                       "-Wno-missing-field-initializers", "-I", str(header),
                       str(Path(__file__).with_suffix(".cpp")), "-l:libvulkan.so.1", "-o", str(binary)], check=True)
        return subprocess.run([str(binary)] + sys.argv[1:]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
