"""CPU-only upload tracking must not react to large GPU write declarations."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class CpuUploadTrackingTests(unittest.TestCase):
    def test_cpu_and_gpu_generations(self):
        source = (ROOT / "gpu/vk/vk_memory.cpp").read_text()
        cpu = function(source, "void stamp_cpu_page(")
        gpu = function(source, "void memory_stamp_gpu(")
        # Both sources of real CPU dirty pages must reach the upload tracker. GPU declarations keep
        # the existing texture generations and hazards, without entering this CPU-only index.
        self.assertIn("stamp_cpu_page(pages[i], g)", function(source, "void memory_collect_written("))
        self.assertIn("stamp_cpu_page(p, g)", function(source, "bool take_capture("))
        before = function(source, "void memory_copy_before_submit(")
        self.assertIn("copy_cpu_writes.runs(", before)
        self.assertNotIn("memory_written_runs(", before)
        fixture = r'''
#include <cassert>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>
#include "guest_copy_dirty.h"
using namespace mkwvk;
GuestCopyCpuWrites copy_cpu_writes;
bool guest_copy_mode=true;
uint32_t gen_now=1;
constexpr int kPageShift=12;
std::mutex gpu_pages_mu;
std::unordered_map<uint32_t,uint32_t> gpu_pages;
uint64_t generic_stamps=0;
void stamp_page(uint64_t, uint32_t) { ++generic_stamps; }
void memory_stamp(uint64_t, uint64_t) { ++generic_stamps; ++gen_now; }
bool gpu_pages_on() { return false; }
CPU_FUNCTION
GPU_FUNCTION
using Runs=std::vector<std::pair<uint64_t,uint64_t>>;
int main() {
    Runs ranges;
    copy_cpu_writes.runs(0,150ull<<20,0,ranges);
    assert(ranges.empty());
    memory_stamp_gpu(0,150ull<<20);
    copy_cpu_writes.runs(0,150ull<<20,0,ranges);
    assert(ranges.empty() && generic_stamps==1);
    stamp_cpu_page(12,++gen_now);
    copy_cpu_writes.runs(0,150ull<<20,0,ranges);
    assert((ranges==Runs{{12*4096,13*4096}}));
    ranges.clear();
    copy_cpu_writes.runs(0,150ull<<20,gen_now,ranges);
    assert(ranges.empty());
    guest_copy_mode=false;
    stamp_cpu_page(100,++gen_now);
    copy_cpu_writes.runs(0,150ull<<20,0,ranges);
    assert((ranges==Runs{{12*4096,13*4096}}) && generic_stamps==3);

    GuestCopyCpuWrites index;
    index.note(65535,1); index.note(65536,2); // chunk boundary, contiguous runs
    ranges.clear();
    index.runs(65535ull*4096+100,8092,0,ranges);
    assert((ranges==Runs{{0,8092}}));
    ranges.clear();
    index.runs(65535ull*4096+100,8092,1,ranges);
    assert((ranges==Runs{{3996,8092}}));
    ranges.clear();
    index.runs(0,0,0,ranges); index.runs(UINT64_MAX,100,0,ranges);
    assert(ranges.empty());
    constexpr uint64_t limit=1ull<<39;
    index.note((limit>>12)-1,3); index.note(limit>>12,4);
    index.runs(limit-100,SIZE_MAX,0,ranges);
    assert((ranges==Runs{{0,100}}));
    // Compare sparse regions and merged results with an independent page reference.
    GuestCopyCpuWrites random;
    std::map<uint32_t,uint32_t> pages;
    std::mt19937 rng(7);
    for(uint32_t g=1;g<=1000;++g) {
        const uint32_t p=rng()%200000;
        pages[p]=g; random.note(p,g);
    }
    for(int trial=0;trial<100;++trial) {
        const uint64_t lo=uint64_t(rng()%100000)*4096+17, n=uint64_t(rng()%100000)*4096+71;
        const uint32_t since=rng()%1001;
        Runs expected; ranges.clear();
        for(auto [p,g]:pages) {
            const uint64_t a=std::max(lo,uint64_t(p)*4096), b=std::min(lo+n,(uint64_t(p)+1)*4096);
            if(g<=since || a>=b) continue;
            if(!expected.empty() && expected.back().second==a-lo) expected.back().second=b-lo;
            else expected.push_back({a-lo,b-lo});
        }
        random.runs(lo,n,since,ranges);
        assert(ranges==expected);
    }
}
'''.replace("CPU_FUNCTION", cpu).replace("GPU_FUNCTION", gpu)
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "C++20 compiler required")
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "dirty.cpp"
            exe = Path(tmp) / ("dirty.exe" if os.name == "nt" else "dirty")
            src.write_text(fixture)
            subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "gpu/vk"),
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
