/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/

Adapted from Federico Vaccaro
*/

#include "Clouds.h"

#define INT_CEIL(n,d) (int)ceil((float)n/d)
extern uint32_t g_FramesInFlight;

Clouds::Clouds(shared_ptr<VulkanDevice>& vulkanDevice, VkExtent2D extent, float windSpeedKN)
{
    mVulkanDevice = vulkanDevice;
    mSwapChainExtent = extent;

    InitVariables();
	ComputeLUTs();
    SetCloudSpeed(windSpeedKN);
    CreateOffScreenPipeline();
}
Clouds::~Clouds()
{
	mPipelineClouds1.destroy(mVulkanDevice->device);
	mPipelineClouds2.destroy(mVulkanDevice->device);
    mPipelinePost.destroy(mVulkanDevice->device);
}

void Clouds::InitVariables()
{
    CloudSpeed = 200.0f;
	Coverage = 0.35f;
	Crispiness = 40.f;
	Curliness = 0.1f;
	Density = 0.02f;
	Absorption = 0.35f;
	Illumination = 4.0f;

	SphereInnerRadius = 7000.0f;
	SphereOuterRadius = 17000.0f;

	PerlinFrequency = 0.8f;

	bPostProcess = true;

	//Seed = vec3(0.0, 0.0, 0.0);
    std::random_device rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_real_distribution<> dis(.0, 100.);

    float x, y, z;
    x = dis(gen);
    y = dis(gen);
    z = dis(gen);

    Seed = vec3(x, y, z);

	CloudColorTop = vec3(0.994f, 0.876f, 0.876f);
    CloudColorBottom = vec3(0.04f, 0.04f, 0.04f);
}
void Clouds::SetCloudSpeed(float windSpeedKN)
{
	float wind = 1.0f;

	if (windSpeedKN < 1.0) wind = 1.0;
	else if (windSpeedKN > 30.0) wind = 30.0;

	// Linear interpolation: CloudSpeed = 100 to 300 between 1 and 30 kt
	double minSpeed = 100.0;
	double maxSpeed = 300.0;
	double minWind = 1.0;
	double maxWind = 30.0;

	CloudSpeed = minSpeed + (windSpeedKN - minWind) * (maxSpeed - minSpeed) / (maxWind - minWind);
}

// Compute LUTs
void Clouds::CreatePerlinPipeline()
{
    // Destroy the old pipelineLayout first
    if (mPipelinePerlin.descSetLayout)      vkDestroyDescriptorSetLayout(mVulkanDevice->device, mPipelinePerlin.descSetLayout, nullptr);
    if (mPipelinePerlin.pipelineLayout)     vkDestroyPipelineLayout(mVulkanDevice->device, mPipelinePerlin.pipelineLayout, nullptr);
    if (mPipelinePerlin.pipeline)           vkDestroyPipeline(mVulkanDevice->device, mPipelinePerlin.pipeline, nullptr);

    // Load SPIR-V
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Clouds/perlinworley.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    // Binding
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelinePerlin.descSetLayout);

    // pipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelinePerlin.descSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelinePerlin.pipelineLayout);

    // pipeline
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mPipelinePerlin.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelinePerlin.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);
    
    CreatePerlinDescriptors();
}
void Clouds::CreatePerlinDescriptors()
{
    mTexPerlin.Create(mVulkanDevice, 128, 128, 128, VK_FORMAT_R8G8B8A8_UNORM, true);
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;  
    samplerInfo.minFilter = VK_FILTER_LINEAR;  
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;  
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // For the Z dimension of a 3D texture
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;     // Optional, but common for minFilter linear
    samplerInfo.maxAnisotropy = 1.0f;                           // Disabled by default
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; // Optional
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mTexPerlin.sampler);

    VkDescriptorPoolSize poolSizes[1] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelinePerlin.descPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelinePerlin.descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mPipelinePerlin.descSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mPipelinePerlin.descSet);

    UpdatePerlinDescriptors();
}
void Clouds::UpdatePerlinDescriptors()
{
    // Image
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = mTexPerlin.imageView;
    imageInfo.sampler = VK_NULL_HANDLE;

    // Write
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mPipelinePerlin.descSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
}

