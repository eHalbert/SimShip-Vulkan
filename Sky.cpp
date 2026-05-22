/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Sky.h"

extern uint32_t g_FramesInFlight;

// Extracted table, format (temperature, r, g, b) from 1000K to 10000K, 500K/step
static const struct { float k, r, g, b; } blackbodyLUT[] = {
    {1000, 1.000f, 0.036f, 0.000f},
    {1500, 1.000f, 0.252f, 0.033f},
    {2000, 1.000f, 0.396f, 0.127f},
    {2500, 1.000f, 0.522f, 0.210f},
    {3000, 1.000f, 0.641f, 0.326f},
    {3500, 1.000f, 0.739f, 0.442f},
    {4000, 1.000f, 0.812f, 0.549f},
    {4500, 1.000f, 0.873f, 0.647f},
    {5000, 1.000f, 0.921f, 0.724f},
    {5500, 1.000f, 0.959f, 0.817f},
    {6000, 1.000f, 0.990f, 0.906f},
    {6500, 1.000f, 1.000f, 1.000f},
    {7000, 0.910f, 0.968f, 1.000f},
    {8000, 0.780f, 0.913f, 1.000f},
    {9000, 0.669f, 0.862f, 1.000f},
    {10000,0.568f, 0.816f, 1.000f}
};
vec3 blackbody_to_rgb(float tempK)
{
    // Clamp at the bounds of the table:
    if (tempK <= blackbodyLUT[0].k) return vec3(blackbodyLUT[0].r, blackbodyLUT[0].g, blackbodyLUT[0].b);
    if (tempK >= blackbodyLUT[15].k) return vec3(blackbodyLUT[15].r, blackbodyLUT[15].g, blackbodyLUT[15].b);

    // Linear interpolation
    for (int i = 0; i < 15; ++i)
    {
        if (tempK >= blackbodyLUT[i].k && tempK < blackbodyLUT[i + 1].k)
        {
            float t = (tempK - blackbodyLUT[i].k) / (blackbodyLUT[i + 1].k - blackbodyLUT[i].k);
            vec3 rgb0(blackbodyLUT[i].r, blackbodyLUT[i].g, blackbodyLUT[i].b);
            vec3 rgb1(blackbodyLUT[i + 1].r, blackbodyLUT[i + 1].g, blackbodyLUT[i + 1].b);
            return glm::mix(rgb0, rgb1, t);
        }
    }
    return vec3(1.0, 1.0, 1.0); // fallback
}


Sky::Sky(shared_ptr<VulkanDevice>& vulkanDevice, vec2 pos, int width, int height)
{
    Longitude = pos.x;
    Latitude = pos.y;
    mWidth = width;
    mHeight = height;

    SetNow();

    mVulkanDevice = vulkanDevice;

    // Bruneton 2008
    CreateTextures1();
    CreateVertexBuffer();
    CreatePipeline2();  // offscreen

    // Sakmary 2022
    InitAtmosphere();
    CreateTransmittanceLUTPipeline();
    CreateMultiscatteringLUTPipeline();
    CreateSkyViewLUTPipeline();
    CreatePipeline4();  // offscreen
}
Sky::~Sky()
{
    vkDeviceWaitIdle(mVulkanDevice->device);

    mPipeline1.destroy(mVulkanDevice->device);
    mPipeline2.destroy(mVulkanDevice->device);
    mPipeline3.destroy(mVulkanDevice->device);
    mPipeline4.destroy(mVulkanDevice->device);

    mTransmittanceLUTPipeline.destroy(mVulkanDevice->device);
    mMultiscatteringLUTPipeline.destroy(mVulkanDevice->device);
    mSkyViewLUTPipeline.destroy(mVulkanDevice->device);

    // Orphaned sampler (not in a VulkanTexture or pipeline)
    if (mSkyViewLutSampler != VK_NULL_HANDLE)
        vkDestroySampler(mVulkanDevice->device, mSkyViewLutSampler, nullptr);

    // Vertex buffer of the screen quad (managed manually)
    vkDestroyBuffer(mVulkanDevice->device, mVertexBuffer, nullptr);
    vkFreeMemory(mVulkanDevice->device, mVertexBufferMemory, nullptr);

    // Textures LUT Bruneton (unique_ptr → reset suffit, le destructeur de VulkanTexture fait le reste)
    mTexInscatterLUT.reset();
    mTexTransmittanceLUT.reset();

    // Sky images (arrays of unique_ptr)
    for (int i = 0; i < g_FramesInFlight; i++)
    {
        if (mSkyImage2.size() > i) mSkyImage2[i].reset();
        if (mSkyImage4.size() > i) mSkyImage4[i].reset();
    }

    // Textures LUT Sakmary
    mTransmittanceLUTImage.reset();
    mMultiscatteringLUTImage.reset();
    mSkyViewLUTImage.reset();

    // UBOs standalone
    mAtmoUBO.reset();
    mCommonUBO.reset();

    for (int i = 0; i < g_FramesInFlight; i++)
    {
        if (mCommonUBO3.size() > i) mCommonUBO3[i].reset();
        if (mAtmoUBO3.size() > i)   mAtmoUBO3[i].reset();
        if (mCommonUBO4.size() > i) mCommonUBO4[i].reset();
        if (mAtmoUBO4.size() > i)   mAtmoUBO4[i].reset();
    }
}
void Sky::SetObserver(vec2 pos)
{
    Longitude = pos.x;
    Latitude = pos.y;
}

