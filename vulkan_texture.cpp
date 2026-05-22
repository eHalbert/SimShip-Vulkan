/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "vulkan_texture.hpp"

/*
Type de texture                     Format                  bSRGB
Diffuse, albedo, couleur terrain    R8G8B8A8_SRGB           true
Normal map                          R8G8B8A8_UNORM          false
Roughness, metallic, AO             R8G8_UNORM ou R8_UNORM  false
Foam, gradients océan, dUdV         R8G8B8A8_UNORM          false
HDR(envmap)                         R32G32B32A32_SFLOAT     N/A
Shadow map, depth                   formats depth           N/A
*/

// public

VulkanTexture::VulkanTexture(shared_ptr<VulkanDevice> vulkanDevice, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImageAspectFlags aspectFlags)
{
    mVulkanDevice = vulkanDevice;

    CreateImageInternal(width, height, depth, mipLevels, numSamples, format, tiling, usage, properties, aspectFlags);
}
VulkanTexture::~VulkanTexture()
{
    if (mbDestroyed || !mVulkanDevice || !mVulkanDevice->device) return;

    // 1. ATTENTE GPU CRITIQUE
    vkDeviceWaitIdle(mVulkanDevice->device);

    VkDevice device = mVulkanDevice->device;

    // 2. ORDRE STRICT Vulkan
    if (mImguiDescriptorSet != nullptr) 
        mImguiDescriptorSet = nullptr;

    if (sampler != nullptr) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = nullptr;
    }

    if (imageView != nullptr) {
        vkDestroyImageView(device, imageView, nullptr);
        imageView = nullptr;
    }

    if (image != nullptr) {
        vkDestroyImage(device, image, nullptr);
        image = nullptr;
    }

    if (gpuMemory != nullptr) {
        vkFreeMemory(device, gpuMemory, nullptr);
        gpuMemory = nullptr;
    }

    if (cpuMemory != nullptr) {
        vkFreeMemory(device, cpuMemory, nullptr);
        cpuMemory = nullptr;
    }

    if (mStagingBuffer != nullptr) {
        vkDestroyBuffer(device, mStagingBuffer, nullptr);
        mStagingBuffer = nullptr;
    }
    mbDestroyed = true;
}
void VulkanTexture::Create(shared_ptr<VulkanDevice> vulkanDevice, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, bool persistentStaging)
{
    mVulkanDevice = vulkanDevice;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * depth) * FormatSize(format);

    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mStagingBuffer, cpuMemory);

    if (persistentStaging)
        vkMapMemory(mVulkanDevice->device, cpuMemory, 0, imageSize, 0, &cpuData);

    CreateImageInternal(width, height, depth, 1, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    SingleTimeCommands([this](VkCommandBuffer cmd) {
        TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);
        CopyBufferToImage(mStagingBuffer, cmd);
        TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmd);
        });

    // Ne détruire que si non persistant
    if (!persistentStaging)
    {
        vkDestroyBuffer(mVulkanDevice->device, mStagingBuffer, nullptr);
        vkFreeMemory(mVulkanDevice->device, cpuMemory, nullptr);
        mStagingBuffer = VK_NULL_HANDLE;
        cpuMemory = VK_NULL_HANDLE;
        cpuData = nullptr;
    }
    // Si persistentStaging : mStagingBuffer et cpuMemory restent valides pour CopyStagingToGPU()

    CreateImageViewInternal((depth > 1) ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT);
    if (depth == 1) CreateSamplerRepeat();
    else            CreateSamplerClampToEdge();
}
void VulkanTexture::CreateFromData(shared_ptr<VulkanDevice> vulkanDevice, uint32_t width, uint32_t height, uint32_t depth, VkFormat format, bool persistentStaging, const float* data)
{
    mVulkanDevice = vulkanDevice;

    // Taille en bytes selon le VkFormat
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * depth) * FormatSize(format);

    // 1. Créer le staging persistant CPU-visible
    VkBuffer stagingBuffer;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, cpuMemory);

    // On mappe en permanence si demandé
    if (persistentStaging)
        vkMapMemory(mVulkanDevice->device, cpuMemory, 0, imageSize, 0, &cpuData);

    // 2. Copier 'data' dans stagingData
    void* dst = nullptr;
    if (!persistentStaging)
        vkMapMemory(mVulkanDevice->device, cpuMemory, 0, imageSize, 0, &dst);        // mapping temporaire
    else
        dst = cpuData;

    // Attention : 'data' est en float*, mais imageSize est en bytes
    memcpy(dst, data, static_cast<size_t>(imageSize));

    if (!persistentStaging)
        vkUnmapMemory(mVulkanDevice->device, cpuMemory);

    // 3. Créer l'image GPU avec les usages souhaités
    CreateImageInternal(width, height, depth, 1, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    // 4. Copier staging ? image + transitions de layout
    SingleTimeCommands([this, stagingBuffer](VkCommandBuffer cmd) {
        TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);
        CopyBufferToImage(stagingBuffer, cmd);
        TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmd);
        });

    // 5. Si on ne veut pas garder un staging persistant, on libère
    if (!persistentStaging)
    {
        vkDestroyBuffer(mVulkanDevice->device, stagingBuffer, nullptr);
        vkFreeMemory(mVulkanDevice->device, cpuMemory, nullptr);
        stagingBuffer = VK_NULL_HANDLE;
        cpuMemory = VK_NULL_HANDLE;
        cpuData = nullptr;
    }

    // 6. Vue + sampler
    CreateImageViewInternal((depth > 1) ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT);
    CreateSamplerRepeat();
}
bool VulkanTexture::CreateFromFile(shared_ptr<VulkanDevice> vulkanDevice, const string& filename, bool bMipMap)
{
    mVulkanDevice = vulkanDevice;

    // Détection extension
    string ext;
    size_t dotPos = filename.find_last_of(".");
    if (dotPos != string::npos && dotPos < filename.size() - 1) 
    {
        ext = filename.substr(dotPos + 1);
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    bool isHDR = (ext == "hdr" || ext == "exr" || ext == "pic" || ext == "pfm");

    int width = 0, height = 0, channels = 0;
    VkDeviceSize imageSize = 0;
    void* pixels = nullptr;

    if (isHDR)
    {
        // HDR (float)
        float* pixels_hdr = stbi_loadf(filename.c_str(), &width, &height, &channels, 0);
        if (!pixels_hdr || channels < 3) 
        {
            stbi_image_free(pixels_hdr);
            return false;
        }

        // Check transparency
        bTransparency = false;
        if (channels == 4)
        {
            for (int i = 0; i < width * height; ++i)
            {
                if (pixels_hdr[i * 4 + 3] < 1.0f)
                {
                    bTransparency = true;
                    break;
                }
            }
        }

        extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageSize = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);

        // Convertir RGB->RGBA
        float* rgba = new float[width * height * 4];
        for (int i = 0; i < width * height; ++i) 
        {
            rgba[i * 4 + 0] = pixels_hdr[i * channels + 0];
            rgba[i * 4 + 1] = pixels_hdr[i * channels + 1];
            rgba[i * 4 + 2] = pixels_hdr[i * channels + 2];
            rgba[i * 4 + 3] = (channels == 4) ? pixels_hdr[i * channels + 3] : 1.0f;
        }
        stbi_image_free(pixels_hdr);
        pixels = rgba;
    }
    else 
    {
        // LDR (uint8)
        stbi_uc* pixels_ldr = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels_ldr)
        {
            stbi_image_free(pixels_ldr);
            return false;
        }

        // Check transparency
        bTransparency = false;
        if (channels == 4)
        {
            for (int i = 0; i < width * height; ++i)
            {
                unsigned char alpha = pixels_ldr[i * 4 + 3];
                if (alpha < 255)
                {
                    bTransparency = true;
                    break;
                }
            }
        }

        extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        format = VK_FORMAT_R8G8B8A8_UNORM;// VK_FORMAT_R8G8B8A8_SRGB;
        imageSize = static_cast<VkDeviceSize>(width) * height * 4;

        pixels = pixels_ldr;
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMem;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMem);

    // Copie pixels ? staging
    void* data;
    vkMapMemory(mVulkanDevice->device, stagingMem, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(mVulkanDevice->device, stagingMem);

    // Nettoyage pixels
    if (isHDR)
        delete[] static_cast<float*>(pixels);
    else 
        stbi_image_free(static_cast<stbi_uc*>(pixels));

    if (bMipMap)
        this->mipLevels = static_cast<uint32_t>(floor(std::log2(std::max(width, height)))) + 1;

    // Création image GPU
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (bMipMap)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    CreateImageInternal(width, height, 1, mipLevels, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
        usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    if (this->mipLevels == 1)
    {
        // Copy et finalisation
        SingleTimeCommands([this, stagingBuffer](VkCommandBuffer cmd) {
            TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);
            CopyBufferToImage(stagingBuffer, cmd);
            TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmd);
        });
    }
    else
    {
        SingleTimeCommands([this, stagingBuffer](VkCommandBuffer cmd) {
            // Transition TOUTES les mips UNDEFINED -> TRANSFER_DST
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copier les données dans mip 0
            CopyBufferToImage(stagingBuffer, cmd);
        });
    }

    if (this->mipLevels > 1)
        GenerateMipmaps();

    CreateImageViewInternal(VK_IMAGE_VIEW_TYPE_2D, 1, mipLevels, VK_IMAGE_ASPECT_COLOR_BIT);
    CreateSamplerRepeat();
  
    vkDestroyBuffer(mVulkanDevice->device, stagingBuffer, nullptr);
    vkFreeMemory(mVulkanDevice->device, stagingMem, nullptr);
    return true;
}
void VulkanTexture::CreateDummyTexture(shared_ptr<VulkanDevice> vulkanDevice, uint8_t r, uint8_t g, uint8_t b)
{
    mVulkanDevice = vulkanDevice;

    // Texture blanche 1x1
    extent = { 1, 1, 1 };
    format = VK_FORMAT_R8G8B8A8_UNORM;

    // Données : pixel blanc opaque (255,255,255,255)
    uint8_t whitePixel[4] = { r, g, b, 255 };
    VkDeviceSize imageSize = 4;

    // 1. Staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMem;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMem);

    // Copie pixel blanc
    void* data;
    vkMapMemory(mVulkanDevice->device, stagingMem, 0, imageSize, 0, &data);
    memcpy(data, whitePixel, 4);
    vkUnmapMemory(mVulkanDevice->device, stagingMem);

    // 2. Image GPU
    CreateImageInternal(1, 1, 1, 1, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    // 3. Copy
    SingleTimeCommands([this, stagingBuffer](VkCommandBuffer cmd) {
        TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, cmd);
        CopyBufferToImage(stagingBuffer, cmd);
        TransitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, cmd);
        });

    // Cleanup staging
    vkDestroyBuffer(mVulkanDevice->device, stagingBuffer, nullptr);
    vkFreeMemory(mVulkanDevice->device, stagingMem, nullptr);

    // 4. Finalisation
    CreateImageViewInternal(VK_IMAGE_VIEW_TYPE_2D, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT);
    CreateSamplerRepeat();
}
void VulkanTexture::CopyStagingToGPU()
{
    // cpuData est déjà mappé et à jour. mStagingBuffer est HOST_COHERENT donc pas besoin de flush

    SingleTimeCommands([this](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { extent.width, extent.height, 1 };
        vkCmdCopyBufferToImage(cmd, mStagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });
}
void VulkanTexture::Save(const string& fullname)
{
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(extent.width * extent.height * extent.depth) * FormatSize(format);

    // 1. Créer un staging buffer HOST_VISIBLE pour lire le contenu GPU
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMem;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMem);

    // 2. Copier l'image GPU ? staging buffer
    SingleTimeCommands([this, stagingBuffer](VkCommandBuffer cmd)
        {
            // Transition vers TRANSFER_SRC_OPTIMAL
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = mCurrentLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Copie image -> buffer
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { extent.width, extent.height, 1 };
            vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

            // Retransition vers le layout d'origine
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = mCurrentLayout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

    // 3. Mapper et lire les données
    void* data;
    vkMapMemory(mVulkanDevice->device, stagingMem, 0, imageSize, 0, &data);

    int w = static_cast<int>(extent.width);
    int h = static_cast<int>(extent.height);

    if (format == VK_FORMAT_R8G8B8A8_UNORM)
    {
        // Écriture directe en PNG 8-bit RGBA
        stbi_write_png(fullname.c_str(), w, h, 4, data, w * 4);
    }
    else if (format == VK_FORMAT_R32G32B32A32_SFLOAT)
    {
        // Conversion float ? uint8 pour l'export PNG
        const float* src = static_cast<const float*>(data);
        std::vector<uint8_t> ldr(w * h * 4);
        for (int i = 0; i < w * h * 4; ++i)
        {
            float v = src[i];
            // Tone-mapping simple (clamp [0,1] ? [0,255])
            v = std::max(0.0f, std::min(1.0f, v));
            ldr[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
        }
        stbi_write_png(fullname.c_str(), w, h, 4, ldr.data(), w * 4);
    }
    else if (format == VK_FORMAT_R32_SFLOAT)
    {
        // Grayscale float ? PNG 8-bit (1 canal ? RGBA pour compatibilité)
        const float* src = static_cast<const float*>(data);
        std::vector<uint8_t> ldr(w * h * 4);
        for (int i = 0; i < w * h; ++i)
        {
            uint8_t v = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, src[i])) * 255.0f + 0.5f);
            ldr[i * 4 + 0] = v;
            ldr[i * 4 + 1] = v;
            ldr[i * 4 + 2] = v;
            ldr[i * 4 + 3] = 255;
        }
        stbi_write_png(fullname.c_str(), w, h, 4, ldr.data(), w * 4);
    }
    else
    {
        printf("VulkanTexture::Save — format %d non supporté pour l'export PNG\n", format);
    }

    vkUnmapMemory(mVulkanDevice->device, stagingMem);

    // 4. Nettoyage
    vkDestroyBuffer(mVulkanDevice->device, stagingBuffer, nullptr);
    vkFreeMemory(mVulkanDevice->device, stagingMem, nullptr);
}
void VulkanTexture::TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer commandBuffer = mVulkanDevice->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    // Aspect mask basé sur le FORMAT de l'image (plus fiable que le layout)
    if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
    {
        // Depth + Stencil
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    else if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM)
    {
        // Depth seul
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    else
    {
        // Color
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else
    {
        throw invalid_argument("Unsupported layout transition");
    }

    mCurrentLayout = newLayout;

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    mVulkanDevice->EndSingleTimeCommands(commandBuffer);
}
void VulkanTexture::TransitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkCommandBuffer cmd)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    VkPipelineStageFlags srcStage, dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        throw invalid_argument("Unsupported layout transition");
    }

    mCurrentLayout = newLayout;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void VulkanTexture::GenerateMipmaps()
{
    mipLevels = static_cast<uint32_t>(floor(std::log2(std::max(extent.width, extent.height)))) + 1;

    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(mVulkanDevice->physicalDevice, format, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        throw runtime_error("VulkanTexture::GenerateMipmaps::Format does not support linear blitting");

    SingleTimeCommands([this](VkCommandBuffer cmd)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = image;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.subresourceRange.levelCount = 1;

            int32_t mipWidth = static_cast<int32_t>(extent.width);
            int32_t mipHeight = static_cast<int32_t>(extent.height);

            for (uint32_t i = 1; i < mipLevels; ++i)
            {
                // Transition mip i-1 : TRANSFER_DST -> TRANSFER_SRC
                barrier.subresourceRange.baseMipLevel = i - 1;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                // Blit mip i-1 -> mip i
                VkImageBlit blit{};
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
                blit.srcOffsets[0] = { 0, 0, 0 };
                blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
                blit.dstOffsets[0] = { 0, 0, 0 };
                blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
                vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                // Transition mip i-1 : TRANSFER_SRC -> GENERAL
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                if (mipWidth > 1) mipWidth /= 2;
                if (mipHeight > 1) mipHeight /= 2;
            }

            // Transition dernière mip : TRANSFER_DST -> GENERAL
            barrier.subresourceRange.baseMipLevel = mipLevels - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            mCurrentLayout = VK_IMAGE_LAYOUT_GENERAL;
        });
}
void VulkanTexture::CreateImGuiDescriptor(VkDescriptorPool pool, VkDescriptorSetLayout layout)
{
    // Transition vers SHADER_READ_ONLY si nécessaire
    if (mCurrentLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        SingleTimeCommands([this](VkCommandBuffer cmd) {
            TransitionLayout(mCurrentLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd);
            });
    }

    // Allouer + remplir descriptor (comme votre code original)
    VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &alloc_info, &mImguiDescriptorSet);

    VkDescriptorImageInfo desc = { sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = mImguiDescriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &desc;
    vkUpdateDescriptorSets(mVulkanDevice->device, 1, &write, 0, nullptr);
}
void VulkanTexture::CreateTexture2DArray(shared_ptr<VulkanDevice> vulkanDevice, const string& basePath, const string& baseFilename, int texCount, int width, int height)
{
    mVulkanDevice = vulkanDevice;
    this->extent = { (uint32_t)width, (uint32_t)height, 1 };
    this->format = VK_FORMAT_R8_UNORM;
    this->layerCount = texCount;

    stbi_set_flip_vertically_on_load(true);

    VkDeviceSize layerSize = (VkDeviceSize)width * height * 1; // GL_R8 = 1 byte/pixel
    VkDeviceSize totalSize = layerSize * texCount;

    // 1. Staging buffer unique pour toutes les couches
    VkBuffer       stagingBuffer;
    VkDeviceMemory stagingMem;
    CreateBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingMem);

    uint8_t* mapped = nullptr;
    vkMapMemory(mVulkanDevice->device, stagingMem, 0, totalSize, 0, (void**)&mapped);

    // 2. Charger chaque PNG dans le staging buffer (offset = i * layerSize)
    for (int i = 0; i < texCount; ++i)
    {
        string index;
        if (i < 9)         index = "00" + to_string(i + 1);
        else if (i < 99)   index = "0" + to_string(i + 1);
        else                index = to_string(i + 1);

        string filename = basePath + baseFilename + index + ".png";

        int w, h, channels;
        stbi_uc* data = stbi_load(filename.c_str(), &w, &h, &channels, 1);
        if (!data)
        {
            cerr << "Error loading texture: " << filename << endl;
            memset(mapped + i * layerSize, 0, layerSize); // layer noire par défaut
            continue;
        }
        if (w != width || h != height)
        {
            cerr << "Incorrect size in: " << filename << endl;
            stbi_image_free(data);
            memset(mapped + i * layerSize, 0, layerSize);
            continue;
        }

        memcpy(mapped + i * layerSize, data, layerSize);
        stbi_image_free(data);
    }

    vkUnmapMemory(mVulkanDevice->device, stagingMem);
    stbi_set_flip_vertically_on_load(false);

    // 3. Créer l'image Vulkan 2D_ARRAY
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { (uint32_t)width, (uint32_t)height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = texCount;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(mVulkanDevice->device, &imageInfo, nullptr, &image);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(mVulkanDevice->device, image, &memReq);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &gpuMemory);
    vkBindImageMemory(mVulkanDevice->device, image, gpuMemory, 0);

    // 4. Copie staging -> image (toutes les couches d'un coup)
    SingleTimeCommands([this, stagingBuffer, texCount, width, height, layerSize](VkCommandBuffer cmd)
        {
            // Transition UNDEFINED -> TRANSFER_DST (toutes les couches)
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)texCount };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // Une region de copie par couche
            vector<VkBufferImageCopy> regions(texCount);
            for (int i = 0; i < texCount; ++i)
            {
                regions[i].bufferOffset = i * layerSize;
                regions[i].bufferRowLength = width;     // ? AJOUT CRITIQUE : 1024 pixels/ligne
                regions[i].bufferImageHeight = height;  // ? AJOUT CRITIQUE : 1024 lignes/image
                regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                regions[i].imageSubresource.mipLevel = 0;
                regions[i].imageSubresource.baseArrayLayer = i;
                regions[i].imageSubresource.layerCount = 1;
                regions[i].imageOffset = { 0, 0, 0 };
                regions[i].imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
            }
            vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)texCount, regions.data());

            // Transition TRANSFER_DST -> SHADER_READ_ONLY
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

    mCurrentLayout = VK_IMAGE_LAYOUT_GENERAL;

    // 5. Cleanup staging
    vkDestroyBuffer(mVulkanDevice->device, stagingBuffer, nullptr);
    vkFreeMemory(mVulkanDevice->device, stagingMem, nullptr);

    // 6. ImageView 2D_ARRAY
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = texCount;                      // ? toutes les couches
    vkCreateImageView(mVulkanDevice->device, &viewInfo, nullptr, &imageView);

    // 7. Sampler (équivalent GL_LINEAR + CLAMP_TO_EDGE)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxLod = 1.0f;
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &sampler);
}

