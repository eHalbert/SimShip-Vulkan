#version 450

#define ONE_OVER_4PI 0.0795774715459476
#define PI 3.14159265

layout (location = 0) in vec3   vdir;
layout (location = 1) in vec2   tex;
layout (location = 2) in vec3   vertex;
layout (location = 3) in float  lod;
layout (location = 4) in float  kelvinFoam;
layout (location = 5) in vec4   fragPosLightSpace;

layout(std140, binding = 0) uniform OceanUBO {
    mat4    matViewProj;
    vec3    eyePos;
    int     bEnvmap;
    mat4    lightSpaceMatrix;
    vec3    oceanColor;
    float   transparency;
    vec3    sunColor;
    float   time;
    vec3    sunDir;
    float   exposure;
    vec3    shipPosition;
    float   shipRotation;
    int     bKelvinWakes;
    float   amplitude;
    float   kelvinScale;
    float   centerFore;
    int     bShowPatches;
    int     bShowShadow;
    int     bShowReflection;
    int     bShowWake;
    vec2    shipSize;
    vec2	shipPivot;
    vec2    wakeSize;
	int     texLayer;
    float   mistDensity;
} ubo;
layout (binding = 3) uniform sampler2D  gradients;
layout (binding = 4) uniform sampler2D  foamBuffer;
layout (binding = 5) uniform sampler2D  foamIntensity;
layout (binding = 6) uniform sampler2D  foamBubbles;
layout (binding = 7) uniform sampler2D  foamTexture;
layout (binding = 8) uniform sampler2D  envmap;
layout (binding = 9) uniform sampler2D  reflectionTex;
layout (binding = 10) uniform sampler2D waterdUdV;
layout (binding = 11) uniform sampler2D shadowMap;
layout (binding = 12) uniform sampler2D contourShip;
layout (binding = 13) uniform sampler2D	wakeBuffer;

layout (location = 0) out vec4 FragColor;

vec2 RotatePoint(vec2 point, vec2 pivot, float angle) 
{
    float s = sin(angle);
    float c = cos(angle);
    vec2 translated = point - pivot;
    vec2 rotated = vec2( translated.x * c - translated.y * s, translated.x * s + translated.y * c );
    return rotated + pivot;
}
float ShadowCalculation(vec3 N, vec3 L) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    float currentDepth = projCoords.z;                              // Z profondeur brute [0,1] (déjà corrigée par depthFix)
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;                     // XY texture coords [0,1]
    
    if(projCoords.z > 1.0 || projCoords.x < -1.0 || projCoords.x > 1.0 || projCoords.y < -1.0 || projCoords.y > 1.0)
        return 0.0;

    float closestDepth = texture(shadowMap, texCoords).r;
    float bias = 0.005;
    return currentDepth - bias > closestDepth ? 0.2 : 0.0;
}
vec3 SampleEquirectangular(vec3 dir) {
    float u = 0.5 + (atan(dir.z, dir.x) / (2.0 * PI));
    float v = 0.5 - (asin(clamp(dir.y, -1.0, 1.0)) / PI);
    return texture(envmap, vec2(u, v)).rgb * 0.5;
}
vec4 AddFoamWithBubbles(vec2 uv, float factor) {
	vec2 dx = dFdx(uv);
	vec2 dy = dFdy(uv);

	// Extract only the white component of the foam texture
	vec4 foamTexture = textureGrad(foamTexture, tex * 20.0, dx, dy);
	float foamWhiteness = max(foamTexture.r, max(foamTexture.g, foamTexture.b));
			
	// Extract the drawing from the foam
	vec4 foamBubblesTexture = textureGrad(foamBubbles, tex * 20.0, dx, dy);
	vec4 foamColor = vec4(1.0) * (1.0 + factor * foamBubblesTexture);
			
	// Mix the original color with the white foam
	return mix(FragColor, foamColor, foamWhiteness * factor);
}
vec4 AddFoamForWaves(float factor, vec3 baseColor) {
    vec2 dx = dFdx(tex);
    vec2 dy = dFdy(tex);
    // Extract only the white component of the foam texture
    vec4 foamSample = textureGrad(foamIntensity, tex * 20.0, dx, dy);
    float foamLuminance = max(foamSample.r, max(foamSample.g, foamSample.b));
    // Dessin des bulles
    vec4 foamBubblesSample = textureGrad(foamBubbles, tex * 20.0, dx, dy);
    // Gradient élevé : motifs détaillés de bulles ; faible : motifs foam simples
    float gradientThreshold = 0.7;
    float highGradientMask = step(gradientThreshold, factor);
    vec4 selectedFoam = mix(foamSample, foamBubblesSample, highGradientMask);
    float selectedLuminance = max(selectedFoam.r, max(selectedFoam.g, selectedFoam.b));
    // Blanc pur multiplié par factor
    vec4 whiteFoam = vec4(selectedLuminance);
    return mix(vec4(baseColor, 1.0), whiteFoam, selectedLuminance * factor);
}
vec4 AddFoam(vec2 uv, float factor) {
	vec2 dx = dFdx(uv);
	vec2 dy = dFdy(uv);

	// Extract only the white component of the foam texture
	vec4 foamTexture = textureGrad(foamTexture, tex * 20.0, dx, dy);
	float foamWhiteness = max(foamTexture.r, max(foamTexture.g, foamTexture.b));
		
	// Extract the drawing from the foam
	vec4 foamColor = vec4(1.0) * (1.0 + factor * foamTexture);
			
	// Mix the original color with the white foam
	return mix(FragColor, foamColor, foamWhiteness * factor);
}
float mistGain(float exposure)
{
    if (exposure <= 0.33)
        return 0.0;

    if (exposure >= 0.8)
        return 1.0;

    float t = (exposure - 0.33) / (0.8 - 0.33);

    float k = 4.2;
    return (exp(k * t) - 1.0) / (exp(k) - 1.0);
}
vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}


