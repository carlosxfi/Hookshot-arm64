"""Compile the real transfer, allocation, and submit code against a deterministic Vulkan device.

Tests require a C++20 compiler, but no SDK, game, display, or GPU. Fence callbacks deliberately check
that results are not published before CPU write-back. The transfer tests model independent CPU/GPU writes.
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


TRANSFER = r'''
#include <array>
#include <cassert>
#include <random>
#include "guest_copy.h"
using mkwvk::GuestCopy;
int main() {
    constexpr size_t N = 8195; // a partial last page, with canaries around mapped memory
    std::vector<uint8_t> guest(N, 7), storage(N + 2, 0xAC);
    uint8_t* gpu = storage.data() + 1;
    GuestCopy copy;
    copy.init(gpu, guest.data(), N);
    assert(!memcmp(gpu, guest.data(), N));
    assert(copy.upload(gpu, guest.data()) == 0);
    guest[4095] = 11; guest[4096] = 12; guest[N-1] = 13;
    assert(copy.upload(gpu, guest.data()) == N);
    assert(!memcmp(gpu, guest.data(), N));
    // Concurrent CPU edits survive where the GPU did not change the bytes.
    guest[3] = 45; guest[8] = 46;
    gpu[4] = 60; gpu[N-1] = 61;
    copy.note_write(8, 1); // GPU stored its previous value, still overrides the CPU's conflicting write
    copy.download(guest.data(), gpu);
    assert(guest[3] == 45 && guest[4] == 60 && guest[8] == 7 && guest[N-1] == 61);
    copy.upload(gpu, guest.data());
    guest[8] = 99; // the previous submission's explicit-write range must have been cleared
    copy.download(guest.data(), gpu);
    assert(guest[8] == 99);
    // Overlap, adjacent ranges, clamping at the pool end, overflow, and empty/out-of-bounds writes.
    copy.upload(gpu, guest.data());
    copy.note_write(1, 6); copy.note_write(3, 7); copy.note_write(10, 2);
    copy.note_write(N-1, SIZE_MAX); copy.note_write(N, 1); copy.note_write(0, 0);
    std::fill(guest.begin(), guest.end(), 255);
    copy.download(guest.data(), gpu);
    assert(guest[0] == 255 && guest[1] == gpu[1] && guest[11] == gpu[11] && guest[12] == 255);
    assert(guest[N-1] == gpu[N-1]);
    assert(storage.front() == 0xAC && storage.back() == 0xAC);
    // Reference model across repeated submits and page boundaries, including stores of unchanged bytes.
    std::mt19937 rng(44);
    std::vector<uint8_t> model(N), baseline(N);
    for (int submit = 0; submit < 80; ++submit) {
        for (int j = 0; j < 20; ++j) guest[rng() % N] = uint8_t(rng());
        copy.upload(gpu, guest.data());
        baseline.assign(gpu, gpu + N);
        for (int j = 0; j < 20; ++j) guest[rng() % N] = uint8_t(rng());
        for (int j = 0; j < 20; ++j) gpu[rng() % N] = uint8_t(rng());
        size_t lo = rng() % N, len = std::min<size_t>(rng() % 200, N-lo);
        copy.note_write(lo, len);
        model = guest;
        for (size_t i = 0; i < N; ++i) if (gpu[i] != baseline[i]) model[i] = gpu[i];
        std::copy(gpu + lo, gpu + lo + len, model.begin() + lo);
        copy.download(guest.data(), gpu);
        assert(guest == model);
    }
    GuestCopy empty;
    assert(empty.upload(nullptr, nullptr) == 0);
    assert(empty.download(nullptr, nullptr) == 0);
    // Notifications invalidate cached CPU reads of writes discovered only after GPU execution.
    copy.upload(gpu, guest.data());
    gpu[4096] ^= 1;
    bool notified = false;
    copy.download(guest.data(), gpu, [&](size_t lo, size_t n) {
        assert(lo == 4096 && n == 4096); notified = true;
    });
    assert(notified);
    // Tracked mode: inspect only dirty CPU ranges and copy only recorded GPU destinations.
    GuestCopy tracked;
    std::vector<uint8_t> tg(N, 7), tm(N, 0);
    tracked.init(tm.data(), tg.data(), N);
    tg[3] = 10; tg[4096] = 11;
    assert(tracked.upload_range(tm.data(), tg.data(), 0, 4096) == 4096);
    assert(tm[3] == 10 && tm[4096] == 7);
    assert(tracked.upload_range(tm.data(), tg.data(), 4096, SIZE_MAX) == 4096);
    assert(tm[4096] == 11);
    assert(tracked.upload_range(tm.data(), tg.data(), N, SIZE_MAX) == 0);
    tg[3] = 99; tg[100] = 88; tm[4] = 55;
    tracked.note_write(3, 2); tracked.note_write(4, 2);
    size_t marked = 0;
    assert(tracked.download_tracked(tg.data(), tm.data(), [&](size_t lo, size_t n) {
        assert(lo == 3 && n == 3); marked += n;
    }) == 3);
    assert(marked == 3 && tg[3] == 10 && tg[4] == 55 && tg[100] == 88);
    assert(tracked.download_tracked(tg.data(), tm.data(), [](size_t, size_t) { assert(false); }) == 0);
    assert(tracked.upload_range(tm.data(), tg.data(), 0, 4096) == 4096 && tm[100] == 88);
    // A wide writable descriptor with no actual changes must not invalidate any guest pages.
    tracked.note_write(0, N);
    assert(tracked.download_tracked(tg.data(), tm.data(), [](size_t, size_t) { assert(false); }) == N);
    // GPU stores of unchanged values still win against concurrent CPU edits inside the binding.
    tg[4096] = 77;
    tracked.note_write(0, N);
    marked = 0;
    tracked.download_tracked(tg.data(), tm.data(), [&](size_t lo, size_t n) {
        assert(lo == 4096 && n == 4096); marked += n;
    });
    assert(marked == 4096 && tg[4096] == 11);
    // A partial first/last page may not publish outside its binding, even when neighbours differ.
    tg[4093] ^= 1; tg[4100] ^= 1;
    const auto left = tg[4093], right = tg[4100];
    tm[4095] ^= 1; tm[4096] ^= 1;
    tracked.note_write(4094, 6);
    marked = 0;
    tracked.download_tracked(tg.data(), tm.data(), [&](size_t lo, size_t n) {
        assert((lo == 4094 && n == 2) || (lo == 4096 && n == 4)); marked += n;
    });
    assert(marked == 6 && tg[4093] == left && tg[4100] == right);
    assert(tg[4095] == tm[4095] && tg[4096] == tm[4096]);
    // Reference model for merged wide writes, sparse GPU changes, and racing CPU data.
    for (int submit = 0; submit < 80; ++submit) {
        tracked.upload(tm.data(), tg.data());
        for (int j = 0; j < 20; ++j) tg[rng() % N] = uint8_t(rng());
        for (int j = 0; j < 20; ++j) tm[rng() % N] = uint8_t(rng());
        model = tg;
        for (int j = 0; j < 8; ++j) {
            const size_t lo = rng() % N, n = std::min<size_t>(rng() % 5000, N - lo);
            tracked.note_write(lo, n);
            std::copy(tm.begin() + lo, tm.begin() + lo + n, model.begin() + lo);
        }
        tracked.download_tracked(tg.data(), tm.data(), [](size_t, size_t) {});
        assert(tg == model);
    }
}
'''

VULKAN = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "guest_copy.h"
using mkwvk::GuestCopy;
using VkResult = int;
using VkBuffer = uint64_t;
using VkDeviceMemory = uint64_t;
using VkDeviceAddress = uint64_t;
constexpr int VK_SUCCESS=0, VK_ERROR_OUT_OF_DEVICE_MEMORY=-2, VK_ERROR_INITIALIZATION_FAILED=-3;
constexpr int VK_ERROR_DEVICE_LOST=-4, VK_NULL_HANDLE=0, VK_TRUE=1;
constexpr int VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO=1, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO=2;
constexpr int VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO=3, VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO=4;
constexpr int VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO=5, VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO=6;
constexpr int VK_STRUCTURE_TYPE_SUBMIT_INFO_2=7, VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO=8;
constexpr int VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT=1, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT=2;
constexpr int VK_MEMORY_PROPERTY_HOST_CACHED_BIT=4, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT=8;
constexpr int VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT=1, kUsage=123, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT=999;
struct VkBufferCreateInfo { int sType; uint64_t size=0; int usage=0; };
struct VkMemoryRequirements { uint64_t size=65536; uint32_t memoryTypeBits=15; };
struct VkMemoryAllocateFlagsInfo { int sType; int flags=0; };
struct VkMemoryAllocateInfo { int sType; const void* pNext=nullptr; uint64_t allocationSize=0; uint32_t memoryTypeIndex=0; };
struct VkBufferDeviceAddressInfo { int sType; VkBuffer buffer=0; };
struct VkSemaphoreSubmitInfo { int sType; uint64_t semaphore=0, value=0, stageMask=0; };
struct VkCommandBufferSubmitInfo { int sType; uint64_t commandBuffer=0; };
struct VkSubmitInfo2 {
    int sType; uint32_t commandBufferInfoCount=0; const VkCommandBufferSubmitInfo* pCommandBufferInfos=nullptr;
    uint32_t signalSemaphoreInfoCount=0; const VkSemaphoreSubmitInfo* pSignalSemaphoreInfos=nullptr;
};
struct VkSemaphoreSignalInfo { int sType; uint64_t semaphore=0, value=0; };
struct { int device=1, queue=2; struct { uint32_t memoryTypeCount=4;
    struct { uint32_t propertyFlags; } memoryTypes[4]={{1},{3},{7},{15}}; } memory; std::mutex present_mu; } dev;
struct Pool { uint64_t lo=0, hi=0; VkBuffer buffer=0; VkDeviceMemory memory=0; VkDeviceAddress address=0;
    uint8_t* copy_map=nullptr; std::shared_ptr<GuestCopy> copy; };
uint64_t max_import_size=65536;
VkResult import_result=VK_SUCCESS;
int fail_at=0, destroyed=0, freed=0, remembered=0;
std::vector<uint8_t> guest(65536, 22), mapped(65536, 0);
uint8_t* guest_base() { return guest.data(); }
struct { uint8_t* (*guest_base)(); } host_obj{guest_base};
auto host = &host_obj;
void import_remember_failure(uint64_t, uint64_t) { ++remembered; }
VkResult vkCreateBuffer(int, const VkBufferCreateInfo* b, void*, VkBuffer* out) {
    assert(b->size==4096 && b->usage==kUsage);
    if (fail_at==1) return -2; *out=11; return 0;
}
void vkDestroyBuffer(int, VkBuffer b, void*) { assert(b==11); ++destroyed; }
void vkFreeMemory(int, VkDeviceMemory m, void*) { assert(m==12); ++freed; }
void vkGetBufferMemoryRequirements(int, VkBuffer, VkMemoryRequirements* req) {
    if(fail_at==7) req->memoryTypeBits=1; // visible, but not coherent
    if(fail_at==8) req->size=131072; // buffer padding exceeds the allocation limit
}
VkResult vkAllocateMemory(int, const VkMemoryAllocateInfo* ai, void*, VkDeviceMemory* out) {
    assert(ai->allocationSize==65536 && ai->memoryTypeIndex==3);
    assert(((VkMemoryAllocateFlagsInfo*)ai->pNext)->flags==VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);
    if(fail_at==2) return -2; *out=12; return 0;
}
VkResult vkBindBufferMemory(int, VkBuffer, VkDeviceMemory, uint64_t) { return fail_at==3 ? -2 : 0; }
VkResult vkMapMemory(int, VkDeviceMemory, uint64_t, uint64_t n, int, void** out) {
    assert(n==65536); if(fail_at==4) return -2; *out=mapped.data(); return 0;
}
VkDeviceAddress vkGetBufferDeviceAddress(int, const VkBufferDeviceAddressInfo*) { return fail_at==5 ? 0 : 0xBEEF; }

// The real submit_recording function runs with these deterministic API callbacks.
struct Recording { int slot=0; uint64_t upload=1, cmd=2, value=3, fence=4, batch=0; bool submitted=false; };
uint64_t timeline=41, rec_timeline=42, rec_seq=0, batch_next=0, seq=0;
std::mutex rec_map_mu;
std::map<uint64_t,uint64_t> value_rec;
std::set<uint64_t> rec_open_values;
struct { std::vector<VkCommandBufferSubmitInfo> cbs; uint64_t signal=0, rec=0; bool any_signal=false; } pend;
std::chrono::steady_clock::time_point pend_since;
int flush_reason=0, FR_CAP=1;
bool copy_mode=true, writeback_done=false;
int submit_result=0, wait_result=0, signal_result=0;
std::vector<std::string> events;
bool memory_guest_copy() { return copy_mode; }
bool texture_uploads_recorded() { return true; }
void gprof_submitted(int, bool) {}
void gpu_time_end(Recording&) {}
void memory_copy_before_submit(uint64_t cmd) { assert(cmd==2); events.push_back("upload"); }
void memory_copy_after_submit() { events.push_back("writeback"); writeback_done=true; }
void vkEndCommandBuffer(uint64_t) {}
bool batch_on() { return false; }
void queue_flush_locked() {}
void logf(const char*, ...) {}
[[noreturn]] void device_lost() { throw std::runtime_error("lost"); }
[[noreturn]] void replay_fatal(const char*) { throw std::runtime_error("fatal"); }
#define PROF(x) ((void)0)
VkResult vkQueueSubmit2(int, int, const VkSubmitInfo2* si, uint64_t fence) {
    assert(fence==4 && si->commandBufferInfoCount==2);
    assert(si->signalSemaphoreInfoCount==(copy_mode?0u:2u));
    events.push_back("submit"); return submit_result;
}
VkResult vkWaitForFences(int, int, const uint64_t* f, int, uint64_t) {
    assert(*f==4 && !writeback_done); events.push_back("fence"); return wait_result;
}
VkResult vkSignalSemaphore(int, const VkSemaphoreSignalInfo* info) {
    assert(writeback_done); // Publishing completion before read-back is a real guest-memory race.
    events.push_back(info->semaphore==timeline ? "timeline" : "recording");
    assert(info->value==(info->semaphore==timeline?3u:1u));
    return signal_result;
}
'''

MAIN = r'''
int main(int argc, char** argv) {
    assert(argc==2);
    std::string c=argv[1];
    if(c.rfind("allocation-",0)==0) {
        fail_at=std::stoi(c.substr(11));
        Pool p;
        assert(copy_pool(0,4096,p)==(fail_at==0));
        if(fail_at==0) {
            assert(p.memory==12 && p.buffer==11 && p.address==0xBEEF && p.copy_map==mapped.data());
            assert(!memcmp(mapped.data(),guest.data(),4096));
            assert(!destroyed && !freed && !remembered);
        } else {
            assert(!p.memory && !p.buffer && !p.copy_map && !p.copy);
            assert(remembered==1 && destroyed==(fail_at==1?0:1));
            assert(freed==((fail_at>=3 && fail_at<=5)?1:0));
        }
        return 0;
    }
    if(c=="native") copy_mode=false;
    if(c=="submit-error") submit_result=-2;
    if(c=="device-lost") submit_result=-4;
    if(c=="wait-error") wait_result=-2;
    if(c=="wait-lost") wait_result=-4;
    if(c=="signal-error") signal_result=-2;
    Recording r;
    bool threw=false;
    try { submit_recording(r); } catch(const std::runtime_error&) { threw=true; }
    const bool fails=c!="ok" && c!="native";
    assert(threw==fails);
    if(c=="ok") assert((events==std::vector<std::string>{"upload","submit","fence","writeback","timeline","recording"}));
    if(c=="native") assert((events==std::vector<std::string>{"upload","submit"}));
    if(c=="submit-error" || c=="device-lost") assert((events==std::vector<std::string>{"upload","submit"}));
    if(c=="wait-error" || c=="wait-lost") assert((events==std::vector<std::string>{"upload","submit","fence"}));
    if(c=="signal-error") assert(events.back()=="timeline");
}
'''


class GuestCopyTests(unittest.TestCase):
    def compile_run(self, source, cases=(None,)):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "C++20 compiler required")
        with tempfile.TemporaryDirectory(prefix="hookshot-copy-") as folder:
            cpp = Path(folder) / "fixture.cpp"
            exe = Path(folder) / ("fixture.exe" if os.name == "nt" else "fixture")
            cpp.write_text(source, encoding="utf-8")
            subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wno-missing-field-initializers",
                            "-Wno-misleading-indentation", "-I" + str(ROOT / "gpu/vk"), str(cpp), "-o", str(exe)], check=True)
            for case in cases:
                with self.subTest(case=case):
                    subprocess.run([str(exe)] + ([case] if case else []), check=True, timeout=15)

    def test_cpu_gpu_transfer_and_conflicts(self):
        self.compile_run(TRANSFER)

    def test_pointer_stores_force_conservative_scanning(self):
        memory = (ROOT / "gpu/vk/vk_memory.cpp").read_text(encoding="utf-8")
        policy = function(memory, "bool copy_full_scan()")
        fixture = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
bool follow=false, full=false;
const char* fake_getenv(const char* key) {
    if (!strcmp(key,"RX_VK_FOLLOW_STORES")) return follow ? "0" : nullptr;
    if (!strcmp(key,"RX_VK_GUEST_COPY_FULL_SCAN")) return full ? "1" : nullptr;
    return nullptr;
}
#define getenv fake_getenv
''' + policy + r'''
int main(int argc,char** argv) {
    assert(argc==2);
    follow=!strcmp(argv[1],"follow"); full=!strcmp(argv[1],"full");
    assert(copy_full_scan()==(follow||full));
}
'''
        self.compile_run(fixture, ("tracked", "follow", "full"))

    def test_cached_storage_texel_views_report_every_write(self):
        texture = (ROOT / "gpu/vk/vk_texture.cpp").read_text(encoding="utf-8")
        actual = function(texture, "VkBufferView texel_buffer_view(")
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <unordered_map>
using VkBufferView=uint64_t;
using VkFormat=int;
constexpr int VK_NULL_HANDLE=0, VK_FORMAT_UNDEFINED=0, VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO=1;
struct nvn_tex { uint64_t gpu; struct { uint32_t width=16; } d; };
struct nvn_view {};
struct VkBufferViewCreateInfo { int sType; uint64_t buffer=0; int format=0; uint64_t offset=0,range=0; };
struct BufferRef { uint64_t buffer=1,offset=0; explicit operator bool() const {return buffer!=0;} };
struct { int device=1; struct { struct { uint64_t minTexelBufferOffsetAlignment=16,maxTexelBufferElements=1024; } limits; } props; } dev;
int dirty=0, notes=0, views=0, lookups=0;
int view_nvn_format(const nvn_tex*,const nvn_view*) {return 1;}
struct Format {int format=1;};
Format vk_format(int) {return {};}
int nvn_block_size(int,int*,int*) {return 4;}
void texture_gpu_wrote(uint64_t g,uint64_t n) {assert(g==4096 && n==64);++dirty;}
void memory_note_gpu_write(uint64_t g,uint64_t n) {assert(g==4096 && n==64);++notes;}
uint64_t mix(uint64_t a,uint64_t b) {return a*65537+b;}
BufferRef gpu_buffer(uint64_t,uint64_t) {++lookups;return {};}
int vkCreateBufferView(int,const VkBufferViewCreateInfo*,void*,VkBufferView* view) {*view=++views;return 0;}
''' + actual + r'''
int main() {
    nvn_tex tex{4096};
    assert(texel_buffer_view(&tex,nullptr,false)==1 && notes==0 && dirty==0);
    assert(texel_buffer_view(&tex,nullptr,true)==2 && notes==1 && dirty==1);
    assert(texel_buffer_view(&tex,nullptr,true)==2 && notes==2 && dirty==2);
    assert(views==2 && lookups==2); // cached view still registers the next submission's write
}
'''
        self.compile_run(fixture)

    def test_actual_allocation_and_completion_order(self):
        memory = (ROOT / "gpu/vk/vk_memory.cpp").read_text(encoding="utf-8")
        replay = (ROOT / "gpu/vk/vk_replay.cpp").read_text(encoding="utf-8")
        actual = function(memory, "bool copy_pool(") + "\n" + function(replay, "void submit_recording(")
        self.compile_run(VULKAN + actual + MAIN,
                         tuple("allocation-" + str(i) for i in (0,1,2,3,4,5,7,8)) +
                         ("ok", "native", "submit-error", "device-lost", "wait-error", "wait-lost", "signal-error"))


if __name__ == "__main__":
    unittest.main()
