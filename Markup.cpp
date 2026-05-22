/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Markup.h"
//#define BOUNDS
#ifdef BOUNDS
extern float XMIN;
extern float XMAX;
extern float ZMIN;
extern float ZMAX;
#endif

Markup::Markup(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, wstring fullname)
{
    mVulkanDevice = vulkanDevice;
    mRenderPass = renderPass;
    mExtent = extent;
    
    // Load zones from XML file
    LoadMarksFromXML(fullname.c_str());

    mLight = make_unique<Light>(vulkanDevice, renderPass, extent);

    // Initialiser le générateur aléatoire avec un seed unique (ici avec l'heure)
    std::mt19937 rng(static_cast<unsigned int>(time(nullptr)));
    std::uniform_real_distribution<double> dist(0.0, 8.0); // Cycle max = 8 secondes

    // Pour chaque bouée, initialiser un décalage de temps aléatoire
    for (auto& mark : mvMarks)
        mark.time = dist(rng); // décalage aléatoire en secondes

    vvPatterns = {
        // Buoy
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0}, // N
        {1, 0, 1, 0, 1, 0, 0, 0},                                                       // E
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0},                                     // S
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0},                   // W
        {1, 1, 0, 0},
        {0, 1, 1, 0},
        {1, 0, 1, 0, 0, 0, 0, 0},
        // Beacon
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0}, // N
        {1, 0, 1, 0, 1, 0, 0, 0},                                                       // E
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0},                                     // S
        {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0},                   // W
        {1, 1, 0, 0},
        {0, 1, 1, 0},
        {1, 0, 1, 0, 0, 0, 0, 0},
        // Mooring
        {1, 0, 1, 0},
    };
}
Markup::~Markup()
{
    mLight.reset();
}