void Clouds::CreateWorleyPipeline()
{
    // Destroy the old pipelineLayout first
    if (mPipelineWorley.descSetLayout)      vkDestroyDescriptorSetLayout(mVulkanDevice->device, mPipelineWorley.descSetLayout, nullptr);
    if (mPipelineWorley.pipelineLayout)     vkDestroyPipelineLayout(mVulkanDevice->device, mPipelineWorley.pipelineLayout, nullptr);
    if (mPipelineWorley.pipeline)           vkDestroyPipeline(mVulkanDevice->device, mPipelineWorley.pipeline, nullptr);

    // Load SPIR-V
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Clouds/worley.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    // Binding
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelineWorley.descSetLayout);

    // pipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelineWorley.descSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelineWorley.pipelineLayout);

    // pipeline
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mPipelineWorley.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelineWorley.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);

    CreateWorleyDescriptors();
}
void Clouds::CreateWorleyDescriptors()
{
    mTexWorley.Create(mVulkanDevice, 32, 32, 32, VK_FORMAT_R8G8B8A8_UNORM, true);
   
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR; 
    samplerInfo.minFilter = VK_FILTER_LINEAR;  
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; 
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; 
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // For the Z dimension of a 3D texture
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;     // Optional, but common for minFilter linear
    samplerInfo.maxAnisotropy = 1.0f;                           // Disabled by default
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; // Optional
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mTexWorley.sampler);

    VkDescriptorPoolSize poolSizes[1] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelineWorley.descPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelineWorley.descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mPipelineWorley.descSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mPipelineWorley.descSet);

    UpdateWorleyDescriptors();
}
void Clouds::UpdateWorleyDescriptors()
{
    // Image
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = mTexWorley.imageView;
    imageInfo.sampler = VK_NULL_HANDLE;

    // Write
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = mPipelineWorley.descSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
}

void Clouds::CreateWeatherPipeline()
{
    // Destroy the old pipelineLayout first
    if (mPipelineWeather.descSetLayout)      vkDestroyDescriptorSetLayout(mVulkanDevice->device, mPipelineWeather.descSetLayout, nullptr);
    if (mPipelineWeather.pipelineLayout)     vkDestroyPipelineLayout(mVulkanDevice->device, mPipelineWeather.pipelineLayout, nullptr);
    if (mPipelineWeather.pipeline)           vkDestroyPipeline(mVulkanDevice->device, mPipelineWeather.pipeline, nullptr);

    // Load SPIR-V
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Clouds/weather.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    // Binding
    array<VkDescriptorSetLayoutBinding, 2> bindings{};
   
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelineWeather.descSetLayout);

    // PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelineWeather.descSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelineWeather.pipelineLayout);

    // Pipeline
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mPipelineWeather.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelineWeather.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);

    CreateWeatherDescriptors();
}
void Clouds::CreateWeatherDescriptors()
{
    mTexWeather.Create(mVulkanDevice, 1024, 1024, 1, VK_FORMAT_R32G32B32A32_SFLOAT, true);
   
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;  
    samplerInfo.minFilter = VK_FILTER_LINEAR; 
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;  
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;   
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;     // Optional, but common for minFilter linear
    samplerInfo.maxAnisotropy = 1.0f;                           // Disabled by default
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; // Optional
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mTexWeather.sampler);
    
    mPipelineWeather.ubo = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sWeatherUBO));

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelineWeather.descPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelineWeather.descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mPipelineWeather.descSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mPipelineWeather.descSet);

    UpdateWeatherDescriptors();
}
void Clouds::UpdateWeatherDescriptors()
{
    // Image
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = mTexWeather.imageView;
    imageInfo.sampler = VK_NULL_HANDLE;

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = mPipelineWeather.ubo->buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = mPipelineWeather.ubo->GetSize();

    // Write
    array<VkWriteDescriptorSet, 2> descriptorWrites{};
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = mPipelineWeather.descSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[0].pImageInfo = &imageInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = mPipelineWeather.descSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[1].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mVulkanDevice->device, 2, descriptorWrites.data(), 0, nullptr);
}
void Clouds::ComputeNewWeatherLUT()
{
	std::random_device rd;  // Will be used to obtain a seed for the random number engine
	std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
	std::uniform_real_distribution<> dis(.0, 100.);

	float x, y, z;
	x = dis(gen);
	y = dis(gen);
	z = dis(gen);

	Seed = vec3(x, y, z);

    sWeatherUBO* uboData = static_cast<sWeatherUBO*>(mPipelineWeather.ubo->data);
    uboData->seed = Seed;
    uboData->perlinAmplitude = 0.5f;
    uboData->perlinFrequency = PerlinFrequency;
    uboData->perlinScale = 100.0f;
    uboData->perlinOctaves = 4;
    uboData->pad = 0.0f;

    VkMemoryBarrier barrierFull = {};
    barrierFull.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrierFull.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrierFull.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkCommandBuffer cmd = mVulkanDevice->BeginSingleTimeCommands();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineWeather.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineWeather.pipelineLayout, 0, 1, &mPipelineWeather.descSet, 0, nullptr);
    vkCmdDispatch(cmd, INT_CEIL(1024, 8), INT_CEIL(1024, 8), 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

    mVulkanDevice->EndSingleTimeCommands(cmd);
}

