/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Flag.h"

extern uint32_t g_FramesInFlight;

Flag::Flag(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, int w, int h, float s, const char* path)
{
    mVulkanDevice = vulkanDevice;
    
    mWidth = w;
    mHeight = h;
    mSpacing = s;
    initParticles();
    initBuffers();
    loadTexture(path);

    CreatePipeline(renderPass, extent);
}
Flag::~Flag()
{
    mPipeline.destroy(mVulkanDevice->device);

    for (size_t i = 0; i < 2; i++)
        mVertexBuffers[i].reset();
}

void Flag::initParticles()
{
    vClothPts.resize(mWidth * mHeight);
    for (int y = 0; y < mHeight; ++y)
    {
        for (int x = 0; x < mWidth; ++x)
        {
            sClothPt& p = vClothPts[getIndex(x, y)];
            p.pos = vec3(x * mSpacing, -y * mSpacing, 0.0f);
            p.prevPos = p.pos;
            p.acceleration = vec3(0.0f);
            p.fixed = (x == 0);
        }
    }
}
void Flag::initBuffers()
{
    // Generate the indices to draw each square of the grid into 2 triangles
    mIndices.clear();
    for (uint32_t y = 0; y < mHeight - 1; ++y)
    {
        for (uint32_t x = 0; x < mWidth - 1; ++x)
        {
            uint32_t i0 = getIndex(x, y);
            uint32_t i1 = getIndex(x + 1, y);
            uint32_t i2 = getIndex(x, y + 1);
            uint32_t i3 = getIndex(x + 1, y + 1);

            // Triangle 1
            mIndices.push_back(i0);
            mIndices.push_back(i1);
            mIndices.push_back(i2);

            // Triangle 2
            mIndices.push_back(i2);
            mIndices.push_back(i1);
            mIndices.push_back(i3);
        }
    }
    mIndiceCount = mIndices.size();
    mIndiceBuffer = make_unique<VulkanUBO>(mVulkanDevice, mIndiceCount * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    memcpy(mIndiceBuffer->data, mIndices.data(), mIndiceCount * sizeof(uint32_t));
    mIndiceBuffer->Flush();

    // Generate the data table with positions + UVs
    vector<float> vertices;
    mVertexCount = mWidth * mHeight * 5;
    vertices.reserve(mVertexCount); // 3 for pos + 2 for UV
    for (int y = 0; y < mHeight; ++y)
    {
        for (int x = 0; x < mWidth; ++x)
        {
            sClothPt& p = vClothPts[getIndex(x, y)];
            vertices.push_back(p.pos.x);
            vertices.push_back(p.pos.y);
            vertices.push_back(p.pos.z);
            vertices.push_back(float(x) / (mWidth - 1));  // U
            vertices.push_back(float(y) / (mHeight - 1)); // V
        }
    }

    mVertexBuffers.resize(g_FramesInFlight);
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mVertexBuffers[i] = make_unique<VulkanUBO>(mVulkanDevice, mVertexCount * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}

void Flag::loadTexture(const char* path)
{
    mTexture = make_unique<VulkanTexture>();
    mTexture->CreateFromFile(mVulkanDevice, path);
}
void Flag::CreatePipeline(VkRenderPass renderPass, VkExtent2D extent)
{
    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Flag/flag.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Flag/flag.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input — position (vec3) + UV (vec2) = stride 5 floats
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(float) * 5;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    array<VkVertexInputAttributeDescription, 2> attribs = {};
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };                  // aPos
    attribs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,    sizeof(float) * 3 };  // aTexCoord

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout — binding 0 : UBO, binding 1 : sampler2D
    array<VkDescriptorSetLayoutBinding, 2> layoutBindings = {};

    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline.pipelineLayout);

    // 5. Input Assembly — triangle list
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & Scissor
    VkViewport viewport = { 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
    VkRect2D   scissor = { {0, 0}, extent };

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

    // 7. Rasterizer — double face (drapeau visible des 2 côtés)
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // 10. Color Blending — opaque
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Pipeline final
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
void Flag::CreateDescriptors()
{
    mPipeline.descSet.resize(g_FramesInFlight);
    mPipeline.ubo.resize(g_FramesInFlight);

    // 1. UBO par frame in flight
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sFlagUBO));

    // 2. Descriptor Pool — 1 UBO + 1 sampler, x frames
    array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 * g_FramesInFlight };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * g_FramesInFlight };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
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
void Flag::UpdateDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = mPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(sFlagUBO);

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = mTexture->imageView;
        imageInfo.sampler = mTexture->sampler;

        array<VkWriteDescriptorSet, 2> writes = {};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}
void Flag::updateVerticesBuffer(uint32_t frame)
{
    vector<float> vertices;
    vertices.reserve(vClothPts.size() * 5);
    for (int y = 0; y < mHeight; ++y)
    {
        for (int x = 0; x < mWidth; ++x)
        {
            sClothPt& p = vClothPts[getIndex(x, y)];
            vertices.push_back(p.pos.x);
            vertices.push_back(p.pos.y);
            vertices.push_back(p.pos.z);
            vertices.push_back(float(x) / (mWidth - 1));
            vertices.push_back(float(y) / (mHeight - 1));
        }
    }

    float* gpuData = static_cast<float*>(mVertexBuffers[frame]->data);
    memcpy(gpuData, vertices.data(), vertices.size() * sizeof(float));
    mVertexBuffers[frame]->Flush();
}
vec3 Flag::computeTriangleNormal(int idx0, int idx1, int idx2) const
{
    const vec3& p0 = vClothPts[idx0].pos;
    const vec3& p1 = vClothPts[idx1].pos;
    const vec3& p2 = vClothPts[idx2].pos;
    vec3 edge1 = p1 - p0;
    vec3 edge2 = p2 - p0;
    return normalize(cross(edge1, edge2));
}

