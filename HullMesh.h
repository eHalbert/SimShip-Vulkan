/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include "vulkan_texture.hpp"
#include "Structures.h"
#include "Utility.h"
#include "Camera.h"

// 2. LIB   
#include <vulkan/vulkan.h>
// glm
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
using namespace glm;

// 3. WIN
#include <iostream>
#include <vector>
using namespace std;

// HULL MESH (Colored faces + wireframe) ////////////////////////////////////////////////////////

struct sVertexHull
{
    float pos[3];   // x, y, z
    float color[3]; // r, g, b

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(sVertexHull);
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
        attrs[0].offset = offsetof(sVertexHull, pos);

        // layout(location = 1) vec3 aColor;
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(sVertexHull, color);

        return attrs;
    }
}; 

class HullMesh
{
public:
   HullMesh(shared_ptr<VulkanDevice> vulkanDevice, vector<float> vertices, vector<unsigned int> indices);
    ~HullMesh();

    void CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

    void UpdateUBO(uint32_t currentFrame, Camera& camera, mat4& model);
    void UpdateVertexBuffer(const vector<float>& vertices);
    void Render(VkCommandBuffer cmd, uint32_t currentFrame);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

private:
    shared_ptr<VulkanDevice>	mVulkanDevice;
    uint32_t                    mFramesInFlight;

    unique_ptr<VulkanBuffer>    mVertexBuffer;
    unique_ptr<VulkanBuffer>    mStagingVertexBuffer;
    unique_ptr<VulkanBuffer>    mIndexBuffer;
    uint32_t					mIndicesCount;
    size_t                      mVertexBufferSize;

    sPipeline_x                  mPipeline;
    VkPipeline                  mPipelineWireframe;

    void CreateVertexBuffer(const vector<float>& vertices, vector<unsigned int>& indices);
    void CreateDescriptor();
    void UpdateDescriptor();
};

// LINE MESH (Simple lines) ////////////////////////////////////////////////////////

struct sVertexLine
{
    float pos[3];   // x, y, z only
    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(sVertexLine);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions()
    {
        array<VkVertexInputAttributeDescription, 1> attrs{};
        // layout(location = 0) vec3 aPos;
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(sVertexLine, pos);
        return attrs;
    }
};

class LineMesh
{
public:
    LineMesh(shared_ptr<VulkanDevice> vulkanDevice, const vector<vec3>& vertices);
    ~LineMesh();

    void CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);
    void UpdateUBO(uint32_t currentFrame, Camera& camera, mat4& model, vec4 color);
    void UpdateVertices(const vector<vec3>& vertices);
    void Render(VkCommandBuffer cmd, uint32_t currentFrame);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP);

private:
    shared_ptr<VulkanDevice>    mVulkanDevice;
    uint32_t                    mFramesInFlight;
    uint32_t                    mVertexCount;

    unique_ptr<VulkanBuffer>    mVertexBuffer;
    unique_ptr<VulkanBuffer>    mStagingVertexBuffer;

    sPipeline_x                  mPipeline;

    void CreateVertexBuffer(const vector<vec3>& vertices);
    void CreateDescriptor();
    void UpdateDescriptor();
};

// WAKE MESH (Wake lines) ////////////////////////////////////////////////////////

