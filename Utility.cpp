/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

// 1. PROJET
#include "Utility.h"
#include "vulkan_device.hpp"

// 2.LIB
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// glm
void PrintGlmMatrix(mat4& mat, string name)
{
    cout << "glm::mat : " << name << endl;
    for (int i = 0; i < 4; ++i)
    {
        cout << "[ ";
        for (int j = 0; j < 4; ++j)
            cout << setw(10) << setprecision(4) << fixed << mat[j][i] << " ";
        cout << "]" << endl;
    }
}
void PrintGlmVec3(vec3 vec, string name)
{
    cout << "glm::vec3 : " << name << endl;
    cout << "[ ";
    for (int i = 0; i < 3; ++i)
    {
        cout << setw(10) << setprecision(4) << fixed << vec[i] << " ";
    }
    cout << "]" << endl;
}
void PrintGlmVec3(vec3 vec)
{
    cout << "[ ";
    for (int i = 0; i < 3; ++i)
    {
        cout << setw(10) << setprecision(4) << fixed << vec[i] << " ";
    }
    cout << "]" << endl;
}

// Shaders
vector<char> CompileShaderRuntime(const string& glslPath)
{
    filesystem::path fullPath(glslPath);
    filesystem::path dirPath = fullPath.parent_path() / "build";

    // Create the build/ directory if it doesn't exist
    if (!filesystem::exists(dirPath))
        filesystem::create_directories(dirPath);

    // Build the SPV path in build/
    filesystem::path spvPath = dirPath / (fullPath.filename().string() + ".spv");
    string spv = spvPath.string();
    
    // Detect the shader type by extension
    string shaderType = fullPath.extension().string();
    string glslcFlags = "";

    if (shaderType == ".mesh") 
        glslcFlags = "--target-spv=spv1.5 -fshader-stage=mesh "; 
    else if (shaderType == ".task") 
        glslcFlags = "--target-spv=spv1.6 -fshader-stage=task ";
    else if (shaderType == ".vert") 
        glslcFlags = "--target-spv=spv1.6 -fshader-stage=vert ";
    else if (shaderType == ".frag") 
        glslcFlags = "--target-spv=spv1.6 -fshader-stage=frag ";

    // Compile if necessary
    if (!filesystem::exists(spvPath) || filesystem::last_write_time(glslPath) > filesystem::last_write_time(spvPath))
    {
        string cmd = string("glslc ") + glslcFlags + glslPath + " -o " + spvPath.string();
        int result = system(cmd.c_str());
        if (result != 0) 
            throw runtime_error("Shader compilation failed: " + cmd);
    }

    return ReadSPIRVFile(spvPath.string());
}
vector<char> ReadSPIRVFile(const string& filename)
{
    ifstream file(filename, ios::ate | ios::binary);

    if (!file.is_open())
        throw runtime_error("Échec ouverture fichier SPV: " + filename);

    size_t fileSize = (size_t)file.tellg();
    vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

   return buffer;
}
VkShaderModule CreateShaderModule(VkDevice device, const vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw runtime_error("Échec création shader module");

    return shaderModule;
}