void Flag::applyForces(float time, const vec2& wind)
{
    vec3 windForceBase = vec3(wind.x, 0.0f, wind.y);

    // Gravity
    for (auto& p : vClothPts)
        p.acceleration.y -= 9.81f;

    // Wind force according to the normal
    for (size_t i = 0; i < mIndices.size(); i += 3)
    {
        int i0 = mIndices[i];
        int i1 = mIndices[i + 1];
        int i2 = mIndices[i + 2];
        vec3 normal = computeTriangleNormal(i0, i1, i2);
        float intensity = glm::max(dot(normal, windForceBase), 0.0f);
        vec3 force = normal * intensity;
        vClothPts[i0].acceleration += force;
        vClothPts[i1].acceleration += force;
        vClothPts[i2].acceleration += force;
    }
}
void Flag::integrate(float deltaTime)
{
    float damping = 0.99f;
    for (auto& p : vClothPts)
    {
        if (!p.fixed)
        {
            vec3 temp = p.pos;
            p.pos += (p.pos - p.prevPos) * damping + p.acceleration * deltaTime * deltaTime;
            p.prevPos = temp;
            p.acceleration = vec3(0.0f);
        }
    }
}
void Flag::applyConstraint(int idx1, int idx2, float restLength, float rigidity)
{
    vec3 diff = vClothPts[idx2].pos - vClothPts[idx1].pos;
    float dist = glm::length(diff);
    vec3 correction = diff * (1.0f - restLength / dist) * 0.5f * rigidity;
    if (!vClothPts[idx1].fixed)
        vClothPts[idx1].pos += correction;
    if (!vClothPts[idx2].fixed)
        vClothPts[idx2].pos -= correction;
}
void Flag::satisfyConstraints()
{
    float restLength = mSpacing;
    float rigidity = 0.5f;

    for (int y = 0; y < mHeight; ++y)
    {
        for (int x = 0; x < mWidth; ++x)
        {
            int idx = getIndex(x, y);

            // Structural springs (adjacent points)
            if (x < mWidth - 1)
            {
                int right = getIndex(x + 1, y);
                applyConstraint(idx, right, restLength, rigidity);
            }
            if (y < mHeight - 1)
            {
                int down = getIndex(x, y + 1);
                applyConstraint(idx, down, restLength, rigidity);
            }
            // Flexion springs (2-point skip)
            if (x < mWidth - 2)
            {
                int right2 = getIndex(x + 2, y);
                applyConstraint(idx, right2, restLength * 2.0f, rigidity);
            }
            if (y < mHeight - 2)
            {
                int down2 = getIndex(x, y + 2);
                applyConstraint(idx, down2, restLength * 2.0f, rigidity);
            }
            // Diagonal springs (shear constraints)
            if (x < mWidth - 1 && y < mHeight - 1)
            {
                int diag1 = getIndex(x + 1, y + 1);
                applyConstraint(idx, diag1, restLength * sqrt(2.0f), rigidity);
            }
            if (x < mWidth - 1 && y > 0)
            {
                int diag2 = getIndex(x + 1, y - 1);
                applyConstraint(idx, diag2, restLength * sqrt(2.0f), rigidity);
            }
        }
    }
}
void Flag::applyWindAlignmentConstraint(const vec3& windDir, float strength)
{
    for (int y = 0; y < mHeight; ++y)
    {
        int idx_fixed = getIndex(0, y);     // Index of the fixed point on the column x=0 at the same height
        vec3 origin = vClothPts[idx_fixed].pos;

        for (int x = 1; x < mWidth; ++x)    // Start at x=1 (non-fixed particles)
        {
            int idx = getIndex(x, y);
            sClothPt& p = vClothPts[idx];

            // Project onto the wind axis starting from 'origin'
            vec3 delta = p.pos - origin;
            float projLen = glm::dot(delta, windDir);
            vec3 proj = origin + windDir * projLen;

            vec3 corr = proj - p.pos;
            p.pos += corr * strength;
        }
    }
}

void Flag::Update(uint32_t frame, float deltaTime, const vec2& baseWind)
{
    static int frameCount = 0;

    static float time = 0.0f;
    time += deltaTime;

    applyForces(time, 3.0f * baseWind);

    int iterations = (frameCount < 10) ? 20 : 3; // more iterations at the start
    for (int i = 0; i < iterations; ++i)
        satisfyConstraints();

    integrate(deltaTime);

    vec3 windDir = glm::normalize(glm::vec3(baseWind.x, 0.0f, baseWind.y));
    float windForce = glm::length(baseWind);
    float strengthAlignment = windForce * 0.002f;
    applyWindAlignmentConstraint(windDir, strengthAlignment);

    updateVerticesBuffer(frame);
    frameCount++;
}
void Flag::Render(VkCommandBuffer commandBuffer, uint32_t frame, const mat4& model, const mat4& view, const mat4& projection, float exposure)
{
    if (!bVisible) return;

    // Update UBO
    sFlagUBO ubo{};
    ubo.model = model;
    ubo.view = view;
    ubo.proj = projection;
    ubo.time = glfwGetTime();
    ubo.exposure = exposure;
    memcpy(mPipeline.ubo[frame]->data, &ubo, sizeof(sFlagUBO));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipeline);

    // Descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline.pipelineLayout, 0, 1, &mPipeline.descSet[frame], 0, nullptr);

    VkBuffer vertexBuffers[] = { mVertexBuffers[frame]->buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    
    vkCmdBindIndexBuffer(commandBuffer, mIndiceBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    
    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(mIndices.size()), 1, 0, 0, 0);
}

void Flag::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    mPipeline.destroy(mVulkanDevice->device);
    CreatePipeline(renderPass, newExtent);
}