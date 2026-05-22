/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Structures.h"
#include "Utility.h"
#include "Camera.h"
#include "Sky.h"
#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include "vulkan_texture.hpp"
#include "vulkan_ubo.hpp"

// 2. LIB
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
// glm
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
using namespace glm;
// stb
#include <stb/stb_image.h>
// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// 3. WIN
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <vector>
#include <stdexcept>
#include <array>
#include <chrono>
#include <unordered_map>
using namespace std;


struct sVertex
{
    vec3 pos;
    vec3 normal;
    vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription() 
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(sVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() 
    {
        array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        // Location 0: Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(sVertex, pos);

        // Location 1: Normal
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(sVertex, normal);

        // Location 2: TexCoord
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(sVertex, texCoord);

        return attributeDescriptions;
    }
};
struct sBboxVertex {
    vec3 pos;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(sBboxVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static array<VkVertexInputAttributeDescription, 1> getAttributeDescriptions() {
        array<VkVertexInputAttributeDescription, 1> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = 0;
        return attributeDescriptions;
    }
};
struct sMaterial
{
    vec4            ambient = { 0.1f,0.1f,0.1f,1.0f };
    vec4            diffuse = { 0.8f,0.8f,0.8f,1.0f };
    vec4            specular = { 0.5f,0.5f,0.5f,1.0f };
    vec4            emission = { 0.0f,0.0f,0.0f,1.0f };
    float           shininess = 32.0f;
    float           roughness = 0.1f;
    float           metallic = 0.0f;
    float           padding = 0.0f;
};
struct sTypePath
{
    string          type;
    string          path;
};
struct sSharedTexture
{
    VulkanTexture   texture;
    string          path;                   // For debug
    uint32_t        refCount        = 0;    // Counter of references
	bool		    hasTransparency = false;
};
class Mesh
{
public:
    vector<sVertex>             vVertices;
    vector<unsigned int>        vIndices;
    sMaterial                   Material;   // Assimp material
	vector<sTypePath>           vTypePaths;

    unique_ptr<VulkanBuffer>    vertexBuffer;
    unique_ptr<VulkanBuffer>    indexBuffer;
    uint32_t                    indexCount          = 0;
    VkDescriptorSet             DescriptorSet       = nullptr;
    vector<VulkanTexture*>      vTextures;
    VulkanTexture*              pTexComp            = nullptr;     // Texture COMP (R=AO, G=Roughness, B=Metallic), peut être nullptr

	bool					    HasTransparency     = false;    
    vec3                        Center              = vec3(0.0f);
    vec3                        TransformedCenter   = vec3(0.0f);

    Mesh(vector<sVertex> v, vector<uint32_t> i, sMaterial m, vector<sTypePath> t) :
        vVertices(std::move(v)), vIndices(std::move(i)), Material(m), vTypePaths(std::move(t)) 
    {
        if (vVertices.empty())
            Center = vec3(0.0f);

		vec3 sum(0.0f);
		for (auto& v : vVertices)
			sum += v.pos;
		Center = sum / static_cast<float>(vVertices.size());
    }
private:

};

class Model
{
public:
    Model(shared_ptr<VulkanDevice>& vulkanDevice);
    ~Model();
	
    void LoadModel(const char* modelPath, VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT);
    
