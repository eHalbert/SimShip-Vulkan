/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "HullMesh.h"

extern uint32_t g_FramesInFlight;

// HULL MESH (Colored faces + wireframe) ////////////////////////////////////////////////////////

HullMesh::HullMesh(shared_ptr<VulkanDevice> vulkanDevice, vector<float> vertices, vector<unsigned int> indices)
{
    mVulkanDevice = vulkanDevice;

    CreateVertexBuffer(vertices, indices);
}
HullMesh::~HullMesh()
{
    mPipeline.destroy(mVulkanDevice->device);
	vkDestroyPipeline(mVulkanDevice->device, mPipelineWireframe, nullptr);
    mVertexBuffer.reset();
    mStagingVertexBuffer.reset();
    mIndexBuffer.reset();
}
extern VkRenderPass g_RenderPassScene;
void HullMesh::CreateVertexBuffer(const vector<float>& vertices, vector<unsigned int>& indices)
{
    mIndicesCount = indices.size();
    mVertexBufferSize = sizeof(float) * vertices.size();

    // 2. GPU Buffers
    mVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, mVertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mIndexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, mIndicesCount * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 3. Reusable staging buffer
    mStagingVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, mVertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 4. Upload vertex
    UpdateVertexBuffer(vertices);

	// 5. Upload index
    VkDeviceSize indexSize = mIndicesCount * sizeof(uint32_t);
    VulkanBuffer stagingIndex(mVulkanDevice, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(mVulkanDevice->device, stagingIndex.bufferMemory, 0, indexSize, 0, &data);
    memcpy(data, indices.data(), indexSize);
    vkUnmapMemory(mVulkanDevice->device, stagingIndex.bufferMemory);

    mIndexBuffer->CopyIntoBuffer(stagingIndex, indexSize);
}
void HullMesh::CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Destroy the old pipeline first
    mPipeline.destroy(mVulkanDevice->device);
    
    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/hull_colored.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/hull_colored.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    auto bindingDesc = sVertexHull::getBindingDescription();
    auto attrDescs = sVertexHull::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // 3. Descriptor Set Layout ubo)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },  // UBO → VERTEX
    } };
   
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, swapChainExtent };

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
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state

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
    pipelineInfo.layout = mPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);

    // PIPELINE 2 : WIREFRAME ///////////////////////////////////////////////////////

    auto fragCode2 = CompileShaderRuntime("Resources/Shaders/Model/hull_unicolor.frag");
    VkShaderModule fragModule2 = CreateShaderModule(mVulkanDevice->device, fragCode2);

    VkPipelineShaderStageCreateInfo shaderStages2[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule2, "main" }
    };

    VkGraphicsPipelineCreateInfo wireframeInfo = pipelineInfo;  // Copy
    wireframeInfo.pStages = shaderStages2;

    VkPipelineRasterizationStateCreateInfo& rasterizerWire = *const_cast<VkPipelineRasterizationStateCreateInfo*>(wireframeInfo.pRasterizationState);
    rasterizerWire.lineWidth = 1.0f; 
    rasterizerWire.polygonMode = VK_POLYGON_MODE_LINE;  

    // Wireframe color
    VkPipelineColorBlendAttachmentState wireBlend{};
    wireBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    wireBlend.blendEnable = VK_TRUE;  // BLEND for overlay
    wireBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    wireBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    wireBlend.colorBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo& colorBlendingWire = *const_cast<VkPipelineColorBlendStateCreateInfo*>(wireframeInfo.pColorBlendState);
    colorBlendingWire.pAttachments = &wireBlend;

    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &wireframeInfo, nullptr, &mPipelineWireframe);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule2, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    CreateDescriptor();
}
void HullMesh::CreateDescriptor()
{
    mPipeline.descSet.resize(g_FramesInFlight);
    mPipeline.ubo.resize(g_FramesInFlight);

    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
        mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixUBO));

    // Descriptor Pool
    VkDescriptorPoolSize poolSizes[1] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline.descSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline.descSet.data());

    UpdateDescriptor();
}
void HullMesh::UpdateDescriptor()
{
    // Update descriptors
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mPipeline.ubo[i]->GetSize();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = mPipeline.descSet[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
    }
}

