/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// Simulation Verlet avec des springs structuraux, de flexion et diagonaux pour simuler un tissu

// 1. PROJET
#include "Utility.h"
#include "Camera.h"
#include "Sky.h"
#include "vulkan_device.hpp"
#include "vulkan_pipeline.hpp"
#include "vulkan_ubo.hpp"
#include "vulkan_texture.hpp"

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



using namespace std;
using namespace glm;

struct sClothPt {
    vec3 pos;
    vec3 prevPos;
    vec3 acceleration;
    bool fixed;
};

class Flag
{
public:
    Flag(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, int w, int h, float s, const char* path);
    ~Flag();

    void Update(uint32_t frame, float deltaTime, const vec2& baseWind);
    void Render(VkCommandBuffer commandBuffer, uint32_t frame, const mat4& model, const mat4& view, const mat4& projection, float exposure);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

    bool bVisible = true;

private:
    void initParticles();
    void initBuffers();
    void loadTexture(const char* path);
    void CreatePipeline(VkRenderPass renderPass, VkExtent2D extent);
    void CreateDescriptors();
    void UpdateDescriptors();

    void updateVerticesBuffer(uint32_t frame);
    vec3 computeTriangleNormal(int idx0, int idx1, int idx2) const;

    void applyForces(float time, const vec2& wind);
    void integrate(float deltaTime);
    uint32_t getIndex(int x, int y) const { return static_cast<uint32_t>(y * mWidth + x); }
    void applyConstraint(int idx1, int idx2, float restLength, float rigidity); 
    void satisfyConstraints();
    void applyWindAlignmentConstraint(const vec3& windDir, float strength);

    struct sVertexFlag
    {
        float pos[3];   // x, y, z
        float uv[2];    // s, t

        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = sizeof(sVertexFlag);
            binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return binding;
        }

        static array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            array<VkVertexInputAttributeDescription, 2> attrs{};

            // layout(location = 0) vec3 aPos;
            attrs[0].location = 0;
            attrs[0].binding = 0;
            attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[0].offset = offsetof(sVertexFlag, pos);

            // layout(location = 1) vec3 aColor;
            attrs[1].location = 1;
            attrs[1].binding = 0;
            attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[1].offset = offsetof(sVertexFlag, uv);

            return attrs;
        }
    };
    struct sFlagUBO {
        mat4    model;
        mat4    view;
        mat4    proj;
        float   time;
        float   exposure;
        float   pad[2];
    };
    shared_ptr<VulkanDevice>    mVulkanDevice;
    sPipeline_x                  mPipeline;
    unique_ptr<VulkanTexture>   mTexture;
    vector<unique_ptr<VulkanUBO>>mVertexBuffers;
    uint32_t                    mVertexCount;
    unique_ptr<VulkanUBO>       mIndiceBuffer;
    uint32_t                    mIndiceCount;

    int                         mWidth, mHeight;
    float                       mSpacing;
    vector<sClothPt>            vClothPts;
    vector<uint32_t>            mIndices;
};


