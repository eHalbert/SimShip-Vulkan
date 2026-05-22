#include "Smoke.h"

extern uint32_t g_FramesInFlight;

Smoke::Smoke(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent)
{
    mVulkanDevice = vulkanDevice;
    CreatePipeline(renderPassScene, extent);
}
Smoke::~Smoke()
{
    if (mComputePipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(mVulkanDevice->device, mComputePipeline, nullptr);

    if (mComputeLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(mVulkanDevice->device, mComputeLayout, nullptr);

    mRenderPipeline.destroy(mVulkanDevice->device);

    mParticles.reset();

    for (uint32_t f = 0; f < g_FramesInFlight; f++)
        mComputeUBO[f].reset();
}

void Smoke::CreatePipeline(VkRenderPass renderPassScene, VkExtent2D extent)
{
    // RENDER PIPELINE /////////////////////////////////////////////////////////////

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Smoke/smoke.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Smoke/smoke.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (POINT_LIST = no vertex attributes)
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // 3. Descriptor Set Layout - 2 bindings
    VkDescriptorSetLayoutBinding bindings[2] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // Particles
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr }  // Ubo
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mRenderPipeline.descSetLayout);

    // 4. Pipeline Layout (render)
    VkPushConstantRange renderPushRange{};
    renderPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    renderPushRange.offset = 0;
    renderPushRange.size = sizeof(sSmokeRenderPush);

    VkPipelineLayoutCreateInfo renderLayoutInfo{};
    renderLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    renderLayoutInfo.setLayoutCount = 1;
    renderLayoutInfo.pSetLayouts = &mRenderPipeline.descSetLayout;
    renderLayoutInfo.pushConstantRangeCount = 1;
    renderLayoutInfo.pPushConstantRanges = &renderPushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &renderLayoutInfo, nullptr, &mRenderPipeline.pipelineLayout);

    // 5. Input assembly (POINTS)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    // 6. Viewport & scissor
    VkViewport viewport{ 0, 0, (float)extent.width, (float)extent.height, 0, 1 };
    VkRect2D scissor{ {0,0}, extent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

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

    // 12. Graphics Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = mRenderPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPassScene;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mRenderPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    mParticles = make_unique<VulkanUBO>(mVulkanDevice, static_cast<VkDeviceSize>(SMOKE_MAX_PARTICLES * sizeof(ParticleGPU)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    
    // Init dead particles
    ParticleGPU* data = static_cast<ParticleGPU*>(mParticles->data);
    for (uint32_t j = 0; j < SMOKE_MAX_PARTICLES; j++)
        data[j].life = 0.0f;

    // BUFFERS  //////////////////////////////////////////////////////////////////////

	mComputeUBO.resize(g_FramesInFlight);
    mRenderPipeline.descSet.resize(g_FramesInFlight);

    for (uint32_t f = 0; f < g_FramesInFlight; f++)
        mComputeUBO[f] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sComputeUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    // DESCRIPTOR POOL + SETS ////////////////////////////////////////////////////////

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 * g_FramesInFlight },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mRenderPipeline.descPool);

    for (uint32_t f = 0; f < g_FramesInFlight; f++)
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = mRenderPipeline.descPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &mRenderPipeline.descSetLayout;
        vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mRenderPipeline.descSet[f]);
    }

    // COMPUTE PIPELINE //////////////////////////////////////////////////////////////

    // 1. Compute shader
    auto computeCode = CompileShaderRuntime("Resources/Shaders/Smoke/smoke.comp");
    VkShaderModule computeModule = CreateShaderModule(mVulkanDevice->device, computeCode);

    // 2. Compute Pipeline Layout (reuses descSetLayout)
    VkPushConstantRange computePushRange{};
    computePushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    computePushRange.offset = 0;
    computePushRange.size = sizeof(sSmokeComputePush);

    VkPipelineLayoutCreateInfo computeLayoutInfo{};
    computeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayoutInfo.setLayoutCount = 1;
    computeLayoutInfo.pSetLayouts = &mRenderPipeline.descSetLayout;
    computeLayoutInfo.pushConstantRangeCount = 1;
    computeLayoutInfo.pPushConstantRanges = &computePushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &computeLayoutInfo, nullptr, &mComputeLayout);

    // 3. Compute Pipeline
    VkComputePipelineCreateInfo computePipelineInfo{};
    computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computePipelineInfo.stage.module = computeModule;
    computePipelineInfo.stage.pName = "main";
    computePipelineInfo.layout = mComputeLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &mComputePipeline);

    vkDestroyShaderModule(mVulkanDevice->device, computeModule, nullptr);

    // Initial write of descriptors
    for (uint32_t f = 0; f < g_FramesInFlight; f++)
    {
        VkDescriptorBufferInfo ssboInfo{};
        ssboInfo.buffer = mParticles->buffer;
        ssboInfo.offset = 0;
        ssboInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = mComputeUBO[f]->buffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(sComputeUBO);

        VkWriteDescriptorSet writes[2] = {};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mRenderPipeline.descSet[f];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &ssboInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mRenderPipeline.descSet[f];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &uboInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 2, writes, 0, nullptr);
    }
}