void Sky::SetNow()
{
    // Get the current time in UTC
    chrono::system_clock::time_point now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);

    // Convert to tm structure in UTC
    tm tm_utc;
    gmtime_s(&tm_utc, &now_c);

    // Create the ln_date structure with UTC values
    struct ln_date date;
    date.years = tm_utc.tm_year + 1900;
    date.months = tm_utc.tm_mon + 1;
    date.days = tm_utc.tm_mday;
    date.hours = tm_utc.tm_hour;
    date.minutes = tm_utc.tm_min;
    date.seconds = 0.0;

    SunHour = date.hours;
    SunMinute = date.minutes;
    tmTimeStored = tm_utc;

    UpdateData(date);
}
sHM Sky::GetNow()
{
    // Get the current time in UTC
    chrono::system_clock::time_point now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);

    // Convert to tm structure in UTC
    tm tm_utc;
    gmtime_s(&tm_utc, &now_c);

    sHM hm{};
    hm.hour = SunHour = tm_utc.tm_hour;
    hm.minute = SunMinute = tm_utc.tm_min;

    return hm;
}
void Sky::SetTime(int hour, int minute)
{
    // Get the current time in UTC
    chrono::system_clock::time_point now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);

    // Convert to tm structure in UTC
    tm tm_utc;
    gmtime_s(&tm_utc, &now_c);

    // Create the ln_date structure with UTC values
    struct ln_date date;
    date.years = tm_utc.tm_year + 1900;
    date.months = tm_utc.tm_mon + 1;
    date.days = tm_utc.tm_mday;
    date.hours = hour;
    date.minutes = minute;
    date.seconds = 0.0;

    SunHour = hour;
    SunMinute = minute;
    tmTimeStored = tm_utc;

    UpdateData(date);
}
sHM Sky::GetTime()
{
    // Get the current time in UTC
    chrono::system_clock::time_point now = chrono::system_clock::now();
    time_t now_c = chrono::system_clock::to_time_t(now);

    // Convert to tm structure in UTC
    tm tm_utc;
    gmtime_s(&tm_utc, &now_c);

    // Retrieve timezone offset in seconds
    long timezone_seconds = 0;
    int daylight_savings = 0;
    
    _tzset(); // initialize the internal timezone variable
    _get_timezone(&timezone_seconds); // get offset in seconds
    _get_daylight(&daylight_savings);  // Check if the timezone has a DST rule

    // Check if DST is currently active via tm_isdst
    tm tm_local;
    localtime_s(&tm_local, &now_c);

    int dst_offset = (daylight_savings && tm_local.tm_isdst > 0) ? 3600 : 0;

    // Conversion to rounded hours, subtracting the active DST
    int timezone_hours = static_cast<int>((-timezone_seconds + dst_offset) / 3600);

    int hour = tm_utc.tm_hour - tmTimeStored.tm_hour;
    int minute = tm_utc.tm_min - tmTimeStored.tm_min;
    if (SunHour + hour > 23)
        hour = 0;
    if (SunMinute + minute > 59)
    {
        if (SunHour + hour < 23)
        {
            SunMinute = 0;
            SunHour++;
        }
        else
        {
            SunMinute = 59;
            SunHour = 23;
        }
    }
    sHM hm{};
    hm.hour = glm::clamp(SunHour + hour, 0, 23);
    hm.minute = glm::clamp(SunMinute + minute, 0, 59);
    hm.timezoneOffsetHours = timezone_hours;

    return hm;
}
void Sky::UpdateData(ln_date& date)
{
#pragma region Sun and Moon
    // Calculate the Julian day
    double JD = ln_get_julian_day(&date);

    struct ln_lnlat_posn observer;
    observer.lng = Longitude;
    observer.lat = Latitude;

    // Sun
    struct ln_equ_posn sun;
    ln_get_solar_equ_coords(JD, &sun);

    struct ln_hrz_posn position;
    ln_get_hrz_from_equ(&sun, &observer, JD, &position);

    SunElevation = 90.0f - position.alt;
    SunAzimut = position.az + 180.0f;

    while (SunElevation < 0.0f)     SunElevation += 360.0f;
    while (SunElevation > 360.0f)   SunElevation -= 360.0f;
    while (SunAzimut < 0.0f)        SunAzimut += 360.0f;
    while (SunAzimut > 360.0f)      SunAzimut -= 360.0f;

    float radPhi = glm::radians(SunAzimut + 270.0f);
    float radTheta = glm::radians(SunElevation);
    SunDirection = vec3(sin(radTheta) * cos(radPhi), cos(radTheta), sin(radTheta) * sin(radPhi));
    SunPosition = SunDirection * SunDistance;

    // Moon
    struct ln_equ_posn lunar;
    ln_get_lunar_equ_coords(JD, &lunar);
    ln_get_hrz_from_equ(&lunar, &observer, JD, &position);

    MoonElevation = 90.0f - position.alt;
    MoonAzimut = position.az + 180.0f;

    while (MoonElevation < 0.0f)     MoonElevation += 360.0f;
    while (MoonElevation > 360.0f)   MoonElevation -= 360.0f;
    while (MoonAzimut < 0.0f)        MoonAzimut += 360.0f;
    while (MoonAzimut > 360.0f)      MoonAzimut -= 360.0f;

    radPhi = glm::radians(MoonAzimut + 270.0f);
    radTheta = glm::radians(MoonElevation);
    MoonDirection = vec3(sin(radTheta) * cos(radPhi), cos(radTheta), sin(radTheta) * sin(radPhi));

    float phase = ln_get_lunar_phase(JD);
    double disk_illum = ln_get_lunar_disk(JD);
    double disk_tomorrow = ln_get_lunar_disk(JD + 1.0);
    bool bAscendant = (disk_tomorrow > disk_illum);
    // Ascending : -1 → 0, Descending : 0 → 1
    MoonPhase = bAscendant ? -cos(glm::radians(phase)) : cos(glm::radians(180.0f - phase));
#pragma endregion

#pragma region Exposure
    const float minExposure = 0.02f;
    const float maxExposure = 1.0f;
    const float maxNightAngle = 108.0f;      // beginning of the "dark night"
    const float plateauStart = 65.0f;        // start of decay (adjusts according to desired effect)

    // Clamp the angle
    float clampedAngle = SunElevation;
    if (clampedAngle < 0.0f) clampedAngle = 0.0f;
    if (clampedAngle > maxNightAngle) clampedAngle = maxNightAngle;

    // Plateau at 1.0 on the sunny edge
    if (clampedAngle <= plateauStart)
    {
        Exposure = maxExposure;
        return;
    }

    // Smoothstep transition (Hermite/Cubic)
    // x = 0 when we start to decrease; x = 1 when we are in the night
    float t = (clampedAngle - plateauStart) / (maxNightAngle - plateauStart);
    // Smoothstep : 3t² - 2t³
    float smooth = 3.0f * t * t - 2.0f * t * t * t;
    Exposure = (1.0f - smooth) * maxExposure + smooth * minExposure;
#pragma endregion

#pragma region Color of the sun
    // Empirical model: visible sun temperature as a function of elevation, 2000K (red horizon) to 6500K (white zenith)
    float t_min = 2000.0f, t_max = 6500.0f;

    float tt = glm::clamp(glm::radians(SunElevation) / 1.5708f, 0.0f, 1.0f);
    tt = 1.0f - tt;  // t=1 (zenith), t=0 (horizon)
    float tempK = glm::mix(t_min, t_max, tt);

    vec3 temp = blackbody_to_rgb(tempK);
    SunEmissive = temp;
    SunDiffuse = temp;
    SunSpecular = temp;
#pragma endregion

#pragma region Pct of sun elevation
    time_t now_c;
    ln_get_timet_from_julian(JD, &now_c);
    mAlmanac = Nova::getSunMoonAlmanac(now_c, Latitude, Longitude);

    double sunRise = static_cast<double>(mAlmanac.sunRise % 86400) / 3600.0;
    double sunTransit = static_cast<double>(mAlmanac.sunTransit % 86400) / 3600.0;
    double sunSet = static_cast<double>(mAlmanac.sunSet % 86400) / 3600.0;
    double currentHour = static_cast<double>(now_c % 86400) / 3600.0f;

    double pct = 0.0;

    // Between sunrise and transit: 0 → 1
    if (currentHour >= sunRise && currentHour <= sunTransit)
        pct = (currentHour - sunRise) / (sunTransit - sunRise);
    // Between transit and sunset: 1 → 0  
    else if (currentHour > sunTransit && currentHour <= sunSet)
        pct = 1.0 - (currentHour - sunTransit) / (sunSet - sunTransit);
    // Before sunrise or after sunset: 0

    SunPctElevation = std::clamp(static_cast<float>(pct), 0.0f, 1.0f);
#pragma endregion
}

