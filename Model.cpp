/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Model.h"

mat4 LightViewProjection;

extern uint32_t                 g_FramesInFlight;
extern unique_ptr<VulkanTexture>g_TexShadowDepth;
extern VkSampler                g_TexShadowDepthSampler;


Model::Model(shared_ptr<VulkanDevice>& vulkanDevice)
{
	mVulkanDevice       = vulkanDevice;
}
Model::~Model()
{
    for (int i = 0; i < g_FramesInFlight; i++)
    {
		if (mMsMatrixUBO.size() > i)    mMsMatrixUBO[i].reset();
        if (mMsLightUBO.size() > i)     mMsLightUBO[i].reset();
        if (mMsViewUBO.size() > i)      mMsViewUBO[i].reset();

        if (mCxMatrixUBO.size() > i)    mCxMatrixUBO[i].reset();
        if (mCxLightUBO.size() > i)     mCxLightUBO[i].reset();
        if (mCxViewUBO.size() > i)      mCxViewUBO[i].reset();

        if (mShadowMatrixUBO.size() > i) mShadowMatrixUBO[i].reset();

        if (mReflMatrixUBO.size() > i)  mReflMatrixUBO[i].reset();
        if (mReflLightUBO.size() > i)   mReflLightUBO[i].reset();
        if (mReflViewUBO.size() > i)    mReflViewUBO[i].reset();
    }

    muTextureCache.clear();

    for (auto& mesh : mvMeshes)
    {
        mesh.vertexBuffer.reset();
		mesh.indexBuffer.reset();
    }

	mShadowPipeline.destroy(mVulkanDevice->device);
	mReflPipeline.destroy(mVulkanDevice->device);
	mBboxPipeline.destroy(mVulkanDevice->device);
	mCxPipeline.destroy(mVulkanDevice->device);
	mMsPipeline.destroy(mVulkanDevice->device);

    vkDestroyDescriptorPool(mVulkanDevice->device, mTexMeshDescPool, nullptr);
    vkDestroyDescriptorSetLayout(mVulkanDevice->device, mTexMeshDescSetLayout, nullptr);
}