// Vulkan support
string errorString(VkResult errorCode)
{
    switch (errorCode)
    {
#define STR(r) case VK_ ##r: return #r
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
        STR(ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
#undef STR
    default:
        return "UNKNOWN_ERROR";
    }
}
extern shared_ptr<VulkanDevice> g_Device;
VkFormat FindSupportedFormat(const vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates) 
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(g_Device->physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) 
            return format;
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) 
            return format;
    }
    
    throw runtime_error("Aucun format compatible pour le depth buffer!");
}
VkFormat FindDepthFormat()
{
    return FindSupportedFormat({ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }, 
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
VkSampleCountFlagBits getMaxUsableSampleCount(VkPhysicalDevice physicalDevice) 
{
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
}
uint32_t GetUniformBufferOffsetAlignment(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    return deviceProperties.limits.minUniformBufferOffsetAlignment;
}
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) 
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    // SINGLE pass: priority HOST_CACHED ? HOST_VISIBLE ? fallback
    VkMemoryPropertyFlags ideal = properties | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // 1. Ideal: HOST_CACHED + HOST_VISIBLE + HOST_COHERENT
    if (properties & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
        ideal |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        VkMemoryPropertyFlags typeFlags = memProperties.memoryTypes[i].propertyFlags;

        // Priority test (the fastest)
        if ((typeFilter & (1 << i)) && (typeFlags & ideal) == ideal)
            return i;

        // Fallback HOST_VISIBLE + COHERENT only
        VkMemoryPropertyFlags fallback = properties | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((typeFilter & (1 << i)) && (typeFlags & fallback) == fallback)
            return i;

        // Ultimate fallback (without coherent)
        if ((typeFilter & (1 << i)) && (typeFlags & properties) == properties)
            return i;
    }

    throw runtime_error("failed to find suitable memory type!");
}
void CreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
    // 1. Create the buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) 
        throw std::runtime_error("Failed to create staging buffer");

    // 2. Retrieve memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    // 3. Find the suitable memory type
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) 
        throw std::runtime_error("Failed to allocate staging buffer memory");

    // 4. Bind buffer and memory
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}
VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}
void EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}
void TransitionLayout(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;  // ? PARAMÈTRE !
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
void SaveDepthTexture2D(shared_ptr<VulkanDevice> device, VkImage image, int width, int height, string name)
{
    // 1. Create staging buffer
    VkDeviceSize imageSize = width * height * sizeof(float);
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device->device, &bufferInfo, nullptr, &stagingBuffer);

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(device->physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device->device, &allocInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device->device, stagingBuffer, stagingMemory, 0);

    // 2. Command buffer one-shot
    VkCommandBuffer cmdBuffer;
    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.commandPool = device->graphicsCommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(device->device, &cmdAlloc, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // 3. Transition to TRANSFER_SRC_OPTIMAL (for depth shadowmap)
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;  // After sampling forward pass
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // 4. Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
    vkCmdCopyImageToBuffer(cmdBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // 5. Transition back to READ_ONLY_OPTIMAL (for reuse)
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmdBuffer);

    // 6. Submit & wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(device->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(device->graphicsQueue);

    // 7. Map & read pixels
    void* data;
    vkMapMemory(device->device, stagingMemory, 0, imageSize, 0, &data);
    float* pixels = static_cast<float*>(data);

    // Convert depth to RGBA8 PNG
    unsigned char* imageData = new unsigned char[width * height * 4];
    for (int i = 0; i < width * height; ++i)
    {
        float depth = pixels[i];  // Only one channel
        unsigned char gray = static_cast<unsigned char>(255.0f - depth * 255.0f);  // 1.0=BLACK, 0.0=WHITE
        imageData[i * 4 + 0] = gray;
        imageData[i * 4 + 1] = gray;
        imageData[i * 4 + 2] = gray;
        imageData[i * 4 + 3] = 255;
    }
    vkUnmapMemory(device->device, stagingMemory);

    // 8. Flip vertical (stb_image_write)
    int stride = width * 4;
    unsigned char* flippedData = new unsigned char[width * height * 4];
    for (int y = 0; y < height; ++y)
        memcpy(flippedData + y * stride, imageData + (height - 1 - y) * stride, stride);

    // 9. Save PNG
    stbi_write_png(name.c_str(), width, height, 4, flippedData, stride);

    // Cleanup
    delete[] imageData;
    delete[] flippedData;
    vkFreeMemory(device->device, stagingMemory, nullptr);
    vkDestroyBuffer(device->device, stagingBuffer, nullptr);
    vkFreeCommandBuffers(device->device, device->graphicsCommandPool, 1, &cmdBuffer);
}
VkSampler CreateTextureSamplerColor(VkDevice device)
{
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;           // Magnification
    samplerInfo.minFilter = VK_FILTER_LINEAR;           // Minification  
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;                 // Max of your GPU
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;    // Normalized [0,1]
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.mipLodBias = 0.0f;

    VkSampler sampler;
    vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
    return sampler;
}
float* ConvertRGBtoRGBA(const float* rgbData, uint32_t width, uint32_t height)
{
    // Allocation RGBA (4 floats/pixel)
    float* rgbaData = new float[width * height * 4];

    for (uint32_t i = 0; i < width * height; ++i)
    {
        // Pixel i : RGB to RGBA
        rgbaData[i * 4 + 0] = rgbData[i * 3 + 0];  // R
        rgbaData[i * 4 + 1] = rgbData[i * 3 + 1];  // G  
        rgbaData[i * 4 + 2] = rgbData[i * 3 + 2];  // B
        rgbaData[i * 4 + 3] = 1.0f;                // A = opaque
    }

    return rgbaData;
}
VkDescriptorSetLayout GetImGuiTextureDescriptorSetLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    VkDescriptorSetLayout layout;
    vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout);
    return layout;
}
VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels, uint32_t depth)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    if (depth > 1)
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    else
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

    viewInfo.format = format;

    // subresourceRange describes what the image's purpose is and which part of the image should be accessed
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        throw runtime_error("APP::CREATE_IMAGE_VIEW::failed to create image view");

    return imageView;
}

