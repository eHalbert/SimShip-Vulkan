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
#include <complex>
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


class Spray
{
private:
    struct sSprayUBO {
        mat4    view;
        mat4    proj;
        float   density;
        float   lifeSpan;
        float   exposure;
        float   padding;
    };
    struct ParticleGPU
    {
        vec3    position;
        float   pad1; 
        vec3    velocity;
        float   life;
        vec4    color;
    };

    shared_ptr<VulkanDevice>mVulkanDevice;
    
    static const int        mMaxParticles       = 24000;
    sPipeline_x             mPipeline;
    vector<ParticleGPU>     mvParticles;

    int                     mActiveParticles    = 0;
    const float             shortLife           = 0.5f;
    const float             longLife            = 1.0f;
    int                     lifeSpan;
    bool                    bVisible            = true;
    unique_ptr<VulkanUBO>   mParticleBuffer;

    mt19937                             mRng{ random_device{}() };
    uniform_real_distribution<float>    mDist{ -0.5f, 0.5f };
    uniform_real_distribution<float>    mDistOffset{ -0.05f, 0.05f };      // randomOffset
    uniform_real_distribution<float>    mDistVelocity{ -0.1f, 0.1f };     // randomVelocity
    uniform_real_distribution<float>    mDistLife{ 0.0f, 1.0f };          // life [0,1] × range
    uniform_real_distribution<float>    mDistGray{ 0.9f, 1.0f };          // gray

    void CreatePipeline(VkRenderPass renderPass, VkExtent2D extent);
    void CreateDescriptors();
    void UpdateDescriptors();
    void CreateBufferParticles();

public:
    Spray(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent);
    ~Spray();

    void Emit(vec3 position, vec3 velocity);
    void Update(float deltaTime);
    void UpdateBufferParticles();
    void Render(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, float density, float exposure);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);
};

