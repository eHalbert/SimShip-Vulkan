/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Utility.h"
#include "Camera.h"
#include "Sky.h"
#include "vulkan_device.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_ubo.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;

// 3. WIN
#define NOMINMAX
#include <vector>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>
#include <numeric>
#include <random>
using namespace std;

class Smoke
{
private:
    struct ParticleGPU {
        vec3    position;
        float   life;
        vec3    velocity;
        float   pad;
        vec4    color;
    };
    struct sSmokeComputePush {
        float dt;
        int   particlesPerFrame;
        int   numEmitters;
        int   frameCount;
        float shortLife;
        float longLife;
    };
    struct sSmokeRenderPush {
        mat4  view;         // offset 0
        mat4  projection;   // offset 64  
        float density;      // offset 128
        float lifeSpan;     // offset 132
        float exposure;     // offset 136
        float padding;
    };
    struct sComputeUBO {
        vec4    emitPositions[2];
        vec4    windDirection;
    };

    shared_ptr<VulkanDevice>    mVulkanDevice;

    static constexpr uint32_t   SMOKE_MAX_PARTICLES = 5000;
    unique_ptr<VulkanUBO>       mParticles;
    vector<unique_ptr<VulkanUBO>>mComputeUBO;

    uint32_t                    mFrameSmokeCount = 0;

    VkPipeline                  mComputePipeline;
    VkPipelineLayout            mComputeLayout;
    
	sPipeline_x                 mRenderPipeline;

public:
    Smoke(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent);
    ~Smoke();

    void Update(VkCommandBuffer cmd, uint32_t frame, float dt, int nChimney, vec3 chimney1WorldPos, vec3 chimney2WorldPos, vec3 windDirection);
    void Render(VkCommandBuffer cmd, uint32_t frame, Camera& camera, Sky* sky, float density);

private:
    void CreatePipeline(VkRenderPass renderPassScene, VkExtent2D extent);

};