GLFWmonitor* get_current_monitor(GLFWwindow* window)
{
    int nmonitors, i;
    int wx, wy, ww, wh;
    int mx, my, mw, mh;
    int overlap, bestoverlap;
    GLFWmonitor* bestmonitor;
    GLFWmonitor** monitors;
    const GLFWvidmode* mode;

    bestoverlap = 0;
    bestmonitor = NULL;

    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    monitors = glfwGetMonitors(&nmonitors);

    for (i = 0; i < nmonitors; i++)
    {
        mode = glfwGetVideoMode(monitors[i]);
        glfwGetMonitorPos(monitors[i], &mx, &my);
        mw = mode->width;
        mh = mode->height;

        overlap = std::max(0, std::min(wx + ww, mx + mw) - std::max(wx, mx)) * std::max(0, std::min(wy + wh, my + mh) - std::max(wy, my));

        if (bestoverlap < overlap)
        {
            bestoverlap = overlap;
            bestmonitor = monitors[i];
        }
    }

    return bestmonitor;
}

// Save client area to image file (png)
wstring GetNextAvailableCaptureName(const wstring& folderPath)
{
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    wstring searchPath = folderPath + L"\\SimShip - Capture *.png";
    vector<int> numbers;

    // List files matching the pattern
    hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                wstring filename(ffd.cFileName);
                // Extract the number
                size_t pos = filename.rfind(L"Capture ");
                if (pos != wstring::npos)
                {
                    pos += 8; // "Capture "
                    wstring numStr = filename.substr(pos, filename.size() - pos - 4); // -4 for .png
                    try
                    {
                        int num = std::stoi(numStr);
                        numbers.push_back(num);
                    }
                    catch (...) {}
                }
            }
        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    // Find the highest existing number
    int nextNum = 1;
    if (!numbers.empty())
    {
        std::sort(numbers.begin(), numbers.end());
        nextNum = numbers.back() + 1;
    }

    // Generate the file name
    wstringstream ss;
    ss << L"SimShip - Capture " << std::setw(2) << std::setfill(L'0') << nextNum << L".png";
    return ss.str();
}
void SaveHBITMAP(HBITMAP bitmap, HDC hDC, wchar_t* filename)
{
    BITMAP				bmp;
    PBITMAPINFO			pbmi;
    WORD				cClrBits;
    HANDLE				hf;			// file handle 
    BITMAPFILEHEADER	hdr;		// bitmap file-header 
    PBITMAPINFOHEADER	pbih;		// bitmap info-header 
    LPBYTE				lpBits;		// memory pointer 
    DWORD				dwTotal;	// total count of bytes 
    DWORD				cb;			// incremental count of bytes 
    BYTE* hp;		// byte pointer 
    DWORD				dwTmp;

    // Create the bitmapinfo header information
    if (!GetObject(bitmap, sizeof(BITMAP), (LPSTR)&bmp))
    {
        perror("Could not retrieve bitmap info");
        return;
    }

    // Convert the color format to a count of bits. 
    cClrBits = (WORD)(bmp.bmPlanes * bmp.bmBitsPixel);
    if (cClrBits == 1)
        cClrBits = 1;
    else if (cClrBits <= 4)
        cClrBits = 4;
    else if (cClrBits <= 8)
        cClrBits = 8;
    else if (cClrBits <= 16)
        cClrBits = 16;
    else if (cClrBits <= 24)
        cClrBits = 24;
    else cClrBits = 32;

    // Allocate memory for the BITMAPINFO structure.
    if (cClrBits != 24)
        pbmi = (PBITMAPINFO)LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * (1 << cClrBits));
    else
        pbmi = (PBITMAPINFO)LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER));

    // Initialize the fields in the BITMAPINFO structure. 
    pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    pbmi->bmiHeader.biWidth = bmp.bmWidth;
    pbmi->bmiHeader.biHeight = bmp.bmHeight;
    pbmi->bmiHeader.biPlanes = bmp.bmPlanes;
    pbmi->bmiHeader.biBitCount = bmp.bmBitsPixel;
    if (cClrBits < 24)
        pbmi->bmiHeader.biClrUsed = (1 << cClrBits);

    // If the bitmap is not compressed, set the BI_RGB flag. 
    pbmi->bmiHeader.biCompression = BI_RGB;

    // Compute the number of bytes in the array of color indices and store the result in biSizeImage. 
    pbmi->bmiHeader.biSizeImage = (pbmi->bmiHeader.biWidth + 7) / 8 * pbmi->bmiHeader.biHeight * cClrBits;
    // Set biClrImportant to 0, indicating that all of the device colors are important. 
    pbmi->bmiHeader.biClrImportant = 0;

    // Now open file and save the data
    pbih = (PBITMAPINFOHEADER)pbmi;
    lpBits = (LPBYTE)GlobalAlloc(GMEM_FIXED, pbih->biSizeImage);

    if (!lpBits)
    {
        perror("SaveHBITMAP::Could not allocate memory");
        return;
    }

    // Retrieve the color table (RGBQUAD array) and the bits 
    if (!GetDIBits(hDC, HBITMAP(bitmap), 0, (WORD)pbih->biHeight, lpBits, pbmi, DIB_RGB_COLORS))
    {
        perror("SaveHBITMAP::GetDIB error");
        return;
    }

    // Create the .BMP file. 
    hf = CreateFile(filename, GENERIC_READ | GENERIC_WRITE, (DWORD)0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, (HANDLE)NULL);
    if (hf == INVALID_HANDLE_VALUE)
    {
        perror("Could not create file for writing");
        return;
    }
    hdr.bfType = 0x4d42; // 0x42 = "B" 0x4d = "M" 
    // Compute the size of the entire file
    hdr.bfSize = (DWORD)(sizeof(BITMAPFILEHEADER) + pbih->biSize + pbih->biClrUsed * sizeof(RGBQUAD) + pbih->biSizeImage);
    hdr.bfReserved1 = 0;
    hdr.bfReserved2 = 0;

    // Compute the offset to the array of color indices
    hdr.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + pbih->biSize + pbih->biClrUsed * sizeof(RGBQUAD);

    // Copy the BITMAPFILEHEADER into the .BMP file 
    if (!WriteFile(hf, (LPVOID)&hdr, sizeof(BITMAPFILEHEADER), (LPDWORD)&dwTmp, NULL))
    {
        perror("Could not write in to file");
        return;
    }

    // Copy the BITMAPINFOHEADER and RGBQUAD array into the file
    if (!WriteFile(hf, (LPVOID)pbih, sizeof(BITMAPINFOHEADER) + pbih->biClrUsed * sizeof(RGBQUAD), (LPDWORD)&dwTmp, (NULL)))
    {
        perror("Could not write in to file");
        return;
    }

    // Copy the array of color indices into the .BMP file
    dwTotal = cb = pbih->biSizeImage;
    hp = lpBits;
    if (!WriteFile(hf, (LPSTR)hp, (int)cb, (LPDWORD)&dwTmp, NULL))
    {
        perror("Could not write in to file");
        return;
    }

    // Close the .BMP file
    if (!CloseHandle(hf))
    {
        perror("Could not close file");
        return;
    }

    // Free memory
    GlobalFree((HGLOBAL)lpBits);
}
wstring SaveClientArea(HWND hwnd)
{
    // Get a compatible DC into the client area
    HDC hDC = GetDC(hwnd);
    HDC hTargetDC = CreateCompatibleDC(hDC);

    RECT rect = { 0 };
    GetClientRect(hwnd, &rect);

    HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rect.right - rect.left, rect.bottom - rect.top);
    SelectObject(hTargetDC, hBitmap);
    PrintWindow(hwnd, hTargetDC, PW_CLIENTONLY);

    wstring name = GetNextAvailableCaptureName(L"Outputs");
    name = L"Outputs/" + name;
    SaveHBITMAP(hBitmap, hTargetDC, const_cast<wchar_t*>(name.c_str()));

    DeleteObject(hBitmap);
    ReleaseDC(hwnd, hDC);
    DeleteDC(hTargetDC);

    return name;
}