void HullMesh::UpdateUBO(uint32_t currentFrame, Camera& camera, mat4& model)
{
    sMatrixUBO* ubo = static_cast<sMatrixUBO*>(mPipeline.ubo[currentFrame]->data);
    *ubo = { model, camera.GetView(), camera.GetProjection() };
}
void HullMesh::UpdateVertexBuffer(const vector<float>& mvVertexColored)
{
    // 1. CPU → Staging (the floats directly)
    void* data;
    vkMapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory, 0, mVertexBufferSize, 0, &data);
    memcpy(data, mvVertexColored.data(), mVertexBufferSize);
    vkUnmapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory);

    // 2. Staging → GPU
    mVertexBuffer->CopyIntoBuffer(*mStagingVertexBuffer, mVertexBufferSize);
}
void HullMesh::Render(VkCommandBuffer cmd, uint32_t currentFrame)
{
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[currentFrame], 0, nullptr);
    
    VkDeviceSize offset = 0;

	// Pipeline colored faces
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mIndicesCount, 1, 0, 0, 0);

	// Pipeline wireframe
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineWireframe);
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mIndicesCount, 1, 0, 0, 0);
}

void HullMesh::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    vkDeviceWaitIdle(mVulkanDevice->device);
    mPipeline.destroy(mVulkanDevice->device);
    vkDestroyPipeline(mVulkanDevice->device, mPipelineWireframe, nullptr);
    mPipelineWireframe = VK_NULL_HANDLE;
    CreatePipeline(renderPass, newExtent);
}

// LINE MESH (simple lines) ////////////////////////////////////////////////////////

LineMesh::LineMesh(shared_ptr<VulkanDevice> vulkanDevice, const vector<vec3>& vertices)
{
    mVulkanDevice = vulkanDevice;

    CreateVertexBuffer(vertices);
}
LineMesh::~LineMesh()
{
    mPipeline.destroy(mVulkanDevice->device);
    mVertexBuffer.reset();
    mStagingVertexBuffer.reset();
}

void LineMesh::CreateVertexBuffer(const vector<vec3>& vertices)
{
    mVertexCount = static_cast<uint32_t>(vertices.size());
    size_t vertexBufferSize = sizeof(sVertexLine) * vertices.size();

    // GPU Buffer
    mVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Reusable staging buffer
    mStagingVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Upload initial
    UpdateVertices(vertices);
}
void LineMesh::CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent, VkPrimitiveTopology topology)
{
    mPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/line_unicolor.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/line_unicolor.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (only position)
    auto bindingDesc = sVertexLine::getBindingDescription();
    auto attrDescs = sVertexLine::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // 3. Descriptor Set Layout (UBO)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, swapChainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer → Mode ligne
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

	// 11. Dynamic state
    
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
    pipelineInfo.layout = mPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateDescriptor();
}
void LineMesh::CreateDescriptor()
{
    mPipeline.descSet.resize(g_FramesInFlight);
    mPipeline.ubo.resize(g_FramesInFlight); 
    
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
        mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixColorUBO));

    VkDescriptorPoolSize poolSizes[1] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline.descSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline.descSet.data());

    UpdateDescriptor();
}
void LineMesh::UpdateDescriptor()
{
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mPipeline.ubo[i]->GetSize();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = mPipeline.descSet[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
    }
}

void LineMesh::UpdateUBO(uint32_t currentFrame, Camera& camera, mat4& model, vec4 color)
{
    sMatrixColorUBO* ubo = static_cast<sMatrixColorUBO*>(mPipeline.ubo[currentFrame]->data);
    *ubo = { model, camera.GetView(), camera.GetProjection(), color };
}
void LineMesh::UpdateVertices(const vector<vec3>& vertices)
{
    mVertexCount = static_cast<uint32_t>(vertices.size());
    size_t vertexBufferSize = sizeof(sVertexLine) * vertices.size();

    // Conversion vec3 → VertexLine
    vector<sVertexLine> vertexLines(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        vertexLines[i].pos[0] = vertices[i].x;
        vertexLines[i].pos[1] = vertices[i].y;
        vertexLines[i].pos[2] = vertices[i].z;
    }

    // Upload
    void* data;
    vkMapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, vertexLines.data(), vertexBufferSize);
    vkUnmapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory);

    mVertexBuffer->CopyIntoBuffer(*mStagingVertexBuffer, vertexBufferSize);
}
void LineMesh::Render(VkCommandBuffer cmd, uint32_t currentFrame)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[currentFrame], 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);

    vkCmdDraw(cmd, mVertexCount, 1, 0, 0);
}