void Sky::CreateVertexBuffer()
{
    float vertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f
    };
    size_t size = sizeof(vertices);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(mVulkanDevice->device, &bufferInfo, nullptr, &mVertexBuffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(mVulkanDevice->device, mVertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(mVulkanDevice->physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &mVertexBufferMemory);
    vkBindBufferMemory(mVulkanDevice->device, mVertexBuffer, mVertexBufferMemory, 0);

    void* data;
    vkMapMemory(mVulkanDevice->device, mVertexBufferMemory, 0, size, 0, &data);
    memcpy(data, vertices, size);
    vkUnmapMemory(mVulkanDevice->device, mVertexBufferMemory);
}

// BRUNETON 2008

void Sky::CreateTextures1()
{
    int res = 64;
    int nr = res / 2;    // 32
    int nv = res * 2;    // 128  
    int nb = res / 2;    // 32
    int na = 8;
    FILE* f = fopen("Resources/Shaders/Sky2008/inscatter.raw", "rb");
    float* data0 = new float[nr * nv * nb * na * 4];
    fread(data0, sizeof(float), nr * nv * nb * na * 4, f);
    fclose(f);
    mTexInscatterLUT = make_unique<VulkanTexture>();
    mTexInscatterLUT->CreateFromData(mVulkanDevice, na * nb, nv, nr, VK_FORMAT_R32G32B32A32_SFLOAT, false, data0);
    delete[] data0;

    float* data1 = new float[256 * 64 * 3];
    f = fopen("Resources/Shaders/Sky2008/transmittance.raw", "rb");
    fread(data1, 1, 256 * 64 * 3 * sizeof(float), f);
    fclose(f);
    float* data2 = ConvertRGBtoRGBA(data1, 256, 64);
    mTexTransmittanceLUT = make_unique<VulkanTexture>();
    mTexTransmittanceLUT->Create(mVulkanDevice, 256, 64, 1, VK_FORMAT_R32G32B32A32_SFLOAT, true);
    memcpy(mTexTransmittanceLUT->cpuData, data2, 256 * 64 * 4 * sizeof(float));
    mTexTransmittanceLUT->CopyStagingToGPU();
    delete[] data1;
    delete[] data2;
}

// Render to screen
void Sky::CreatePipeline1(VkRenderPass renderPassScene, VkExtent2D extent)
{
    // Previous cleanup
	mPipeline1.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Sky2008/sky_bruneton.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Sky2008/sky_bruneton.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = 4 * sizeof(float);  // 2 pos + 2 texcoord = 16 bytes
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2] = {};
    // pos
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;    
    // texcoord
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = 2 * sizeof(float);   

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    // 3. DescriptorSetLayout (1 UBO + 2 textures)
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        VkDescriptorSetLayoutBinding { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },      // UBO - VERTEX
        VkDescriptorSetLayoutBinding { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },    // Sampler Inscatter - FRAGMENT
        VkDescriptorSetLayoutBinding { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }     // Sampler Transmittance - FRAGMENT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline1.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;  // 1 descriptor set
    pipelineLayoutInfo.pSetLayouts = &mPipeline1.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline1.pipelineLayout);

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
    rasterizer.depthClampEnable = VK_TRUE;  // ← IMPORTANT for sky
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
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples;  // ← MSAA 8x like g_RenderPassScene

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
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE };  // ← For toggling depth test
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 1;
    dynamicState.pDynamicStates = dynamicStates;

	// 12. pipeline
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
    pipelineInfo.layout = mPipeline1.pipelineLayout;
    pipelineInfo.renderPass = renderPassScene;  // ← g_RenderPassScene
    pipelineInfo.subpass = 0;  // First subpass
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline1.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateDescriptors1();
}
void Sky::CreateDescriptors1()
{   
    mPipeline1.descSet.resize(g_FramesInFlight);
    mPipeline1.ubo.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
        mPipeline1.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sSkyUBO));
    
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight},           // 1 UBO
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * g_FramesInFlight}    // 2 textures
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline1.descPool);

    // Global sets (1 per frame)
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline1.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline1.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline1.descSet.data());

    UpdateDescriptors1();
}
void Sky::UpdateDescriptors1()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = mPipeline1.ubo[i]->buffer;
        uboInfo.offset = 0;
        uboInfo.range = mPipeline1.ubo[i]->GetSize();

        VkDescriptorImageInfo inscatterInfo{};
        inscatterInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        inscatterInfo.imageView = mTexInscatterLUT->imageView;
        inscatterInfo.sampler = mTexInscatterLUT->sampler;

        VkDescriptorImageInfo transmittanceInfo{};
        transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        transmittanceInfo.imageView = mTexTransmittanceLUT->imageView;
        transmittanceInfo.sampler = mTexTransmittanceLUT->sampler;

        VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline1.descSet[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline1.descSet[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &inscatterInfo, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline1.descSet[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &transmittanceInfo, nullptr, nullptr}
        };

        vkUpdateDescriptorSets(mVulkanDevice->device, 3, writes, 0, nullptr);
    }
}
void Sky::Render1(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera)
{
    if(!bVisible)
		return;

	// Update UBO
    vec3 cameraPos = camera.GetPosition();
	mat4 proj = camera.GetProjection();
    mat4 rotY = glm::rotate(mat4(1.0f), glm::radians((float)camera.GetNorthAngleDEG()), vec3(0.0f, 1.0f, 0.0f));
    mat4 rotX = glm::rotate(mat4(1.0f), glm::radians((float)- camera.GetAttitudeDEG()), vec3(1.0f, 0.0f, 0.0f));
    mat4 view = rotX * rotY * glm::translate(mat4(1.0f), -cameraPos);

    sSkyUBO* uboData = static_cast<sSkyUBO*>(mPipeline1.ubo[currentFrame]->data);
    uboData->invProj = glm::inverse(proj);      // NDC → eye space
    uboData->invView = glm::inverse(view);      // eye → world space (Z↑)
    uboData->camera = cameraPos;                // World position
    uboData->mieG = GetMieG();
    uboData->sunDir = SunDirection;                
    uboData->sunIntensity = SunIntensity;

    // Rendering
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline1.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline1.pipelineLayout, 0, 1, &mPipeline1.descSet[currentFrame], 0, nullptr);
    vkCmdSetDepthTestEnable(cmd, VK_FALSE);

    VkBuffer vertexBuffers[] = { mVertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdDraw(cmd, 6, 1, 0, 0);

    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
}

