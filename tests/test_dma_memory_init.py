"""Exercise the renderer's actual DMA/host import startup and failure guard with a fake Vulkan device.

No game, Vulkan SDK or GPU is needed. Both Linux and non-Linux preprocessing paths are compiled.
"""
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


HEAD = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_RDWR
#define O_RDWR 2
#endif
#if TEST_LINUX
#ifndef __linux__
#define __linux__ 1
#endif
#else
#undef __linux__
#endif
constexpr int VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT = 1;
constexpr int VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 = 2;
constexpr int VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES = 3;
constexpr int VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES = 4;
constexpr int VK_ERROR_INVALID_EXTERNAL_HANDLE = -1000072003;
struct Base { int sType; void* pNext; };
struct VkPhysicalDeviceExternalMemoryHostPropertiesEXT {
    int sType; void* pNext = nullptr; uint64_t minImportedHostPointerAlignment = 0;
};
struct VkPhysicalDeviceProperties2 { int sType; void* pNext = nullptr; };
struct VkPhysicalDeviceVulkan11Properties { int sType; void* pNext = nullptr; uint64_t maxMemoryAllocationSize = 0; };
struct VkPhysicalDeviceVulkan13Properties { int sType; void* pNext = nullptr; uint64_t maxBufferSize = 0; };
struct { bool external_memory_host = false, dma_buf_import = false, sparse_buffers = true; int physical = 1; } dev;
uint64_t align = 4096, max_import_size = UINT64_MAX;
int dmabuf_memfd = -2, dmabuf_dev = -1, failures = 0, import_result = 0;
uint64_t dmabuf_limit = 0, dmabuf_piece = 0;
bool symbol_available = false, device_available = true, disabled = false;
bool guest_copy_mode = false;
const char* copy_override = nullptr;
int guest_fd = 17, open_calls = 0, sym_calls = 0, queries = 0, host_queries = 0, host_calls = 0, remembered = 0;
const char* piece_override = nullptr;
void host_proc() { ++host_calls; }
void (*vkGetMemoryHostPointerPropertiesEXT)() = host_proc;
void (*vkGetMemoryFdPropertiesKHR)() = host_proc;
void (*vkMapMemory)() = host_proc;
void (*vkSignalSemaphore)() = host_proc;
void logf(const char*, ...) {}
void import_remember_failure(uint64_t, uint64_t) { ++remembered; }
int fake_memfd() { return guest_fd; }
void* fake_dlsym(void*, const char*) { ++sym_calls; return symbol_available ? (void*)fake_memfd : nullptr; }
int fake_open(const char*, int) { ++open_calls; return device_available ? 19 : -1; }
const char* fake_getenv(const char* name) {
    if (!strcmp(name, "RX_VK_GUEST_COPY")) return copy_override;
    if (!strcmp(name, "RX_VK_NO_DMABUF")) return disabled ? "1" : nullptr;
    return !strcmp(name, "RX_VK_DMABUF_PIECE_MB") ? piece_override : nullptr;
}
FILE* fake_fopen(const char*, const char*) {
    FILE* f = tmpfile(); assert(f); fputs("64\n", f); rewind(f); return f;
}
void vkGetPhysicalDeviceProperties2(int, VkPhysicalDeviceProperties2* props) {
    ++queries;
    for (Base* b = (Base*)props->pNext; b; b = (Base*)b->pNext) {
        if (b->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT) {
            assert(dev.external_memory_host && vkGetMemoryHostPointerPropertiesEXT);
            ++host_queries;
            ((VkPhysicalDeviceExternalMemoryHostPropertiesEXT*)b)->minImportedHostPointerAlignment = 65536;
        } else if (b->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES)
            ((VkPhysicalDeviceVulkan11Properties*)b)->maxMemoryAllocationSize = 1024;
        else if (b->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES)
            ((VkPhysicalDeviceVulkan13Properties*)b)->maxBufferSize = 512;
        else assert(false);
    }
}
#define RTLD_DEFAULT nullptr
#define dlsym fake_dlsym
#define open fake_open
#define getenv fake_getenv
#define fopen fake_fopen
'''

MAIN = r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    const char* c = argv[1];
    if (!strcmp(c, "copy-forced")) { copy_override = "1"; dev.external_memory_host = true; }
    if (!strcmp(c, "copy-disabled")) copy_override = "0";
    if (!strcmp(c, "copy-no-signal")) vkSignalSemaphore = nullptr;
    bool expected = false;
    if (!strncmp(c, "guard-", 6)) {
        dev.external_memory_host = strcmp(c, "guard-extension") != 0;
        if (!strcmp(c, "guard-function")) vkGetMemoryHostPointerPropertiesEXT = nullptr;
        bool ok = guard(10, 20);
        expected = !strcmp(c, "guard-ok");
        assert(ok == expected && host_calls == (expected ? 1 : 0));
        assert(remembered == (expected ? 0 : 1));
        if (!expected) assert(import_result == VK_ERROR_INVALID_EXTERNAL_HANDLE);
        return 0;
    }
    if (!strcmp(c, "host")) dev.external_memory_host = expected = true;
    else if (!strncmp(c, "copy-", 5)) {}
    else {
        symbol_available = true; dev.dma_buf_import = true;
        expected = TEST_LINUX;
        if (!strcmp(c, "both")) dev.external_memory_host = expected = true;
        else if (!strcmp(c, "missing-symbol")) symbol_available = expected = false;
        else if (!strcmp(c, "anonymous")) { guest_fd = -1; expected = false; }
        else if (!strcmp(c, "missing-device")) device_available = expected = false;
        else if (!strcmp(c, "missing-extension")) dev.dma_buf_import = expected = false;
        else if (!strcmp(c, "missing-function")) { vkGetMemoryFdPropertiesKHR = nullptr; expected = false; }
        else if (!strcmp(c, "disabled")) { disabled = true; expected = false; }
        else if (!strcmp(c, "piece")) piece_override = "8";
        else assert(!strcmp(c, "dma"));
    }
    bool ok = memory_init();
    const bool copy_expected = !expected && strcmp(c, "copy-disabled") && strcmp(c, "copy-no-signal");
    assert(ok == (expected || copy_expected));
    assert(guest_copy_mode == (copy_expected || !strcmp(c, "copy-no-signal")));
    assert(queries == (ok ? 1 : 0));
    if (ok) {
        assert(max_import_size == 512);
        assert(align == (dev.external_memory_host && !copy_expected ? 65536 : 4096));
        assert(host_queries == (dev.external_memory_host ? 1 : 0));
    }
    if (!TEST_LINUX) assert(sym_calls == 0 && open_calls == 0);
    if (TEST_LINUX && (!strcmp(c, "dma") || !strcmp(c, "piece"))) {
        assert(dmabuf_memfd == 17 && dmabuf_dev == 19);
        assert(dmabuf_piece == ((!strcmp(c, "piece") ? 8ull : 64ull) << 20));
        assert(dma_buf_init());
        assert(open_calls == 1 && sym_calls == 1); // startup and first pool reuse the same setup
    }
}
'''