// Files
vector<string> ListFiles(const string& folder, const string& ext)
{
    vector<string> files;
    for (const auto& entry : filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() == ext)
            files.push_back(entry.path().string());
    }
    return files;
}

// Strings
string wstring_to_utf8(const wstring& wstr)
{
    if (wstr.empty()) return string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
wstring utf8_to_wstring(const string& str)
{
    if (str.empty()) return wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
string Utf8ToAnsi(const string& utf8)
{
    // UTF-8 -> UTF-16
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    wstring wide(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], wlen);

    // UTF-16 -> ANSI (Windows-1252)
    int len = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string ansi(len, 0);
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &ansi[0], len, nullptr, nullptr);

    return ansi;
}

// Conversions
float ms_to_knot(float speedMS)
{
    return speedMS * 3600.0f / 1852.0f;
}
float knot_to_ms(float speedKnots)
{
    return speedKnots * 1852.0f / 3600.0f;
}
float wind_to_dirdeg(vec2 windVector)
{
    // Extract x and z from the wind vector
    float x = -windVector.x; // Invert x to swap East and West
    float z = windVector.y;

    // Calculate the angle in radians
    float angleRad = atan(x, z);

    // Convert to degrees and adjust for the desired reference frame
    float angleDeg = mod(degrees(angleRad) + 180.0, 360.0);

    return angleDeg;
}
vec2 wind_from_speeddir(float directionDEG, float speedKN)
{
    // Convert the direction to radians
    float directionRad = radians(directionDEG);

    // Calculate the x and y components of the vector
    float x = knot_to_ms(speedKN) * sin(directionRad);
    float y = knot_to_ms(-speedKN) * cos(directionRad);

    // Return the wind vector
    return vec2(x, y);
}

