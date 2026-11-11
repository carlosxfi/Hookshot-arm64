// Game-free Linux probe for GPU-owned guest memory. No game addresses are remapped.
// Build: c++ -std=c++17 probe_shared_memory.cpp -l:libvulkan.so.1 -o /tmp/hookshot-shared-probe
#include <vulkan/vulkan.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <string>

static void check(VkResult r, const char* operation) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(r));
}

struct Probe {
    VkInstance instance{};
    VkDevice device{};
    VkCommandPool pool{};
    VkFence fence{};
    VkBuffer source{}, destination{};
    VkDeviceMemory source_memory{}, destination_memory{};
    void* mapped_source{};
    void* mapped_destination{};
    void* alias = MAP_FAILED;
    int fd = -1;
    static constexpr size_t bytes = 65536;
    ~Probe() {
        if (device) vkDeviceWaitIdle(device);
        if (alias != MAP_FAILED) munmap(alias, bytes);
        if (fd >= 0) close(fd);
        if (device) {
            if (mapped_source) vkUnmapMemory(device, source_memory);
            if (mapped_destination) vkUnmapMemory(device, destination_memory);
            if (source) vkDestroyBuffer(device, source, nullptr);
            if (destination) vkDestroyBuffer(device, destination, nullptr);
            if (source_memory) vkFreeMemory(device, source_memory, nullptr);
            if (destination_memory) vkFreeMemory(device, destination_memory, nullptr);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }
};

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        Probe p;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Hookshot shared-memory probe";
        app.apiVersion = VK_API_VERSION_1_2;
        VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ii.pApplicationInfo = &app;
        check(vkCreateInstance(&ii, nullptr, &p.instance), "create instance");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(p.instance, &count, nullptr), "count GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(p.instance, &count, devices.data()), "enumerate GPUs");
        VkPhysicalDevice gpu{};
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate, &props);
            printf("device: %s (type %u)\n", props.deviceName, props.deviceType);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
            if (argc > 1 && !strstr(props.deviceName, argv[1])) continue;
            if (!gpu || strstr(props.deviceName, "Adreno")) gpu = candidate;
        }
        if (!gpu) throw std::runtime_error("no matching hardware GPU; refusing a software-renderer result");
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        printf("selected: %s\n", props.deviceName);
        check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr), "count extensions");
        std::vector<VkExtensionProperties> extensions(count);
        check(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data()), "extensions");
        const char* required[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
        for (const char* name : required) {
            bool found = false;
            for (const auto& e : extensions) found |= !strcmp(e.extensionName, name);
            if (!found) throw std::runtime_error(std::string("missing ") + name);
        }
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VkPhysicalDeviceExternalBufferInfo ei{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO};
        ei.usage = usage;
        ei.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkExternalBufferProperties ep{VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
        vkGetPhysicalDeviceExternalBufferProperties(gpu, &ei, &ep);
        printf("dma-buf buffer features: %#x\n", ep.externalMemoryProperties.externalMemoryFeatures);
        if (!(ep.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT))
            throw std::runtime_error("driver cannot export this buffer usage as dma-buf");
        VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &bda;
        vkGetPhysicalDeviceFeatures2(gpu, &features);
        if (!bda.bufferDeviceAddress) throw std::runtime_error("missing buffer device addresses");
        bda.bufferDeviceAddressCaptureReplay = bda.bufferDeviceAddressMultiDevice = VK_FALSE;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
        uint32_t family = 0;
        while (family < count && !(families[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) ++family;
        if (family == count) throw std::runtime_error("no graphics/compute queue");
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.pNext = &bda; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
        di.enabledExtensionCount = 2; di.ppEnabledExtensionNames = required;
        check(vkCreateDevice(gpu, &di, nullptr, &p.device), "create device");
        auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(p.device, "vkGetMemoryFdKHR"));
        if (!get_fd) throw std::runtime_error("no vkGetMemoryFdKHR entry point");
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(gpu, &memory);
        auto allocate = [&](bool exported, VkBuffer& buffer, VkDeviceMemory& allocation, void*& mapped) {
            VkExternalMemoryBufferCreateInfo eb{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO};
            eb.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bi.pNext = exported ? &eb : nullptr; bi.size = Probe::bytes; bi.usage = usage;
            check(vkCreateBuffer(p.device, &bi, nullptr, &buffer), "create buffer");
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(p.device, buffer, &req);
            uint32_t type = UINT32_MAX;
            int best = -1;
            for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
                auto flags = memory.memoryTypes[i].propertyFlags;
                constexpr auto needed = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                if (!(req.memoryTypeBits & (1u << i)) || (flags & needed) != needed) continue;
                int score = (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? 2 : 0) +
                            (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? 1 : 0);
                if (score > best) best = score, type = i;
            }
            if (type == UINT32_MAX) throw std::runtime_error("no coherent host-visible memory type");
            printf("%s memory: type %u, flags %#x, allocation %llu bytes\n", exported ? "export" : "copy",
                   type, memory.memoryTypes[type].propertyFlags, (unsigned long long)req.size);
            VkMemoryAllocateFlagsInfo af{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
            af.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
            dedicated.buffer = buffer; dedicated.pNext = &af;
            VkExportMemoryAllocateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
            export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            export_info.pNext = &dedicated;
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.pNext = exported ? static_cast<void*>(&export_info) : static_cast<void*>(&af);
            ai.allocationSize = req.size; ai.memoryTypeIndex = type;
            check(vkAllocateMemory(p.device, &ai, nullptr, &allocation), "allocate memory");
            check(vkBindBufferMemory(p.device, buffer, allocation, 0), "bind memory");
            check(vkMapMemory(p.device, allocation, 0, req.size, 0, &mapped), "map Vulkan memory");
            VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
            address.buffer = buffer;
            if (!vkGetBufferDeviceAddress(p.device, &address)) throw std::runtime_error("zero GPU buffer address");
        };
        allocate(true, p.source, p.source_memory, p.mapped_source);
        allocate(false, p.destination, p.destination_memory, p.mapped_destination);
        VkMemoryGetFdInfoKHR fi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        fi.memory = p.source_memory; fi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        check(get_fd(p.device, &fi, &p.fd), "export dma-buf fd");
        p.alias = mmap(nullptr, Probe::bytes, PROT_READ | PROT_WRITE, MAP_SHARED, p.fd, 0);
        if (p.alias == MAP_FAILED) throw std::runtime_error("dma-buf mmap failed: " + std::string(strerror(errno)));
        VkQueue queue{};
        vkGetDeviceQueue(p.device, family, 0, &queue);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pi.queueFamilyIndex = family;
        check(vkCreateCommandPool(p.device, &pi, nullptr, &p.pool), "command pool");
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ci.commandPool = p.pool; ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ci.commandBufferCount = 1;
        VkCommandBuffer cmd{};
        check(vkAllocateCommandBuffers(p.device, &ci, &cmd), "command buffer");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(p.device, &fence_info, nullptr, &p.fence), "fence");
        auto submit = [&](bool fill, uint32_t pattern) {
            check(vkResetCommandBuffer(cmd, 0), "reset command");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            check(vkBeginCommandBuffer(cmd, &begin), "begin command");
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            if (fill) vkCmdFillBuffer(cmd, p.source, 0, Probe::bytes, pattern);
            else {
                VkBufferCopy region{0, 0, Probe::bytes};
                vkCmdCopyBuffer(cmd, p.source, p.destination, 1, &region);
            }
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            check(vkEndCommandBuffer(cmd), "end command");
            VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
            check(vkQueueSubmit(queue, 1, &si, p.fence), "submit");
            check(vkWaitForFences(p.device, 1, &p.fence, VK_TRUE, 10000000000ull), "GPU completion (10s limit)");
            check(vkResetFences(p.device, 1, &p.fence), "reset fence");
        };
        auto* alias = static_cast<volatile uint32_t*>(p.alias);
        auto* original = static_cast<volatile uint32_t*>(p.mapped_source);
        auto* copied = static_cast<volatile uint32_t*>(p.mapped_destination);
        for (uint32_t round = 1; round <= 16; ++round) {
            uint32_t pattern = 0x13579bdfu * round;
            submit(true, pattern);
            for (size_t i = 0; i < Probe::bytes / 4; ++i)
                if (alias[i] != pattern || original[i] != pattern) throw std::runtime_error("GPU -> CPU alias mismatch");
            for (size_t i = 0; i < Probe::bytes / 4; ++i) alias[i] = pattern ^ static_cast<uint32_t>(i);
            submit(false, 0);
            for (size_t i = 0; i < Probe::bytes / 4; ++i)
                if (copied[i] != (pattern ^ static_cast<uint32_t>(i))) throw std::runtime_error("CPU alias -> GPU mismatch");
        }
        puts("PASS: coherent dma-buf export + mmap, CPU -> GPU and GPU -> CPU (16 rounds).");
        puts("This validates a sharing primitive, not live guest remapping or game performance.");
        return 0;
    } catch (const std::exception& e) {
        printf("UNAVAILABLE: %s\n", e.what());
        return 2;
    }
}