// Load model
void Model::LoadModel(const char* modelPath, VkFrontFace frontFace, VkCullModeFlags cullMode)
{
    mFrontFace = frontFace;
    mCullMode = cullMode;

    // Assimp : Load model
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(modelPath, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
        return;
    }

    // Directory path
    string path(modelPath);
    mDirectory = path.substr(0, path.find_last_of('/'));
    if (mDirectory.length() == 0)
        mDirectory = path.substr(0, path.find_last_of('\\'));

    // Process hierarchy + meshes
    ProcessNode(scene->mRootNode, scene);

    // Get bounding box
    SetBoundingBox();

#ifdef INFO_INIT    // Structures.h
    cout << "Model : " << path << "   " << mvMeshes.size() << " meshes, " << vTypePaths.size() << " textures" << std::endl;
#endif

    // GPU Buffers (vertex/index)
    CreateMeshBuffers();

    // Shared textures
    for (auto& mesh : mvMeshes)
    {
        mesh.vTextures.resize(mesh.vTypePaths.size());
        for (size_t i = 0; i < mesh.vTypePaths.size(); i++)
        {
            bool bTransparency = false;
            mesh.vTextures[i] = LoadTextureShared(mesh.vTypePaths[i].path, bTransparency);
            mesh.HasTransparency = bTransparency;
            if (!mesh.vTextures[i])
                cout << "Failed: " << mesh.vTypePaths[i].path << std::endl;
        }
        if (mesh.Material.diffuse.a < 0.99f)
            mesh.HasTransparency = true;

        // Identify the COMP texture among the loaded textures
        mesh.pTexComp = nullptr;
        for (size_t i = 0; i < mesh.vTypePaths.size(); i++)
        {
            if (mesh.vTypePaths[i].type == "texture_comp" && mesh.vTextures[i])
            {
                mesh.pTexComp = mesh.vTextures[i];
                break;
            }
        }
    }

    mTexDummyWhite.CreateDummyTexture(mVulkanDevice);
    mTexDummyBlack.CreateDummyTexture(mVulkanDevice, 0, 0, 0);  // R=AO=0(plein), G=Roughness=0, B=Metallic=0
	mTexEnvMap.CreateFromFile(mVulkanDevice, "Resources/Textures/citrus_orchard_puresky_2k.hdr");

    // Create lists of opaque and transparent meshes
    mvTransparentMeshes.clear();
    mvOpaqueMeshes.clear();
    for (auto& mesh : mvMeshes)
    {
        if (mesh.HasTransparency)
            mvTransparentMeshes.push_back(&mesh);
        else
            mvOpaqueMeshes.push_back(&mesh);
    }

	// Gloabal transparency flag
    if (mvTransparentMeshes.size() > 0)
        HasTransparency = true;
}
void Model::ProcessNode(aiNode* node, const aiScene* scene)
{
    for (unsigned int i = 0; i < node->mNumMeshes; i++) 
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        NbVertices += mesh->mNumVertices;
        NbFaces += mesh->mNumFaces;
        mvMeshes.push_back(ProcessMesh(mesh, scene));
    }
    for (unsigned int i = 0; i < node->mNumChildren; i++) 
        ProcessNode(node->mChildren[i], scene);
}
Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene)
{
    vector<sVertex>         vVertices;
    vector<unsigned int>    vIndices;
    vector<sTypePath>       vTypePaths;

    // Vertices

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) 
    {
        sVertex vertex;
		// position
        vec3 vector;
        vector.x = mesh->mVertices[i].x;
        vector.y = mesh->mVertices[i].y;
        vector.z = mesh->mVertices[i].z;
        vertex.pos = vector;
        // normals
        if (mesh->HasNormals())
        {
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.normal = vector;
        }
        // texture coordinates
        if (mesh->mTextureCoords[0]) 
        {
            vec2 vec;
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.texCoord = vec;
        }
        else 
            vertex.texCoord = vec2(0.0f, 0.0f);

        vVertices.push_back(vertex);
    }

	// Indices

    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        // retrieve all mvIndices of the face and store them in the mvIndices vector
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            vIndices.push_back(face.mIndices[j]);
    }
  
    // Materials

    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
    sMaterial mat;

    // Name
    aiString name;
    if (AI_SUCCESS == material->Get(AI_MATKEY_NAME, name))
        string matName = name.C_Str();

    // Ambient
    mat.ambient = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    aiColor4D ambient(0.0f, 0.0f, 0.0f, 1.0f);
    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, ambient))
        mat.ambient = vec4(ambient.r, ambient.g, ambient.b, ambient.a);

    // Diffuse
    mat.diffuse = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    aiColor4D diffuse(0.0f, 0.0f, 0.0f, 1.0f);
    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse))
        mat.diffuse = vec4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);

    // Specular
    mat.specular = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    aiColor4D specular(0.0f, 0.0f, 0.0f, 1.0f);
    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, specular))
        mat.specular = vec4(specular.r, specular.g, specular.b, specular.a);

    // Emission
    mat.emission = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    aiColor4D emission(0.0f, 0.0f, 0.0f, 1.0f);
    if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_EMISSIVE, emission))
        mat.emission = vec4(emission.r, emission.g, emission.b, emission.a);

    // Shininess
    float shininess = 0.0f;
    if (AI_SUCCESS == material->Get(AI_MATKEY_SHININESS, shininess))
    {
        float strength;
        if (AI_SUCCESS == material->Get(AI_MATKEY_SHININESS_STRENGTH, strength))
            mat.shininess = shininess * strength;
        else
            mat.shininess = shininess;
    }
    else
        mat.shininess = 0.0f;

    // Roughness
    float roughness = 1.0f;
    if (AI_SUCCESS == material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness))
        mat.roughness = roughness;
    else
        mat.roughness = 1.0f;

    // Metallic
    float metallic = 0.0f;
    if (AI_SUCCESS == material->Get(AI_MATKEY_METALLIC_FACTOR, metallic))
        mat.metallic = metallic;
    else
        mat.metallic = 0.0f;

    // Textures

    // 1. diffuse maps
    vector<sTypePath> diffuseMaps = ListTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
    vTypePaths.insert(vTypePaths.end(), diffuseMaps.begin(), diffuseMaps.end());

    // 2. specular maps
    vector<sTypePath> specularMaps = ListTextures(material, aiTextureType_SPECULAR, "texture_specular");
    vTypePaths.insert(vTypePaths.end(), specularMaps.begin(), specularMaps.end());

    // 3. normal maps
    vector<sTypePath> heightMaps = ListTextures(material, aiTextureType_NORMALS, "texture_normals");
    vTypePaths.insert(vTypePaths.end(), heightMaps.begin(), heightMaps.end());

    // 4. height maps
    vector<sTypePath> normalMaps = ListTextures(material, aiTextureType_HEIGHT, "texture_height");
    vTypePaths.insert(vTypePaths.end(), normalMaps.begin(), normalMaps.end());

    // 5. COMP map (GLTF metallicRoughness = aiTextureType_UNKNOWN via Assimp)
    //    Fallback : search in other types for a texture whose name contains "_COMP"
    vector<sTypePath> compMaps = ListTextures(material, aiTextureType_UNKNOWN, "texture_comp");
    if (compMaps.empty())
    {
        // Fallback : iterate through all known types, search for "_COMP" in the path
        static const aiTextureType allTypes[] = {
            aiTextureType_DIFFUSE, aiTextureType_SPECULAR, aiTextureType_AMBIENT,
            aiTextureType_EMISSIVE, aiTextureType_HEIGHT, aiTextureType_NORMALS,
            aiTextureType_SHININESS, aiTextureType_OPACITY, aiTextureType_DISPLACEMENT,
            aiTextureType_LIGHTMAP, aiTextureType_REFLECTION, aiTextureType_UNKNOWN
        };
        for (auto t : allTypes)
        {
            for (unsigned int i = 0; i < material->GetTextureCount(t); i++)
            {
                aiString str;
                material->GetTexture(t, i, &str);
                string path(str.C_Str());
                string pathUpper = path;
                transform(pathUpper.begin(), pathUpper.end(), pathUpper.begin(), ::toupper);
                if (pathUpper.find("_COMP") != string::npos)
                {
                    compMaps.push_back({ "texture_comp", path });
                    break;
                }
            }
            if (!compMaps.empty()) break;
        }
    }
    vTypePaths.insert(vTypePaths.end(), compMaps.begin(), compMaps.end());

    return Mesh(vVertices, vIndices, mat, vTypePaths);
}
void Model::SetBoundingBox()
{
    mBbox.min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    mBbox.max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

    for (const auto& mesh : mvMeshes)
    {
        for (const auto& vertex : mesh.vVertices)
        {
            mBbox.min.x = std::min(mBbox.min.x, vertex.pos.x);
            mBbox.min.y = std::min(mBbox.min.y, vertex.pos.y);
            mBbox.min.z = std::min(mBbox.min.z, vertex.pos.z);

            mBbox.max.x = std::max(mBbox.max.x, vertex.pos.x);
            mBbox.max.y = std::max(mBbox.max.y, vertex.pos.y);
            mBbox.max.z = std::max(mBbox.max.z, vertex.pos.z);
        }
    }

    mCorners[0] = { mBbox.min.x, mBbox.min.y, mBbox.min.z, 1.0f };
	mCorners[1] = { mBbox.max.x, mBbox.min.y, mBbox.min.z, 1.0f };
	mCorners[2] = { mBbox.min.x, mBbox.max.y, mBbox.min.z, 1.0f };
    mCorners[3] = { mBbox.max.x, mBbox.max.y, mBbox.min.z, 1.0f };
    mCorners[4] = { mBbox.min.x, mBbox.min.y, mBbox.max.z, 1.0f };
    mCorners[5] = { mBbox.max.x, mBbox.min.y, mBbox.max.z, 1.0f };
    mCorners[6] = { mBbox.min.x, mBbox.max.y, mBbox.max.z, 1.0f };
	mCorners[7] = { mBbox.max.x, mBbox.max.y, mBbox.max.z, 1.0f };
}
vector<sTypePath> Model::ListTextures(aiMaterial* mat, aiTextureType type, string typeName)
{
    // checks all material textures of a given type and loads the textures if they're not loaded yet. The required info is returned as a Texture struct.

    vector<sTypePath> vTypePaths;
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);
        // check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
        bool skip = false;
        for (unsigned int j = 0; j < vTypePaths.size(); j++)
        {
            if (strcmp(vTypePaths[j].path.data(), str.C_Str()) == 0)
            {
                skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
                break;
            }
        }
        if (!skip)
        {   // if texture hasn't been loaded already, load it
            sTypePath texture{ typeName, str.C_Str() };
            vTypePaths.push_back(texture);
        }
    }
    return vTypePaths;
}
VulkanTexture* Model::LoadTextureShared(const string& texturePath, bool& bTransparency)
{
    string fullPath = mDirectory + '/' + texturePath;

    // Search cache
    auto it = muTextureCache.find(fullPath);
    if (it != muTextureCache.end()) {
        it->second.refCount++;
        bTransparency = it->second.hasTransparency;
        return &it->second.texture;
    }

    // Create directly in cache → NO copy!
    auto& newEntry = muTextureCache[fullPath];
    newEntry.path = fullPath;

    if (!newEntry.texture.CreateFromFile(mVulkanDevice, fullPath, true)) {
        muTextureCache.erase(fullPath);
        return nullptr;
    }

    newEntry.hasTransparency = newEntry.texture.bTransparency;
    newEntry.refCount = 1;
    return &newEntry.texture;
}
void Model::CreateMeshBuffers()
{
    for (auto& mesh : mvMeshes)
    {
        // Vertex buffer
        VkDeviceSize size = sizeof(sVertex) * mesh.vVertices.size();
        VulkanBuffer vertexBuffer = VulkanBuffer(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        void* data;
        vkMapMemory(mVulkanDevice->device, vertexBuffer.bufferMemory, 0, size, 0, &data);
        memcpy(data, mesh.vVertices.data(), (size_t)size);
        vkUnmapMemory(mVulkanDevice->device, vertexBuffer.bufferMemory);

        mesh.vertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        mesh.vertexBuffer->CopyIntoBuffer(vertexBuffer, size);

        // Index buffer
        size = sizeof(uint32_t) * mesh.vIndices.size();
        VulkanBuffer indexBuffer = VulkanBuffer(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        vkMapMemory(mVulkanDevice->device, indexBuffer.bufferMemory, 0, size, 0, &data);
        memcpy(data, mesh.vIndices.data(), (size_t)size);
        vkUnmapMemory(mVulkanDevice->device, indexBuffer.bufferMemory);

        mesh.indexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        mesh.indexBuffer->CopyIntoBuffer(indexBuffer, size);

        mesh.indexCount = static_cast<uint32_t>(mesh.vIndices.size());
    }
}
bool Model::IsVisibleInFrustum(const mat4& mvp)
{
    // Plane i: all corners are on the wrong side → object is outside the frustum
    for (int plane = 0; plane < 6; plane++)
    {
        int cornersOutside = 0;
        for (int i = 0; i < 8; i++)
        {
            vec4 clip = mvp * mCorners[i];

            bool outside = false;
            switch (plane)
            {
            case 0: outside = clip.x < -clip.w; break; // left
            case 1: outside = clip.x >  clip.w; break; // right
            case 2: outside = clip.y < -clip.w; break; // bottom
            case 3: outside = clip.y >  clip.w; break; // top
            case 4: outside = clip.z <    0.0f; break; // near  (Vulkan: z in [0,1])
            case 5: outside = clip.z >  clip.w; break; // far
            }
            if (outside) cornersOutside++;
        }
        // All corners outside this plane → object is invisible
        if (cornersOutside == 8)
        {
            bIsVisibleInFrustum = false;
            return false;
        }
    }
    bIsVisibleInFrustum = true;
    return true;
}

void Model::CreateTexMeshDescriptors()
{
    array<VkDescriptorSetLayoutBinding, 2> texBindings{};
    texBindings[0].binding = 0;
    texBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[0].descriptorCount = 1;
    texBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    texBindings[1].binding = 1;
    texBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texBindings[1].descriptorCount = 1;
    texBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(texBindings.size());
    layoutInfo.pBindings = texBindings.data();

    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mTexMeshDescSetLayout);

    // mTexMeshDescPool : 2 samplers par mesh (diffuse + COMP)

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<uint32_t>(mvMeshes.size() * 2);  // x2 : diffuse + COMP

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(mvMeshes.size());

    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mTexMeshDescPool);
}
void Model::UpdateTexMeshDescriptors()
{
    vector<VkDescriptorSetLayout> layouts(mvMeshes.size(), mTexMeshDescSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mTexMeshDescPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(mvMeshes.size());
    allocInfo.pSetLayouts = layouts.data();

    vector<VkDescriptorSet> textureSets(mvMeshes.size());
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, textureSets.data());

    // Update each set
    for (size_t i = 0; i < mvMeshes.size(); i++)
    {
        auto& mesh = mvMeshes[i];
        mesh.DescriptorSet = textureSets[i];

        // binding 0 : diffuse (or dummy white if absent)
        VulkanTexture* texDiffuse = (!mesh.vTextures.empty() && mesh.vTextures[0]) ? mesh.vTextures[0] : &mTexDummyWhite;

        VkDescriptorImageInfo diffuseInfo{};
        diffuseInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        diffuseInfo.imageView   = texDiffuse->imageView;
        diffuseInfo.sampler     = texDiffuse->sampler;

        VkWriteDescriptorSet writeDiffuse{};
        writeDiffuse.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDiffuse.dstSet          = mesh.DescriptorSet;
        writeDiffuse.dstBinding      = 0;
        writeDiffuse.descriptorCount = 1;
        writeDiffuse.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeDiffuse.pImageInfo      = &diffuseInfo;

        // binding 1 : COMP (R=AO, G=Roughness, B=Metallic) — dummy black if absent
        VulkanTexture* texComp = mesh.pTexComp ? mesh.pTexComp : &mTexDummyBlack;
       
        VkDescriptorImageInfo compInfo{};
        compInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        compInfo.imageView   = texComp->imageView;
        compInfo.sampler     = texComp->sampler;

        VkWriteDescriptorSet writeComp{};
        writeComp.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeComp.dstSet          = mesh.DescriptorSet;
        writeComp.dstBinding      = 1;
        writeComp.descriptorCount = 1;
        writeComp.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writeComp.pImageInfo      = &compInfo;

        array<VkWriteDescriptorSet, 2> writes = { writeDiffuse, writeComp };
        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

// Pipeline of shadow
void Model::CreateShadowPipeline(VkRenderPass renderPass, VkExtent2D shadowExtent)
{
    mShadowExtent = shadowExtent;

    // Previous cleanup
    mShadowPipeline.destroy(mVulkanDevice->device);

    // Texture descriptors (Set 1: Per-mesh) - reused from MsPipeline
    CreateTexMeshDescriptors();
    UpdateTexMeshDescriptors();

    // 1. Shaders
    
    // Shader opaque (vertex)
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_shadow.vert");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);

    VkPipelineShaderStageCreateInfo opaqueShaderStage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main"
    };

    // Shaders transparent (vertex + fragment)
    auto vertTranspCode = CompileShaderRuntime("Resources/Shaders/Model/model_shadow_transparent.vert");
    auto fragTranspCode = CompileShaderRuntime("Resources/Shaders/Model/model_shadow_transparent.frag");
    VkShaderModule vertTranspModule = CreateShaderModule(mVulkanDevice->device, vertTranspCode);
    VkShaderModule fragTranspModule = CreateShaderModule(mVulkanDevice->device, fragTranspCode);

    VkPipelineShaderStageCreateInfo transpShaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertTranspModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragTranspModule, "main" }
    };

    // 2. Vertex input 
    
    // opaque (position only)
    auto bindingDescription = sVertex::getBindingDescription();

    array<VkVertexInputAttributeDescription, 1> opaqueAttributes = { {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(sVertex, pos)}
    } };

    VkPipelineVertexInputStateCreateInfo opaqueVertexInputInfo{};
    opaqueVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    opaqueVertexInputInfo.vertexBindingDescriptionCount = 1;
    opaqueVertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    opaqueVertexInputInfo.vertexAttributeDescriptionCount = 1;
    opaqueVertexInputInfo.pVertexAttributeDescriptions = opaqueAttributes.data();

    // transparent (position + UV)
    array<VkVertexInputAttributeDescription, 2> transpAttributes = { {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(sVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(sVertex, texCoord)}
    } };

    VkPipelineVertexInputStateCreateInfo transpVertexInputInfo{};
    transpVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    transpVertexInputInfo.vertexBindingDescriptionCount = 1;
    transpVertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    transpVertexInputInfo.vertexAttributeDescriptionCount = 2;
    transpVertexInputInfo.pVertexAttributeDescriptions = transpAttributes.data();

    // 3. DescriptorSetLayout Set 0 (UBO shadow)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { VkDescriptorSetLayoutBinding{
        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mShadowPipeline.descSetLayout);

    // 4. PipelineLayout : Set 0 (UBO) + Set 1 (textures mesh)
    array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        mShadowPipeline.descSetLayout,  // Set 0 : UBO shadow
        mTexMeshDescSetLayout           // Set 1 : textures per mesh (reused)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = pipelineLayouts.data();
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mShadowPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, (float)shadowExtent.width, (float)shadowExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, shadowExtent };

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
    rasterizer.frontFace = mFrontFace;
    rasterizer.depthBiasEnable = VK_TRUE;

    // 8. Multisampling 1x
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 9. Depth stencil (write only)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_DEPTH_BIAS };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 3;
    dynamicState.pDynamicStates = dynamicStates;

    // 11. Base pipelineInfo commune
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mShadowPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    // 12. Pipeline opaque (vertex only, position only, 0 attachment)
    VkPipelineColorBlendStateCreateInfo opaqueColorBlending{};
    opaqueColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    opaqueColorBlending.logicOpEnable = VK_FALSE;
    opaqueColorBlending.attachmentCount = 0;
    opaqueColorBlending.pAttachments = nullptr;

    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &opaqueShaderStage;
    pipelineInfo.pVertexInputState = &opaqueVertexInputInfo;
    pipelineInfo.pColorBlendState = &opaqueColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mShadowPipeline.pipelineOpaque);

    // 13. Pipeline transparent (vertex + frag, position + UV, discard alpha)
    VkPipelineColorBlendStateCreateInfo transpColorBlending{};
    transpColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    transpColorBlending.logicOpEnable = VK_FALSE;
    transpColorBlending.attachmentCount = 0;
    transpColorBlending.pAttachments = nullptr;

    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = transpShaderStages;
    pipelineInfo.pVertexInputState = &transpVertexInputInfo;
    pipelineInfo.pColorBlendState = &transpColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mShadowPipeline.pipelineTransparent);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertTranspModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragTranspModule, nullptr);

    CreateShadowDescriptors();
}
void Model::CreateShadowDescriptors()
{
    mShadowMatrixUBO.resize(g_FramesInFlight);
    mShadowPipeline.descSet.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
        mShadowMatrixUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sShadowUBO));

    // Pool : UBO only (Set 0), The Set 1 texture is managed by CreateTexMeshDescriptors
    array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(g_FramesInFlight);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(g_FramesInFlight);
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mShadowPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mShadowPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mShadowPipeline.descPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(g_FramesInFlight);
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mShadowPipeline.descSet.data());

    UpdateShadowDescriptors();
}
void Model::UpdateShadowDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo shadowInfo{};
        shadowInfo.buffer = mShadowMatrixUBO[i]->buffer;
        shadowInfo.offset = 0;
        shadowInfo.range = sizeof(sShadowUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mShadowPipeline.descSet[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &shadowInfo;
        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &write, 0, nullptr);
    }
    mHasShadowPipeline = true;
}
void Model::UpdateShadowUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, vec3 Position)
{
    vec3 lightDir = glm::normalize(sky->SunPosition);
    vec3 lightPos = lightDir * 200.0f;
    mat4 lightView = glm::lookAt(Position, -lightPos + Position, vec3(0.0f, 1.0f, 0.0f));

    vec3 BBmin = glm::vec3(model * vec4(mBbox.min, 1.0f));
    vec3 BBmax = glm::vec3(model * vec4(mBbox.max, 1.0f));

    vec3 bboxCorners[8];
    bboxCorners[0] = vec3(BBmin.x, BBmin.y, BBmin.z);
    bboxCorners[1] = vec3(BBmax.x, BBmin.y, BBmin.z);
    bboxCorners[2] = vec3(BBmin.x, BBmax.y, BBmin.z);
    bboxCorners[3] = vec3(BBmax.x, BBmax.y, BBmin.z);
    bboxCorners[4] = vec3(BBmin.x, BBmin.y, BBmax.z);
    bboxCorners[5] = vec3(BBmax.x, BBmin.y, BBmax.z);
    bboxCorners[6] = vec3(BBmin.x, BBmax.y, BBmax.z);
    bboxCorners[7] = vec3(BBmax.x, BBmax.y, BBmax.z);

    vec3 lightSpaceMin(FLT_MAX);
    vec3 lightSpaceMax(-FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        vec4 lightSpacePos = lightView * vec4(bboxCorners[i], 1.0f);
        lightSpaceMin = glm::min(lightSpaceMin, vec3(lightSpacePos));
        lightSpaceMax = glm::max(lightSpaceMax, vec3(lightSpacePos));
    }
    vec3 lightSpaceSize = lightSpaceMax - lightSpaceMin;
    float dimOptimized = glm::max(lightSpaceSize.x, lightSpaceSize.y) * 0.5f;
    dimOptimized *= 1.0f; // Margin
    const float far_plane = 300.0f;
    mat4 lightProjection = glm::ortho(-dimOptimized, dimOptimized, -dimOptimized, dimOptimized, -dimOptimized, dimOptimized * 2.0f);

    /*  Z_openGL ∈ [-1,1] --lightProjection(GLM)--> Z_ndc ∈ [-1,1]
                               ↓
        Z_ndc ∈ [-1,1] --depthFix--> Z_vulkan ∈ [0,1] */
    mat4 depthFix(1.0f);
    depthFix[2][2] = 0.5f;
    depthFix[3][2] = 0.5f;
    lightProjection = depthFix * lightProjection;

    LightViewProjection = lightProjection * lightView;

    sShadowUBO* ubo = static_cast<sShadowUBO*>(mShadowMatrixUBO[currentImage]->data);
    *ubo = { LightViewProjection, model };
}
void Model::RenderShadow(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    RenderShadowOpaque(cmd, iCurrentFrame);
    RenderShadowTransparent(cmd, iCurrentFrame);
}
void Model::RenderShadowOpaque(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipeline.pipelineOpaque);
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 0.025f);  // Anti-acne

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipeline.pipelineLayout, 0, 1, &mShadowPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvOpaqueMeshes)
    {
        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}
