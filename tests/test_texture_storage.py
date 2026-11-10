"""Exercise the renderer's real format selection and block footprint without a Vulkan device."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TextureStorageTests(unittest.TestCase):
    def test_native_and_decoded_storage(self):
        source = (ROOT / "gpu/vk/vk_texture.cpp").read_text()
        start = source.index("    uint32_t cost_bw = 1, cost_bh = 1;")
        end = source.index("    // (the budget counts", start)
        selection = source[start:end]
        fixture = r'''
#include <cassert>
#include "texture_storage.h"
using namespace mkwvk;
enum Format {
    RGBA8=0, VK_FORMAT_BC1_RGBA_SRGB_BLOCK=4, VK_FORMAT_BC4_UNORM_BLOCK=9,
    VK_FORMAT_BC4_SNORM_BLOCK=10, ASTC=100
};
bool is_astc(int f) { return f == ASTC; }
bool is_bc(int f) { return f >= 1 && f <= 14; }
struct Texture {
    uint32_t width=8, height=8, depth=1, layers=1, levels=4, samples=1;
    int format=RGBA8;
    bool sparse=false;
    uint64_t bytes=0;
};
uint64_t estimate(Texture x, bool astc, int bw, int bh, int bpb, int from=0) {
SELECTION
    return x.bytes;
}
int main() {
    Texture x;
    assert(estimate(x,false,1,1,4)==340); // 8x8, 4x4, 2x2, 1x1 RGBA8
    assert(estimate(x,true,6,6,16)==340); // decoded ASTC is RGBA8
    assert(estimate(x,false,1,1,3,3)==340); // RGB widened to RGBA
    x.samples=4;
    assert(estimate(x,false,1,1,4)==1360);
    x.samples=1; x.depth=8;
    assert(estimate(x,false,1,1,4)==2340); // 3D depth shrinks with the mips
    x.depth=1;
    for(int f=1;f<=14;++f) {
        x.format=f;
        // Each chain has 4+1+1+1 blocks. BC1 and BC4 use 8 bytes, others 16.
        assert(estimate(x,false,4,4,16)==uint64_t(7*((f<=4 || f==9 || f==10)?8:16)));
    }
    x.format=ASTC;
    assert(estimate(x,false,4,4,16)==112);
    assert(estimate(x,false,6,6,16)==112); // incomplete blocks round up
    x.width=x.height=1365; x.layers=90; x.levels=11;
    assert(estimate(x,false,8,8,16)==56365920);
    x.sparse=true;
    assert(estimate(x,false,8,8,16)==0); // sparse residency accounts for its own pages
}
'''.replace("SELECTION", selection)
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "C++20 compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "storage.cpp"
            exe = Path(tmp) / ("storage.exe" if os.name == "nt" else "storage")
            src.write_text(fixture)
            subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "gpu/vk"),
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