// Render offscreen
void Sky::CreatePipeline2()
{
    // Previous cleanup
    mPipeline2.destroy(mVulkanDevice->device);

    // 1. Shader
    auto compCode = CompileShaderRuntime("Resources/Shaders/Sky2008/sky_bruneton.comp");
    VkShaderModule compModule = CreateShaderModule(mVulkanDevice->device, compCode);

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = compModule;
    shaderStage.pName = "main";

    // 3. DescriptorSetLayout (UBO + 2 textures input + 1 image output)
    array<VkDescriptorSetLayoutBinding, 4> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },      // sSkyUBO
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },      // Sampler Inscatter
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },      // Sampler Transmittance
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }       // Image output mSkyImage2
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline2.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline2.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline2.pipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = mPipeline2.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline2.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, compModule, nullptr);
    
    CreateDescriptors2();
}
void Sky::CreateDescriptors2()
{
    mPipeline2.descSet.resize(g_FramesInFlight);
    mPipeline2.ubo.resize(g_FramesInFlight);
    mSkyImage2.resize(g_FramesInFlight);

    // Creation of output image mSkyImage4 if not yet done
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        if (!mSkyImage2[i].get()) 
        {
            mSkyImage2[i] = make_unique<VulkanTexture>(mVulkanDevice, mWidth, mHeight, 1, 
                1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            mSkyImage2[i]->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }

        mPipeline2.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sSkyUBO));
    }

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 * g_FramesInFlight}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline2.descPool);

    // Global sets (1 per frame)
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline2.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline2.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline2.descSet.data());

    UpdateDescriptors2();
}
void Sky::UpdateDescriptors2()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = mPipeline2.ubo[i]->buffer;
        uboInfo.offset = 0;
        uboInfo.range = mPipeline2.ubo[i]->GetSize();

        VkDescriptorImageInfo inscatterInfo{};
        inscatterInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        inscatterInfo.imageView = mTexInscatterLUT->imageView;
        inscatterInfo.sampler = mTexInscatterLUT->sampler;

        VkDescriptorImageInfo transmittanceInfo{};
        transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        transmittanceInfo.imageView = mTexTransmittanceLUT->imageView;
        transmittanceInfo.sampler = mTexTransmittanceLUT->sampler;

        VkDescriptorImageInfo skyOutputInfo{};
        skyOutputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        skyOutputInfo.imageView = mSkyImage2[i]->imageView;
        skyOutputInfo.sampler = VK_NULL_HANDLE;  // No sampler for storage image

        VkWriteDescriptorSet writes[] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline2.descSet[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline2.descSet[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &inscatterInfo, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline2.descSet[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &transmittanceInfo, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, mPipeline2.descSet[i], 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &skyOutputInfo, nullptr, nullptr}
        };

        vkUpdateDescriptorSets(mVulkanDevice->device, 4, writes, 0, nullptr);
    }
}
void Sky::ComputeSkyImageBruneton(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera)
{
    if (!bVisible) return;

    // Update UBO (identical)
	vec3 cameraPos = vec3(0.0); // RTE uses camera at origin for precomputation, with only rotation (view dir) changing
    mat4 proj = camera.GetProjection();
    mat4 rotY = glm::rotate(mat4(1.0f), glm::radians((float)camera.GetNorthAngleDEG()), vec3(0.0f, 1.0f, 0.0f));
    mat4 rotX = glm::rotate(mat4(1.0f), glm::radians((float)-camera.GetAttitudeDEG()), vec3(1.0f, 0.0f, 0.0f));
    mat4 view = rotX * rotY;

    sSkyUBO* uboData = static_cast<sSkyUBO*>(mPipeline2.ubo[currentFrame]->data);
    uboData->invProj = glm::inverse(proj);
    uboData->invView = glm::inverse(view);
    uboData->camera = cameraPos;
    uboData->mieG = GetMieG();
    uboData->sunDir = SunDirection;
    uboData->sunIntensity = SunIntensity;
    mPipeline2.ubo[currentFrame]->Flush();

    // Dispatch compute (workgroups 8x8 = 64 threads)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline2.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline2.pipelineLayout, 0, 1, &mPipeline2.descSet[currentFrame], 0, nullptr);

    // Dispatch (local_size=16x16 → divide by 16)
    uint32_t groupsX = (mWidth + 15) / 16;
    uint32_t groupsY = (mHeight + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}

// SAKMARY 2022

void Sky::InitAtmosphere()
{
    // Sun
    mAtmoParams.solar_irradiance = { 1.474000f, 1.850400f, 1.911980f };
    mAtmoParams.sun_angular_radius = 0.004675f;

    // Earth
    mAtmoParams.bottom_radius = 6360.0f;
    mAtmoParams.top_radius = 6460.0f;
    mAtmoParams.ground_albedo = { 0.0f, 0.0f, 0.0f };

    // Raleigh scattering
    mAtmoParams.rayleigh_density.layers[0] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    mAtmoParams.rayleigh_density.layers[1] = { 0.0f, 1.0f, -1.0f / 8.0f, 0.0f, 0.0f };
    mAtmoParams.rayleigh_scattering = { 0.005802f, 0.013558f, 0.033100f };		// 1/km

    // Mie scattering
    mAtmoParams.mie_density.layers[0] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    mAtmoParams.mie_density.layers[1] = { 0.0f, 1.0f, -1.0f / 1.2f, 0.0f, 0.0f };
    mAtmoParams.mie_scattering = { 0.003996f, 0.003996f, 0.003996f };			// 1/km
    mAtmoParams.mie_extinction = { 0.004440f, 0.004440f, 0.004440f };			// 1/km
    mAtmoParams.mie_phase_function_g = 0.8f;

    // Ozone absorption
    mAtmoParams.absorption_density.layers[0] = { 25.0f, 0.0f, 0.0f, 1.0f / 15.0f, -2.0f / 3.0f };
    mAtmoParams.absorption_density.layers[1] = { 0.0f, 0.0f, 0.0f, -1.0f / 15.0f, 8.0f / 3.0f };
    mAtmoParams.absorption_extinction = { 0.000650f, 0.001881f, 0.000085f };	// 1/km
}
void Sky::UpdateAtmosphereUBO(sAtmosphereUBO* buffer, Camera& camera)
{
    /*
    Values shown here are the result of integration over wavelength power spectrum integrated with particular function.
    Refer to https://github.com/ebruneton/precomputed_atmospheric_scattering for details. All units in kilometers
    */
    auto MaxZero3 = [](vec3 a) {
        vec3 r;
        r.x = a.x > 0.0f ? a.x : 0.0f;
        r.y = a.y > 0.0f ? a.y : 0.0f;
        r.z = a.z > 0.0f ? a.z : 0.0f;
        return r;
        };
    auto sub3 = [](vec3 a, vec3 b) {
        vec3 r;
        r.x = a.x - b.x;
        r.y = a.y - b.y;
        r.z = a.z - b.z;
        return r;
        };

    buffer->solar_irradiance = mAtmoParams.solar_irradiance;
    buffer->sun_angular_radius = mAtmoParams.sun_angular_radius;
    
    buffer->absorption_extinction = mAtmoParams.absorption_extinction;
   
    buffer->rayleigh_scattering = mAtmoParams.rayleigh_scattering;
    buffer->mie_phase_function_g = mAtmoParams.mie_phase_function_g;
    
    buffer->mie_scattering = mAtmoParams.mie_scattering;
    buffer->bottom_radius = mAtmoParams.bottom_radius;
    
    buffer->mie_extinction = mAtmoParams.mie_extinction;
    buffer->top_radius = mAtmoParams.top_radius;
    
    buffer->mie_absorption = MaxZero3(sub3(mAtmoParams.mie_extinction, mAtmoParams.mie_scattering));
    buffer->ground_albedo = mAtmoParams.ground_albedo;

    memcpy(buffer->rayleigh_density, &mAtmoParams.rayleigh_density, sizeof(mAtmoParams.rayleigh_density));
    memcpy(buffer->mie_density, &mAtmoParams.mie_density, sizeof(mAtmoParams.mie_density));
    memcpy(buffer->absorption_density, &mAtmoParams.absorption_density, sizeof(mAtmoParams.absorption_density));

    buffer->TransmittanceTexDimensions = vec2(256, 64);
    buffer->MultiscatteringTexDimensions = vec2(32, 32);
    buffer->SkyViewTexDimensions = vec2(192, 128);

    buffer->sunDirection = glm::normalize(SunDirection);
    vec3 camPos = camera.GetPosition() * 0.001f;
    buffer->cameraPosition = vec3(camPos.x, camPos.z, camPos.y);
    buffer->moonDirection = glm::normalize(MoonDirection);
    buffer->moonPhase = MoonPhase;        // Phase [-1,1] 
    buffer->moonIntensity = 1.0;
}

void Sky::CreateTransmittanceLUTPipeline()
{
    // Previous cleanup
    if (mTransmittanceLUTPipeline.pipelineLayout) vkDestroyPipelineLayout(mVulkanDevice->device, mTransmittanceLUTPipeline.pipelineLayout, nullptr);
    if (mTransmittanceLUTPipeline.descSetLayout)  vkDestroyDescriptorSetLayout(mVulkanDevice->device, mTransmittanceLUTPipeline.descSetLayout, nullptr);
    if (mTransmittanceLUTPipeline.descPool)       vkDestroyDescriptorPool(mVulkanDevice->device, mTransmittanceLUTPipeline.descPool, nullptr);
    if (mTransmittanceLUTPipeline.pipeline)       vkDestroyPipeline(mVulkanDevice->device, mTransmittanceLUTPipeline.pipeline, nullptr);

    // 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Sky2022/transmittanceLUT.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (UBO0, UBO1, storage image)
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // sCommonUBO 
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // sAtmosphereUBO
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Image output transmittanceLUT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mTransmittanceLUTPipeline.descSetLayout);
    
    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mTransmittanceLUTPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mTransmittanceLUTPipeline.pipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mTransmittanceLUTPipeline.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mTransmittanceLUTPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);

    CreateTransmittanceLUTDescriptors();
}
void Sky::CreateTransmittanceLUTDescriptors()
{
    mTransmittanceLUTImage = make_unique<VulkanTexture>(mVulkanDevice, 256, 64, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mTransmittanceLUTImage->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    mCommonUBO = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sCommonUBO));
    mAtmoUBO = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sAtmosphereUBO));

    // Descriptor Pool with the 2 types
    array<VkDescriptorPoolSize, 2> poolSizes = { {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },   // 2 UBOs
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }     // 1 storage image
    } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mTransmittanceLUTPipeline.descPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mTransmittanceLUTPipeline.descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mTransmittanceLUTPipeline.descSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mTransmittanceLUTPipeline.descSet);

    UpdateTransmittanceLUTDescriptors();
}
void Sky::UpdateTransmittanceLUTDescriptors()
{
    array<VkWriteDescriptorSet, 3> descriptorWrites{};

    // Binding 0 : First UBO (sCommonUBO)
    VkDescriptorBufferInfo uboInfo0{};
    uboInfo0.buffer = mCommonUBO->buffer;
    uboInfo0.offset = 0;
    uboInfo0.range = sizeof(sCommonUBO);

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = mTransmittanceLUTPipeline.descSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].pBufferInfo = &uboInfo0;

    // Binding 1 : Second UBO (AtmosphereParametersUBO)
    VkDescriptorBufferInfo uboInfo1{};
    uboInfo1.buffer = mAtmoUBO->buffer;
    uboInfo1.offset = 0;
    uboInfo1.range = sizeof(sAtmosphereUBO); 

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = mTransmittanceLUTPipeline.descSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[1].pBufferInfo = &uboInfo1;

    // Binding 2 : Storage Image (mTransmittanceLUTImage)
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = mTransmittanceLUTImage->imageView;
    imageInfo.sampler = VK_NULL_HANDLE;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = mTransmittanceLUTPipeline.descSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[2].pImageInfo = &imageInfo;

    // Update descriptors in one go
    vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Sky::CreateMultiscatteringLUTPipeline()
{
    // Previous cleanup
    if (mMultiscatteringLUTPipeline.pipelineLayout) vkDestroyPipelineLayout(mVulkanDevice->device, mMultiscatteringLUTPipeline.pipelineLayout, nullptr);
    if (mMultiscatteringLUTPipeline.descSetLayout)  vkDestroyDescriptorSetLayout(mVulkanDevice->device, mMultiscatteringLUTPipeline.descSetLayout, nullptr);
    if (mMultiscatteringLUTPipeline.descPool)       vkDestroyDescriptorPool(mVulkanDevice->device, mMultiscatteringLUTPipeline.descPool, nullptr);
    if (mMultiscatteringLUTPipeline.pipeline)       vkDestroyPipeline(mVulkanDevice->device, mMultiscatteringLUTPipeline.pipeline, nullptr);

    // 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Sky2022/multiscatteringLUT.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (UBO0, UBO1, 2 storage images)
    array<VkDescriptorSetLayoutBinding, 4> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // sCommonUBO 
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // sAtmosphereUBO 
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Image transmittanceLUT
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Image output multiscatteringLUT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mMultiscatteringLUTPipeline.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mMultiscatteringLUTPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mMultiscatteringLUTPipeline.pipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mMultiscatteringLUTPipeline.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mMultiscatteringLUTPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);

    CreateMultiscatteringLUTDescriptors();
}
void Sky::CreateMultiscatteringLUTDescriptors()
{
    mMultiscatteringLUTImage = make_unique<VulkanTexture>(mVulkanDevice, 32, 32, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mMultiscatteringLUTImage->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // Descriptor Pool with the 2 types
    array<VkDescriptorPoolSize, 2> poolSizes = { {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },   // 2 UBOs
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 }     // 2 storage image
    } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mMultiscatteringLUTPipeline.descPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mMultiscatteringLUTPipeline.descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mMultiscatteringLUTPipeline.descSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mMultiscatteringLUTPipeline.descSet);

    UpdateMultiscatteringLUTDescriptors();
}
void Sky::UpdateMultiscatteringLUTDescriptors()
{
    array<VkWriteDescriptorSet, 4> descriptorWrites{};

    // Binding 0 : First UBO (sCommonUBO)
    VkDescriptorBufferInfo uboInfo0{};
    uboInfo0.buffer = mCommonUBO->buffer;
    uboInfo0.offset = 0;
    uboInfo0.range = sizeof(sCommonUBO);

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = mMultiscatteringLUTPipeline.descSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].pBufferInfo = &uboInfo0;

    // Binding 1 : Second UBO (AtmosphereParametersUBO)
    VkDescriptorBufferInfo uboInfo1{};
    uboInfo1.buffer = mAtmoUBO->buffer;
    uboInfo1.offset = 0;
    uboInfo1.range = sizeof(sAtmosphereUBO);

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = mMultiscatteringLUTPipeline.descSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[1].pBufferInfo = &uboInfo1;

    // Binding 2 : Storage Image (mTransmittanceLUTImage)
    VkDescriptorImageInfo transmittanceInfo{};
    transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    transmittanceInfo.imageView = mTransmittanceLUTImage->imageView;
    transmittanceInfo.sampler = VK_NULL_HANDLE;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = mMultiscatteringLUTPipeline.descSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[2].pImageInfo = &transmittanceInfo;

    // Binding 3 : Storage Image (mMultiscatteringLUTImage)
    VkDescriptorImageInfo imageInmultiscatteringInfofo{};
    imageInmultiscatteringInfofo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInmultiscatteringInfofo.imageView = mMultiscatteringLUTImage->imageView;
    imageInmultiscatteringInfofo.sampler = VK_NULL_HANDLE;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = mMultiscatteringLUTPipeline.descSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[3].pImageInfo = &imageInmultiscatteringInfofo;

    // Update descriptors in one go
    vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Sky::CreateSkyViewLUTPipeline()
{
    // Previous cleanup
    if (mSkyViewLUTPipeline.pipelineLayout) vkDestroyPipelineLayout(mVulkanDevice->device, mSkyViewLUTPipeline.pipelineLayout, nullptr);
    if (mSkyViewLUTPipeline.descSetLayout)  vkDestroyDescriptorSetLayout(mVulkanDevice->device, mSkyViewLUTPipeline.descSetLayout, nullptr);
    if (mSkyViewLUTPipeline.descPool)       vkDestroyDescriptorPool(mVulkanDevice->device, mSkyViewLUTPipeline.descPool, nullptr);
    if (mSkyViewLUTPipeline.pipeline)       vkDestroyPipeline(mVulkanDevice->device, mSkyViewLUTPipeline.pipeline, nullptr);
    
    // 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Sky2022/skyviewLUT.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (UBO0, UBO1, 3 images)
    array<VkDescriptorSetLayoutBinding, 5> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // sCommonUBO 
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },  // sAtmosphereUBO
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Image transmittanceLUT
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Image multiscatteringLUT
		{ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Image output skyviewLUT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mSkyViewLUTPipeline.descSetLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mSkyViewLUTPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mSkyViewLUTPipeline.pipelineLayout);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mSkyViewLUTPipeline.pipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mSkyViewLUTPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);

    CreateSkyViewLUTDescriptors();
}
void Sky::CreateSkyViewLUTDescriptors()
{
    mSkyViewLUTImage = make_unique<VulkanTexture>(mVulkanDevice, 192, 128, 1,
        1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    mSkyViewLUTImage->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // Descriptor Pool with the 2 types
    array<VkDescriptorPoolSize, 2> poolSizes = { {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },   // 2 UBOs
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 }     // 3 storage image
    } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mSkyViewLUTPipeline.descPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mSkyViewLUTPipeline.descPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mSkyViewLUTPipeline.descSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mSkyViewLUTPipeline.descSet);

    UpdateSkyViewLUTDescriptors();
}
void Sky::UpdateSkyViewLUTDescriptors()
{
    array<VkWriteDescriptorSet, 5> descriptorWrites{};

    // Binding 0 : First UBO (sCommonUBO)
    VkDescriptorBufferInfo uboInfo0{};
    uboInfo0.buffer = mCommonUBO->buffer;
    uboInfo0.offset = 0;
    uboInfo0.range = sizeof(sCommonUBO);

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = mSkyViewLUTPipeline.descSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].pBufferInfo = &uboInfo0;

    // Binding 1 : Second UBO (AtmosphereParametersUBO)
    VkDescriptorBufferInfo uboInfo1{};
    uboInfo1.buffer = mAtmoUBO->buffer;
    uboInfo1.offset = 0;
    uboInfo1.range = sizeof(sAtmosphereUBO);

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = mSkyViewLUTPipeline.descSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[1].pBufferInfo = &uboInfo1;

    // Binding 2 : Storage Image (mTransmittanceLUTImage)
    VkDescriptorImageInfo transmittanceInfo{};
    transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    transmittanceInfo.imageView = mTransmittanceLUTImage->imageView;
    transmittanceInfo.sampler = VK_NULL_HANDLE;

    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = mSkyViewLUTPipeline.descSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[2].pImageInfo = &transmittanceInfo;

    // Binding 3 : Storage Image (mMultiscatteringLUTImage)
    VkDescriptorImageInfo multiscatteringInfo{};
    multiscatteringInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    multiscatteringInfo.imageView = mMultiscatteringLUTImage->imageView;
    multiscatteringInfo.sampler = VK_NULL_HANDLE;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = mSkyViewLUTPipeline.descSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[3].pImageInfo = &multiscatteringInfo;

    // Binding 4 : Storage Image (mSkyViewLUTImage)
    VkDescriptorImageInfo skyviewInfo{};
    skyviewInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    skyviewInfo.imageView = mSkyViewLUTImage->imageView;
    skyviewInfo.sampler = VK_NULL_HANDLE;

    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = mSkyViewLUTPipeline.descSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorWrites[4].pImageInfo = &skyviewInfo;

    // Update descriptors in one go
    vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void Sky::ComputeLUTsSakmary2022(Camera &camera)
{
    static auto startTime = chrono::high_resolution_clock::now();

    auto currentTime = chrono::high_resolution_clock::now();
    float time = chrono::duration<float, chrono::seconds::period> (currentTime - startTime).count();

    sCommonUBO* commonUbo = static_cast<sCommonUBO*>(mCommonUBO->data);
    commonUbo->model = mat4(1.0f);
    commonUbo->proj = camera.GetProjection();
    commonUbo->view = camera.GetViewRTE();      // RTE: Center the sphere around the camera
    commonUbo->time = time;

    sAtmosphereUBO* atmoUbo = static_cast<sAtmosphereUBO*>(mAtmoUBO->data);
    UpdateAtmosphereUBO(atmoUbo, camera);

    VkMemoryBarrier barrierFull = {};
    barrierFull.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrierFull.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrierFull.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkCommandBuffer cmd = mVulkanDevice->BeginSingleTimeCommands();

#pragma region PASSE_1_Transmittance
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mTransmittanceLUTPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mTransmittanceLUTPipeline.pipelineLayout, 0, 1, &mTransmittanceLUTPipeline.descSet, 0, nullptr);
    vkCmdDispatch(cmd, 256 / 8, 64 / 4, 1);
