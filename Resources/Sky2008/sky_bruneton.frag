#version 450

/**
 * Precomputed Atmospheric Scattering
 * Copyright (c) 2008 INRIA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Author: Eric Bruneton
 */

layout(binding = 1) uniform sampler3D inscatterSampler;
layout(binding = 2) uniform sampler2D transmittanceSampler;

layout(location = 0) in vec3 ViewDir;
layout(location = 1) in vec3 Camera;
layout(location = 2) in vec3 SunDir;
layout(location = 3) in float SunIntensity;
layout(location = 4) in float MieG;

layout(location = 0) out vec4 FragColor;

// ----------------------------------------------------------------------------
// PHYSICAL MODEL PARAMETERS
// ----------------------------------------------------------------------------

const vec3 earthPos = vec3(0.0, 0.0, 6360010.0);
const float SCALE = 1000.0;
const float Rg = 6360.0 * SCALE;
const float Rt = 6420.0 * SCALE;
const float RL = 6421.0 * SCALE;
const vec3 betaR = vec3(5.8e-3, 1.35e-2, 3.31e-2) / SCALE;// Rayleigh

const float M_PI = 3.141592657;

// ----------------------------------------------------------------------------
// PARAMETERIZATION OPTIONS
// ----------------------------------------------------------------------------

const int RES_R = 32;
const int RES_MU = 128;
const int RES_MU_S = 32;
const int RES_NU = 8;

// ----------------------------------------------------------------------------
// PARAMETERIZATION FUNCTIONS
// ----------------------------------------------------------------------------

vec2 getTransmittanceUV(float r, float mu) {
    float uR, uMu;
    uR = sqrt((r - Rg) / (Rt - Rg));
    uMu = atan((mu + 0.15) / (1.0 + 0.15) * tan(1.5)) / 1.5;
    return vec2(uMu, uR);
}
vec4 texture4D(float r, float mu, float muS, float nu)
{
    float H = sqrt(Rt * Rt - Rg * Rg);
    float rho = sqrt(r * r - Rg * Rg);
    float rmu = r * mu;
    float delta = rmu * rmu - r * r + Rg * Rg;
    vec4 cst = rmu < 0.0 && delta > 0.0 ? vec4(1.0, 0.0, 0.0, 0.5 - 0.5 / float(RES_MU)) : vec4(-1.0, H * H, H, 0.5 + 0.5 / float(RES_MU));
    float uR = 0.5 / float(RES_R) + rho / H * (1.0 - 1.0 / float(RES_R));
    float uMu = cst.w + (rmu * cst.x + sqrt(delta + cst.y)) / (rho + cst.z) * (0.5 - 1.0 / float(RES_MU));
    float uMuS = 0.5 / float(RES_MU_S) + (atan(max(muS, -0.1975) * tan(1.26 * 1.1)) / 1.1 + (1.0 - 0.26)) * 0.5 * (1.0 - 1.0 / float(RES_MU_S));
    float lerp = (nu + 1.0) / 2.0 * (float(RES_NU) - 1.0);
    float uNu = floor(lerp);
    lerp = lerp - uNu;
    return texture(inscatterSampler, vec3((uNu + uMuS) / float(RES_NU), uMu, uR)) * (1.0 - lerp) + texture(inscatterSampler, vec3((uNu + uMuS + 1.0) / float(RES_NU), uMu, uR)) * lerp;
}

// ----------------------------------------------------------------------------
// UTILITY FUNCTIONS
// ----------------------------------------------------------------------------

vec3 transmittance(float r, float mu) {
    // Transmittance(=transparency) of atmosphere for infinite ray (r,mu)
    // (mu=cos(view zenith angle)), intersections with ground ignored
    vec2 uv = getTransmittanceUV(r, mu);
    return texture(transmittanceSampler, uv).rgb;
}

float phaseFunctionR(float mu) {
    // Rayleigh phase function
    return (3.0 / (16.0 * M_PI)) * (1.0 + mu * mu);
}

float phaseFunctionM(float mu) {
    // Mie phase function
    return 1.5 * 1.0 / (4.0 * M_PI) * (1.0 - MieG * MieG) * pow(1.0 + (MieG * MieG) - 2.0 * MieG * mu, -3.0/2.0) * (1.0 + mu * mu) / (2.0 + MieG * MieG);
}

vec3 getMie(vec4 rayMie) {
    // Approximated single Mie scattering (cf. approximate Cm in paragraph "Angular precision")
    return rayMie.rgb * rayMie.w / max(rayMie.r, 1e-4) * (betaR.r / betaR);
}

// ----------------------------------------------------------------------------
// PUBLIC FUNCTIONS
// ----------------------------------------------------------------------------

vec3 skyRadiance(vec3 camera, vec3 viewdir, vec3 sundir, out vec3 extinction)
{
    // scattered sunlight between two points
    // camera=observer
    // viewdir=unit vector towards observed point
    // sundir=unit vector towards the sun
    // return scattered light and extinction coefficient

    vec3 result;
    float r = length(camera);
    float rMu = dot(camera, viewdir);
    float mu = rMu / r;
    float r0 = r;
    float mu0 = mu;

    float deltaSq = sqrt(rMu * rMu - r * r + Rt * Rt);
    float din = max(-rMu - deltaSq, 0.0);
    if (din > 0.0) 
    {
        camera += din * viewdir;
        rMu += din;
        mu = rMu / Rt;
        r = Rt;
    }

    if (r <= Rt) 
    {
        float nu = dot(viewdir, sundir);
        float muS = dot(camera, sundir) / r;

        vec4 inScatter = texture4D(r, rMu / r, muS, nu);
        extinction = transmittance(r, mu);

        vec3 inScatterM = getMie(inScatter);
        float phase = phaseFunctionR(nu);
        float phaseM = phaseFunctionM(nu);
        result = inScatter.rgb * phase + inScatterM * phaseM;
    } 
    else 
    {
        result = vec3(0.0);
        extinction = vec3(1.0);
    }

    return result;
}

// ----------------------------------------------------------------------------
// TONE MAPPING
// ----------------------------------------------------------------------------

vec3 aces(vec3 x) {
    // Krzysztof Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    vec3 mapped = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    return mapped; 
}


void main() 
{
    vec3 v = normalize(ViewDir);

    vec3 sunColor = vec3(step(0.99986967678, dot(v, SunDir))) * SunIntensity; // cos(0.53°) = 0,99986967678
    vec3 extinction;
    vec3 inscatter = skyRadiance(Camera + earthPos, v, SunDir, extinction) * SunIntensity;
    FragColor.rgb = sunColor * extinction + inscatter;

    // Tone mapping
    FragColor.rgb = aces(FragColor.rgb);
    FragColor.a = 1.0;

}
