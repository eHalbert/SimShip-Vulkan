#version 450

#define PI  3.1415926535897932

// Set 0 : UBOs globaux au modèle
layout(set = 0, binding = 1) uniform sLight {
    vec3    position;
    float   exposure;
    vec3    ambient;
    float   envmapFactor;
    vec3    diffuse;
    float   mistDensity;
    vec3    specular;
    float   specularIntensity;
} light;
layout(set = 0, binding = 2) uniform sView {
    vec3 position;
} view;
layout(set = 0, binding = 3) uniform sampler2D envMap;
layout(set = 0, binding = 4) uniform sampler2D shadowMap;

// Set 1 : textures spécifiques au mesh
layout(set = 1, binding = 0) uniform sampler2D meshTexture;      // Diffuse / Albedo
layout(set = 1, binding = 1) uniform sampler2D meshCompTexture;  // COMP : R=AO, G=Roughness, B=Metallic

// Push Constant: Matériau par mesh
layout(push_constant) uniform sMaterial {
    vec4    ambient;
    vec4    diffuse;
    vec4    specular;
    vec4    emission;
    float   shininess;
    float   roughness;
    float   metallic;
    float   padding;
} material;

// Vertex Input (du vertex shader)
layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 fragPosLightSpace;

layout(location = 0) out vec4 FragColor;


vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / denom;
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
float ShadowCalculation(vec3 N, vec3 L) {
    vec3 projCoords   = fragPosLightSpace.xyz / fragPosLightSpace.w;
    float currentDepth = projCoords.z;
    vec2 texCoords    = projCoords.xy * 0.5 + 0.5;
    float closestDepth = texture(shadowMap, texCoords).r;
    float bias = 0.005 + 0.001 * (1.0 - dot(normalize(N), normalize(L)));
    return currentDepth > closestDepth + bias ? 1.0 : 0.0;
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


void main() 
{
    vec3 N = normalize(normal);
    vec3 V = normalize(view.position - fragPos);
    vec3 L = normalize(light.position - fragPos);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // --- Texture diffuse (ou couleur matériau si dummy 1x1) ---
    ivec2 texSize = textureSize(meshTexture, 0);
    bool isDummyTexture = (texSize.x < 10 && texSize.y < 10);

    vec4 materialColor;
    if (isDummyTexture)
        materialColor = material.diffuse;
    else
        materialColor = texture(meshTexture, texCoord);

    // --- Texture COMP (R=AO, G=Roughness, B=Metallic) ---
    //     Si dummy (1x1 noir) : utilise les valeurs scalaires du matériau.
    ivec2 compSize    = textureSize(meshCompTexture, 0);
    bool  hasCompTex  = (compSize.x >= 10 && compSize.y >= 10);
    vec3  compSample  = hasCompTex ? texture(meshCompTexture, texCoord).rgb : vec3(0.0);

    float ao        = hasCompTex ? compSample.r : 1.0;               // R : Ambient Occlusion (1 = pas d'occlusion)
    float roughness = hasCompTex ? compSample.g : material.roughness; // G : Roughness
    float metallic  = hasCompTex ? compSample.b : material.metallic;  // B : Metallic

    // --- Cook-Torrance BRDF ---
    vec3 F0 = mix(vec3(0.04), materialColor.rgb, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  nominator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL;
    vec3  specular    = light.specular * (nominator / max(denominator, 0.001)) * light.specularIntensity * NdotL;

    // --- Ambient (modulé par AO) ---
    vec3 ambient = light.ambient * materialColor.rgb * ao;

    // --- Diffuse ---
    vec3 kS    = F;
    vec3 kD    = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = light.diffuse * kD * materialColor.rgb * NdotL;

    // --- Emission ---
    vec3 emission = material.emission.rgb;

    // --- Shadow ---
    float shadow = ShadowCalculation(N, L);

    // --- Résultat HDR ---
    vec3 hdrColor = ambient + (1.0 - shadow) * (diffuse + specular + emission);

    // --- Réflexion environnementale via HDRI ---
    if (light.envmapFactor > 0.0) 
    {
        vec3  R      = reflect(-V, N);
        float phi    = atan(R.z, R.x);
        float theta  = acos(R.y);
        vec2  envUV  = vec2(phi * 0.5 / PI + 0.5, theta * 0.5 / PI);
        vec3  envColor = texture(envMap, envUV).rgb;
        float envReflect = light.envmapFactor * clamp(metallic + (1.0 - roughness), 0.0, 1.0);
        hdrColor += envColor * envReflect;
    }

    // Mist
    if (light.mistDensity > 0.0)
    {
        float linearDepth = length(view.position - fragPos);
        float mistFactor = 1.0 - exp(-light.mistDensity * linearDepth);
        mistFactor = clamp(mistFactor, 0.0, 1.0);
        mistFactor *= mistGain(light.exposure);
        hdrColor.rgb = mix(hdrColor.rgb, vec3(1.0), mistFactor);
    }

    hdrColor = pow(hdrColor, vec3(1.3));
    hdrColor = max(hdrColor, vec3(0.001));

    // --- Post-processing ---
    FragColor.rgb = vec3(1.0) - exp(-hdrColor * light.exposure);
    FragColor.rgb = ACESFilm(FragColor.rgb * light.exposure);
    FragColor.a   = materialColor.a;
}