#pragma endregion

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

#pragma region PASSE_2_Multiplescattering (lit transmittance)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mMultiscatteringLUTPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mMultiscatteringLUTPipeline.pipelineLayout, 0, 1, &mMultiscatteringLUTPipeline.descSet, 0, nullptr);
    vkCmdDispatch(cmd, 32, 32, 1);
#pragma endregion

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

#pragma region PASSE_3_Skyview (lit transmittance + multiplescattering)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mSkyViewLUTPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mSkyViewLUTPipeline.pipelineLayout, 0, 1, &mSkyViewLUTPipeline.descSet, 0, nullptr);
    vkCmdDispatch(cmd, 192 / 16, 128 / 16, 1);
#pragma endregion

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, 0, 0, 0);

    mVulkanDevice->EndSingleTimeCommands(cmd);
}

// Render to screen
void Sky::CreatePipeline3(VkRenderPass renderPassScene, VkExtent2D extent)
{
    // Previous cleanup
    mPipeline3.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Sky2022/sky_sakmary.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Sky2022/sky_sakmary.frag");
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

    // 3. DescriptorSetLayout (2 UBO + 1 texture)
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },         // sCommonUBO 
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },         // sAtmosphereUBO
		{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // Image skyViewLUT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline3.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline3.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline3.pipelineLayout);

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
    rasterizer.depthClampEnable = VK_TRUE;  // ← IMPORTANT for sky
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
    multisampling.rasterizationSamples = mVulkanDevice->msaaSamples; // ← MSAA 8x like g_RenderPassScene

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

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 1;
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
    pipelineInfo.layout = mPipeline3.pipelineLayout;
    pipelineInfo.renderPass = renderPassScene;  // ← g_RenderPassScene
    pipelineInfo.subpass = 0;// First subpass
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline3.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateDescriptors3();
}
void Sky::CreateDescriptors3()
{
    mPipeline3.descSet.resize(g_FramesInFlight);
    mPipeline3.ubo.resize(g_FramesInFlight); 
	mCommonUBO3.resize(g_FramesInFlight);
    mAtmoUBO3.resize(g_FramesInFlight);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;  // Pas de mipmaps pour LUT
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mSkyViewLutSampler);

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        mCommonUBO3[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sCommonUBO));
        mAtmoUBO3[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sAtmosphereUBO));
    }

    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * g_FramesInFlight},         // 2 UBO
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * g_FramesInFlight}  // 1 texture
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline3.descPool);

    // Global sets (1 per frame)
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline3.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline3.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline3.descSet.data());

    UpdateDescriptors3();
}
void Sky::UpdateDescriptors3()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        array<VkWriteDescriptorSet, 3> descriptorWrites{};

        // Binding 0 : First UBO (sCommonUBO)
        VkDescriptorBufferInfo uboInfo0{};
        uboInfo0.buffer = mCommonUBO3[i]->buffer;
        uboInfo0.offset = 0;
        uboInfo0.range = mCommonUBO3[i]->GetSize();

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mPipeline3.descSet[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].pBufferInfo = &uboInfo0;

        // Binding 1 : Second UBO (AtmosphereParametersUBO)
        VkDescriptorBufferInfo uboInfo1{};
        uboInfo1.buffer = mAtmoUBO3[i]->buffer;
        uboInfo1.offset = 0;
        uboInfo1.range = mAtmoUBO3[i]->GetSize();

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = mPipeline3.descSet[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].pBufferInfo = &uboInfo1;

        // Binding 2 : Storage Image (mSkyViewLUTImage)
        VkDescriptorImageInfo skyviewInfo{};
        skyviewInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        skyviewInfo.imageView = mSkyViewLUTImage->imageView;
        skyviewInfo.sampler = mSkyViewLutSampler;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = mPipeline3.descSet[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].pImageInfo = &skyviewInfo;

        // Mise à jour des descriptors en une fois
        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
