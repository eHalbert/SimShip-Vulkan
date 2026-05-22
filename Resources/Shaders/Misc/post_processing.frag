#version 450

layout(location = 0) in vec2 tex;

layout(std140, binding = 0) uniform PostProcessingUBO {
    float   exposure; 
    float   zNear;
    float   zFar;
    float   horizonHeight;
    vec3    eyePos;
    int	    bOcean;
    vec3    oceanColor;
    float   fogDensity;
    vec3    fogColor;
    float   uTime;        
    vec2    screenSize;
    int     bLowIntensity;
    int     bNightVision; 
    vec3    sunDirection;
    float   mieExponent;
    vec3    sunColor;
	int     bBinoculars;
    int	    bRainDropsTrails;
    int     bRainBlurDrips;
    int	    bInside;
};
layout(binding = 1) uniform sampler2D texColor;     // g_ColorViewResolve (1x)
layout(binding = 2) uniform sampler2D texDepth;     // g_DepthViewResolve (1x)
layout(binding = 3) uniform usampler2D texStencil;

layout(location = 0) out vec4 FragColor;

const float f =  2.0 * sqrt(2.0);

float hash(vec2 p) {
    return fract(43758.5453 * fract(p.x * 0.3183099 + p.y * 0.3678794));
}

// DROPLETS ========================================

#define S(a, b, t) smoothstep(a, b, t)