void Model::RenderShadowTransparent(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipeline.pipelineTransparent);
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 0.025f);  // Anti-acne

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipeline.pipelineLayout, 0, 1, &mShadowPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvTransparentMeshes)
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);
        
        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}

// Pipeline of reflection
void Model::CreateReflectionPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Texture descriptors (Set 1: Per-mesh)
    CreateTexMeshDescriptors();
    UpdateTexMeshDescriptors();

    // Previous cleanup
	mReflPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_reflection.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/model_reflection.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    auto attributeDescriptions = sVertex::getAttributeDescriptions();
    auto bindingDescription = sVertex::getBindingDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 3. DescriptorSetLayout
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },         // Matrix
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },       // Light
        { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }        // CamPos
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mReflPipeline.descSetLayout);

    // 4. Pipeline Layout
    array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        mReflPipeline.descSetLayout,       // Set 0: MVP + Light + CamPos
        mTexMeshDescSetLayout   // Set 1: Texture mesh
    };

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(sMaterial);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = pipelineLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mReflPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 4. Viewport & scissors
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 5. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = mCullMode;
    rasterizer.frontFace = mFrontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 6. Multisampling 1x
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;  

    // 7. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 8. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // CREATION OF TWO PIPELINES

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
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mReflPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    // 12. Pipeline opaque
    VkPipelineColorBlendAttachmentState opaqueBlendAttachment{};
    opaqueBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    opaqueBlendAttachment.blendEnable = VK_TRUE;
    opaqueBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    opaqueBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    opaqueBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    opaqueBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    opaqueBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    opaqueBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo opaqueColorBlending{};
    opaqueColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    opaqueColorBlending.logicOpEnable = VK_FALSE;
    opaqueColorBlending.logicOp = VK_LOGIC_OP_COPY;
    opaqueColorBlending.attachmentCount = 1;
    opaqueColorBlending.pAttachments = &opaqueBlendAttachment;

    pipelineInfo.pColorBlendState = &opaqueColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mReflPipeline.pipelineOpaque);

	// 12. Pipeline transparent
    VkPipelineColorBlendAttachmentState transparentBlendAttachment{};
    transparentBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    transparentBlendAttachment.blendEnable = VK_TRUE;
    transparentBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    transparentBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    transparentBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    transparentBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo transparentColorBlending{};
    transparentColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    transparentColorBlending.logicOpEnable = VK_FALSE;
    transparentColorBlending.logicOp = VK_LOGIC_OP_COPY;
    transparentColorBlending.attachmentCount = 1;
    transparentColorBlending.pAttachments = &transparentBlendAttachment;

    pipelineInfo.pColorBlendState = &transparentColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mReflPipeline.pipelineTransparent);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateReflectionDescriptors();
}
void Model::CreateReflectionDescriptors()
{
    mReflMatrixUBO.resize(g_FramesInFlight);
    mReflLightUBO.resize(g_FramesInFlight);
    mReflViewUBO.resize(g_FramesInFlight);
    mReflPipeline.descSet.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        mReflMatrixUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixReflUBO));
        mReflLightUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sLightUBO));
        mReflViewUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sViewUBO));
    }

    // Pool
    array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 12;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mReflPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mReflPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mReflPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mReflPipeline.descSet.data());

    UpdateReflectionDescriptors();
}
void Model::UpdateReflectionDescriptors()
{
    // Filling the 2 identical sets
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        array<VkWriteDescriptorSet, 3> writes{};

        // 0. MVP UBO (binding 0)
        VkDescriptorBufferInfo mvpInfo{};
        mvpInfo.buffer = mReflMatrixUBO[i]->buffer;
        mvpInfo.offset = 0;
        mvpInfo.range = VK_WHOLE_SIZE;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mReflPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &mvpInfo;

        // 1. Light UBO (binding 1)
        VkDescriptorBufferInfo lightViewInfo{};
        lightViewInfo.buffer = mReflLightUBO[i]->buffer;
        lightViewInfo.offset = 0;
        lightViewInfo.range = VK_WHOLE_SIZE;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mReflPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightViewInfo;

        // 2. CamPos UBO (binding 2)
        VkDescriptorBufferInfo camInfo{};
        camInfo.buffer = mReflViewUBO[i]->buffer;
        camInfo.offset = 0;
        camInfo.range = VK_WHOLE_SIZE;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mReflPipeline.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &camInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    mHasReflPipeline = true;
}
// Rendering reflection
void Model::UpdateReflectionUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model)
{
    vec3 camPos = camera.GetPosition();
    mat4 view = camera.GetViewReflexion();
    mat4 proj = camera.GetProjection();
    vec4 clipPlane = vec4(0.0, 1.0, 0.0, 0.0);

    // Update mesh centers
    for (auto& mesh : mvMeshes)
    {
        vec4 worldCenter = model * vec4(mesh.Center, 1.0f);
        mesh.TransformedCenter = vec3(worldCenter);
    }
    // Sort transparent meshes by decreasing distance
    std::sort(mvTransparentMeshes.begin(), mvTransparentMeshes.end(),
        [&](Mesh* a, Mesh* b) {
            float distA = glm::distance(camPos, a->TransformedCenter);
            float distB = glm::distance(camPos, b->TransformedCenter);
            return distA > distB; // farther first
        }
    );

    // 1. MVP Uniform Buffer
    sMatrixReflUBO* ubo = static_cast<sMatrixReflUBO*>(mReflMatrixUBO[currentImage]->data);
    *ubo = { model, view, proj, clipPlane };

    // 2. Light Buffer
    sLightUBO* lightData = static_cast<sLightUBO*>(mReflLightUBO[currentImage]->data);
    lightData->position = vec3(-5600.0f, 6333.0f, 18125.0f);
    lightData->exposure = sky->Exposure;
    lightData->ambient = vec3(0.2f, 0.2f, 0.2f);
    lightData->envmapFactor = 0.05f;
    lightData->diffuse = vec3(1.0f, 0.9f, 0.8f);
    lightData->mistDensity = sky->MistDensity;
    lightData->specular = vec3(1.0f, 1.0f, 1.0f);
    lightData->specularIntensity = 0.5f;

    // 3. Camera Position Buffer
    sViewUBO* camData = static_cast<sViewUBO*>(mReflViewUBO[currentImage]->data);
    camData->position = camPos;
}
void Model::RenderReflection(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    RenderReflectionOpaque(cmd, iCurrentFrame);
    if (HasTransparency)
        RenderReflectionTransparent(cmd, iCurrentFrame);
}
void Model::RenderReflectionOpaque(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mReflPipeline.pipelineOpaque);

    // Set 0: GLOBAL (MVP + Light)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mReflPipeline.pipelineLayout, 0, 1, &mReflPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvOpaqueMeshes)
    {
        // Set 1: TEXTURE SPECIFIC to the mesh
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mReflPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);

        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mReflPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh->Material);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}
