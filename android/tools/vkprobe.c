/* vkprobe: how long does a Vulkan queue submit take to complete on this phone?
 *
 * Runs from adb shell (no window, no game):
 *   adb push vkprobe /data/local/tmp/ && adb shell /data/local/tmp/vkprobe
 * Times submit + vkWaitForFences for (A) an empty command buffer, (B) a 1280x720 colour clear,
 * (C) a 1280x720 image-to-image blit, and (D) a 16 MB buffer copy, 60 times each, and prints
 * the median and worst in milliseconds. It separates "the GPU queue itself is slow to
 * round-trip" from "the game's work is slow".
 * Build: $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang -O2 -o vkprobe vkprobe.c -lvulkan
 * Measured on an AYN Thor (Adreno 740): empty submit 0.43 ms, 720p clear/blit 0.46 ms, 16 MB copy 1.1 ms. */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, (int)r_, __LINE__); exit(1); } } while (0)

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }

static uint32_t find_mem(VkPhysicalDevice pd, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0xFFFFFFFFu;
}

int main(void)
{
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vkprobe", 1, "none", 1, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai };
    VkInstance inst; CK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t n = 1; VkPhysicalDevice pd; CK(vkEnumeratePhysicalDevices(inst, &n, &pd));
    VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(pd, &pp);
    printf("device: %s (Vulkan %u.%u.%u, driver 0x%x)\n", pp.deviceName, VK_VERSION_MAJOR(pp.apiVersion),
           VK_VERSION_MINOR(pp.apiVersion), VK_VERSION_PATCH(pp.apiVersion), pp.driverVersion);
    uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, NULL);
    VkQueueFamilyProperties qp[16]; if (qn > 16) qn = 16; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qp);
    uint32_t qf = 0; for (uint32_t i = 0; i < qn; i++) if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qf = i; break; }
    float pr = 1.0f;
    VkDeviceQueueCreateInfo dq = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, qf, 1, &pr };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &dq };
    VkDevice dev; CK(vkCreateDevice(pd, &dci, NULL, &dev));
    VkQueue q; vkGetDeviceQueue(dev, qf, 0, &q);
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qf };
    VkCommandPool pool; CK(vkCreateCommandPool(dev, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
    VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence; CK(vkCreateFence(dev, &fci, NULL, &fence));

    /* two 1280x720 RGBA8 images and two 16 MB buffers */
    VkImageCreateInfo ic = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        { 1280, 720, 1 }, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage im[2]; VkDeviceMemory imm[2];
    for (int i = 0; i < 2; i++) {
        CK(vkCreateImage(dev, &ic, NULL, &im[i]));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, im[i], &mr);
        VkMemoryAllocateInfo ma = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, find_mem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        CK(vkAllocateMemory(dev, &ma, NULL, &imm[i])); CK(vkBindImageMemory(dev, im[i], imm[i], 0));
    }
    const VkDeviceSize BS = 16u << 20;
    VkBufferCreateInfo bc = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, BS, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer bf[2]; VkDeviceMemory bm[2];
    for (int i = 0; i < 2; i++) {
        CK(vkCreateBuffer(dev, &bc, NULL, &bf[i]));
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, bf[i], &mr);
        uint32_t t = find_mem(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo ma = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, t };
        CK(vkAllocateMemory(dev, &ma, NULL, &bm[i])); CK(vkBindBufferMemory(dev, bf[i], bm[i], 0));
    }

    VkImageSubresourceRange rng = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    for (int test = 0; test < 4; test++) {
        const char* names[] = { "A empty submit", "B 1280x720 clear", "C 1280x720 blit (nearest)", "D 16 MB host buffer copy" };
        double ms[60];
        for (int it = 0; it < 60; it++) {
            VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
            CK(vkResetCommandBuffer(cb, 0)); CK(vkBeginCommandBuffer(cb, &bi));
            if (test == 1 || test == 2) {
                for (int k = 0; k < 2; k++) {
                    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, im[k], rng };
                    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
                }
                if (test == 1) { VkClearColorValue c = { { 0.1f, 0.2f, 0.3f, 1.0f } }; vkCmdClearColorImage(cb, im[0], VK_IMAGE_LAYOUT_GENERAL, &c, 1, &rng); }
                else {
                    VkImageBlit bl = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, 0, 0 }, { 1280, 720, 1 } },
                                       { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, 0, 0 }, { 1280, 720, 1 } } };
                    vkCmdBlitImage(cb, im[0], VK_IMAGE_LAYOUT_GENERAL, im[1], VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_NEAREST);
                }
            } else if (test == 3) {
                VkBufferCopy cp = { 0, 0, BS }; vkCmdCopyBuffer(cb, bf[0], bf[1], 1, &cp);
            }
            CK(vkEndCommandBuffer(cb));
            VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &cb };
            CK(vkResetFences(dev, 1, &fence));
            double t0 = now_ms();
            CK(vkQueueSubmit(q, 1, &si, fence));
            CK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
            ms[it] = now_ms() - t0;
            struct timespec gap = { 0, 5 * 1000 * 1000 }; nanosleep(&gap, NULL);   /* idle 5 ms between frames, like a game */
        }
        qsort(ms, 60, sizeof ms[0], cmp);
        printf("%-28s median %7.2f ms   p90 %7.2f   worst %7.2f\n", names[test], ms[30], ms[54], ms[59]);
    }
    return 0;
}