void LineMesh::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent, VkPrimitiveTopology topology)
{
    CreatePipeline(renderPass, newExtent, topology);
}

// WAKE MESH (wake lines) ////////////////////////////////////////////////////////

WakeMesh::WakeMesh(shared_ptr<VulkanDevice> vulkanDevice, const vector<sFoamVertex>& vertices)
{
    mVulkanDevice = vulkanDevice;
}
WakeMesh::~WakeMesh()
{
    mPipeline.destroy(mVulkanDevice->device);
    mVertexBuffer.reset();
    mStagingVertexBuffer.reset();

	mPipelineTexture.destroy(mVulkanDevice->device);

    vkDestroyPipeline(mVulkanDevice->device, mBlurHorizontalPipeline, nullptr);
    vkDestroyPipeline(mVulkanDevice->device, mBlurVerticalPipeline, nullptr);
    vkDestroyPipelineLayout(mVulkanDevice->device, mBlurHorizontalPipelineLayout, nullptr);
	// Don't destroy mBlurVerticalPipelineLayout because it's the same as mBlurHorizontalPipelineLayout
    vkDestroyDescriptorSetLayout(mVulkanDevice->device, mBlurDescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(mVulkanDevice->device, mBlurDescriptorPool, nullptr);

    for (auto& ubo : mBlurUBO) ubo.reset();
}

// Pipeline for drawing the wake mesh
void WakeMesh::CreateVertexBuffer(const vector<sFoamVertex>& vertices)
{
    mVertexCount = static_cast<uint32_t>(vertices.size());
    mVertexBufferSize = sizeof(sVertexWake) * vertices.size();

    mVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, mVertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mStagingVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, mVertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}
void WakeMesh::CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    mPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/line_unicolor.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/line_unicolor.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    auto bindingDesc = sVertexWake::getBindingDescription();
    auto attrDescs = sVertexWake::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // 3. Descriptor Set Layout (UBO)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input assembly → TRIANGLES (comme GL_TRIANGLES)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, swapChainExtent };

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
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
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
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

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
    pipelineInfo.layout = mPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateDescriptor();
}
void WakeMesh::CreateDescriptor()
{
    mPipeline.descSet.resize(g_FramesInFlight);
    mPipeline.ubo.resize(g_FramesInFlight); 
    
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
        mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixColorUBO));

    VkDescriptorPoolSize poolSizes[1] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline.descSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline.descSet.data());

    UpdateDescriptor();
}
void WakeMesh::UpdateDescriptor()
{
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mPipeline.ubo[i]->GetSize();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = mPipeline.descSet[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
    }
}

void WakeMesh::UpdateUBO(uint32_t imageIndex, Camera& camera, vec4 color)
{
    sMatrixColorUBO* ubo = static_cast<sMatrixColorUBO*>(mPipeline.ubo[imageIndex]->data);
    *ubo = { mat4(1.0f), camera.GetView(), camera.GetProjection(), color };
}
void WakeMesh::UpdateVertices(const vector<sFoamVertex>& vertices)
{
    if (vertices.empty())
        return;

    size_t uploadSize = sizeof(sVertexWake) * vertices.size();

    // Reallocation if the buffer is too small (or not yet created)
    if (uploadSize > mVertexBufferSize)
    {
        // Wait for the GPU to finish before destroying the buffers
        vkDeviceWaitIdle(mVulkanDevice->device);

        mVertexBuffer.reset();
        mStagingVertexBuffer.reset();

        CreateVertexBuffer(vertices);  // Recreate with new dimensions
    }

    mVertexCount = static_cast<uint32_t>(vertices.size());

    // Conversion sFoamVertex → VertexWake
    vector<sVertexWake> vkVerts(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
        vkVerts[i].pos = vertices[i].pos;

    void* data;
    vkMapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory, 0, uploadSize, 0, &data);
    memcpy(data, vkVerts.data(), uploadSize);
    vkUnmapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory);

    mVertexBuffer->CopyIntoBuffer(*mStagingVertexBuffer, uploadSize);
}
void WakeMesh::RenderMesh(VkCommandBuffer cmd, uint32_t currentFrame)
{
    if (mVertexCount < 3)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[currentFrame], 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);
    vkCmdDraw(cmd, mVertexCount, 1, 0, 0);
}