void main()
{
    // Vectors
    vec4 grad = texture(gradients, tex);    // xyz = position, w = jacobian
    vec3 N = normalize(vec3(grad.x, 0.75 * grad.z, grad.y));   // Swapped y and z for correct normal orientation
    vec3 V = normalize(vdir);
    vec3 R = reflect(-V, N);

    // Distance fade
    float dist = distance(ubo.eyePos, vertex);
    float distFactor = exp(-0.001 * dist);

    // Shadow
	vec3 L = normalize(ubo.sunDir);
	float shadow = (ubo.bShowShadow > 0) ? ShadowCalculation(N, L) : 0.0;                      

    // Fresnel
    float F0 = 0.020018673;
    float F = F0 + (1.0 - F0) * pow(1.0 - dot(V, N), 5.0);
    F *= exp(-0.001 * dist);    // Less Fresnel if far away
    
    // Environment reflection (hdr texture)
    vec3 reflection = vec3(0.0);
    if (ubo.bEnvmap == 0) 
    {
        vec3 skyHorizon = mix(ubo.oceanColor, vec3(0.8), 0.5);
        vec3 skyZenith = vec3(0.45, 0.65, 1.0);
        float skyLerp = smoothstep(-0.2, 1.0, R.y) * (1.0 + lod * 0.2);  // Plus flou avec LOD
        reflection = mix(skyHorizon, skyZenith, skyLerp) * 0.6 * (1.0 - shadow);
    }
    else
    {
        float nightFactor = smoothstep(0.1, -0.1, ubo.sunDir.y);  // 0 jour, 1 nuit
        reflection = SampleEquirectangular(R) * (0.1 + 0.9 * (1.0 - nightFactor));
    }

    // Ship reflection
    if (ubo.bShowReflection > 0)
    {
        vec2 screenUV = gl_FragCoord.xy / textureSize(reflectionTex, 0);
        // Noise
	    float grain = 10.0;
	    vec2 distortion1 = texture(waterdUdV, vec2(tex.x - ubo.time, tex.y) * grain).rg * 2.0 - 1.0;
	    vec2 distortion2 = texture(waterdUdV, vec2(tex.x - ubo.time, tex.y - ubo.time) * grain).rg * 2.0 - 1.0;
	    vec2 totalDistortion = distortion1 + distortion2;
	    screenUV += 0.01 * totalDistortion;
        vec3 shipReflection = texture(reflectionTex, screenUV).rgb;
        if (shipReflection != vec3(0.0))	// Apply only the part of the ship
            reflection = mix(reflection, shipReflection, 0.25 * (1.0 - shadow));
    }

    // Turbulence modulation
    float turbulence = max(grad.w + 0.8, 0.0);
    float color_mod = 1.0 + 0.5 * smoothstep(1.2, 1.8, turbulence);

    // Sun effect on water (Ward model)
    float spec = 0.0;
    const float sunSolidAngle = 0.01453859;
    if (ubo.sunDir.y > -sunSolidAngle) 
    {
        float sunVisibility = clamp((ubo.sunDir.y + sunSolidAngle) / (2.0 * sunSolidAngle), 0.0, 1.0);

        vec3 sunDirStable = normalize(vec3(ubo.sunDir.x, max(ubo.sunDir.y, 0.05), ubo.sunDir.z));

        // --- Anisotropy axes ---
        // sunPath : direction from sun toward camera, projected onto the water plane
        // This is the LONG axis of the glitter trail (light column toward the camera)
        vec3 toCamera   = normalize(vec3(V.x, 0.0, V.z));          // horizontal projection of V
        vec3 sunHoriz   = normalize(vec3(sunDirStable.x, 0.0, sunDirStable.z));
        // tangent perpendicular to the trail (short axis, angular width of the sun disk)
        vec3 tangent    = normalize(cross(sunHoriz, N));            // lateral axis (short)
        vec3 bitangent  = normalize(cross(N, tangent));             // radial axis  (long, toward camera)

        // Roughness values:
        // ay_base : minimum angular width = apparent size of the solar disk (~0.27°)
        // ax is wide to stretch the trail all the way to the camera
        float sunElevation = max(ubo.sunDir.y, 0.0);
        float sunElevationFactor = 1.0 / max(sunElevation + 0.05, 0.05);

        // ax = SHORT axis (lateral) — matches the width of the solar disk
        // Slightly widened near the horizon due to atmospheric refraction distorting the disk
        float ax = 0.05 + 0.06 * sunElevationFactor;

        // ay = LONG axis (radial, toward camera) — the trail stretches toward the camera
        // Very wide so the lobe covers the full distance to the camera
        float ay = clamp(0.4 * sunElevationFactor, 0.4, 4.0);

        const float rho = 2.0;

        vec3 h      = normalize(sunDirStable + V);
        float NdotL = max(dot(N, sunDirStable), 0.0);
        float NdotV = max(dot(N, V), 0.0);

        float hdotx = dot(h, tangent)   / ax;   // short axis: lateral
        float hdoty = dot(h, bitangent) / ay;   // long axis : radial (trail)
        float hdotn = max(dot(h, N), 1e-4);

        float mult = (ONE_OVER_4PI * rho) / (ax * ay * sqrt(max(1e-5, NdotL * NdotV)));
        spec = mult * exp(-((hdotx * hdotx) + (hdoty * hdoty)) / (hdotn * hdotn));

        // Visible sun disk (sharp specular highlight at the exact reflection point)
        float sunDisk = pow(max(dot(reflect(-V, N), sunDirStable), 0.0), 256.0);
        spec = max(spec, sunDisk * 2.0);

        spec *= sunVisibility;
        spec *= (1.0 - shadow);
    }
    
    // Submarine refraction + extinction (Beer-Lambert) I = I? × exp(-? × distance)
    vec3 waterExtinction = vec3(0.5, 0.3, 0.2); // L'eau absorbe plus le rouge
    float depth = max(-vertex.y, 0.0);          // Profondeur physique
    vec3 subsurface = ubo.oceanColor * exp(-waterExtinction * depth);
    // BRDF finale avec réfraction
    vec3 brdf = mix(subsurface, reflection * color_mod, F) + ubo.sunColor * spec;
    
    // Base color
    FragColor = vec4(brdf, 1.0);

    // Wake around the ship (texture: contourShip)
	if (ubo.bShowWake > 0)
	{
		// Calculate the position of the fragment in the decal coordinate system
		vec2 decalSpacePos = RotatePoint(vertex.xz, ubo.shipPivot, -ubo.shipRotation);
    
		// Calculate the UV coordinates of the decal
		vec2 wakeUV = (decalSpacePos - ubo.shipPivot + ubo.shipSize * 0.5) / ubo.shipSize;

		// Check that the sampling is in the valid region [0,1]
		if (wakeUV.x >= 0.0 && wakeUV.x <= 1.0 && wakeUV.y >= 0.0 && wakeUV.y <= 1.0) 
		{
			float wakefactor = texture(contourShip, wakeUV).r;
			if (wakefactor > 0.0)
				FragColor = AddFoam(wakeUV, wakefactor);
		}
	}

    // Wake after the ship (texture: wakeBuffer)
	if (ubo.bShowWake > 0)
	{
		// Calculate the UV coordinates of the decal
		vec2 wakeUV = (vertex.xz - ubo.shipPivot + ubo.wakeSize * 0.5) / ubo.wakeSize;
		
		// Check that the sampling is in the valid region [0,1]
		if (wakeUV.x >= 0.0 && wakeUV.x <= 1.0 && wakeUV.y >= 0.0 && wakeUV.y <= 1.0)
		{
			float wakeFactor = texture(wakeBuffer, wakeUV).r;
			if (wakeFactor > 0.0)
				FragColor = AddFoam(wakeUV, wakeFactor);
		}
	}

    // Highest waves are greener and more transparent
    vec3 greenHighlight = vec3(0.0, 0.6, 0.4);  // Green
    float heightFactor = smoothstep(1.0, 4.0, vertex.y) * distFactor;   // Less difference if far away
    FragColor.rgb = mix(FragColor.rgb, greenHighlight, heightFactor);

    // Shadow final global
	FragColor.rgb *= (1.0 - shadow);

    // Foam
	float combinedFoamFactor = max(texture(foamBuffer, tex).r, kelvinFoam);
	if (combinedFoamFactor > 0.0)
		FragColor = AddFoamWithBubbles(tex, combinedFoamFactor * distFactor);   // Less foam if far away

    // Alpha
    float viewAngle = abs(dot(N, V));
    FragColor.a = mix(1.0, 1.0 - ubo.transparency, viewAngle);

    // Show patches with white border
    if (ubo.bShowPatches > 0)
    {
        ivec2 patchCoord = ivec2(tex * 128.0);
    
        // Distance to the edges of the patches (0=center, 1=corner)
        float edgeDistX = min(patchCoord.x, 127 - patchCoord.x) / 128.0;
        float edgeDistY = min(patchCoord.y, 127 - patchCoord.y) / 128.0;
        float edgeDist = min(edgeDistX, edgeDistY);
    
        // Thin white line (1-2 pixels)
        float border = smoothstep(0.0, 0.03, edgeDist);
        vec3 whiteBorder = vec3(1.0, 1.0, 1.0);
    
        vec3 lodColor;
        if (lod < 0.5)      lodColor = vec3(1.0, 0.0, 1.0);     // LOD0 = magenta
        else if (lod < 1.5) lodColor = vec3(0.0, 1.0, 0.0);     // LOD1 = vert
        else if (lod < 2.5) lodColor = vec3(0.0, 0.5, 1.0);     // LOD2 = bleu
        else if (lod < 3.5) lodColor = vec3(1.0, 0.5, 0.0);     // LOD3 = orange
        else                lodColor = vec3(0.0, 1.0, 1.0);     // LOD4 = cyan
    
        // LOD color mix + white border
        FragColor.rgb = mix(FragColor.rgb * lodColor, whiteBorder, 1.0 - border);
    }

    // Mist
    if (ubo.mistDensity > 0.0)
    {
        float mistFactor = 1.0 - exp(-ubo.mistDensity * dist);
        mistFactor = clamp(mistFactor, 0.0, 1.0);
        mistFactor *= mistGain(ubo.exposure);
        FragColor.rgb = mix(FragColor.rgb, vec3(1.0), mistFactor);
    }

    // Post processing
    FragColor.rgb = vec3(1.0) - exp(-FragColor.rgb * ubo.exposure); // Apply dynamic exposure
    FragColor.rgb = ACESFilm(FragColor.rgb);                        // Convert HDR to LDR
}