// Interpolations
quat RotationBetweenVectors(vec3 A, vec3 B)
{
    A = glm::normalize(A);
    B = glm::normalize(B);

    float cosTheta = glm::dot(A, B);
    vec3 rotationAxis;

    if (cosTheta < -1 + 0.001f)
    {
        // The vectors point in opposite directions
        rotationAxis = glm::cross(vec3(0.0f, 0.0f, 1.0f), A);
        if (glm::length2(rotationAxis) < 0.01f)
            rotationAxis = glm::cross(vec3(1.0f, 0.0f, 0.0f), A);
        rotationAxis = glm::normalize(rotationAxis);
        return glm::angleAxis(glm::radians(180.0f), rotationAxis);
    }

    rotationAxis = glm::cross(A, B);

    float s = sqrt((1 + cosTheta) * 2);
    float invs = 1 / s;

    return glm::quat(
        s * 0.5f,
        rotationAxis.x * invs,
        rotationAxis.y * invs,
        rotationAxis.z * invs
    );
}
float Sign(float value)
{
    if (value > 0.0f)
        return 1.0f;
    else if (value < 0.0f)
        return -1.0f;
    else
        return 0.0f;
}
double InterpolateAValue(const double start_1, const double end_1, const double start_2, const double end_2, double value_between_start_1_and_end_1)
{
    // Normalize the value between start_1 and end_1
    double normalized = (value_between_start_1_and_end_1 - start_1) / (end_1 - start_1);

    // Interpolate to the range [start_2, end_2]
    return start_2 + normalized * (end_2 - start_2);
}
bool IsInRect(vec4& rect, vec2& point)
{
    return (point.x >= rect.x && point.x <= rect.x + rect.z && point.y >= rect.y && point.y <= rect.y + rect.w);
}
bool IsInCircle(const vec3& circle, const vec2& point)
{
    float dx = point.x - circle.x;
    float dy = point.y - circle.y;
    return (dx * dx + dy * dy) <= (circle.z * circle.z);
}
bool IntersectionOfSegments(const vec2& p1, const vec2& p2, const vec2& p3, const vec2& p4, vec2& p)
{
    float denom = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);
    if (denom == 0)
        return 0;		// the segments are parallel

    float t = ((p1.x - p3.x) * (p3.y - p4.y) - (p1.y - p3.y) * (p3.x - p4.x)) / denom;
    float u = -((p1.x - p2.x) * (p1.y - p3.y) - (p1.y - p2.y) * (p1.x - p3.x)) / denom;
    if (t >= 0 && t <= 1 && u >= 0 && u <= 1)
    {
        p.x = p1.x + t * (p2.x - p1.x);
        p.y = p1.y + t * (p2.y - p1.y);
        return true;
    }
    else
        return false;	// the segments do not intersect
}
bool IntersectionOfSegments(const vec2& p1, const vec2& p2, const vec2& p3, const vec2& p4)
{
    float denom = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);
    if (denom == 0)
        return 0;		// the segments are parallel

    float t = ((p1.x - p3.x) * (p3.y - p4.y) - (p1.y - p3.y) * (p3.x - p4.x)) / denom;
    float u = -((p1.x - p2.x) * (p1.y - p3.y) - (p1.y - p2.y) * (p1.x - p3.x)) / denom;
    if (t >= 0 && t <= 1 && u >= 0 && u <= 1)
        return true;
    else
        return false;	// the segments do not intersect
}