void Model::RenderReflectionTransparent(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mReflPipeline.pipelineTransparent);

    // Set 0: GLOBAL (MVP + Light)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mReflPipeline.pipelineLayout, 0, 1, &mReflPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvTransparentMeshes)
    {
        // Set 1: TEXTURE SPECIFIC to the mesh
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mReflPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);

        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mReflPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh->Material);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}

// Pipeline bridge masks
void Model::CreateBridgeMaskPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Previous cleanup
    mBridgeMaskPipeline.destroy(mVulkanDevice->device);

    // Texture descriptors (Set 1: Per-mesh) — reused as in CreateShadowPipeline
    CreateTexMeshDescriptors();
    UpdateTexMeshDescriptors();

    // 1. Shaders opaque walls (vertex only, like shadow opaque)
    auto vertWallCode = CompileShaderRuntime("Resources/Shaders/Model/model_bridge_wall.vert");
    VkShaderModule vertWallModule = CreateShaderModule(mVulkanDevice->device, vertWallCode);

    VkPipelineShaderStageCreateInfo wallShaderStage = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertWallModule, "main"
    };

    // Shaders windows (vertex + fragment for alpha discard if necessary)
    auto vertWinCode = CompileShaderRuntime("Resources/Shaders/Model/model_bridge_window.vert");
    auto fragWinCode = CompileShaderRuntime("Resources/Shaders/Model/model_bridge_window.frag");
    VkShaderModule vertWinModule = CreateShaderModule(mVulkanDevice->device, vertWinCode);
    VkShaderModule fragWinModule = CreateShaderModule(mVulkanDevice->device, fragWinCode);

    VkPipelineShaderStageCreateInfo winShaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertWinModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragWinModule, "main" }
    };

    // 2. Vertex input walls (position only, like shadow opaque)
    auto bindingDescription = sVertex::getBindingDescription();

    array<VkVertexInputAttributeDescription, 1> wallAttributes = { {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(sVertex, pos) }
    } };

    VkPipelineVertexInputStateCreateInfo wallVertexInputInfo{};
    wallVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    wallVertexInputInfo.vertexBindingDescriptionCount = 1;
    wallVertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    wallVertexInputInfo.vertexAttributeDescriptionCount = 1;
    wallVertexInputInfo.pVertexAttributeDescriptions = wallAttributes.data();

    // Vertex input windows (position + UV, like shadow transparent)
    array<VkVertexInputAttributeDescription, 2> winAttributes = { {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(sVertex, pos) },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(sVertex, texCoord) }
    } };

    VkPipelineVertexInputStateCreateInfo winVertexInputInfo{};
    winVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    winVertexInputInfo.vertexBindingDescriptionCount = 1;
    winVertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    winVertexInputInfo.vertexAttributeDescriptionCount = 2;
    winVertexInputInfo.pVertexAttributeDescriptions = winAttributes.data();

    // 3. DescriptorSetLayout Set 0 (UBO matrices only, like shadow)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { VkDescriptorSetLayoutBinding{
        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mBridgeMaskPipeline.descSetLayout);

    // 4. PipelineLayout : Set 0 (UBO) + Set 1 (textures mesh, reused)
    array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        mBridgeMaskPipeline.descSetLayout,  // Set 0 : UBO matrices
        mTexMeshDescSetLayout               // Set 1 : textures per mesh
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = pipelineLayouts.data();
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mBridgeMaskPipeline.pipelineLayout);

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;   // No culling: we want both faces
    rasterizer.frontFace = mFrontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 8. Multisampling 1x (render pass BridgeMask is non-MSAA)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 9. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // 10. Color blend : no color attachment (depth+stencil only)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;
    colorBlending.pAttachments = nullptr;

    // 11. Base pipelineInfo common
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mBridgeMaskPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    // 12. Pipeline opaque walls

    // depth write + stencil REPLACE = 1
    VkStencilOpState wallStencilOp{};
    wallStencilOp.failOp = VK_STENCIL_OP_KEEP;
    wallStencilOp.passOp = VK_STENCIL_OP_REPLACE;  // ← stencil = ref (1)
    wallStencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
    wallStencilOp.compareOp = VK_COMPARE_OP_ALWAYS;
    wallStencilOp.compareMask = 0xFF;
    wallStencilOp.writeMask = 0xFF;
    wallStencilOp.reference = 1;                       // ← stencil = 1 for walls

    VkPipelineDepthStencilStateCreateInfo wallDepthStencil{};
    wallDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    wallDepthStencil.depthTestEnable = VK_TRUE;
    wallDepthStencil.depthWriteEnable = VK_TRUE;   // ← depth write for walls
    wallDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    wallDepthStencil.depthBoundsTestEnable = VK_FALSE;
    wallDepthStencil.stencilTestEnable = VK_TRUE;   // ← stencil active
    wallDepthStencil.front = wallStencilOp;
    wallDepthStencil.back = wallStencilOp;

    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &wallShaderStage;
    pipelineInfo.pVertexInputState = &wallVertexInputInfo;
    pipelineInfo.pDepthStencilState = &wallDepthStencil;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mBridgeMaskPipeline.pipelineOpaque);

    // 13. Pipeline windows

    // depth NOT written + stencil REPLACE = 2
    VkStencilOpState winStencilOp{};
    winStencilOp.failOp = VK_STENCIL_OP_KEEP;
    winStencilOp.passOp = VK_STENCIL_OP_REPLACE;  // ← stencil = ref (2)
    winStencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
    winStencilOp.compareOp = VK_COMPARE_OP_ALWAYS;
    winStencilOp.compareMask = 0xFF;
    winStencilOp.writeMask = 0xFF;
    winStencilOp.reference = 2;                       // ← stencil = 2 for windows

    VkPipelineDepthStencilStateCreateInfo winDepthStencil{};
    winDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    winDepthStencil.depthTestEnable = VK_TRUE;
    winDepthStencil.depthWriteEnable = VK_FALSE;  // ← depth NOT written for windows
    winDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    winDepthStencil.depthBoundsTestEnable = VK_FALSE;
    winDepthStencil.stencilTestEnable = VK_TRUE;   // ← stencil active
    winDepthStencil.front = winStencilOp;
    winDepthStencil.back = winStencilOp;

    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = winShaderStages;
    pipelineInfo.pVertexInputState = &winVertexInputInfo;
    pipelineInfo.pDepthStencilState = &winDepthStencil;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mBridgeMaskPipeline.pipelineTransparent);

    vkDestroyShaderModule(mVulkanDevice->device, vertWallModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertWinModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragWinModule, nullptr);

    CreateBridgeMaskDescriptors();
}
void Model::CreateBridgeMaskDescriptors()
{
    mBridgeMaskMatrixUBO.resize(g_FramesInFlight);
    mBridgeMaskPipeline.descSet.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
        mBridgeMaskMatrixUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixUBO));

    // Pool : UBO only (Set 0), Set 1 textures managed by CreateTexMeshDescriptors
    array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(g_FramesInFlight);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mBridgeMaskPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mBridgeMaskPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mBridgeMaskPipeline.descPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(g_FramesInFlight);
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mBridgeMaskPipeline.descSet.data());

    UpdateBridgeMaskDescriptors();
}
void Model::UpdateBridgeMaskDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = mBridgeMaskMatrixUBO[i]->buffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(sMatrixUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mBridgeMaskPipeline.descSet[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &uboInfo;
        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &write, 0, nullptr);
    }
    mHasBridgeMaskPipeline = true;
}
// Rendering bridge masks
void Model::UpdateBridgeMaskUBOs(uint32_t currentImage, Camera& camera, mat4 model)
{
    sMatrixUBO* ubo = static_cast<sMatrixUBO*>(mBridgeMaskMatrixUBO[currentImage]->data);
    *ubo = { model, camera.GetView(), camera.GetProjection() };
}
void Model::RenderOpaqueWalls(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    // Pipeline opaque walls : depth write, stencil = 1
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mBridgeMaskPipeline.pipelineOpaque);

    // Set 0 : UBO matrix
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mBridgeMaskPipeline.pipelineLayout, 0, 1, &mBridgeMaskPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvOpaqueMeshes)
    {
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}
void Model::RenderWindows(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;
    if (!bIsVisibleInFrustum) return;

    // Pipeline windows : depth NOT written, stencil = 2
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mBridgeMaskPipeline.pipelineTransparent);

    // Set 0 : UBO matrix
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mBridgeMaskPipeline.pipelineLayout, 0, 1, &mBridgeMaskPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvTransparentMeshes)
    {
        // Set 1 : texture per mesh (for discard alpha in the fragment shader)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            mBridgeMaskPipeline.pipelineLayout, 1, 1,
            &mesh->DescriptorSet, 0, nullptr);

        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}

