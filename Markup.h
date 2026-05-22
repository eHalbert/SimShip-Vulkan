/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#pragma once

// 1. PROJET
#include "Utility.h"
#include "Camera.h"
#include "Model.h"
#include "Ocean.h"
#include "Light.h"

// 2. LIB
#include <vulkan/vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
using namespace glm;
#include "pugixml/pugixml.hpp"
#ifdef _DEBUG
#pragma comment(lib, "pugixml/Debug/pugixml.lib")
#else
#pragma comment(lib, "pugixml/Release/pugixml.lib")
#endif

// 3. WIN
#include <iostream>
#include <fstream>
#include <string>
#include <list>
#include <vector>
#include <limits>


struct sMark
{
    wstring name;
    vec3    pos;
    wstring colour;
    int     boyshp;
    int     bcnshp;
    int     cardinal;
    int     lateral;
    int     landmark;
    int     mooring;
    int     idxModel = -1;
    double  time;
    vec3    lightColor;
    bool    isInViewFrustum;
    vec3    waterPosition;
    unique_ptr<Model> model;
};

// File must have Lon and Lat with dots nor decimal

class Markup
{
public:
    Markup(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, wstring fullname);
    ~Markup();

    void Render(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, Ocean* ocean, Sky* sky);
    void RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights = false);

    void RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent);

    bool bVisible = true;

private:

    void LoadMarksFromXML(const wstring filename);

    shared_ptr<VulkanDevice>mVulkanDevice;
    VkRenderPass			mRenderPass;
    VkExtent2D				mExtent;

    vector<sMark>           mvMarks;
    vector<vector<int>>     vvPatterns;
    
    unique_ptr<Light>		mLight;

    const vector<string>    mvModelPaths = {
    "Resources/Buoys/Boy-North.gltf",
    "Resources/Buoys/Boy-East.gltf",
    "Resources/Buoys/Boy-South.gltf",
    "Resources/Buoys/Boy-West.gltf",
    "Resources/Buoys/Boy-Portside.gltf",
    "Resources/Buoys/Boy-Starboard.gltf",
    "Resources/Buoys/Boy-Danger.gltf",
    "Resources/Buoys/Bcn-North.gltf",
    "Resources/Buoys/Bcn-East.gltf",
    "Resources/Buoys/Bcn-South.gltf",
    "Resources/Buoys/Bcn-West.gltf",
    "Resources/Buoys/Bcn-Portside.gltf",
    "Resources/Buoys/Bcn-Starboard.gltf",
    "Resources/Buoys/Bcn-Danger.gltf",
    "Resources/Buoys/Boy-Mooring.gltf",
    };
};

/*
File lut.json in BALISAGE.z
{
    "BCNSHP": {
        "0": "Forme de balise",
        "1": "Perche, jalon, pieu",
        "2": "Branche",
        "3": "Tourelle",
        "4": "Balise à treillis",
        "5": "Balise pilier",
        "6": "Cairn",
        "7": "Balise à flotteurs"
    },
    "BOYSHP": {
        "0": "Forme de bouée",
        "1": "Cônique",
        "2": "Cylindrique",
        "3": "Sphérique",
        "4": "Pylône, charpente",
        "5": "Espar, fuseau",
        "6": "Bouée tonne",
        "7": "Super-bouée/bouée géante",
        "8": "Pour région glaciaire"
    },
    "BUISHP": {
        "0": "Forme de bâtiment",
        "1": "{no specific shape},",
        "2": "{tower},",
        "3": "{spire},",
        "4": "{cupola (dome) },",
        "5": "Immeuble haut",
        "6": "Pyramide",
        "7": "Cylindrique",
        "8": "Sphérique",
        "9": "Cubique",
        "701": "UNKNOWN",
        "703": "Not Applicable",
        "704": "Autre"
    },
    "CATCAM": {
        "0": "Catégorie de marque cardinale",
        "1": "Marque cardinale nord",
        "2": "Marque cardinale est",
        "3": "Marque cardinale sud",
        "4": "Marque cardinale ouest",
        "701": "UNKNOWN",
        "703": "Not Applicable"
    },
    "CATLAM": {
        "0": "Catégorie de marque latérale",
        "1": "Latérale bâbord",
        "2": "Latérale tribord",
        "3": "Chenal préféré à tribord",
        "4": "Chenal préféré à bâbord",
        "701": "UNKNOWN",
        "703": "Not Applicable"
    },
    "CATLMK": {
        "0": "Catégorie d'amers",
        "1": "Cairn",
        "2": "Cimetière",
        "3": "Cheminée",
        "4": "Antenne à réflecteur",
        "5": "Mât de pavillon",
        "6": "Torchère",
        "7": "Mât, pilier",
        "8": "Manche à air",
        "9": "Monument",
        "10": "Colonne",
        "11": "Plaque commémorative",
        "12": "Obélisque",
        "13": "Statue",
        "14": "Croix",
        "15": "Dôme",
        "16": "Antenne radar",
        "17": "Tour",
        "18": "Moulin à vent",
        "19": "Eolienne",
        "20": "Flèche/minaret",
        "21": "Rocher/bloc rocheux à terre",
        "22": "rock pinnacle",
        "701": "UNKNOWN",
        "702": "Multiple",
        "703": "Not Applicable",
        "704": "Autre"
    },
    "CATMOR": {
        "0": "Catégorie de dispositif d'amarrage",
        "1": "Duc d'Albe/dauphin",
        "2": "Duc d'Albe pour la régulation des compas",
        "3": "Bollard",
        "4": "Mur d'amarrage",
        "5": "Poteau/pilier",
        "6": "Chaine/câble",
        "7": "Coffres ou bouées d'amarrage",
        "501": "fast patrol boat waiting position",
        "701": "UNKNOWN",
        "703": "Not Applicable",
        "704": "Autre"
    },
    "COLOUR": {
        "0": "Couleur",
        "1": "Blanc",
        "2": "Noir",
        "3": "Rouge",
        "4": "Vert",
        "5": "Bleu",
        "6": "Jaune",
        "7": "Gris",
        "8": "Marron",
        "9": "Ambre",
        "10": "Violet",
        "11": "Orange",
        "12": "Magenta ",
        "13": "Rose",
        "701": "UNKNOWN",
        "702": "Multiple",
        "703": "Not Applicable",
        "704": "Autre"
    },
}*/