vec3 N13(float p) {
    vec3 p3 = fract(vec3(p) * vec3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yzx + 19.19);
    return fract(vec3((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x));
}
float N(float t) {
    return fract(sin(t * 12345.564) * 7658.76);
}
float Saw(float b, float t) {
    return S(0.0, b, t) * S(1.0, b, t);
}
vec2 DropLayer(vec2 uv, float t) {
    vec2 UV = uv;
    uv.y += t * 0.75;
    vec2 a = vec2(6.0, 1.0);
    vec2 grid = a * 3.5;
    vec2 id = floor(uv * grid);
    
    float colShift = N(id.x); 
    uv.y += colShift;
    
    id = floor(uv * grid);
    vec3 n = N13(id.x * 35.2 + id.y * 2376.1);
    vec2 st = fract(uv * grid) - vec2(.5, 0);
    
    float x = n.x - 0.5;
    
    float y = UV.y * 20.0;
    float wiggle = sin(y + sin(y));
    x += wiggle * (0.5 - abs(x)) * (n.z - 0.5);
    x *= 0.7;
    float ti = fract(t + n.z);
    y = (Saw(0.85, ti) -0.5) * 0.9 + 0.5;
    vec2 p = vec2(x, y);
    
    float d = length((st - p) * a.yx);
    float mainDrop = S(0.2, 0.0, d);
    
    float r = sqrt(S(1.0, y, st.y));
    float cd = abs(st.x - x);
    float trail = S(0.23 * r, 0.15 * r * r, cd);
    float trailFront = S(-0.02, 0.02, st.y - y);
    trail *= trailFront * r * r;
    
    y = UV.y;
    float trail2 = S(0.2 * r, .0, cd);
    float droplets = max(0.0, (sin(y * (1.0 - y) * 120.0) - st.y)) * trail2 * trailFront * n.z;
    y = fract(y * 10.0) + (st.y - .5);
    float dd = length(st - vec2(x, y));
    droplets = S(0.3, 0.0, dd);
    float m = mainDrop + droplets * r * trailFront;
    
    return vec2(m, trail);
}
float StaticDrops(vec2 uv, float t) {
    uv *= 40.0;
    
    vec2 id = floor(uv);
    uv = fract(uv) - 0.5;
    vec3 n = N13(id.x * 107.45 + id.y * 3543.654);
    vec2 p = (n.xy - 0.5) * 0.7;
    float d = length(uv - p);
    
    float fade = Saw(0.025, fract(t + n.z));
    float c = S(0.1, 0.0, d) * fract(n.z * 10.0) * fade;
    return c;
}

vec2 Drops(vec2 uv, float t, float l0, float l1, float l2) {
    float s = StaticDrops(uv, t) * l0; 
    vec2 m1 = DropLayer(uv, t) * l1;
    vec2 m2 = DropLayer(uv * 1.85, t) * l2;
    
    float c = s + m1.x + m2.x;
    c = S(0.3, 1.0, c);
    return vec2(c, max(m1.y * l0, m2.y * l1));
}

// TRAILS ==================================================

float hash(float n) {
    return fract(sin(n) * 687.3123);
}
float noise(in vec2 x) {
    vec2 p = floor(x);
    vec2 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n = p.x + p.y * 157.0;
    return mix(mix(hash(n), hash(n + 1.0), f.x), mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y);
}
float rainEffect(vec2 uv, float time) {
    vec2 st = 256.0 * (uv * vec2(3.0, 0.1) + vec2(time * 0.13 - uv.y * 0.01, -time * 0.13));
    float f = noise(st) * noise(st * 0.773) * 1.55;
    f = 0.25 + clamp(pow(abs(f), 13.0) * 13.0, 0.0, (1.0 - uv.y) * 0.14);
    return f;
}

// BLUR + DRIPS ==============================================

#define size 0.2

vec4 N14(float t) {
	return fract(sin(t * vec4(123.0, 1024.0, 1456.0, 264.0)) * vec4(6547.0, 345.0, 8799.0, 1564.0));
}
vec2 Drops(vec2 uv, float t) {
    vec2 UV = uv;
    
    // DEFINE GRID
    uv.y += t * 0.8;
    vec2 a = vec2(6.0, 1.0);
    vec2 grid = a * 2.0;
    vec2 id = floor(uv * grid);
    
    // RANDOM SHIFT Y
    float colShift = N(id.x); 
    uv.y += colShift;
    
    // DEFINE SPACES
    id = floor(uv * grid);
    vec3 n = N13(id.x * 35.2 + id.y * 2376.1);
    vec2 st = fract(uv*grid) - vec2(0.5, 0);
    
    // POSITION DROPS
    float x = n.x - 0.5;
    
    float y = UV.y * 20.0;
    
    float distort = sin(y + sin(y));
    x += distort * (0.5 - abs(x)) * (n.z - 0.5);
    x *= 0.7;
    float ti = fract(t + n.z);
    y = (Saw(0.85, ti) - 0.5) * 0.9 + 0.5;
    vec2 p = vec2(x, y);
    
    // DROPS
    float d = length((st - p) * a.yx);
    float dSize = size; 
    float Drop = S(dSize, 0.0, d);
    
    float r = sqrt(S(1.0, y, st.y));
    float cd = abs(st.x - x);
    
    // TRAILS
    float trail = S((dSize * 0.5 + 0.03) * r, (dSize * 0.5 - 0.05) * r, cd);
    float trailFront = S(-0.02, 0.02, st.y - y);
    trail *= trailFront;
    
    // DROPLETS
    y = UV.y;
    y += N(id.x);
    float trail2 = S(dSize * r, .0, cd);
    float droplets = max(0.0, (sin(y * (1.0 - y) * 120.0) - st.y)) * trail2 * trailFront * n.z;
    y = fract(y * 10.0) + (st.y - 0.5);
    float dd = length(st - vec2(x, y));
    droplets = S(dSize * N(id.x), 0.0, dd);
    float m = Drop + droplets * r * trailFront;
    
    return vec2(m, trail);
}
float StaticDrops2(vec2 uv, float t) {
	uv *= 30.0;
    
    vec2 id = floor(uv);
    uv = fract(uv) - 0.5;
    vec3 n = N13(id.x * 107.45 + id.y * 3543.654);
    vec2 p = (n.xy - 0.5) * 0.5;
    float d = length(uv-p);
    
    float fade = Saw(0.025, fract(t + n.z));
    float c = S(size, 0.0, d) * fract(n.z * 10.0) * fade;

    return c;
}
vec2 Rain(vec2 uv, float t) {
    float s = StaticDrops2(uv, t); 
    vec2 r1 = Drops(uv, t);
    vec2 r2 = Drops(uv * 1.8, t);
    
    float c = s + r1.x + r2.x;
    c = S(0.3, 1.0, c);
    
    return vec2(c, max(r1.y, r2.y));
}


void main()
{
    vec4 color = texture(texColor, tex);
    float depth = texture(texDepth, tex).r;
    float linearDepth = (2.0 * zNear * zFar) / (zFar + zNear - depth * (zFar - zNear));
    linearDepth = clamp(linearDepth, zNear, zFar);

    // Underwater effect always active if the camera is underwater
    if (eyePos.y < 0.0 && bOcean != 0)
    {
        // Plus la caméra est profonde, plus c'est sombre
        float darknessFactor = exp(-0.2 * (-eyePos.y));    // 0.08 = vitesse d'assombrissement
        darknessFactor = clamp(darknessFactor, 0.0, 1.0);  // 0.15 = minimum (nuit noire à grande profondeur)
        color.rgb = mix(vec3(0.0), color.rgb, darknessFactor);

        // Underwater fog: depends only on camera-pixel distance
        float fogFactor = 1.0 - exp(-0.025 * linearDepth); // The further you look, the denser it is.
        fogFactor = clamp(fogFactor, 0.0, 1.0);

        // Intensity depending on camera depth
        float depthBlend = clamp(-eyePos.y / 20.0, 0.0, 1.0);

        vec3 underwaterFogColor = color.rgb * 0.4;
        vec3 fogCol = mix(oceanColor, underwaterFogColor, depthBlend) * exposure;

        // Applies underwater fog regardless of viewing angle
        color = mix(vec4(fogCol, 1.0), color, 1.0 - fogFactor);

        // Underwater suspended particle effect, animated towards the camera
        vec2 uv = tex * screenSize;
        vec2 screenCenter = 0.5 * screenSize;

        float particleDensity = 0.8;    // Particle density
        float particleSize = 2.2;       // Size in pixels
        float speed = 6.0;              // Radial velocity
        float lifetime = 2.6;           // Lifespan of a particle (in seconds)

        float particleMask = 0.0;
        int numLayers = 2;              // Multiple layers for added depth

        for (int layer = 0; layer < numLayers; ++layer) 
        {
            // For each particle "slot"
            for (int i = 0; i < 20; ++i) 
            {
                // Generates a unique seed for each particle
                float seed = float(i) + float(layer) * 100.0;
                float angle = hash(vec2(seed, 0.0)) * 6.2831853; // [0, 2*PI]
                float radius = hash(vec2(seed, 1.0)) * 0.45 * min(screenSize.x, screenSize.y);

                // Starting position at center + radius
                vec2 dir = vec2(cos(angle), sin(angle));
                vec2 start = screenCenter + dir * radius * 0.3;

                // Animated radial shift (advance from center to outward)
                float t = mod(uTime + hash(vec2(seed, 2.0)) * 100.0, lifetime) / lifetime;
                float travel = t * (0.7 * min(screenSize.x, screenSize.y) - radius);

                vec2 pos = start + dir * travel;

                // Displays the particle if it exists (density)
                if (hash(vec2(seed, 3.0)) < particleDensity) 
                {
                    float dist = length(uv - pos);
                    float alpha = smoothstep(particleSize, 0.0, dist);
                    // Attenuation according to the particle's progression (soft appearance/disappearance)
                    alpha *= smoothstep(0.05, 0.15, t) * (1.0 - smoothstep(0.7, 1.0, t));
                    particleMask += alpha;
                }
            }
        }

        // Particle color (slightly opaque)
        vec3 particleColor = mix(oceanColor, vec3(1.0), 0.55);
        float particleAlpha = clamp(particleMask, 0.0, 0.17);   // Adjusts the overall intensity
        color.rgb = mix(color.rgb, particleColor, particleAlpha);
    }
    // Fog everywhere
    else if (fogDensity > 0.0)
    {
        float fogFactor = exp(-fogDensity * linearDepth);
        fogFactor = clamp(fogFactor, 0.0, 1.0);

        // Assombrit et désature le fogColor selon l'exposure
        float nightBlend = clamp(1.0 - exposure * 4.0, 0.0, 1.0);   // exposure < 0.25 → nuit
        vec3 nightFogColor = vec3(0.02, 0.03, 0.04);                // bleu nuit quasi-noir
        vec3 finalFogColor = mix(fogColor, nightFogColor, nightBlend);

        color = mix(vec4(finalFogColor * exposure, 1.0), color, fogFactor);
    }

    // Low vision
    if (bLowIntensity > 0)
    {
        float intensity = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        vec3 lowIntensity = vec3(intensity * 0.4);                  // Only 40% brightness
        color = vec4(mix(color.rgb, lowIntensity, 0.9), color.a);   // Almost monochrome and faint
    }

    // Night vision
    if (bNightVision > 0)
    {
        float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        vec3 nightVision = vec3(0.1, 0.95, 0.2) * luminance * 1.5;  // Midnight green
        float noise = fract(sin(dot(tex * uTime * 400.0, vec2(12.9898, 78.233))) * 43758.5453);
        nightVision += (noise - 0.5) * 0.04;
        float vignette = smoothstep(0.75, 0.5, distance(tex, vec2(0.5, 0.5)));
        nightVision *= vignette;
        color.rgb = nightVision;
    }
    
    // Drops + Trails
    if (bRainDropsTrails > 0)
    {
        uint stencilVal = texture(texStencil, tex).r;   // 0u = extérieur, 1u = parois, 2u = vitres
        if (bInside > 0 && stencilVal == 2u) // vitres
        {
            // Center and moderate zoom
            float zoom = 1.0;
            vec2 uv = (tex - 0.5) * zoom;

            // Slight extension to avoid cuts
            uv = uv * 1.1; 
            uv.y = 1.0 - uv.y;

            float t = uTime * 0.2;  // speed rain
            float rainAmount = 0.8;
            vec2 drops = Drops(uv, t, rainAmount, rainAmount * 0.6, rainAmount * 0.4);

            // Cheap normal calculation for refraction
            vec2 n = vec2(dFdx(drops.x), dFdy(drops.x));
            float blurAmount = mix(3.0, 6.0, rainAmount);
            float focus = mix(blurAmount, 2.0, smoothstep(0.1, 0.2, drops.x));
            vec3 refractColor = textureLod(texColor, tex + n, focus).rgb;

            // Blend the refracted color based on drop intensity
            color.rgb = mix(color.rgb, refractColor, drops.x * 0.8);

            // Modulate color with drops for wet effect
            color.rgb = mix(color.rgb, vec3(0.8, 0.9, 1.0), drops.x * 0.3);

            // Rain Trails
            float fRain = rainEffect(tex, uTime * 0.5);
            vec3 rainCol = vec3(0.7, 0.8, 0.9); 
            float maskTrail = smoothstep(0.01, 1.0, fRain);
            color.rgb += fRain * rainCol * maskTrail * 1.5 * clamp(exposure * 4.0, 0.2, 1.0);
        }
        else if (bInside == 0)
        {
            // Rain Trails
            float fRain = rainEffect(tex, uTime * 0.5);
            vec3 rainCol = vec3(0.7, 0.8, 0.9); 
            float maskTrail = smoothstep(0.01, 1.0, fRain);
            color.rgb += fRain * rainCol * maskTrail * 1.5 * clamp(exposure * 4.0, 0.2, 1.0);
        }
    }

    // Binoculars
    float mask = 0.0;
    if (bBinoculars > 0)
    {
        // Centers of the two circles in UV coordinates
        vec2 centerLeft = vec2(0.35, 0.5);
        vec2 centerRight = vec2(0.65, 0.5);
        float radius = 0.45;

        // Calculating the width/height ratio
        float aspect = screenSize.x / screenSize.y;

        // Adjusting coordinates to compensate for aspect ratio
        vec2 coordLeft = tex - centerLeft;
        coordLeft.x *= aspect;

        vec2 coordRight = tex - centerRight;
        coordRight.x *= aspect;

        float distLeft = length(coordLeft);
        float distRight = length(coordRight);

        float edgeWidth = 0.02; // edge gradient width

        float maskLeft = smoothstep(radius, radius - edgeWidth, distLeft);
        float maskRight = smoothstep(radius, radius - edgeWidth, distRight);

        mask = max(maskLeft, maskRight);
    }

 #ifdef DEBUG_HORIZON
    // Ligne de 1 pixel centrée sur horizonHeight
    float screenY = tex.y * 2.0 - 1.0;
    float lineThickness = 1.0 / screenSize.y;  // 1 pixel en coordonnées normalisées
    float horizonLine = 1.0 - smoothstep(0.0, lineThickness, abs(screenY - horizonHeight));
    color.rgb = mix(color.rgb, vec3(1.0, 0.0, 0.0), horizonLine * 0.8);
#endif

    if (bBinoculars > 0)
        FragColor = vec4(color.rgb * mask, 1.0);
    else
        FragColor = color;
}