void Sky::Render3(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera)
{
    if (!bVisible) return;

    ComputeLUTsSakmary2022(camera);
    
    // Update UBO
    sCommonUBO* commonUbo = static_cast<sCommonUBO*>(mCommonUBO3[currentFrame]->data);
    commonUbo->model = mat4(1.0f);
    commonUbo->proj = camera.GetProjection();
    commonUbo->view = camera.GetView();
    commonUbo->time = glfwGetTime();
    mCommonUBO3[currentFrame]->Flush();

    sAtmosphereUBO* atmoUbo = static_cast<sAtmosphereUBO*>(mAtmoUBO3[currentFrame]->data);
    UpdateAtmosphereUBO(atmoUbo, camera);
    mAtmoUBO3[currentFrame]->Flush();

    // Render
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline3.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeline3.pipelineLayout, 0, 1, &mPipeline3.descSet[currentFrame], 0, nullptr);
    vkCmdSetDepthTestEnable(cmd, VK_FALSE);
 
    vkCmdDraw(cmd, 3, 1, 0, 0);
 
    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
}

// Render offscreen
void Sky::CreatePipeline4()
{
    // Previous cleanup
	mPipeline4.destroy(mVulkanDevice->device);

    // 1. Shader
    auto compCode = CompileShaderRuntime("Resources/Shaders/Sky2022/sky_sakmary.comp");
    VkShaderModule compModule = CreateShaderModule(mVulkanDevice->device, compCode);

    VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, compModule, "main" };

    // 3. DescriptorSetLayout (3 UBO + 1 texture + 1 storage image)
    array<VkDescriptorSetLayoutBinding, 4> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,          VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // sCommonUBO
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,          VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // sAtmosphereUBO  
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,  VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // skyViewLUT
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,           VK_SHADER_STAGE_COMPUTE_BIT, nullptr }  // mSkyImage4
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipeline4.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipeline4.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipeline4.pipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = mPipeline4.pipelineLayout;

    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipeline4.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, compModule, nullptr);

    CreateDescriptors4();
}
void Sky::CreateDescriptors4()
{
    mPipeline4.descSet.resize(g_FramesInFlight);
    mPipeline4.ubo.resize(g_FramesInFlight);
    mSkyImage4.resize(g_FramesInFlight);
    mCommonUBO4.resize(g_FramesInFlight);
	mAtmoUBO4.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        if (!mSkyImage4[i].get())
        {
            mSkyImage4[i] = make_unique<VulkanTexture>(mVulkanDevice, mWidth, mHeight, 1,
                1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            mSkyImage4[i]->TransitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }
        mCommonUBO4[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sCommonUBO));
        mAtmoUBO4[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sAtmosphereUBO));
    }

    // Reuse the existing sampler
    if (!mSkyViewLutSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        vkCreateSampler(mVulkanDevice->device, &samplerInfo, nullptr, &mSkyViewLutSampler);
    }

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 * g_FramesInFlight}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipeline4.descPool);

    // Global sets (1 per frame)
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipeline4.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipeline4.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipeline4.descSet.data());

    UpdateDescriptors4();
}
void Sky::UpdateDescriptors4()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        array<VkWriteDescriptorSet, 4> descriptorWrites{};

        // 0: CommonUBO
        VkDescriptorBufferInfo uboInfo0{};
        uboInfo0.buffer = mCommonUBO4[i]->buffer;
        uboInfo0.offset = 0;
        uboInfo0.range = mCommonUBO4[i]->GetSize();

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mPipeline4.descSet[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].pBufferInfo = &uboInfo0;

        // 1: AtmosphereUBO
        VkDescriptorBufferInfo uboInfo1{};
        uboInfo1.buffer = mAtmoUBO4[i]->buffer;
        uboInfo1.offset = 0;
        uboInfo1.range = mAtmoUBO4[i]->GetSize();

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = mPipeline4.descSet[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].pBufferInfo = &uboInfo1;

        // 2: skyViewLUT
        VkDescriptorImageInfo skyviewInfo{};
        skyviewInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        skyviewInfo.imageView = mSkyViewLUTImage->imageView;
        skyviewInfo.sampler = mSkyViewLutSampler;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = mPipeline4.descSet[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].pImageInfo = &skyviewInfo;

        // 3: skyImage (storage)
        VkDescriptorImageInfo skyImageInfo{};
        skyImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        skyImageInfo.imageView = mSkyImage4[i]->imageView;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = mPipeline4.descSet[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[3].pImageInfo = &skyImageInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}