// Geography
const float EARTH_RADIUS = 6371000.0; // Mean radius of the Earth in meters
const vec2 REFERENCE_POINT(-2.94097114, 47.38162231); // Houat
float lon_to_opengl(float lon)
{
    float dLon = glm::radians(lon - REFERENCE_POINT.x);

    return EARTH_RADIUS * dLon * cos(glm::radians(REFERENCE_POINT.y));
}
float lat_to_opengl(float lat)
{
    float dLat = glm::radians(lat - REFERENCE_POINT.y);

    return -EARTH_RADIUS * dLat;
}
vec3 lonlat_to_opengl(float lon, float lat)
{
    float dLon = glm::radians(lon - REFERENCE_POINT.x);
    float dLat = glm::radians(lat - REFERENCE_POINT.y);

    float x = EARTH_RADIUS * dLon * cos(glm::radians(REFERENCE_POINT.y));
    float z = -EARTH_RADIUS * dLat;

    return vec3(x, 0.0f, z);
}
vec2 opengl_to_lonlat(float x, float z)
{
    float lon = REFERENCE_POINT.x + glm::degrees(x / (EARTH_RADIUS * cos(glm::radians(REFERENCE_POINT.y))));
    float lat = REFERENCE_POINT.y - glm::degrees(z / EARTH_RADIUS);

    return vec2(lon, lat);
}
float get_angle_from_north(vec3 dir)
{
    // Vector representing North (negative Z axis)
    vec3 north(0.0f, 0.0f, -1.0f);

    // Projection of the direction onto the XZ plane
    vec3 directionXZ(dir.x, 0.0f, dir.z);

    // Normalization of the projected vector
    directionXZ = normalize(directionXZ);

    // Calculating the angle between the projected direction and North
    float North = glm::orientedAngle(north, directionXZ, vec3(0.0f, 1.0f, 0.0f));

    // Converting angle to degrees
    North = degrees(North);

    North = 360.0f - North;

    // Adjusting the angle to always be positive (0-360)
    while (North < 0)
        North += 360.0f;
    while (North > 360.0f)
        North -= 360.0f;

    return North;
}
float get_yaw_from_hdg(float hdgDeg)
{
    float deg_Yaw = fmod(450.0f - hdgDeg, 360.0f);
    if (deg_Yaw < 0.0f)
        deg_Yaw += 360.0f;
    return glm::radians(deg_Yaw);
}
float get_hdg_from_yaw(float yawRad)
{
    float yaw_deg = glm::degrees(yawRad);
    float hdg = fmod(450.0f - yaw_deg, 360.0f);
    if (hdg < 0.0f)
        hdg += 360.0f;
    return hdg;
}
string display_geographic_angle(float angle, int decimal)
{
    // Returns a formatted geographic angle string (e.g. "045°" or "045.3°")
     // angle   : input angle in degrees (any value, will be normalized to [0, 360))
     // decimal : number of decimal places (0 = integer only)
  
    // Normalize angle to [0, 360)
    float norm = fmodf(angle, 360.0f);
    if (norm < 0.0f) norm += 360.0f;

    char buf[16];

    if (decimal <= 0)
    {
        // Round to nearest integer, then format as 3-digit zero-padded
        int deg = (int)roundf(norm);
        if (deg >= 360) deg = 0; // e.g. 359.6 rounds to 360 ? wrap to 0
        snprintf(buf, sizeof(buf), "%03d\xC2\xB0", deg);
    }
    else
    {
        // Multiply by 10^decimal to work in integer arithmetic (avoids float formatting quirks)
        float factor = powf(10.0f, (float)decimal);
        int   scaled = (int)roundf(norm * factor);
        int   max_val = (int)(360.0f * factor);
        if (scaled >= max_val) scaled = 0; // wrap after rounding

        int int_part = scaled / (int)factor;
        int dec_part = scaled % (int)factor;

        // Build format string dynamically: "%03d.%0Nd°" where N = decimal
        char fmt[32];
        snprintf(fmt, sizeof(fmt), "%%03d.%%0%dd\xC2\xB0", decimal);
        snprintf(buf, sizeof(buf), fmt, int_part, dec_part);
    }

    return string(buf);
}

// Colors
vec3 color_255_to_1(vec3 v)
{
    return vec3((float)v.x / 255.0f, (float)v.y / 255.0f, (float)v.z / 255.0f);
}
void rgb_to_hsl(const vec3& rgb, float& h, float& s, float& l)
{
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;

    float max = std::max(std::max(r, g), b);
    float min = std::min(std::min(r, g), b);
    l = (max + min) / 2.0f;

    if (max == min) {
        h = s = 0.0f;
        return;
    }

    float d = max - min;
    s = (l > 0.5f) ? d / (2.0f - max - min) : d / (max + min);

    if (max == r)
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (max == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;

    h /= 6.0f;
}
vec3 sRGB_to_linear(const vec3& c)
{
    // Convert sRGB to linear (exact formula IEC 61966-2-1)
    auto channel = [](float x) -> float
        {
            return (x <= 0.04045f)
                ? x / 12.92f
                : std::pow((x + 0.055f) / 1.055f, 2.4f);
        };
    return vec3(channel(c.r), channel(c.g), channel(c.b));
}
