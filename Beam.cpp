/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Beam.h"

Beam::~Beam()
{
    mPipeline.destroy(mVulkanDevice->device);
    mVertexBuffer.reset();
    mIndiceBuffer.reset();
}

void Beam::Init(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, float range)
{
    mVulkanDevice = vulkanDevice;
    
    CreateGeometry(range);
    CreatePipeline(renderPass, extent);
}
void Beam::CreateGeometry(float range)
{
	float angle_deg = 0.5f;
	int slices = 8;

	vector<sVertexBeam> vertices;
	vector<uint32_t> indices;

	float angle_rad = glm::radians(angle_deg);
	float ConeRadiusSmall = 1.0f;
	float ConeRadiusLarge = ConeRadiusSmall + range * tan(angle_rad / 2.0f);

	vec3 base_center = vec3(0.0f);
	vec3 tip_center = base_center + vec3(range, 0, 0);

	// Analytic lateral normal
	float slope = (ConeRadiusLarge - ConeRadiusSmall) / range;
	float normal_x = 1.0f / sqrt(slope * slope + 1.0f);
	float normal_yrz = slope * normal_x;

	// Generation of vertices + normals
	for (int i = 0; i < slices; ++i)
	{
		float theta = 2.0f * glm::pi<float>() * float(i) / float(slices);
		float y = cos(theta);
		float z = sin(theta);

		vec3 pos0 = base_center + vec3(0, ConeRadiusSmall * y, ConeRadiusSmall * z);
		vec3 pos1 = tip_center + vec3(0, ConeRadiusLarge * y, ConeRadiusLarge * z);
		vec3 normal = glm::normalize(vec3(normal_x, normal_yrz * y, normal_yrz * z));

		vertices.push_back({ pos0, normal, 1.0f }); // base
		vertices.push_back({ pos1, normal, 0.0f }); // tip
	}

    size_t size = vertices.size() * sizeof(sVertexBeam);
    mVertexBuffer = make_unique<VulkanUBO>(mVulkanDevice, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    float* gpuData1 = static_cast<float*>(mVertexBuffer->data);
    memcpy(gpuData1, vertices.data(), size);
    mVertexBuffer->Flush();

	// Lateral surface indices
	for (uint32_t i = 0; i < slices; ++i)
	{
        uint32_t next = (i + 1) % slices;
        uint32_t i0 = i * 2 + 0;
        uint32_t i1 = i * 2 + 1;
        uint32_t j0 = next * 2 + 0;
        uint32_t j1 = next * 2 + 1;

		// Two triangles for the quad
		indices.push_back(i0);
		indices.push_back(j1);
		indices.push_back(i1);

		indices.push_back(i0);
		indices.push_back(j0);
		indices.push_back(j1);
	}
	mIndiceCount = indices.size();

    mIndiceBuffer = make_unique<VulkanUBO>(mVulkanDevice, mIndiceCount * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    uint32_t* gpuData2 = static_cast<uint32_t*>(mIndiceBuffer->data);
    memcpy(gpuData2, indices.data(), mIndiceCount * sizeof(uint32_t));
    mIndiceBuffer->Flush();
}
void Beam::CreatePipeline(VkRenderPass renderPass, VkExtent2D extent)
{
    mPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Misc/lighthouse.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Misc/lighthouse.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" },
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(sVertexBeam);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    array<VkVertexInputAttributeDescription, 3> attribs{};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(sVertexBeam, pos) };
    attribs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(sVertexBeam, normal) };
    attribs[2] = { 2, 0, VK_FORMAT_R32_SFLOAT,       offsetof(sVertexBeam, t) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout  (binding 0 = UBO uniquement)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(sLightPushConstants);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input Assembly 
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // 6. Viewport & Scissor
    VkViewport viewport{ 0.f, 0.f, (float)extent.width, (float)extent.height, 0.f, 1.f };
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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;     // billboard → pas de backface culling
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;   // transparent → ne pas écrire en profondeur
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // 10. Color blending (additive, idéal pour les halos lumineux)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;             // additif
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Pipeline 
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
    pipelineInfo.layout = mPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
}
void Beam::Render(VkCommandBuffer commandBuffer, uint32_t frame, const mat4& model, const mat4& view, const mat4& proj, vec3 color, float intensity)
{
    if (!bVisible) return;

    sLightPushConstants pc{};
    pc.model = model;
    pc.view = view;
    pc.proj = proj;
    pc.color = color;
    pc.intensity = intensity;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);

    VkBuffer vertexBuffers[] = { mVertexBuffer->buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdPushConstants(commandBuffer, mPipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sLightPushConstants), &pc);

    vkCmdBindIndexBuffer(commandBuffer, mIndiceBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(mIndiceCount), 1, 0, 0, 0);
}

void Beam::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    CreatePipeline(renderPass, newExtent);
}