void Markup::Render(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, Ocean* ocean, Sky* sky)
{
    if (!bVisible)
        return;

    for (auto& mark : mvMarks)
    {
        if (camera.IsInViewFrustum(mark.pos))
        {
            mark.isInViewFrustum = true;
            vec3 p;
            if (mark.boyshp || mark.mooring)
            {
                ocean->GetVerticeXYZ(mark.pos, mark.waterPosition);
                p = mark.waterPosition;
            }
            else
                p = mark.pos;

            mat4 model = glm::translate(mat4(1.0f), p);
            mark.model->UpdateMsUBOs(frame, camera, sky, model, 0.0f, 1.0f);
            mark.model->RenderMsOpaque(commandBuffer, frame);
        }
        else
            mark.isInViewFrustum = false;
    }
}
void Markup::RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights)
{
    if (!bLights)
        return;

    for (auto& mark : mvMarks)
    {
        if (mark.isInViewFrustum)
        {
            int patternLength = static_cast<int>(vvPatterns[mark.idxModel].size());
            int currentIndex = static_cast<int>(floor((glfwGetTime() + mark.time) * 2)) % patternLength;
            bool lightOn = vvPatterns[mark.idxModel][currentIndex] == 1;
            if (!lightOn)
                continue;

            vec3 p;
            if (mark.boyshp > 0 || mark.mooring > 0)
                p = mark.waterPosition;
            else
                p = mark.pos;

            if (mark.boyshp)
                p.y += 4.2f;
            else if (mark.bcnshp)
                p.y += 4.5f;
            else if (mark.mooring)
                p.y += 1.4f;

            mat4 model = glm::translate(mat4(1.0f), p);

            // Cylindrical billboard - the quad is always facing the camera
            vec3 camRight = vec3(camera.GetView()[0][0], camera.GetView()[1][0], camera.GetView()[2][0]);
            vec3 camUp = vec3(camera.GetView()[0][1], camera.GetView()[1][1], camera.GetView()[2][1]);
            model[0] = vec4(camRight, 0.0f);
            model[1] = vec4(camUp, 0.0f);

            float scale = 5.0f;
            model = glm::scale(model, vec3(scale, scale, scale));

            mLight->Render(commandBuffer, camera, model, mark.lightColor, 1.0, 0.1f);
        }
    }
}
void Markup::LoadMarksFromXML(const wstring filename)
{
    mvMarks.clear();
    pugi::xml_document doc;

    // Load XML file
    pugi::xml_parse_result result = doc.load_file(filename.c_str());

    if (result)
    {
        // Get the root node
        pugi::xml_node root = doc.child(L"Marks");

        // Browse all "Mark" nodes
        for (pugi::xml_node markNode : root.children(L"Mark"))
        {
            sMark mark;

            // Read the data for each mark
            mark.name = markNode.child(L"Name").text().as_string();
            mark.colour = markNode.child(L"Colour").text().as_string();
            mark.boyshp = markNode.child(L"Boyshp").attribute(L"value").as_int();
            mark.bcnshp = markNode.child(L"Bcnshp").attribute(L"value").as_int();
            mark.cardinal = markNode.child(L"Cardinal").attribute(L"value").as_int();
            mark.lateral = markNode.child(L"Lateral").attribute(L"value").as_int();
            mark.landmark = markNode.child(L"Landmark").attribute(L"value").as_int();
            mark.mooring = markNode.child(L"Mooring").attribute(L"value").as_int();
            float lat = markNode.child(L"Latitude").attribute(L"value").as_float();
            float lon = markNode.child(L"Longitude").attribute(L"value").as_float();

#ifdef BOUNDS
            if (lon < XMIN) XMIN = lon;
            if (lon > XMAX) XMAX = lon;
            if (lat < ZMIN) ZMIN = lat;
            if (lat > ZMAX) ZMAX = lat;
#endif
            mark.pos = lonlat_to_opengl(lon, lat);

            if (mark.boyshp > 0)
            {
                if (mark.cardinal == 1)         mark.idxModel = 0;  // Buoy - North
                else if (mark.cardinal == 2)    mark.idxModel = 1;  // Buoy - East
                else if (mark.cardinal == 3)    mark.idxModel = 2;  // Buoy - South
                else if (mark.cardinal == 4)    mark.idxModel = 3;  // Buoy - West

                else if (mark.lateral == 1)     mark.idxModel = 4;  // Buoy - Portside
                else if (mark.lateral == 2)     mark.idxModel = 5;  // Buoy - Starboard

                else if (mark.colour == L"2,3,2") mark.idxModel = 6;// Buoy - Danger
            }
            if (mark.bcnshp > 0)
            {
                if (mark.cardinal == 1)         mark.idxModel = 7;  // Beacon - North
                else if (mark.cardinal == 2)    mark.idxModel = 8;  // Beacon - East
                else if (mark.cardinal == 3)    mark.idxModel = 9;  // Beacon - South
                else if (mark.cardinal == 4)    mark.idxModel = 10; // Beacon - West

                else if (mark.lateral == 1)     mark.idxModel = 11;  // Beacon - Portside
                else if (mark.lateral == 2)     mark.idxModel = 12;  // Beacon - Starboard

                else if (mark.colour == L"2,3,2") mark.idxModel = 13;// Buoy - Danger
            }
            if (mark.mooring > 0)
                mark.idxModel = 14;// Buoy - Mooring

            switch (mark.idxModel)
            {
            case 4:     // Buoy-Portside
            case 11:    // Bcn-Portside
                mark.lightColor = vec3(1.0f, 0.0f, 0.0f); break;    // Red
            case 5:     // Buoy-Starboard
            case 12:    // Bcn-Starboard
                mark.lightColor = vec3(0.0f, 1.0f, 0.0f); break;    // Green
            case 6:     // Buoy-Danger
            case 13:    // Bcn-Danger
            case 14:    // Boy-Mooring
                mark.lightColor = vec3(1.0f, 1.0f, 0.0f); break;    // Yellow
            default:    // Cardinals
                mark.lightColor = vec3(1.0f, 1.0f, 1.0f); break;    // White
            }

            if (mark.idxModel != -1)
            {
                mark.model = make_unique<Model>(mVulkanDevice);
                mark.model->LoadModel(mvModelPaths[mark.idxModel].c_str(), VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_CULL_MODE_NONE);
                mark.model->CreateMsPipeline(mRenderPass, mExtent);
                mvMarks.push_back(std::move(mark));
            }
        }
    }
    else
    {
        // Handle file loading error
        std::wcerr << L"Error loading XML file: " << result.description() << std::endl;
    }
}

void Markup::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    mRenderPass = renderPass;
    mExtent = newExtent;

    mLight->RecreatePipelines(renderPass, newExtent);

    for (auto& mark : mvMarks)
        if (mark.model)
            mark.model->RecreatePipelines(renderPass, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, newExtent);
}