// Pipeline multisample simple
void Model::CreateMsPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Previous cleanup
	mMsPipeline.destroy(mVulkanDevice->device);

    // Texture descriptors (Set 1: Per-mesh)
    CreateTexMeshDescriptors();
    UpdateTexMeshDescriptors();

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_simple.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/model_simple.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    auto attributeDescriptions = sVertex::getAttributeDescriptions();
    auto bindingDescription = sVertex::getBindingDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 3. DescriptorSetLayout
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },         // Matrix
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },       // Light
        { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }        // CamPos
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mMsPipeline.descSetLayout);

    // 4. PipelineLayout
    array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        mMsPipeline.descSetLayout,  // Set 0: MVP + Light + CamPos
        mTexMeshDescSetLayout       // Set 1: Texture mesh
    };

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(sMaterial);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = pipelineLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mMsPipeline.pipelineLayout);

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
    rasterizer.cullMode = mCullMode;
    rasterizer.frontFace = mFrontFace;
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

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // CREATION OF TWO PIPELINES

    // 12. Pipeline opaque
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
    pipelineInfo.pColorBlendState = nullptr;  // Sera défini par pipeline
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mMsPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipelineColorBlendAttachmentState opaqueBlendAttachment{};
    opaqueBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    opaqueBlendAttachment.blendEnable = VK_TRUE;
    opaqueBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    opaqueBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    opaqueBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    opaqueBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    opaqueBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    opaqueBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo opaqueColorBlending{};
    opaqueColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    opaqueColorBlending.logicOpEnable = VK_FALSE;
    opaqueColorBlending.logicOp = VK_LOGIC_OP_COPY;
    opaqueColorBlending.attachmentCount = 1;
    opaqueColorBlending.pAttachments = &opaqueBlendAttachment;

    pipelineInfo.pColorBlendState = &opaqueColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mMsPipeline.pipelineOpaque);

	// 12. Pipeline transparent
    VkPipelineColorBlendAttachmentState transparentBlendAttachment{};
    transparentBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    transparentBlendAttachment.blendEnable = VK_TRUE;
    transparentBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    transparentBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    transparentBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    transparentBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo transparentColorBlending{};
    transparentColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    transparentColorBlending.logicOpEnable = VK_FALSE;
    transparentColorBlending.logicOp = VK_LOGIC_OP_COPY;
    transparentColorBlending.attachmentCount = 1;
    transparentColorBlending.pAttachments = &transparentBlendAttachment;

    pipelineInfo.pColorBlendState = &transparentColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mMsPipeline.pipelineTransparent);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateMsDescriptors();
}
void Model::CreateMsDescriptors()
{
    mMsMatrixUBO.resize(g_FramesInFlight);
    mMsLightUBO.resize(g_FramesInFlight);
    mMsViewUBO.resize(g_FramesInFlight);
    mMsPipeline.descSet.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        mMsMatrixUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixUBO));
        mMsLightUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sLightUBO));
        mMsViewUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sViewUBO));
    }

    array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;          // UBOs
    poolSizes[0].descriptorCount = 12;                              // 12 UBOs max
   
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;  // Textures
    poolSizes[1].descriptorCount = 3;                               // 3 textures max

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = g_FramesInFlight;                                           // 2 descriptor sets (for 2 frames)
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mMsPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mMsPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mMsPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mMsPipeline.descSet.data());

    UpdateMsDescriptors();
}
void Model::UpdateMsDescriptors()
{
    // Filling the 2 identical sets
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        array<VkWriteDescriptorSet, 3> writes{};

        // 0. MVP UBO (binding 0)
        VkDescriptorBufferInfo mvpInfo{};
        mvpInfo.buffer = mMsMatrixUBO[i]->buffer;
        mvpInfo.offset = 0;
        mvpInfo.range = VK_WHOLE_SIZE;
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mMsPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &mvpInfo;

        // 1. Light UBO (binding 1)
        VkDescriptorBufferInfo lightViewInfo{};
        lightViewInfo.buffer = mMsLightUBO[i]->buffer;
        lightViewInfo.offset = 0;
        lightViewInfo.range = VK_WHOLE_SIZE; 
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mMsPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightViewInfo;

        // 2. CamPos UBO (binding 2)
        VkDescriptorBufferInfo camInfo{};
        camInfo.buffer = mMsViewUBO[i]->buffer;
        camInfo.offset = 0;
        camInfo.range = VK_WHOLE_SIZE;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mMsPipeline.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &camInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    mHasMsPipeline = true;
}
// Rendering multisample simple
void Model::UpdateMsUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, float envmapFactor, float specularIntensity)
{
    vec3 camPos = camera.GetPosition();
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();

    if (!IsVisibleInFrustum(proj * view * model))
        return;

    // Update mesh centers
    for (auto& mesh : mvMeshes)
    {
        vec4 worldCenter = model * vec4(mesh.Center, 1.0f);
        mesh.TransformedCenter = vec3(worldCenter);
    }
    // Sort transparent meshes by decreasing distance
    std::sort(mvTransparentMeshes.begin(), mvTransparentMeshes.end(),
        [&](Mesh* a, Mesh* b) {
            float distA = glm::distance(camPos, a->TransformedCenter);
            float distB = glm::distance(camPos, b->TransformedCenter);
            return distA > distB; // farther first
        }
    );

    // === DIRECT PERSISTENT MAPPED WRITE (0 map/unmap) ===

    // 1. MVP Uniform Buffer
    sMatrixUBO* ubo = static_cast<sMatrixUBO*>(mMsMatrixUBO[currentImage]->data);
    *ubo = { model, view, proj };

    // 2. Light Buffer
    sLightUBO* lightData = static_cast<sLightUBO*>(mMsLightUBO[currentImage]->data);
    lightData->position = sky->SunPosition;
    lightData->exposure = sky->Exposure;
    lightData->ambient = sky->SunAmbient;
    lightData->envmapFactor = envmapFactor;
    lightData->diffuse = sky->SunDiffuse;
	lightData->mistDensity = sky->MistDensity;
    lightData->specular = sky->SunSpecular;
    lightData->specularIntensity = specularIntensity;

    // 3. Camera Position Buffer
    sViewUBO* camData = static_cast<sViewUBO*>(mMsViewUBO[currentImage]->data);
    camData->position = camPos;
}
void Model::RenderMs(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    RenderMsOpaque(cmd, iCurrentFrame);
    if (HasTransparency)
        RenderMsTransparent(cmd, iCurrentFrame);
}
void Model::RenderMsOpaque(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMsPipeline.pipelineOpaque);

    // Set 0: GLOBAL (MVP + Light)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMsPipeline.pipelineLayout, 0, 1, &mMsPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvOpaqueMeshes)
    {
        // Set 1: TEXTURE SPECIFIC TO THE MESH
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMsPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);

        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mMsPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh->Material);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}