void Clouds::ComputeLUTs()
{
    VkMemoryBarrier barrierFull = {};
    barrierFull.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrierFull.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrierFull.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    // PERLIN texture
    CreatePerlinPipeline();

    VkCommandBuffer cmd = mVulkanDevice->BeginSingleTimeCommands();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelinePerlin.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelinePerlin.pipelineLayout, 0, 1, &mPipelinePerlin.descSet, 0, nullptr);
    vkCmdDispatch(cmd, 32, 32, 32);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

    // WORLEY texture
    CreateWorleyPipeline();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineWorley.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineWorley.pipelineLayout, 0, 1, &mPipelineWorley.descSet, 0, nullptr);
    vkCmdDispatch(cmd, INT_CEIL(32, 4), INT_CEIL(32, 4), INT_CEIL(32, 4));

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

    // WEATHER texture
    CreateWeatherPipeline();

    sWeatherUBO* uboData = static_cast<sWeatherUBO*>(mPipelineWeather.ubo->data);
    uboData->seed = Seed;
    uboData->perlinAmplitude = 0.5f;
    uboData->perlinFrequency = PerlinFrequency;
    uboData->perlinScale = 100.0f;
    uboData->perlinOctaves = 4;
    uboData->pad = 0.0f;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineWeather.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineWeather.pipelineLayout, 0, 1, &mPipelineWeather.descSet, 0, nullptr);
    vkCmdDispatch(cmd, INT_CEIL(1024, 16), INT_CEIL(1024, 16), 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

    mVulkanDevice->EndSingleTimeCommands(cmd);
}

