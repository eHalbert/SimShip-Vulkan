/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Lighthouses.h"

Lighthouses::Lighthouses(shared_ptr<VulkanDevice> vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, const wstring filename)
{
	mVulkanDevice = vulkanDevice;
	mRenderPass = renderPass;
	mExtent = extent;
	
	LoadFromXML(filename);
	mLight = make_unique<Light>(vulkanDevice, renderPass, extent);
}
Lighthouses::~Lighthouses()
{
	mLight.reset();
}

void Lighthouses::LoadFromXML(const wstring filename)
{
	std::mt19937 rng(static_cast<unsigned int>(time(nullptr)));
	std::uniform_real_distribution<double> dist(0.0, 2.0 * glm::pi<double>());

	mvLighthouses.clear();

	pugi::xml_document doc;

	// Load XML file
	pugi::xml_parse_result result = doc.load_file(filename.c_str());

	if (result)
	{
		// Get the root node
		pugi::xml_node root = doc.child(L"Lighthouses");

		// Browse all "Lighthouse" nodes
		for (pugi::xml_node lhNode : root.children(L"Lighthouse"))
		{
			sLighthouse lh;

			lh.name = lhNode.child(L"Name").text().as_string();
			float lat = lhNode.child(L"Latitude").attribute(L"value").as_float();
			float lon = lhNode.child(L"Longitude").attribute(L"value").as_float();
			float height = lhNode.child(L"Height").attribute(L"value").as_float();
			lh.pos = lonlat_to_opengl(lon, lat);
			lh.pos.y = height;
			lh.range = lhNode.child(L"Range").attribute(L"value").as_float() * 1852.0f * 0.25f;

			wstring t = lhNode.child(L"Type").text().as_string();
			if (t == L"Flash")
				lh.type = eLightType::beam;
			else if (t == L"Occultation")
				lh.type = eLightType::light;

			// vAngles
			pugi::xml_node anglesNode = lhNode.child(L"Angles");
			if (anglesNode)
			{
				std::wstringstream ss(anglesNode.text().as_string());
				float angle;
				while (ss >> angle)
					lh.vAngles.push_back(angle);
			}

			// vColors
			for (pugi::xml_node colorNode : lhNode.child(L"Colors").children(L"color"))
			{
				float r = colorNode.attribute(L"r").as_float();
				float g = colorNode.attribute(L"g").as_float();
				float b = colorNode.attribute(L"b").as_float();
				lh.vColors.push_back(vec3(r, g, b));
			}

			lh.durationOfTurn = lhNode.child(L"DurationOfTurn").attribute(L"value").as_float();
			lh.angleStart = dist(rng);

			if (lh.type == eLightType::beam)
			{
				lh.beam = new Beam();
				lh.beam->Init(mVulkanDevice, mRenderPass, mExtent, lh.range);
			}

			if (lh.vColors.size() > 0)
				mvLighthouses.push_back(lh);
		}
	}
	else
		std::wcerr << L"Error loading XML file: " << result.description() << std::endl;
}
vec3 Lighthouses::GetSectorColor(sLighthouse& lh, float angleDeg)
{
	size_t n = lh.vAngles.size();

	if (n == 0)
		return lh.vColors[0];

	for (size_t i = 0; i < n; ++i)
	{
		float a0 = lh.vAngles[i];
		float a1 = lh.vAngles[(i + 1) % n];
		// Wrap-around 360°
		if (a0 < a1)
		{
			if (angleDeg >= a0 && angleDeg < a1 && i < lh.vColors.size())
				return lh.vColors[i];
		}
		else
		{
			if (i < lh.vColors.size() && (angleDeg >= a0 || angleDeg < a1))
				return lh.vColors[i];
		}
	}
	// Default (no sector)
	return vec3(0.0f, 0.0f, 0.0f);
}
void Lighthouses::RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights)
{
	if (!bVisible)
		return;

	if (!bLights)
		return;
	
	for (auto& lh : mvLighthouses)
	{
		if (lh.type == eLightType::light)
		{
			// Color
			vec3 toCamera = camera.GetPosition() - lh.pos;
			float dirToLighthouse = get_angle_from_north(-toCamera);
			vec3 color = GetSectorColor(lh, dirToLighthouse);
			if (color == vec3(0.0f))	// No light
				return;

			if (camera.IsInViewFrustum(lh.pos))
			{
				// Determination of the occulted phase
				double t = lh.angleStart + glfwGetTime();
				double tInTurn = fmod(t, lh.durationOfTurn);
				const double occultedDuration = 1.0; // duration in seconds during which the fire is extinguished

				if (tInTurn < (lh.durationOfTurn - occultedDuration)) // On most of the time
				{
					mat4 model = glm::translate(mat4(1.0f), lh.pos);

					// Cylindrical billboard - the quad is always facing the camera
					vec3 camRight = vec3(camera.GetView()[0][0], camera.GetView()[1][0], camera.GetView()[2][0]);
					vec3 camUp = vec3(camera.GetView()[0][1], camera.GetView()[1][1], camera.GetView()[2][1]);
					model[0] = vec4(camRight, 0.0f);
					model[1] = vec4(camUp, 0.0f);

					float dCameraToLight = glm::length(camera.GetPosition() - lh.pos);
					// Distance limits and scales
					const float dMin = 0.5f * 1852.0f;      // 1/2 NM
					const float dMax = 9.0f * 1852.0f;      // 9 NM
					const float sMin = 10.0f;
					const float sMax = 30.0f;
					// Linear interpolation with clamp
					float t = (dCameraToLight - dMin) / (dMax - dMin);
					t = glm::clamp(t, 0.0f, 1.0f);          // t in [0,1]
					float scale = sMin + t * (sMax - sMin); // scale in [2,20]
					model = glm::scale(model, glm::vec3(scale));

					mLight->Render(commandBuffer, camera, model, color, 1.0, 0.1f);
				}
			}
		}
	}
}
void Lighthouses::RenderBeams(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights)
{
	if (!bVisible)
		return;

	if (!bLights)
		return;

	for (auto& lh : mvLighthouses)
	{
		if (lh.type == eLightType::beam)
		{
			// Color
			vec3 toCamera = camera.GetPosition() - lh.pos;
			float dirToLighthouse = get_angle_from_north(-toCamera);
			vec3 color = GetSectorColor(lh, dirToLighthouse);
			if (color == vec3(0.0f))	// No light
				return;

			// Rotation
			mat4 model = mat4(1.0f);
			model = glm::translate(model, lh.pos);
			float angle = lh.angleStart + (float)(glfwGetTime() * 2.0f * glm::pi<float>() / lh.durationOfTurn);
			model = glm::rotate(model, angle, vec3(0.0f, 1.0f, 0.0f));

			// Intensity function of the direction of the cone
			mat4 rotMat = glm::rotate(mat4(1), angle, vec3(0, 1, 0));
			vec3 coneDirLocal = vec3(1, 0, 0);
			vec3 coneDirWorld = vec3(rotMat * vec4(coneDirLocal, 0.0));
			float dotInt = glm::clamp(glm::dot(glm::normalize(coneDirWorld), glm::normalize(toCamera)), 0.0f, 1.0f);
			float intensity = 0.5f * pow(dotInt, 10.0f);

			lh.beam->Render(commandBuffer, frame, model, camera.GetView(), camera.GetProjection(), color, intensity);
		}
	}
}

void Lighthouses::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
	mRenderPass = renderPass;
	mExtent = newExtent;

	mLight->RecreatePipelines(renderPass, newExtent);

	for (auto& lh : mvLighthouses)
		if (lh.type == eLightType::beam && lh.beam)
			lh.beam->RecreatePipelines(renderPass, newExtent);
}