void Model::RenderMsTransparent(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMsPipeline.pipelineTransparent);

    // Set 0: GLOBAL (MVP + Light)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMsPipeline.pipelineLayout, 0, 1, &mMsPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvTransparentMeshes)
    {
        // Set 1: TEXTURE SPECIFIC TO THE MESH
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mMsPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);

        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mMsPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh->Material);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}

// Pipeline multisample complex, shadow & environment map
void Model::CreateCxPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Previous cleanup
	mCxPipeline.destroy(mVulkanDevice->device);
    
    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_complex.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/model_complex.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    auto attributeDescriptions = sVertex::getAttributeDescriptions();
    auto bindingDescription = sVertex::getBindingDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 3. DescriptorSetLayout
    array<VkDescriptorSetLayoutBinding, 5> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },             // UBO / vertex / Matrix
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },           // UBO / Fragment / Light
        { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },           // UBO / Fragment / CamPos
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },   // TEX / Fragment / envMap
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }    // TEX / Fragment / shadowMap
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mCxPipeline.descSetLayout);

    // 4. PipelineLayout
    array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        mCxPipeline.descSetLayout,  // Set 0: MVP + Light + CamPos
        mTexMeshDescSetLayout       // Set 1: Texture mesh
    };

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(sMaterial);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = pipelineLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mCxPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport = { 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, swapChainExtent };

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
    rasterizer.cullMode = mCullMode;
    rasterizer.frontFace = mFrontFace;
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

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // CREATION OF TWO PIPELINES

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
    pipelineInfo.pColorBlendState = nullptr;  // Sera défini par pipeline
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mCxPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    // 12. Pipeline opaque
    VkPipelineColorBlendAttachmentState opaqueBlendAttachment{};
    opaqueBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    opaqueBlendAttachment.blendEnable = VK_TRUE;
    opaqueBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    opaqueBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    opaqueBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    opaqueBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    opaqueBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    opaqueBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo opaqueColorBlending{};
    opaqueColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    opaqueColorBlending.logicOpEnable = VK_FALSE;
    opaqueColorBlending.logicOp = VK_LOGIC_OP_COPY;
    opaqueColorBlending.attachmentCount = 1;
    opaqueColorBlending.pAttachments = &opaqueBlendAttachment;

    pipelineInfo.pColorBlendState = &opaqueColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mCxPipeline.pipelineOpaque);

	// 12. Pipeline transparent
    VkPipelineDepthStencilStateCreateInfo depthStencilTransparent = depthStencil; // copie
    depthStencilTransparent.depthWriteEnable = VK_FALSE;  // ← NE PAS écrire le depth des vitres
    pipelineInfo.pDepthStencilState = &depthStencilTransparent;

    VkPipelineColorBlendAttachmentState transparentBlendAttachment{};
    transparentBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    transparentBlendAttachment.blendEnable = VK_TRUE;
    transparentBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    transparentBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    transparentBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    transparentBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    transparentBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo transparentColorBlending{};
    transparentColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    transparentColorBlending.logicOpEnable = VK_FALSE;
    transparentColorBlending.logicOp = VK_LOGIC_OP_COPY;
    transparentColorBlending.attachmentCount = 1;
    transparentColorBlending.pAttachments = &transparentBlendAttachment;

    pipelineInfo.pColorBlendState = &transparentColorBlending;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mCxPipeline.pipelineTransparent);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateCxDescriptors();
}
void Model::CreateCxDescriptors()
{
    mCxMatrixUBO.resize(g_FramesInFlight);
    mCxLightUBO.resize(g_FramesInFlight);
    mCxViewUBO.resize(g_FramesInFlight);
    mCxPipeline.descSet.resize(g_FramesInFlight);

    // UBO
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        mCxMatrixUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixShadowUBO));
        mCxLightUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sLightUBO));
        mCxViewUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sViewUBO));
    }
    
    // Pool : UBO + 2 samplers
    array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 3 * g_FramesInFlight;   // 3 UBO (matrix, light, view) × frames

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 2 * g_FramesInFlight;   // 2 TEX (envMap, shadowMap) x frames

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mCxPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mCxPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mCxPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mCxPipeline.descSet.data());

    UpdateCxDescriptors();
}
void Model::UpdateCxDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        // 0 : matrices
        VkDescriptorBufferInfo matrixInfo{};
        matrixInfo.buffer = mCxMatrixUBO[i]->buffer;
        matrixInfo.offset = 0;
        matrixInfo.range = sizeof(sMatrixShadowUBO);

        // 1 : light
        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = mCxLightUBO[i]->buffer;
        lightInfo.offset = 0;
        lightInfo.range = sizeof(sLightUBO);

        // 2 : view / camPos
        VkDescriptorBufferInfo viewInfo{};
        viewInfo.buffer = mCxViewUBO[i]->buffer;
        viewInfo.offset = 0;
        viewInfo.range = sizeof(sViewUBO);

        // 3 : envMap
        VkDescriptorImageInfo envMapInfo{};
        envMapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        envMapInfo.imageView = mTexEnvMap.imageView;
        envMapInfo.sampler = mTexEnvMap.sampler;

        // 4 : shadowMap
        VkDescriptorImageInfo shadowMapInfo{};
        shadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowMapInfo.imageView = g_TexShadowDepth->imageView;
        shadowMapInfo.sampler = g_TexShadowDepthSampler;

        array<VkWriteDescriptorSet, 5> writes{};

        // binding 0 : matrices
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mCxPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &matrixInfo;

        // binding 1 : light
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mCxPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightInfo;

        // binding 2 : view
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mCxPipeline.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &viewInfo;

        // binding 3 : envMap
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = mCxPipeline.descSet[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &envMapInfo;

        // binding 4 : shadowMap
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = mCxPipeline.descSet[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &shadowMapInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    mHasCxPipeline = true;
}
// Rendering multisample complex with shadow and environment map
void Model::UpdateCxUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, float envmapFactor, float specularIntensity)
{
    vec3 camPos = camera.GetPosition();
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();

    if (!IsVisibleInFrustum(proj * view * model))
        return;

    // Update mesh centers
    for (auto& mesh : mvMeshes)
    {
        vec4 worldCenter = model * vec4(mesh.Center, 1.0f);
        mesh.TransformedCenter = vec3(worldCenter);
    }
    // Sort transparent meshes by decreasing distance
    std::sort(mvTransparentMeshes.begin(), mvTransparentMeshes.end(),
        [&](Mesh* a, Mesh* b) {
            float distA = glm::distance(camPos, a->TransformedCenter);
            float distB = glm::distance(camPos, b->TransformedCenter);
            return distA > distB; // farther first
        }
    );

    // === DIRECT PERSISTENT MAPPED WRITE (0 map/unmap) ===

    // 1. MVP Uniform mCxMatrixUBO
    sMatrixShadowUBO* ubo = static_cast<sMatrixShadowUBO*>(mCxMatrixUBO[currentImage]->data);
    *ubo = { model, view, proj, LightViewProjection };

    // 2. Light Buffer
    sLightUBO* lightData = static_cast<sLightUBO*>(mCxLightUBO[currentImage]->data);
    lightData->position = sky->SunPosition;
    lightData->exposure = sky->Exposure;
    lightData->ambient = sky->SunAmbient;
    lightData->envmapFactor = envmapFactor;
    lightData->diffuse = sky->SunDiffuse;
    lightData->mistDensity = sky->MistDensity;
    lightData->specular = sky->SunSpecular;
    lightData->specularIntensity = specularIntensity;

    // 3. Camera Position Buffer
    sViewUBO* camData = static_cast<sViewUBO*>(mCxViewUBO[currentImage]->data);
    camData->position = camPos;
}
void Model::RenderCx(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    RenderCxOpaque(cmd, iCurrentFrame);
    if (HasTransparency)
        RenderCxTransparent(cmd, iCurrentFrame);
}
void Model::RenderCxOpaque(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCxPipeline.pipelineOpaque);

    // Set 0: GLOBAL (MVP + Light)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCxPipeline.pipelineLayout, 0, 1, &mCxPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvOpaqueMeshes)
    {
        // Set 1: TEXTURE SPECIFIC TO THE MESH
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCxPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);

        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mCxPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh->Material);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}