void Sky::ComputeSkyImageSakmary(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera)
{
    if (!bVisible) return;

    // Update LUTs if necessary (submits its own internal CB, rare)
    float delta = glm::length(SunDirection - mLastSunDirection);
    if (delta > mLUTRecomputeThreshold || bAtmoHasChanged)
        ComputeLUTsSakmary2022(camera);
    mLastSunDirection = SunDirection;

    // Update UBOs (CPU only)
    sCommonUBO* commonUbo = static_cast<sCommonUBO*>(mCommonUBO4[currentFrame]->data);
    commonUbo->model = mat4(1.0f);          // RTE: Center the sphere around the camera
    commonUbo->proj = camera.GetProjection();
    commonUbo->view = camera.GetViewRTE();  // RTE: Center the sphere around the camera
    commonUbo->time = glfwGetTime();
    mCommonUBO4[currentFrame]->Flush();

    sAtmosphereUBO* atmoUbo = static_cast<sAtmosphereUBO*>(mAtmoUBO4[currentFrame]->data);
    UpdateAtmosphereUBO(atmoUbo, camera);
    mAtmoUBO4[currentFrame]->Flush();

    // Record into the main CB (no Begin/End, no Submit)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline4.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline4.pipelineLayout, 0, 1, &mPipeline4.descSet[currentFrame], 0, nullptr);

    uint32_t groupsX = (mWidth + 15) / 16;
    uint32_t groupsY = (mHeight + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);
}