struct sVertexWake
{
    vec3 pos;   // x, y, z

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(sVertexWake);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions()
    {
        array<VkVertexInputAttributeDescription, 1> attrs{};

        // layout(location = 0) vec3 aPos
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = 0;

        return attrs;
    }
};
struct VertexWakeAlpha
{
    vec3 pos;   // x, y, z
	float alpha; // transparency (1.0 = opaque, 0.0 = invisible)

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(VertexWakeAlpha);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions()
    {
        array<VkVertexInputAttributeDescription, 2> attrs{};

        // layout(location = 0) vec3 aPos
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = 0;

		// layout(location = 1) float aAlpha
        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32_SFLOAT;
		attrs[1].offset = offsetof(VertexWakeAlpha, alpha);

        return attrs;
    }
};
struct sWakeUBO
{
    float scaleX;
    float scaleZ;
    float offsetX;
    float offsetZ;
    float originX;
    float originZ;
    float pad[2];
};
class WakeMesh
{
public:
    WakeMesh(shared_ptr<VulkanDevice> vulkanDevice, const vector<sFoamVertex>& vertices);
    ~WakeMesh();
    // Mesh wireframe (for debug purpose)
    void CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateUBO(uint32_t imageIndex, Camera& camera, vec4 color);
    void UpdateVertices(const vector<sFoamVertex>& vertices);
    void RenderMesh(VkCommandBuffer cmd, uint32_t currentFrame);
	// Texture (applied on the ocean)
    void CreatePipelineTexture(VkRenderPass renderPass, VkExtent2D extent);
    void UpdateTextureUBO(uint32_t imageIndex, float scaleX, float scaleZ, float offsetX, float offsetZ, float originX, float originZ);
    void UpdateTextureVertices(const vector<sFoamVertex>& vertices);
    void RenderTexture(VkCommandBuffer cmd, uint32_t currentFrame);
    void CreateBlurPipelines();
    void UpdateBlurDescriptorsHorizontal(uint32_t imageIndex, VulkanTexture& input, VulkanTexture& output);
    void UpdateBlurDescriptorsVertical(uint32_t imageIndex, VulkanTexture& input, VulkanTexture& output);
    void ComputeBlur(uint32_t imageIndex, VulkanTexture& inputView, VulkanTexture& tempView, VulkanTexture& outputView, float texSize);

private:
    shared_ptr<VulkanDevice>    mVulkanDevice;
    uint32_t                    mFramesInFlight;
    uint32_t                    mVertexCount = 0;
    size_t                      mVertexBufferSize = 0;

	// Mesh wireframe
    unique_ptr<VulkanBuffer>    mVertexBuffer;
    unique_ptr<VulkanBuffer>    mStagingVertexBuffer;
    sPipeline_x                 mPipeline;
    void CreateVertexBuffer(const vector<sFoamVertex>& vertices);
    void CreateDescriptor();
    void UpdateDescriptor();

    // Texture
    sPipeline_x                 mPipelineTexture;
    uint32_t                    mVertexCountTexture;
    size_t                      mVertexBufferTextureSize ;
    unique_ptr<VulkanBuffer>    mVertexBufferTexture;
    unique_ptr<VulkanBuffer>    mStagingVertexBufferTexture;
    void CreateVertexBufferTexture(const vector<sFoamVertex>& vertices);
    void CreateDescriptorTexture();
    void UpdateDescriptorTexture();

    VkPipeline                  mBlurHorizontalPipeline, mBlurVerticalPipeline;
    VkPipelineLayout            mBlurHorizontalPipelineLayout, mBlurVerticalPipelineLayout;
    VkDescriptorSetLayout       mBlurDescriptorSetLayout;
    VkDescriptorPool            mBlurDescriptorPool;
    vector<VkDescriptorSet>     mBlurDescriptorSetHorizontal;
    vector<VkDescriptorSet>     mBlurDescriptorSetVertical;
    vector<unique_ptr<VulkanUBO>>mBlurUBO;
    void CreateBlurDescriptors();
};

// GRID MESH //////////////////////////////////////////////////////////////////

class GridMesh 
{
public:
    GridMesh(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D swapChainExtent, uint32_t gridSize, float cellSize);
    ~GridMesh();

    void UpdateUBO(uint32_t currentFrame, Camera& camera, const mat4& model, const vec4& color);
    void Render(VkCommandBuffer cmd, uint32_t currentFrame);
    void RecreatePipeline(VkRenderPass renderPassScene, VkExtent2D newExtent);

    bool                        bVisible = false;

private:
    void CreateVertices();
    void CreateVertexBuffer();
    void CreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void CreateDescriptor();

private:
    shared_ptr<VulkanDevice>    mVulkanDevice;
    uint32_t                    mFramesInFlight;

    // Geometry
    vector<sVertexLine>         mVertices;
    unique_ptr<VulkanBuffer>    mVertexBuffer;
    unique_ptr<VulkanBuffer>    mStagingVertexBuffer;

    // Pipeline
    sPipeline_x                 mPipeline;

    uint32_t                    mGridSize;
    float                       mCellSize;
};