void Model::RenderCxTransparent(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCxPipeline.pipelineTransparent);

    // Set 0: GLOBAL (MVP + Light)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCxPipeline.pipelineLayout, 0, 1, &mCxPipeline.descSet[iCurrentFrame], 0, nullptr);

    for (const auto* mesh : mvTransparentMeshes)
    {
        // Set 1: TEXTURE SPECIFIC TO THE MESH
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCxPipeline.pipelineLayout, 1, 1, &mesh->DescriptorSet, 0, nullptr);

        // Vertex buffers
        VkBuffer vertexBuffers[] = { mesh->vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh->indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mCxPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh->Material);

        vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
}

// Pipeline wireframe multisample  (same shaders as Ms, polygonMode = LINE, single sub-pipeline, all meshes)
void Model::CreateWireframeMsPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Previous cleanup
    mWireframeMsPipeline.destroy(mVulkanDevice->device);

	// Texture descriptors (Set 1: Per-mesh) — already created if Ms exists, but we recreate them if necessary
    CreateTexMeshDescriptors();
    UpdateTexMeshDescriptors();

    // 1. Shaders (same as Ms)
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_simple.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/model_simple.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (sVertex : pos + normal + texCoord)
    auto attributeDescriptions = sVertex::getAttributeDescriptions();
    auto bindingDescription = sVertex::getBindingDescription();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 3. DescriptorSetLayout (Set 0 : same as Ms)
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,   nullptr },    // Matrix
        { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },    // Light
        { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }     // CamPos
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mWireframeMsPipeline.descSetLayout);

    // 4. PipelineLayout (Set 0 global + Set 1 texture mesh + push material)
    array<VkDescriptorSetLayout, 2> pipelineLayouts = {
        mWireframeMsPipeline.descSetLayout,     // Set 0 : MVP + Light + CamPos
        mTexMeshDescSetLayout                   // Set 1 : Texture mesh
    };

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(sMaterial);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = pipelineLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mWireframeMsPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D   scissor{ {0, 0}, swapChainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer — WIREFRAME MODE
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;     // Les deux faces pour voir toutes les arêtes
    rasterizer.frontFace = mFrontFace;
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

    // 10. Color blending (opaque, no transparency for wireframe)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 1;
    dynamicState.pDynamicStates = dynamicStates;

    // 12. Pipeline (un seul, toutes meshes confondues)
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
    pipelineInfo.layout = mWireframeMsPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mWireframeMsPipeline.pipelineOpaque);

    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);

    CreateWireframeMsDescriptors();
}
void Model::CreateWireframeMsDescriptors()
{
    mWireframeMsMatrixUBO.resize(g_FramesInFlight);
    mWireframeMsLightUBO.resize(g_FramesInFlight);
    mWireframeMsViewUBO.resize(g_FramesInFlight);
    mWireframeMsPipeline.descSet.resize(g_FramesInFlight);

    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        mWireframeMsMatrixUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixUBO));
        mWireframeMsLightUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sLightUBO));
        mWireframeMsViewUBO[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sViewUBO));
    }

    array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 12;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mWireframeMsPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mWireframeMsPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mWireframeMsPipeline.descPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mWireframeMsPipeline.descSet.data());

    UpdateWireframeMsDescriptors();
}
void Model::UpdateWireframeMsDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        array<VkWriteDescriptorSet, 3> writes{};

        // 0. MVP UBO (binding 0)
        VkDescriptorBufferInfo mvpInfo{};
        mvpInfo.buffer = mWireframeMsMatrixUBO[i]->buffer;
        mvpInfo.offset = 0;
        mvpInfo.range = VK_WHOLE_SIZE;
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mWireframeMsPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &mvpInfo;

        // 1. Light UBO (binding 1)
        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = mWireframeMsLightUBO[i]->buffer;
        lightInfo.offset = 0;
        lightInfo.range = VK_WHOLE_SIZE;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mWireframeMsPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightInfo;

        // 2. CamPos UBO (binding 2)
        VkDescriptorBufferInfo camInfo{};
        camInfo.buffer = mWireframeMsViewUBO[i]->buffer;
        camInfo.offset = 0;
        camInfo.range = VK_WHOLE_SIZE;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mWireframeMsPipeline.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &camInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    mHasWireframeMsPipeline = true;
}
// Rendering wireframe multisample
void Model::UpdateWireframeMsUBOs(uint32_t currentImage, Camera& camera, Sky* sky, mat4 model, float envmapFactor, float specularIntensity)
{
    vec3 camPos = camera.GetPosition();
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();

    if (!IsVisibleInFrustum(proj * view * model))
        return;

    // 1. MVP Uniform Buffer
    sMatrixUBO* ubo = static_cast<sMatrixUBO*>(mWireframeMsMatrixUBO[currentImage]->data);
    *ubo = { model, view, proj };

    // 2. Light Buffer
    sLightUBO* lightData = static_cast<sLightUBO*>(mWireframeMsLightUBO[currentImage]->data);
    lightData->position = sky->SunPosition;
    lightData->exposure = sky->Exposure;
    lightData->ambient = sky->SunAmbient;
    lightData->envmapFactor = envmapFactor;
    lightData->diffuse = sky->SunDiffuse;
    lightData->mistDensity = sky->MistDensity;
    lightData->specular = sky->SunSpecular;
    lightData->specularIntensity = specularIntensity;

    // 3. Camera Position Buffer
    sViewUBO* camData = static_cast<sViewUBO*>(mWireframeMsViewUBO[currentImage]->data);
    camData->position = camPos;
}
void Model::RenderWireframeMs(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframeMsPipeline.pipelineOpaque);

    // Set 0: GLOBAL (MVP + Light + CamPos)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframeMsPipeline.pipelineLayout, 0, 1, &mWireframeMsPipeline.descSet[iCurrentFrame], 0, nullptr);

    // All meshes (opaque + transparent) : wireframe does not distinguish
    for (const auto& mesh : mvMeshes)
    {
        // Set 1: TEXTURE SPECIFIC TO THE MESH
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframeMsPipeline.pipelineLayout, 1, 1, &mesh.DescriptorSet, 0, nullptr);

        VkBuffer     vertexBuffers[] = { mesh.vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        // Push material
        vkCmdPushConstants(cmd, mWireframeMsPipeline.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sMaterial), &mesh.Material);

        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    }
}

