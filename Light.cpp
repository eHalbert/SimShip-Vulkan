/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Light.h"

Light::Light(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent)
{
    mVulkanDevice = vulkanDevice;

    CreateGeometry();
    CreatePipeline(renderPass, extent);
}
Light::~Light()
{
    mPipeline.destroy(mVulkanDevice->device);
    mVertexBuffer.reset();
}

void Light::CreateGeometry()
{
    // Quad billboard - position (vec2)
    const array<sVertexLight, 4> vertices = { {
        { {-0.5f,  0.5f} },
        { {-0.5f, -0.5f} },
        { { 0.5f,  0.5f} },
        { { 0.5f, -0.5f} },
    } };

    VkDeviceSize size = sizeof(vertices);

    mVertexBuffer = make_unique<VulkanUBO>(mVulkanDevice, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    
    float* gpuData = static_cast<float*>(mVertexBuffer->data);
    memcpy(gpuData, vertices.data(), size);
    mVertexBuffer->Flush();

}
void Light::CreatePipeline(VkRenderPass renderPass, VkExtent2D extent)
{
    mPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Misc/light.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Misc/light.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" },
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(sVertexLight);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    array<VkVertexInputAttributeDescription, 1> attribs{};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(sVertexLight, position) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout  (binding 0 = UBO only)
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
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; 

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
    depthStencil.depthWriteEnable = VK_FALSE;   // transparent → do not write to depth
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // 10. Color blending (additive, ideal for light halos)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;     
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
void Light::Render(VkCommandBuffer cmd, Camera& camera, vec3 lightPosition, vec3 lightColor, float lightIntensity, float starIntensity)
{
    // Calculation of the model matrix billboard
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();

    vec3 camRight = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 camUp = vec3(view[0][1], view[1][1], view[2][1]);

    mat4 model = glm::translate(mat4(1.0f), lightPosition);
    model[0] = vec4(camRight, 0.0f);
    model[1] = vec4(camUp, 0.0f);

    // Scale according to camera distance
    float dist = glm::length(camera.GetPosition() - lightPosition);
    const float dMin = 0.5f * 1852.0f;
    const float dMax = 9.0f * 1852.0f;
    const float sMin = 2.0f;
    const float sMax = 20.0f;
    float t = glm::clamp((dist - dMin) / (dMax - dMin), 0.0f, 1.0f);
    float scale = sMin + t * (sMax - sMin);
    model = glm::scale(model, vec3(scale));

    sLightPushConstants pc{};
    pc.model = model;
    pc.view = view;
    pc.proj = proj;
    pc.lightColor = lightColor;
    pc.lightIntensity = lightIntensity;
    pc.starIntensity = starIntensity;

    // Draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
    vkCmdPushConstants(cmd, mPipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sLightPushConstants), &pc);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);
    
    vkCmdDraw(cmd, 4, 1, 0, 0);    // TRIANGLE_STRIP, 4 vertices
}
void Light::Render(VkCommandBuffer cmd, Camera& camera, mat4 model, vec3 lightColor, float lightIntensity, float starIntensity)
{
    sLightPushConstants pc{};
    pc.model = model;
    pc.view = camera.GetView();
    pc.proj = camera.GetProjection();
    pc.lightColor = lightColor;
    pc.lightIntensity = lightIntensity;
    pc.starIntensity = starIntensity;

    // Draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);
    vkCmdPushConstants(cmd, mPipeline.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sLightPushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);

    vkCmdDraw(cmd, 4, 1, 0, 0);    // TRIANGLE_STRIP, 4 vertices
}

void Light::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    CreatePipeline(renderPass, newExtent);
}