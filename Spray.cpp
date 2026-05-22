/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Spray.h"

extern uint32_t g_FramesInFlight;

Spray::Spray(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent)
{
    mVulkanDevice = vulkanDevice;
    
    lifeSpan = (int)((longLife - shortLife) * 10.0f) + 1;
    mvParticles.resize(mMaxParticles);
    CreatePipeline(renderPass, extent);
    CreateBufferParticles();
}
Spray::~Spray()
{
    mPipeline.destroy(mVulkanDevice->device);
    mParticleBuffer.reset();
}
void Spray::CreatePipeline(VkRenderPass renderPass, VkExtent2D extent)
{
    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Spray/spray.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Spray/spray.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0; 
    binding.stride = sizeof(ParticleGPU); 
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    array<VkVertexInputAttributeDescription, 3> attribs = {};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,       offsetof(ParticleGPU, position) };
    attribs[1] = { 1, 0, VK_FORMAT_R32_SFLOAT,             offsetof(ParticleGPU, life) };
    attribs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,    offsetof(ParticleGPU, color) };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout (1 UBO)
    VkDescriptorSetLayoutBinding layoutBinding = {};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input assembly (POINTS)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport = { 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, extent };
    
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // 10. Color blending (alpha)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.colorWriteMask = 0xF;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = mPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateDescriptors();
}
void Spray::CreateDescriptors()
{
    mPipeline.descSet.resize(g_FramesInFlight);
    mPipeline.ubo.resize(g_FramesInFlight);
    
    // 1. UBO Buffer (1 per frame in flight)
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sSprayUBO));

    // 2. Descriptor Pool
    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight };
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline.descPool);

    // 3. Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline.descSetLayout);
  
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline.descPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline.descSet.data());

    UpdateDescriptors();
}
void Spray::UpdateDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = mPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(sSprayUBO);

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = mPipeline.descSet[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
    }
}
void Spray::CreateBufferParticles()
{
    mParticleBuffer = make_unique<VulkanUBO>(mVulkanDevice, mMaxParticles * sizeof(ParticleGPU), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}

void Spray::Emit(vec3 position, vec3 velocity)
{
    if (mActiveParticles >= mMaxParticles)
        return;

    vec3 randomOffset = vec3(mDistOffset(mRng), mDistOffset(mRng), mDistOffset(mRng));
    mvParticles[mActiveParticles].position = position + randomOffset;

    vec3 randomVelocity = vec3(mDistVelocity(mRng), mDistVelocity(mRng), mDistVelocity(mRng));
    mvParticles[mActiveParticles].velocity = velocity + randomVelocity;

    mvParticles[mActiveParticles].life = shortLife + mDistLife(mRng) * (longLife - shortLife);

    float gray = mDistGray(mRng);
    mvParticles[mActiveParticles].color = vec4(gray, gray, gray, 0.8f);

    mActiveParticles++;
}
void Spray::Update(float deltaTime)
{
    const float gravity = 9.81f;

    for (int i = 0; i < mActiveParticles; ++i)
    {
        mvParticles[i].life -= deltaTime;
        if (mvParticles[i].life < 0)
        {
            // Replace dead by the last
            mvParticles[i] = mvParticles[mActiveParticles - 1];
            mActiveParticles--;
            i--;
            continue;
        }

        // Apply gravity
        mvParticles[i].velocity.y -= gravity * deltaTime;

        // Turbulence independent of time, which decreases with age
        float lifeRatio = mvParticles[i].life / lifeSpan;       // 1.0 = young, 0.0 = old
        float turbulenceStrength = 2.5f * (1.0f - lifeRatio);   // stronger at the end of life
        mvParticles[i].velocity += turbulenceStrength * vec3(mDist(mRng), mDist(mRng) * 0.3f, mDist(mRng));

        // Update position
        mvParticles[i].position += mvParticles[i].velocity * deltaTime;
    }

    UpdateBufferParticles();
}
void Spray::UpdateBufferParticles()
{ 
    ParticleGPU* gpuData = static_cast<ParticleGPU*>(mParticleBuffer->data);
    memcpy(gpuData, mvParticles.data(), mActiveParticles * sizeof(ParticleGPU));
    mParticleBuffer->Flush();
}
void Spray::Render(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, float density, float exposure)
{
    if (!bVisible || mActiveParticles == 0) return;

    // Update UBO
    sSprayUBO ubo{};
    ubo.view = camera.GetView();
    ubo.proj = camera.GetProjection();
    ubo.density = density;
    ubo.lifeSpan = lifeSpan;
    ubo.exposure = exposure;
    memcpy(mPipeline.ubo[frame]->data, &ubo, sizeof(sSprayUBO));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);

    // Descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[frame], 0, nullptr);

    // Vertex buffer (fictional + particles)
    VkBuffer vertexBuffers[] = { mParticleBuffer->buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    vkCmdDraw(commandBuffer, 1, mActiveParticles, 0, 0);
}

void Spray::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    mPipeline.destroy(mVulkanDevice->device);
    CreatePipeline(renderPass, newExtent);
}