// Pipeline colored wireframe (no texture, uniform color, same approach as Bbox but on the model's meshes)
void Model::CreateWireframeColorPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    // Previous cleanup
    mWireframeColorPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders (reuse Bbox shaders: vert MVP, frag uniform color)
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_bbox.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/model_bbox.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input — only the position is necessary (Bbox shader expects location 0 = pos)
    //    We use sVertex but only declare the pos attribute to share the same vertex buffers as the meshes without creating new buffers.
    auto bindingDescription = sVertex::getBindingDescription();

    VkVertexInputAttributeDescription posAttribute{};
    posAttribute.binding = 0;
    posAttribute.location = 0;
    posAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttribute.offset = offsetof(sVertex, pos);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &posAttribute;

    // 3. DescriptorSetLayout (sMatrixColorUBO : MVP + color)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr }
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mWireframeColorPipeline.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mWireframeColorPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mWireframeColorPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D   scissor{ {0, 0}, swapChainExtent };

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer — WIREFRAME MODE
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;  // ← Wireframe
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

    // 10. Color blending (opaque)
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
    pipelineInfo.layout = mWireframeColorPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mWireframeColorPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateWireframeColorDescriptors();
}
void Model::CreateWireframeColorDescriptors()
{
    mWireframeColorPipeline.descSet.resize(g_FramesInFlight);
    mWireframeColorPipeline.ubo.resize(g_FramesInFlight);
    
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mWireframeColorPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixColorUBO));

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(g_FramesInFlight) }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = static_cast<uint32_t>(g_FramesInFlight);
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mWireframeColorPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mWireframeColorPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mWireframeColorPipeline.descPool;
    allocInfo.descriptorSetCount = g_FramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mWireframeColorPipeline.descSet.data());

    UpdateWireframeColorDescriptors();
}
void Model::UpdateWireframeColorDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mWireframeColorPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mWireframeColorPipeline.ubo[i]->GetSize();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mWireframeColorPipeline.descSet[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &write, 0, nullptr);
    }
    mHasWireframeColorPipeline = true;
}
// Rendering colored wireframe
void Model::RenderWireframeColor(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, mat4 model, vec4 color)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    // Writing to the UBO of the current frame
    sMatrixColorUBO* ubo = static_cast<sMatrixColorUBO*>(mWireframeColorPipeline.ubo[iCurrentFrame]->data);
    *ubo = { model, camera.GetView(), camera.GetProjection(), color };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframeColorPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframeColorPipeline.pipelineLayout, 0, 1, &mWireframeColorPipeline.descSet[iCurrentFrame], 0, nullptr);

    // All meshes of the model
    for (const auto& mesh : mvMeshes)
    {
        VkBuffer     vertexBuffers[] = { mesh.vertexBuffer->buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    }
}

// Pipeline of the bounding box
void Model::CreateBboxBuffers()
{
    vkDeviceWaitIdle(mVulkanDevice->device); 
    mBboxVertexBuffer.reset();
    mBboxIndexBuffer.reset();

    // 8 vertices of the BBox
    vector<sBboxVertex> vertices(8);
    vertices[0] = { mBbox.min };                                    // 0: min
    vertices[1] = { vec3(mBbox.max.x, mBbox.min.y, mBbox.min.z) };  // 1: x+
    vertices[2] = { vec3(mBbox.max.x, mBbox.max.y, mBbox.min.z) };  // 2: x+y+
    vertices[3] = { vec3(mBbox.min.x, mBbox.max.y, mBbox.min.z) };  // 3: y+
    vertices[4] = { vec3(mBbox.min.x, mBbox.min.y, mBbox.max.z) };  // 4: z+
    vertices[5] = { vec3(mBbox.max.x, mBbox.min.y, mBbox.max.z) };  // 5: x+z+
    vertices[6] = { mBbox.max };                                    // 6: x+y+z+
    vertices[7] = { vec3(mBbox.min.x, mBbox.max.y, mBbox.max.z) };  // 7: y+z+

    // 24 indices for 12 edges (each edge = 2 indices)
    vector<uint32_t> indices = {
        0,1, 1,2, 2,3, 3,0,  // Front face (Z min)
        4,5, 5,6, 6,7, 7,4,  // Back face (Z max)
        0,4, 1,5, 2,6, 3,7   // Side connections
    };

    // Vertex buffer
    VkDeviceSize vertexSize = sizeof(sBboxVertex) * vertices.size();
    VulkanBuffer stagingBuffer(mVulkanDevice, vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(mVulkanDevice->device, stagingBuffer.bufferMemory, 0, vertexSize, 0, &data);
    memcpy(data, vertices.data(), vertexSize);
    vkUnmapMemory(mVulkanDevice->device, stagingBuffer.bufferMemory);

    mBboxVertexBuffer = std::make_unique<VulkanBuffer>(mVulkanDevice, vertexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mBboxVertexBuffer->CopyIntoBuffer(stagingBuffer, vertexSize);

    // Index buffer
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();
    VulkanBuffer stagingIndexBuffer(mVulkanDevice, indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkMapMemory(mVulkanDevice->device, stagingIndexBuffer.bufferMemory, 0, indexSize, 0, &data);
    memcpy(data, indices.data(), indexSize);
    vkUnmapMemory(mVulkanDevice->device, stagingIndexBuffer.bufferMemory);

    mBboxIndexBuffer = std::make_unique<VulkanBuffer>(mVulkanDevice, indexSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mBboxIndexBuffer->CopyIntoBuffer(stagingIndexBuffer, indexSize);
}
void Model::CreateBboxPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent)
{
    CreateBboxBuffers();

    // Previous cleanup
	mBboxPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Model/model_bbox.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Model/model_bbox.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input (position only)
    auto bindingDescription = sBboxVertex::getBindingDescription();
    auto attributeDescriptions = sBboxVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();


    // 3. DescriptorSetLayout (sMatrixColorUBO)
    array<VkDescriptorSetLayoutBinding, 1> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr }
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mBboxPipeline.descSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mBboxPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mBboxPipeline.pipelineLayout);

    // 5. Input assembly (lines)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissor
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
    pipelineInfo.layout = mBboxPipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mBboxPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateBboxDescriptors();
}
void Model::CreateBboxDescriptors()
{
    mBboxPipeline.descSet.resize(g_FramesInFlight);
    mBboxPipeline.ubo.resize(g_FramesInFlight);
    
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mBboxPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixColorUBO));

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(g_FramesInFlight) }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = static_cast<uint32_t>(g_FramesInFlight);// 2 sets
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mBboxPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mBboxPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mBboxPipeline.descPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(g_FramesInFlight);// 2 sets
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mBboxPipeline.descSet.data());

    UpdateBboxDescriptors();
}
void Model::UpdateBboxDescriptors()
{
    for (size_t i = 0; i < g_FramesInFlight; i++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mBboxPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = mBboxPipeline.ubo[i]->GetSize();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = mBboxPipeline.descSet[i];  // Set par frame
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 1, &write, 0, nullptr);
    }
    mHasBboxPipeline = true;
}
// Rendering of the bounding box
void Model::RenderBbox(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, mat4 model, vec4 color)
{
    if (!bVisible) return;

    if (!bIsVisibleInFrustum) return;

    // Writing to the UBO of the current frame
    sMatrixColorUBO* ubo = static_cast<sMatrixColorUBO*>(mBboxPipeline.ubo[iCurrentFrame]->data);
    *ubo = { model, camera.GetView(), camera.GetProjection(), color };

    // Bind the pipeline and the descriptor set of the current frame
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mBboxPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mBboxPipeline.pipelineLayout, 0, 1, &mBboxPipeline.descSet[iCurrentFrame], 0, nullptr);  // Set par frame !

    // Bind vertex/index buffers
    VkBuffer vertexBuffers[] = { mBboxVertexBuffer->buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, mBboxIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, 24, 1, 0, 0, 0);
}

// Recreate all pipelines if SwapChain has changed
void Model::RecreatePipelines(VkRenderPass renderPassScene, VkRenderPass renderPassShadow, VkRenderPass renderPassReflection,  VkRenderPass renderPassBridgeMask, VkExtent2D swapChainExtent)
{
    vkDeviceWaitIdle(mVulkanDevice->device); 
    
    if (mHasBboxPipeline)
        CreateBboxPipeline(renderPassScene, swapChainExtent);

    if (mHasMsPipeline)
    {
        for (int i = 0; i < g_FramesInFlight; i++)
        {
            mMsMatrixUBO[i].reset();
            mMsLightUBO[i].reset();
            mMsViewUBO[i].reset();
        }
        CreateMsPipeline(renderPassScene, swapChainExtent);
    }

    if (mHasShadowPipeline)
    {
        for (int i = 0; i < g_FramesInFlight; i++)
            mShadowMatrixUBO[i].reset();
        CreateShadowPipeline(renderPassShadow, mShadowExtent);
    }
    
    if (mHasReflPipeline)
    {
        for (int i = 0; i < g_FramesInFlight; i++)
        {
            mReflMatrixUBO[i].reset();
            mReflLightUBO[i].reset();
            mReflViewUBO[i].reset();
        }
        CreateReflectionPipeline(renderPassReflection, swapChainExtent);
    }
    
    if (mHasBridgeMaskPipeline)
    {
        for (int i = 0; i < g_FramesInFlight; i++)
            mBridgeMaskMatrixUBO[i].reset();
        CreateBridgeMaskPipeline(renderPassBridgeMask, swapChainExtent);
    }
    
    if (mHasCxPipeline)
    {
        for (int i = 0; i < g_FramesInFlight; i++)
        {
            mCxMatrixUBO[i].reset();
            mCxLightUBO[i].reset();
            mCxViewUBO[i].reset();
        }
        CreateCxPipeline(renderPassScene, swapChainExtent);
    }
    if (mHasWireframeMsPipeline)
    {
        for (int i = 0; i < g_FramesInFlight; i++)
        {
            mWireframeMsMatrixUBO[i].reset();
            mWireframeMsLightUBO[i].reset();
            mWireframeMsViewUBO[i].reset();
        }
        CreateWireframeMsPipeline(renderPassScene, swapChainExtent);
    }

    if (mHasWireframeColorPipeline)
        CreateWireframeColorPipeline(renderPassScene, swapChainExtent);

}