void Smoke::Update(VkCommandBuffer cmd, uint32_t frame, float dt, int nChimney, vec3 chimney1WorldPos, vec3 chimney2WorldPos, vec3 windDirection)
{
    // 1. Update UBO (per frame)
    sComputeUBO uboData;
    uboData.emitPositions[0] = vec4(chimney1WorldPos, 1.0f);
    uboData.emitPositions[1] = vec4(chimney2WorldPos, 1.0f);
    uboData.windDirection = vec4(windDirection, 1.0f);
    memcpy(mComputeUBO[frame]->data, &uboData, sizeof(sComputeUBO));

    // 2. Barrier HOST → COMPUTE
    // mParticles : the render of the previous frame has read it, the compute will write it
    // mComputeUBO : the CPU has just written it, the compute will read it
    VkBufferMemoryBarrier barriers[2] = {};

    barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;   // lu par le vertex shader avant
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].buffer = mParticles->buffer;
    barriers[0].offset = 0;
    barriers[0].size = VK_WHOLE_SIZE;

    barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barriers[1].buffer = mComputeUBO[frame]->buffer;
    barriers[1].offset = 0;
    barriers[1].size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, barriers, 0, nullptr);

    // 3. Compute dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mComputePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mComputeLayout, 0, 1, &mRenderPipeline.descSet[frame], 0, nullptr);

    sSmokeComputePush pushConstants = {
        dt, 3, nChimney, (int)mFrameSmokeCount, 1.0f, 4.0f
    };
    vkCmdPushConstants(cmd, mComputeLayout, VK_SHADER_STAGE_COMPUTE_BIT,  0, sizeof(sSmokeComputePush), &pushConstants);

    vkCmdDispatch(cmd, (SMOKE_MAX_PARTICLES + 255) / 256, 1, 1);

    // 4. Barrier COMPUTE → VERTEX
    VkBufferMemoryBarrier computeToVertex{};
    computeToVertex.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    computeToVertex.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    computeToVertex.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    computeToVertex.buffer = mParticles->buffer;
    computeToVertex.offset = 0;
    computeToVertex.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr, 1, &computeToVertex, 0, nullptr);

    mFrameSmokeCount++;
}
void Smoke::Render(VkCommandBuffer cmd, uint32_t frame, Camera& camera, Sky* sky, float density)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderPipeline.pipelineLayout, 0, 1, &mRenderPipeline.descSet[frame], 0, nullptr);

    // Push constants for render
    sSmokeRenderPush pushConstants = {
        camera.GetView(),
        camera.GetProjection(),
        density,
        3.0f,
        sky->Exposure,
        0.0f
    };
    vkCmdPushConstants(cmd, mRenderPipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sSmokeRenderPush), &pushConstants);
    
    vkCmdDraw(cmd, 1, SMOKE_MAX_PARTICLES, 0, 0);
}