// Render on screen
void Clouds::CreateOnScreenPipeline(VkRenderPass renderPassScene, VkExtent2D extent)
{
    // Destroy the old pipeline first
	mPipelineClouds1.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Clouds/volumetric_clouds.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Clouds/volumetric_clouds.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (Null)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // 3. DescriptorSetLayout : 3 textures + 1 sky + 1 UBO
    array<VkDescriptorSetLayoutBinding, 4> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // perlin 3D
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // worley 3D
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // weather 2D
        { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }   // sCloudsUBO
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelineClouds1.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelineClouds1.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelineClouds1.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissor
    VkViewport viewport{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, extent };
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_TRUE; 
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling 8x
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 12. Pipeline creation
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineClouds1.pipelineLayout;
    pipelineInfo.renderPass = renderPassScene;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelineClouds1.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateOnScreenDescriptors();
}
void Clouds::CreateOnScreenDescriptors()
{
    mPipelineClouds1.descSet.resize(g_FramesInFlight);
    mPipelineClouds1.ubo.resize(g_FramesInFlight);
    
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mPipelineClouds1.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sCloudsUBO));

    // Descriptor Pool : 3 textures + 1 UBO
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * g_FramesInFlight},  // 3 textures
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}           // 1 UBO
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelineClouds1.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipelineClouds1.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelineClouds1.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipelineClouds1.descSet.data());

}
void Clouds::UpdateOnScreenDescriptors(uint32_t currentFrame)
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        array<VkWriteDescriptorSet, 4> descriptorWrites{};

        // 1. Perlin texture 3D (binding 0)
        VkDescriptorImageInfo cloudInfo{};
        cloudInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        cloudInfo.imageView = mTexPerlin.imageView;
        cloudInfo.sampler = mTexPerlin.sampler;
    
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mPipelineClouds1.descSet[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].pImageInfo = &cloudInfo;

        // 2. Worley texture 3D (binding 1)
        VkDescriptorImageInfo worleyInfo{};
        worleyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        worleyInfo.imageView = mTexWorley.imageView;
        worleyInfo.sampler = mTexWorley.sampler;
    
        descriptorWrites[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[1].dstSet = mPipelineClouds1.descSet[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].pImageInfo = &worleyInfo;

        // 3. Weather texture 2D (binding 2)
        VkDescriptorImageInfo weatherInfo{};
        weatherInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        weatherInfo.imageView = mTexWeather.imageView;
        weatherInfo.sampler = mTexWeather.sampler;
    
        descriptorWrites[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[2].dstSet = mPipelineClouds1.descSet[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].pImageInfo = &weatherInfo;

        // 4. CloudParams UBO (binding 3)
        VkDescriptorBufferInfo cloudUboInfo{};
        cloudUboInfo.buffer = mPipelineClouds1.ubo[i]->buffer;
        cloudUboInfo.offset = 0;
        cloudUboInfo.range = mPipelineClouds1.ubo[i]->GetSize();
    
        descriptorWrites[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[3].dstSet = mPipelineClouds1.descSet[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[3].pBufferInfo = &cloudUboInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
void Clouds::Render(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec2 wind)
{
    if (!bVisible) return;

    // Update Cloud UBO
    sCloudsUBO* cloudsUbo = static_cast<sCloudsUBO*>(mPipelineClouds1.ubo[currentFrame]->data);
    
    cloudsUbo->iResolution = vec2(mSwapChainExtent.width, mSwapChainExtent.height);
    cloudsUbo->FOV = camera.GetZoom();
    cloudsUbo->iTime = glfwGetTime();
   
    cloudsUbo->inv_view = inverse(camera.GetView());
    cloudsUbo->inv_proj = inverse(camera.GetProjection());
    cloudsUbo->invViewProj = inverse(camera.GetViewProjection());

    cloudsUbo->cameraPosition = camera.GetPosition();
    cloudsUbo->coverage_multiplier = Coverage;

    cloudsUbo->lightColor = sky->SunDiffuse;
    cloudsUbo->cloudSpeed = CloudSpeed;

    cloudsUbo->lightDirection = glm::normalize(sky->SunPosition - camera.GetPosition());
    cloudsUbo->crispiness= Crispiness;
    
    cloudsUbo->wind = glm::normalize(vec3(wind.x, 0.0, wind.y));
    cloudsUbo->illumination = Illumination;
    
    cloudsUbo->cloudColorTop = CloudColorTop;
    cloudsUbo->curliness = Curliness;

    cloudsUbo->cloudColorBottom = CloudColorBottom;
    cloudsUbo->absorption = Absorption * 0.01;

    cloudsUbo->densityFactor = Density;
    cloudsUbo->sphereInnerRadius = SphereInnerRadius;
    cloudsUbo->sphereOuterRadius = SphereOuterRadius;
    cloudsUbo->exposure = sky->Exposure;

    mPipelineClouds1.ubo[currentFrame]->Flush();

    // Update sky descriptor (image actuelle)
    UpdateOnScreenDescriptors(currentFrame);

    // Render nuages
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineClouds1.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineClouds1.pipelineLayout, 0, 1, &mPipelineClouds1.descSet[currentFrame], 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);  // Full-screen quad
}

// Render offscreen
void Clouds::CreateOffScreenPipeline()
{
    // Destroy the old pipeline first
    mPipelineClouds2.destroy(mVulkanDevice->device);

    // 1. Compute Shader
    auto computeCode = CompileShaderRuntime("Resources/Shaders/Clouds/volumetric_clouds.comp");
    VkShaderModule computeModule = CreateShaderModule(mVulkanDevice->device, computeCode);

    VkPipelineShaderStageCreateInfo computeStage{};
    computeStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeStage.module = computeModule;
    computeStage.pName = "main";

    // 2. DescriptorSetLayout : 4 input textures + 1 UBO
    array<VkDescriptorSetLayoutBinding, 5> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // perlin 3D
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // worley 3D
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // weather 2D
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // output clouds image
        { 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }   // UBO
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelineClouds2.descSetLayout);

    // 3. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelineClouds2.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelineClouds2.pipelineLayout);

    // 4. Compute Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeStage;
    pipelineInfo.layout = mPipelineClouds2.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelineClouds2.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, computeModule, nullptr);
    
    CreateOffScreenDescriptors();
}
void Clouds::CreateOffScreenDescriptors()
{
    mPipelineClouds2.descSet.resize(g_FramesInFlight);
    mPipelineClouds2.ubo.resize(g_FramesInFlight);
    mCloudsImage.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        mCloudsImage[i] = make_unique<VulkanTexture>(mVulkanDevice, mSwapChainExtent.width, mSwapChainExtent.height, 1,
            1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        mCloudsImage[i]->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        mPipelineClouds2.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sCloudsUBO));
    }

    // Descriptor Pool : 3 textures + 1 storage image + 1 UBO
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 * g_FramesInFlight}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelineClouds2.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipelineClouds2.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelineClouds2.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipelineClouds2.descSet.data());

    UpdateOffScreenDescriptors();
}
void Clouds::UpdateOffScreenDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        array<VkWriteDescriptorSet, 5> descriptorWrites{};

        // 1. Perlin 3D (binding 0)
        VkDescriptorImageInfo perlinInfo{};
        perlinInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        perlinInfo.imageView = mTexPerlin.imageView;
        perlinInfo.sampler = mTexPerlin.sampler;

        descriptorWrites[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[0].dstSet = mPipelineClouds2.descSet[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].pImageInfo = &perlinInfo;

        // 2. Worley 3D (binding 1)
        VkDescriptorImageInfo worleyInfo{};
        worleyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        worleyInfo.imageView = mTexWorley.imageView;
        worleyInfo.sampler = mTexWorley.sampler;

        descriptorWrites[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[1].dstSet = mPipelineClouds2.descSet[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].pImageInfo = &worleyInfo;

        // 3. Weather 2D (binding 2)
        VkDescriptorImageInfo weatherInfo{};
        weatherInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        weatherInfo.imageView = mTexWeather.imageView;
        weatherInfo.sampler = mTexWeather.sampler;

        descriptorWrites[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[2].dstSet = mPipelineClouds2.descSet[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].pImageInfo = &weatherInfo;

        // 4. Output clouds image (binding 3)
        VkDescriptorImageInfo cloudsInfo{};
        cloudsInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        cloudsInfo.imageView = mCloudsImage[i]->imageView;

        descriptorWrites[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[3].dstSet = mPipelineClouds2.descSet[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[3].pImageInfo = &cloudsInfo;

        // 5. UBO (binding 4)
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = mPipelineClouds2.ubo[i]->buffer;
        uboInfo.offset = 0;
        uboInfo.range = mPipelineClouds2.ubo[i]->GetSize();

        descriptorWrites[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[4].dstSet = mPipelineClouds2.descSet[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[4].pBufferInfo = &uboInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
void Clouds::ComputeOffScreenImage(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec2 wind)
{
    if (!bVisible) return;

    // Update Cloud UBO
    sCloudsUBO* cloudsUbo = static_cast<sCloudsUBO*>(mPipelineClouds2.ubo[currentFrame]->data);

    cloudsUbo->iResolution = vec2(mSwapChainExtent.width, mSwapChainExtent.height);
    cloudsUbo->FOV = camera.GetZoom();
    cloudsUbo->iTime = glfwGetTime();

    cloudsUbo->inv_view = inverse(camera.GetView());
    cloudsUbo->inv_proj = inverse(camera.GetProjection());
    cloudsUbo->invViewProj = inverse(camera.GetViewProjection());

    cloudsUbo->cameraPosition = camera.GetPosition();
    cloudsUbo->coverage_multiplier = Coverage;

    cloudsUbo->lightColor = sky->SunDiffuse;
    cloudsUbo->cloudSpeed = CloudSpeed;

    cloudsUbo->lightDirection = glm::normalize(sky->SunPosition - camera.GetPosition());
    cloudsUbo->crispiness = Crispiness;

    cloudsUbo->wind = glm::normalize(vec3(wind.x, 0.0, wind.y));
    cloudsUbo->illumination = Illumination;

    cloudsUbo->cloudColorTop = CloudColorTop;
    cloudsUbo->curliness = Curliness;

    cloudsUbo->cloudColorBottom = CloudColorBottom;
    cloudsUbo->absorption = Absorption * 0.01;

    cloudsUbo->densityFactor = Density;
    cloudsUbo->sphereInnerRadius = SphereInnerRadius;
    cloudsUbo->sphereOuterRadius = SphereOuterRadius;
    cloudsUbo->exposure = sky->Exposure;

    mPipelineClouds2.ubo[currentFrame]->Flush();

    // Bind & Dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineClouds2.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineClouds2.pipelineLayout, 0, 1, &mPipelineClouds2.descSet[currentFrame], 0, nullptr);

    // Dispatch : workgroups adapted to the resolution (32x32 threads per workgroup typical)
    uint32_t workgroupX = (mCloudsImage[currentFrame]->extent.width + 15) / 16;
    uint32_t workgroupY = (mCloudsImage[currentFrame]->extent.height + 15) / 16;
    vkCmdDispatch(cmd, workgroupX, workgroupY, 1);
}

// Render on screen (sky + clouds + postprocessing)
void Clouds::CreatePostPipeline(VkRenderPass renderPassScene, VkExtent2D extent)
{
    // Destroy the old pipeline first
	mPipelinePost.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Clouds/clouds_post.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Clouds/clouds_post.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (Null)
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;  // Pas de vertex buffer
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // 3. DescriptorSetLayout : sky + clouds + UBO post
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // sky texture
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // mCloudsImage
        { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }   // sCloudsPost UBO
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelinePost.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelinePost.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelinePost.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissor
    VkViewport viewport{ 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, extent };
   
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_TRUE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling 8x
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    //depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 12. Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelinePost.pipelineLayout;
    pipelineInfo.renderPass = renderPassScene;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelinePost.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreatePostDescriptors();
}
void Clouds::CreatePostDescriptors()
{
    mPipelinePost.descSet.resize(g_FramesInFlight);
    mPipelinePost.ubo.resize(g_FramesInFlight);
    
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mPipelinePost.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sCloudsPostUBO));

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * g_FramesInFlight},  // sky + clouds
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}           // UBO post
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelinePost.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipelinePost.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelinePost.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipelinePost.descSet.data());
}
void Clouds::UpdatePostDescriptors(VulkanTexture* skyTexture)
{
    if (!mSampler)
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mSampler);
    }

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        array<VkWriteDescriptorSet, 3> descriptorWrites{};

        // 1. Sky texture (binding 0)
        VkDescriptorImageInfo skyInfo{};
        skyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        skyInfo.imageView = skyTexture->imageView;
        skyInfo.sampler = mSampler;
    
        descriptorWrites[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[0].dstSet = mPipelinePost.descSet[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].pImageInfo = &skyInfo;

        // 2. Clouds image (binding 1)
        VkDescriptorImageInfo cloudsInfo{};
        cloudsInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        cloudsInfo.imageView = mCloudsImage[i]->imageView;
        cloudsInfo.sampler = mSampler;
    
        descriptorWrites[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[1].dstSet = mPipelinePost.descSet[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].pImageInfo = &cloudsInfo;

        // 3. Post UBO (binding 2)
        VkDescriptorBufferInfo postUboInfo{};
        postUboInfo.buffer = mPipelinePost.ubo[i]->buffer;
        postUboInfo.offset = 0;
        postUboInfo.range = mPipelinePost.ubo[i]->GetSize();
    
        descriptorWrites[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrites[2].dstSet = mPipelinePost.descSet[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[2].pBufferInfo = &postUboInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
void Clouds::RenderPost(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky)
{
    // Update UBO (sCloudsPost)
    sCloudsPostUBO* postUbo = static_cast<sCloudsPostUBO*>(mPipelinePost.ubo[currentFrame]->data);
    vec2 resolution = vec2(camera.GetViewportWidth(), camera.GetViewportHeight());
    postUbo->cloudsResolution = resolution;
    postUbo->bShowSky = (int)sky->bVisible;
    postUbo->bShowClouds = (int)bVisible;
    mPipelinePost.ubo[currentFrame]->Flush();

    // Render composition
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelinePost.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelinePost.pipelineLayout, 0, 1, &mPipelinePost.descSet[currentFrame], 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