class DmaMemoryTests(unittest.TestCase):
    def test_actual_startup_and_failed_import_guard(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "A C++ compiler is required for the memory-import check")
        source = (ROOT / "gpu/vk/vk_memory.cpp").read_text(encoding="utf-8")
        start = source.index("    // A failed DMA import cannot")
        end = source.index("    // (the host allocations' memory type", start)
        guard = "bool guard(uint64_t lo, uint64_t hi) {\n" + source[start:end] + "vkGetMemoryHostPointerPropertiesEXT(); return true; }\n"
        actual = function(source, "bool dma_buf_init()") + "\n" + function(source, "bool memory_init()")
        cases = ("host", "dma", "both", "missing-symbol", "anonymous", "missing-device", "missing-extension",
                 "missing-function", "disabled", "piece", "guard-extension", "guard-function", "guard-ok",
                 "copy-forced", "copy-disabled", "copy-no-signal")
        with tempfile.TemporaryDirectory(prefix="hookshot-memory-init-") as folder:
            cpp = Path(folder) / "fixture.cpp"
            cpp.write_text(HEAD + actual + guard + MAIN, encoding="utf-8")
            exe = Path(folder) / ("fixture.exe" if os.name == "nt" else "fixture")
            for linux in (0, 1):
                subprocess.run([compiler, "-std=c++20", "-O2", "-DTEST_LINUX=" + str(linux), str(cpp), "-o", str(exe)], check=True)
                for case in cases:
                    with self.subTest(linux=linux, case=case):
                        subprocess.run([str(exe), case], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