    // Shadow
    void CreateShadowPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateShadowUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, vec3 Position);
    void RenderShadow(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderShadowOpaque(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderShadowTransparent(VkCommandBuffer cmd, int iCurrentFrame);
    
    // Reflection
    void CreateReflectionPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateReflectionUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model);
    void RenderReflection(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderReflectionOpaque(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderReflectionTransparent(VkCommandBuffer cmd, int iCurrentFrame);
    
    // Bridge masks
    void CreateBridgeMaskPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateBridgeMaskDescriptors();
    void UpdateBridgeMaskUBOs(uint32_t currentImage, Camera& camera, mat4 model);
    void RenderOpaqueWalls(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderWindows(VkCommandBuffer cmd, int iCurrentFrame);

    // Multisample simple without shadow and reflection
    void CreateMsPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateMsUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, float envmapFactor = 0.05f, float specularIntensity = 0.5f);
    void RenderMs(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderMsOpaque(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderMsTransparent(VkCommandBuffer cmd, int iCurrentFrame);
    
    // Multisample complex with shadow and reflection
    void CreateCxPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateCxUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, float envmapFactor = 0.05f, float specularIntensity = 0.5f);
    void RenderCx(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderCxOpaque(VkCommandBuffer cmd, int iCurrentFrame);
    void RenderCxTransparent(VkCommandBuffer cmd, int iCurrentFrame);

    // Wireframe multisample (same shaders as Ms, polygonMode = LINE, no transparency split)
    void CreateWireframeMsPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void UpdateWireframeMsUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, float envmapFactor = 0.05f, float specularIntensity = 0.5f);
    void RenderWireframeMs(VkCommandBuffer cmd, int iCurrentFrame);

    // Wireframe colored (no texture, uniform color, same approach as Bbox)
    void CreateWireframeColorPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void RenderWireframeColor(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, mat4 model, vec4 color);

    // Bounding box
    void CreateBboxPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void RenderBbox(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, mat4 model, vec4 color);


    void RecreatePipelines(VkRenderPass renderPassScene, VkRenderPass renderPassShadow, VkRenderPass renderPassReflection, VkRenderPass renderPassBridgeMask, VkExtent2D swapChainExtent);

	sBBox GetBoundingBox() const { return mBbox; }
    vector<Mesh>& GetMesh() { return mvMeshes; }
    
    bool                        bVisible        = true;
    unsigned int                NbVertices      = 0;
    unsigned int                NbFaces         = 0;

private:
    struct sLightUBO
    {
        vec3            position;
        float           exposure;
        vec3            ambient;
        float	        envmapFactor;
        vec3            diffuse;
        float           mistDensity;
        vec3            specular;
        float           specularIntensity;
    };
    
    sBBox				        mBbox;				// Bounding box
    vec4                        mCorners[8];

    shared_ptr<VulkanDevice>	mVulkanDevice;
    VkFrontFace                 mFrontFace;
    VkCullModeFlags             mCullMode;

    // Data
    vector<Mesh>                mvMeshes;
    vector<sTypePath>           vTypePaths;	        // stores all the textures
    string                      mDirectory;
    unordered_map<string, sSharedTexture> muTextureCache;
    vector<Mesh*>               mvOpaqueMeshes;
    vector<Mesh*>               mvTransparentMeshes;
    bool					    HasTransparency = false;
    bool                        bIsVisibleInFrustum = false;
    void                ProcessNode(aiNode* node, const aiScene* scene);
    void                CreateMeshBuffers();
    bool                IsVisibleInFrustum(const mat4& mvp);
    Mesh                ProcessMesh(aiMesh* mesh, const aiScene* scene);
    vector<sTypePath>   ListTextures(aiMaterial* mat, aiTextureType type, string typeName);
    VulkanTexture     * LoadTextureShared(const string& texturePath, bool& bTransparency);

    // For shadow rendering
    sPipeline_x_2                   mShadowPipeline;
    vector<unique_ptr<VulkanUBO>>   mShadowMatrixUBO;
    VkExtent2D                      mShadowExtent;
    bool                            mHasShadowPipeline = false;
    void                CreateShadowDescriptors();
    void                UpdateShadowDescriptors();

    // For reflection rendering
    sPipeline_x_2                   mReflPipeline;
    vector<unique_ptr<VulkanUBO>>   mReflMatrixUBO;
    vector<unique_ptr<VulkanUBO>>   mReflLightUBO;
    vector<unique_ptr<VulkanUBO>>   mReflViewUBO;
    bool                            mHasReflPipeline = false;
    void                CreateReflectionDescriptors();
    void                UpdateReflectionDescriptors();

    // For bridge mask
    sPipeline_x_2                   mBridgeMaskPipeline;
    vector<unique_ptr<VulkanUBO>>   mBridgeMaskMatrixUBO;
    bool                            mHasBridgeMaskPipeline = false;
    void                CreateBridgeMaskDescriptors();

	// For simple multisample rendering (opaque + transparent)
    sPipeline_x_2                   mMsPipeline;
    vector<unique_ptr<VulkanUBO>>   mMsMatrixUBO;
    vector<unique_ptr<VulkanUBO>>   mMsLightUBO;
    vector<unique_ptr<VulkanUBO>>   mMsViewUBO;
    bool                            mHasMsPipeline = false;
    void                CreateMsDescriptors();
    void                UpdateMsDescriptors();

	// For complex multisample rendering (opaque + transparent with shadow + reflection + envmap)
    sPipeline_x_2                   mCxPipeline;
    vector<unique_ptr<VulkanUBO>>   mCxMatrixUBO;
    vector<unique_ptr<VulkanUBO>>   mCxLightUBO;
    vector<unique_ptr<VulkanUBO>>   mCxViewUBO;
    bool                            mHasCxPipeline = false;
    void                CreateCxDescriptors();
    void                UpdateCxDescriptors();

    // For wireframe multisample rendering (same shaders as Ms, polygonMode = LINE)
    sPipeline_x_2                   mWireframeMsPipeline;
    vector<unique_ptr<VulkanUBO>>   mWireframeMsMatrixUBO;
    vector<unique_ptr<VulkanUBO>>   mWireframeMsLightUBO;
    vector<unique_ptr<VulkanUBO>>   mWireframeMsViewUBO;
    bool                            mHasWireframeMsPipeline = false;
    void                CreateWireframeMsDescriptors();
    void                UpdateWireframeMsDescriptors();

    // For wireframe colored rendering (no texture, uniform color via sMatrixColorUBO, same approach as Bbox)
    sPipeline_x                     mWireframeColorPipeline;
    bool                            mHasWireframeColorPipeline = false;
    void                CreateWireframeColorDescriptors();
    void                UpdateWireframeColorDescriptors();

	// For bounding box rendering (wireframe colored)
    sPipeline_x                     mBboxPipeline;
    unique_ptr<VulkanBuffer>        mBboxVertexBuffer;
    unique_ptr<VulkanBuffer>        mBboxIndexBuffer;
    bool                            mHasBboxPipeline = false;
    void                SetBoundingBox();
    void                CreateBboxBuffers();
    void                CreateBboxDescriptors();
    void                UpdateBboxDescriptors();

	// Textures
    VulkanTexture                   mTexDummyWhite;
    VulkanTexture                   mTexDummyBlack;     // Fallback COMP : AO=0 (no occlusion), Roughness=0, Metallic=0
    VulkanTexture                   mTexEnvMap;
    VkDescriptorSetLayout           mTexMeshDescSetLayout       = nullptr;
    VkDescriptorPool                mTexMeshDescPool            = nullptr;
    void                CreateTexMeshDescriptors();
    void                UpdateTexMeshDescriptors();

};