// private

void VulkanTexture::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(mVulkanDevice->device, &bufferInfo, nullptr, &buffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(mVulkanDevice->device, buffer, &memReq);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, props);
    vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &memory);
    
    vkBindBufferMemory(mVulkanDevice->device, buffer, memory, 0);
}
void VulkanTexture::CopyBufferToImage(VkBuffer buffer, VkCommandBuffer cmd)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { extent.width, extent.height, extent.depth };
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}
uint32_t VulkanTexture::FormatSize(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8_UNORM:             return 1;
    case VK_FORMAT_R8G8_UNORM:           return 2;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:        return 4;
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:        return 4;
    case VK_FORMAT_R16_SFLOAT:           return 2;
    case VK_FORMAT_R16G16_SFLOAT:        return 4;
    case VK_FORMAT_R32_SFLOAT:           return 4;
    case VK_FORMAT_R32G32_SFLOAT:        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:     return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT:  return 16;
    case VK_FORMAT_R16G16B16A16_SFLOAT:  return 8;
    default:
        throw runtime_error("Unsupported Vulkan format");
    }
}
uint32_t VulkanTexture::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) 
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(mVulkanDevice->physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) 
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) 
            return i;

    throw runtime_error("Failed to find suitable memory type!");
}
VkCommandBuffer VulkanTexture::SingleTimeCommands(function<void(VkCommandBuffer)> func) 
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = mVulkanDevice->graphicsCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(mVulkanDevice->device, &allocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    func(cmdBuffer);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;
    vkQueueSubmit(mVulkanDevice->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(mVulkanDevice->graphicsQueue);

    vkFreeCommandBuffers(mVulkanDevice->device, mVulkanDevice->graphicsCommandPool, 1, &cmdBuffer);
    return VK_NULL_HANDLE;
}
void VulkanTexture::CreateImageInternal(uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
            VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImageAspectFlags aspectFlags)
{
    this->format = format;
    this->mipLevels = mipLevels;
    this->extent = { width, height, depth };

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = (depth > 1) ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, depth };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = numSamples;
    vkCreateImage(mVulkanDevice->device, &imageInfo, nullptr, &image);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(mVulkanDevice->device, image, &memReq);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, properties);
    vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &gpuMemory);
    vkBindImageMemory(mVulkanDevice->device, image, gpuMemory, 0);

    // ImageView générique
    CreateImageViewInternal( (depth > 1) ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D, 1, mipLevels, aspectFlags);
}
void VulkanTexture::CreateImageViewInternal(VkImageViewType viewType, uint32_t layerCount, uint32_t mipLevels, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;
    vkCreateImageView(mVulkanDevice->device, &viewInfo, nullptr, &imageView);
}
void VulkanTexture::CreateSamplerRepeat()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(mVulkanDevice->physicalDevice, &props);
    if (props.limits.maxSamplerAnisotropy > 1.0f)
    {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
    }
    else
    {
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &sampler);
}
void VulkanTexture::CreateSamplerClampToEdge()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(mVulkanDevice->physicalDevice, &props);
    if (props.limits.maxSamplerAnisotropy > 1.0f)
    {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(16.0f, props.limits.maxSamplerAnisotropy);
    }
    else
    {
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
    }
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &sampler);
}

