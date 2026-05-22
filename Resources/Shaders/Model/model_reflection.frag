#version 450
//#extension GL_KHR_vulkan_glsl : enable

// Set 0 : UBOs globaux au modèle
layout(set = 0, binding = 1) uniform sLight {
    vec3    position;
    float   exposure;
    vec3    ambient;
    float   envmapFactor;
    vec3    diffuse;
    float   pad;
    vec3    specular;
    float   specularIntensity;
} light;

layout(set = 0, binding = 2) uniform sView {
    vec3 position;
} view;

// Set 1 : texture spécifique au mesh
layout(set = 1, binding = 0) uniform sampler2D meshTexture;

// Push Constant: Matériau par mesh
layout(push_constant) uniform Material {
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
layout(location = 1) in vec3 outNormal;
layout(location = 2) in vec2 outTexCoord;

layout(location = 0) out vec4 FragColor;

vec3 ACESFilm(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    vec3 mapped = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    return mapped; 
}
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265359 * denom * denom;

    return nom / denom;
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void main() 
{
    vec3 N = normalize(outNormal);
    vec3 V = normalize(view.position - fragPos);      // Direction towards the camera
    vec3 L = normalize(light.position - fragPos);       // Direction towards light (point)
    vec3 H = normalize(V + L);                          // Midway vector

    // Si texture dummy -> utilise couleur matériau, sinon texture
    ivec2 texSize = textureSize(meshTexture, 0);  // Récupère taille exacte
    bool isDummyTexture = (texSize.x < 10 && texSize.y < 10);

    vec4 materialColor;
    if (isDummyTexture)     // Utilise couleur matériau
        materialColor = material.diffuse;  
    else                    // Texture normale
        materialColor = texture(meshTexture, outTexCoord);  
       
    // F0 (reflectivity at normal incidence)
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, materialColor.rgb, material.specular.r);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, material.roughness);      // Normal Distribution Function        
    float G   = GeometrySmith(N, V, L, material.roughness);     // Geometric Shadowing/Masking Function
    vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);        // Fresnel
    
    vec3 nominator    = NDF * G * F;
    float denominator = 4 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular     = nominator / max(denominator, 0.001);  
    specular = light.specular * specular * light.specularIntensity;
   
    // Ambient
    vec3 ambient = light.ambient * materialColor.rgb;

    // Diffuse
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - material.metallic;
    float NdotL = max(dot(N, L), 0.0);        
    vec3 diffuse = light.diffuse * kD * materialColor.rgb * NdotL;
    
    // Emission
    vec3 emission = material.emission.rgb;
    
    // Result HDR
    vec3 hdrColor = ambient + diffuse + specular + emission;

    // Post-traitements identiques à OpenGL
    hdrColor = pow(hdrColor, vec3(1.3));    
    hdrColor = max(hdrColor, vec3(0.001));  

    // Tone mapping ACES
    vec3 mapped = ACESFilm(hdrColor * light.exposure);
    //mapped = pow(mapped, vec3(1.0 / 2.2));

    FragColor = vec4(mapped, materialColor.a);
}