// Pipeline for the TexWake texture
void WakeMesh::CreateVertexBufferTexture(const vector<sFoamVertex>& vertices)
{
    mVertexCountTexture = static_cast<uint32_t>(vertices.size());
    mVertexBufferTextureSize = sizeof(VertexWakeAlpha) * vertices.size();

    mVertexBufferTexture = make_unique<VulkanBuffer>(mVulkanDevice, mVertexBufferTextureSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mStagingVertexBufferTexture = make_unique<VulkanBuffer>(mVulkanDevice, mVertexBufferTextureSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}
void WakeMesh::CreatePipelineTexture(VkRenderPass renderPass, VkExtent2D extent)
{
    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Ship/wake.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Ship/wake.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (VertexWakeAlpha : pos + alpha)
    auto bindingDesc = VertexWakeAlpha::getBindingDescription();
    auto attrDescs = VertexWakeAlpha::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // 3. Descriptor Set Layout (UBO binding=0)
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelineTexture.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelineTexture.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelineTexture.pipelineLayout);

    // 5. Input Assembly → TRIANGLES
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 6. Viewport & Scissor (offscreen texture size)
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
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling (pas de MSAA en offscreen)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 9. Pas de depth-stencil en offscreen 2D
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Blending alpha standard
    VkPipelineColorBlendAttachmentState colorBlendAtt{};
    colorBlendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    colorBlendAtt.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAtt;

    // 12. Création de la pipeline
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
    pipelineInfo.layout = mPipelineTexture.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelineTexture.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateDescriptorTexture();
}
void WakeMesh::CreateDescriptorTexture()
{
    mPipelineTexture.descSet.resize(g_FramesInFlight);
    mPipelineTexture.ubo.resize(g_FramesInFlight);
    
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
        mPipelineTexture.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sWakeUBO));
    
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1 * g_FramesInFlight;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelineTexture.descPool);
    
    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipelineTexture.descSetLayout);
   
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelineTexture.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipelineTexture.descSet.data());
    UpdateDescriptorTexture();
}
void WakeMesh::UpdateDescriptorTexture()
{
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mPipelineTexture.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mPipelineTexture.ubo[i]->GetSize();
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = mPipelineTexture.descSet[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &descriptorWrite, 0, nullptr);
	}
}

void WakeMesh::UpdateTextureUBO(uint32_t imageIndex, float scaleX, float scaleZ, float offsetX, float offsetZ, float originX, float originZ)
{
    sWakeUBO* ubo = static_cast<sWakeUBO*>(mPipelineTexture.ubo[imageIndex]->data);
    ubo->scaleX = scaleX;
    ubo->scaleZ = scaleZ;
    ubo->offsetX = offsetX;
    ubo->offsetZ = offsetZ;
    ubo->originX = originX;
	ubo->originZ = originZ;
}
void WakeMesh::UpdateTextureVertices(const vector<sFoamVertex>& vertices)
{
    if (vertices.empty())
        return;

    size_t uploadSize = sizeof(VertexWakeAlpha) * vertices.size();

    // Reallocation if the buffer is too small (or not yet created)
    if (uploadSize > mVertexBufferTextureSize)
    {
        // Wait for the GPU to finish before destroying the buffers
        vkDeviceWaitIdle(mVulkanDevice->device);

        mVertexBufferTexture.reset();
        mStagingVertexBufferTexture.reset();

        CreateVertexBufferTexture(vertices);  // Recreate with new dimensions
    }

    mVertexCountTexture = static_cast<uint32_t>(vertices.size());

    // Conversion sFoamVertex → VertexWake
    vector<VertexWakeAlpha> vkVerts(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        vkVerts[i].pos = vertices[i].pos;
		vkVerts[i].alpha = vertices[i].alpha;
    }

    void* data;
    vkMapMemory(mVulkanDevice->device, mStagingVertexBufferTexture->bufferMemory, 0, uploadSize, 0, &data);
    memcpy(data, vkVerts.data(), uploadSize);
    vkUnmapMemory(mVulkanDevice->device, mStagingVertexBufferTexture->bufferMemory);

    mVertexBufferTexture->CopyIntoBuffer(*mStagingVertexBufferTexture, uploadSize);
}
void WakeMesh::RenderTexture(VkCommandBuffer cmd, uint32_t currentFrame)
{
    if (mVertexCountTexture < 3)
        return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineTexture.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineTexture.pipelineLayout, 0, 1, &mPipelineTexture.descSet[currentFrame], 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBufferTexture->buffer, &offset);
	vkCmdDraw(cmd, mVertexCountTexture, 1, 0, 0);
}

// Pipelines for horizontal and vertical passes
void WakeMesh::CreateBlurPipelines() 
{
    // Previous cleanup
    if (mBlurHorizontalPipeline)        vkDestroyPipeline(mVulkanDevice->device, mBlurHorizontalPipeline, nullptr);
    if (mBlurVerticalPipeline)          vkDestroyPipeline(mVulkanDevice->device, mBlurVerticalPipeline, nullptr);
    if (mBlurHorizontalPipelineLayout)  vkDestroyPipelineLayout(mVulkanDevice->device, mBlurHorizontalPipelineLayout, nullptr);
    if (mBlurVerticalPipelineLayout)    vkDestroyPipelineLayout(mVulkanDevice->device, mBlurVerticalPipelineLayout, nullptr);
    if (mBlurDescriptorSetLayout)       vkDestroyDescriptorSetLayout(mVulkanDevice->device, mBlurDescriptorSetLayout, nullptr);

    // 1. Shader Horizontal
    auto hShaderCode = CompileShaderRuntime("Resources/Shaders/Ship/wake_gauss_h.comp");
    VkShaderModule hShaderModule = CreateShaderModule(mVulkanDevice->device, hShaderCode);

    // 1. Shader Vertical  
    auto vShaderCode = CompileShaderRuntime("Resources/Shaders/Ship/wake_gauss_v.comp");
    VkShaderModule vShaderModule = CreateShaderModule(mVulkanDevice->device, vShaderCode);

    VkPipelineShaderStageCreateInfo hShaderStageInfo{}, vShaderStageInfo{};
    hShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    hShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    hShaderStageInfo.module = hShaderModule;
    hShaderStageInfo.pName = "main";

    vShaderStageInfo = hShaderStageInfo;
    vShaderStageInfo.module = vShaderModule;

    // 2. DescriptorSetLayout (inputTex + outputTex + UBO)
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // inputTex
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // outputTex  
        { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // BlurParams
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mBlurDescriptorSetLayout);

    // 4. PipelineLayout (identical for both)
    VkDescriptorSetLayout layouts[1] = { mBlurDescriptorSetLayout };
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(float);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mBlurHorizontalPipelineLayout);
    mBlurVerticalPipelineLayout = mBlurHorizontalPipelineLayout; // Reuse

    // 5. Pipelines
    VkComputePipelineCreateInfo hPipelineInfo{}, vPipelineInfo{};
    hPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    hPipelineInfo.stage = hShaderStageInfo;
    hPipelineInfo.layout = mBlurHorizontalPipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &hPipelineInfo, nullptr, &mBlurHorizontalPipeline);

    vPipelineInfo = hPipelineInfo;
    vPipelineInfo.stage = vShaderStageInfo;
    vPipelineInfo.layout = mBlurVerticalPipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &vPipelineInfo, nullptr, &mBlurVerticalPipeline);

    vkDestroyShaderModule(mVulkanDevice->device, hShaderModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vShaderModule, nullptr);

    CreateBlurDescriptors();
}
void WakeMesh::CreateBlurDescriptors()
{
    mBlurUBO.resize(g_FramesInFlight);
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
        mBlurUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(float));

    // 2 passes (H + V) × g_FramesInFlight sets, each with 2 storage images + 1 UBO
    uint32_t totalSets = 2 * g_FramesInFlight;

    array<VkDescriptorPoolSize, 2> poolSizes = { {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  totalSets * 2 },  // 2 images per set
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, totalSets * 1 }   // 1 UBO per set
    } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = totalSets;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mBlurDescriptorPool);

    // Allocate all sets at once
    vector<VkDescriptorSetLayout> layouts(totalSets, mBlurDescriptorSetLayout);
    vector<VkDescriptorSet> sets(totalSets);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mBlurDescriptorPool;
    allocInfo.descriptorSetCount = totalSets;
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, sets.data());

    // First half → Horizontal, second half → Vertical
    mBlurDescriptorSetHorizontal.resize(g_FramesInFlight);
    mBlurDescriptorSetVertical.resize(g_FramesInFlight);
    for (uint32_t i = 0; i < g_FramesInFlight; ++i)
    {
        mBlurDescriptorSetHorizontal[i] = sets[i];
        mBlurDescriptorSetVertical[i] = sets[g_FramesInFlight + i];
    }
}
void WakeMesh::UpdateBlurDescriptorsHorizontal(uint32_t imageIndex, VulkanTexture& input, VulkanTexture& output)
{
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = mBlurUBO[imageIndex]->buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(float);

    VkDescriptorImageInfo imageInfos[2];
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[0].imageView = input.imageView;
    imageInfos[0].sampler = VK_NULL_HANDLE;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = output.imageView;
    imageInfos[1].sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = mBlurDescriptorSetHorizontal[imageIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &imageInfos[0];

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = mBlurDescriptorSetHorizontal[imageIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &imageInfos[1];

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = mBlurDescriptorSetHorizontal[imageIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mVulkanDevice->device, 3, writes, 0, nullptr);
}
void WakeMesh::UpdateBlurDescriptorsVertical(uint32_t imageIndex, VulkanTexture& input, VulkanTexture& output)
{
    // Identical but dstSet = mBlurDescriptorSetVertical
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = mBlurUBO[imageIndex]->buffer;
    bufferInfo.offset = 0; 
    bufferInfo.range = sizeof(float);

    VkDescriptorImageInfo imageInfos[2];
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos[0].imageView = input.imageView; 
    imageInfos[0].sampler = VK_NULL_HANDLE;
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos[1].imageView = output.imageView; 
    imageInfos[1].sampler = VK_NULL_HANDLE;

    VkWriteDescriptorSet writes[3] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; 
    writes[0].dstSet = mBlurDescriptorSetVertical[imageIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1; 
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &imageInfos[0];
       
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; 
    writes[1].dstSet = mBlurDescriptorSetVertical[imageIndex];
    writes[1].dstBinding = 1; 
    writes[1].descriptorCount = 1; 
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; 
    writes[1].pImageInfo = &imageInfos[1];
       
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; 
    writes[2].dstSet = mBlurDescriptorSetVertical[imageIndex];
    writes[2].dstBinding = 2; writes[2].descriptorCount = 1; 
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(mVulkanDevice->device, 3, writes, 0, nullptr);
}
void WakeMesh::ComputeBlur(uint32_t imageIndex, VulkanTexture& input, VulkanTexture& temp, VulkanTexture& output, float texSize)
{
    // 1. Descriptors before SingleTimeCommands
    float* uboData = static_cast<float*>(mBlurUBO[imageIndex]->data);
    uboData[0] = 1.0f / input.extent.width;  // Horizontal
    UpdateBlurDescriptorsHorizontal(imageIndex, input, temp);

    uboData[0] = 1.0f / input.extent.height; // Vertical  
    UpdateBlurDescriptorsVertical(imageIndex, temp, output);

    // 2. SingleTimeCommands
    VkCommandBuffer cmd = mVulkanDevice->BeginSingleTimeCommands();

    // 3. Horizontal pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mBlurHorizontalPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mBlurHorizontalPipelineLayout, 0, 1, &mBlurDescriptorSetHorizontal[imageIndex], 0, nullptr);
    vkCmdDispatch(cmd, (input.extent.width + 15) / 16, (input.extent.height + 15) / 16, 1);

    // 4. Memory barrier
    VkMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    // 5. Vertical pass (NO descriptor update !)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mBlurVerticalPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mBlurVerticalPipelineLayout, 0, 1, &mBlurDescriptorSetVertical[imageIndex], 0, nullptr);
    vkCmdDispatch(cmd, (input.extent.width + 15) / 16, (input.extent.height + 15) / 16, 1);

    mVulkanDevice->EndSingleTimeCommands(cmd);
}

// GRID MESH //////////////////////////////////////////////////////////////////

GridMesh::GridMesh(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D swapChainExtent, uint32_t gridSize, float cellSize)
{
    mVulkanDevice = vulkanDevice;
    mGridSize = gridSize;
    mCellSize = cellSize;

    CreateVertices();
    CreateVertexBuffer();
    CreatePipeline(renderPass, swapChainExtent);
}
GridMesh::~GridMesh()
{
    mPipeline.destroy(mVulkanDevice->device);
    mVertexBuffer.reset();
    mStagingVertexBuffer.reset();
}

void GridMesh::CreateVertices() 
{
    float halfSize = (mGridSize * mCellSize) / 2.0f;

    // Lines parallel to the X axis (Z fixed, Y=0)
    for (uint32_t i = 0; i <= mGridSize; ++i) 
    {
        float z = -halfSize + i * mCellSize;

        mVertices.push_back({ -halfSize, 0.0f, z }); // start
        mVertices.push_back({ +halfSize, 0.0f, z }); // end
    }

    // Lines parallel to the Z axis (X fixed, Y=0)
    for (uint32_t i = 0; i <= mGridSize; ++i) 
    {
        float x = -halfSize + i * mCellSize;

        mVertices.push_back({ x, 0.0f, -halfSize }); // start
        mVertices.push_back({ x, 0.0f, +halfSize }); // end
    }
}
void GridMesh::CreateVertexBuffer() 
{
    size_t vertexBufferSize = sizeof(sVertexLine) * mVertices.size();

    // GPU buffer (DEVICE_LOCAL)
    mVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Staging (HOST_VISIBLE + HOST_COHERENT)
    mStagingVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory, 0, vertexBufferSize, 0, &data);
    memcpy(data, mVertices.data(), vertexBufferSize);
    vkUnmapMemory(mVulkanDevice->device, mStagingVertexBuffer->bufferMemory);

    mVertexBuffer->CopyIntoBuffer(*mStagingVertexBuffer, vertexBufferSize);
}
void GridMesh::CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
	mPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders (mono‑color)
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/line_unicolor.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/line_unicolor.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    auto bindingDesc = sVertexLine::getBindingDescription();
    auto attrDescs = sVertexLine::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attrDescs.data();

    // 3. Descriptor set layout (UBO)
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

    // 4. Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input assembly : LINES
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; // each pair of points = a segment
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, float(swapChainExtent.width), float(swapChainExtent.height), 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, swapChainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer (mode ligne)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth & stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state (no dynamic state)
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 0;
    dynamicState.pDynamicStates = nullptr;

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
    pipelineInfo.layout = mPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateDescriptor();
}
void GridMesh::CreateDescriptor() 
{
    mPipeline.descSet.resize(g_FramesInFlight);
    mPipeline.ubo.resize(g_FramesInFlight);
    
    // 1. Descriptor pool
    VkDescriptorPoolSize poolSizes[1] = { {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, g_FramesInFlight  // 1 UBO per frame → 2 UBOs for 2 frames
    } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;  // descriptor sets

    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline.descPool);

    // 2. UBO for each frame
    for (uint32_t i = 0; i < g_FramesInFlight; ++i) 
        mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixColorUBO));

    // 3. Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline.descSetLayout);

    // 4. Allocation of 2 descriptor sets
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();

    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline.descSet.data());

    // 5. Write the UBOs into each descriptor set
    for (uint32_t i = 0; i < g_FramesInFlight; ++i) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mPipeline.ubo[i]->GetSize();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mPipeline.descSet[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &write, 0, nullptr);
    }
}

void GridMesh::UpdateUBO(uint32_t currentFrame, Camera& camera, const mat4& model, const vec4& color)
{
    sMatrixColorUBO* ubo = static_cast<sMatrixColorUBO*>(mPipeline.ubo[currentFrame]->data);
    *ubo = { model, camera.GetView(), camera.GetProjection(), color};
}
void GridMesh::Render(VkCommandBuffer cmd, uint32_t currentFrame)
{
    if (!bVisible) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[currentFrame], 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);

    uint32_t vertexCount = static_cast<uint32_t>(mVertices.size());
    vkCmdDraw(cmd, vertexCount, 1, 0, 0); // LINE_LIST : each pair of vertices = a segment
}

void GridMesh::RecreatePipeline(VkRenderPass renderPassScene, VkExtent2D newExtent)
{
    CreatePipeline(renderPassScene, newExtent);
}
