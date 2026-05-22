/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Ocean.h"

extern uint32_t                     g_FramesInFlight;
extern sChrono                      Chronos[10];
extern unique_ptr<VulkanTexture>    g_TexReflectionColor;
extern unique_ptr<VulkanTexture>    g_TexShadowDepth;
extern VkSampler                    g_TexShadowDepthSampler;
extern mat4                         LightViewProjection;
extern VulkanTexture                TexContourShip;
extern int                          TexContourShipW;
extern int                          TexContourShipH;
extern bool                         g_bShip;
extern bool                         g_bShipShadow;		// Display the shadow of the ship
extern bool                         g_bShipReflection;	// Display the reflection of the ship on water
extern bool                         g_bShipWake;
extern unique_ptr<VulkanTexture>    g_TexWake0;
extern unique_ptr<VulkanTexture>    g_TexWake2;
extern int							g_WakeSize;

int					                TexWakeBufferSize = 512;

pair<string, uint64_t> OceanTimeStamps[5];

Ocean::Ocean(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent)
{
    mVulkanDevice       = vulkanDevice;
    mRenderPass         = renderPass;
    mExtent             = extent;

	ComputeFinishedSem.resize(g_FramesInFlight);
    mComputeFence.resize(g_FramesInFlight);
    mComputeCmd.resize(g_FramesInFlight);

}
Ocean::~Ocean()
{
    // Time buffer
    if (mTimeBuffer) vkDestroyBuffer(mVulkanDevice->device, mTimeBuffer, nullptr);
    if (mTimeMemory) vkFreeMemory(mVulkanDevice->device, mTimeMemory, nullptr);

    // Spectrum
    if (mSpectrumDescriptorPool) vkDestroyDescriptorPool(mVulkanDevice->device, mSpectrumDescriptorPool, nullptr);
    if (mSpectrumDescriptorSetLayout) vkDestroyDescriptorSetLayout(mVulkanDevice->device, mSpectrumDescriptorSetLayout, nullptr);
    if (mSpectrumPipeline) vkDestroyPipeline(mVulkanDevice->device, mSpectrumPipeline, nullptr);

    // IFFT
    if (mIfftDescriptorPool) vkDestroyDescriptorPool(mVulkanDevice->device, mIfftDescriptorPool, nullptr);
    if (mIfftDescriptorSetLayout) vkDestroyDescriptorSetLayout(mVulkanDevice->device, mIfftDescriptorSetLayout, nullptr);
    if (mIfftPipeline) vkDestroyPipeline(mVulkanDevice->device, mIfftPipeline, nullptr);

    // Displacement texture
    if (mDisplacementsDescPool) vkDestroyDescriptorPool(mVulkanDevice->device, mDisplacementsDescPool, nullptr);
    if (mDisplacementsDescSetLayout) vkDestroyDescriptorSetLayout(mVulkanDevice->device, mDisplacementsDescSetLayout, nullptr);
    if (mDisplacementsPipeline) vkDestroyPipeline(mVulkanDevice->device, mDisplacementsPipeline, nullptr);

    // Gradients
    if (mGradientsDescriptorPool) vkDestroyDescriptorPool(mVulkanDevice->device, mGradientsDescriptorPool, nullptr);
    if (mGradientsDescriptorSetLayout) vkDestroyDescriptorSetLayout(mVulkanDevice->device, mGradientsDescriptorSetLayout, nullptr);
    if (mGradientsPipeline) vkDestroyPipeline(mVulkanDevice->device, mGradientsPipeline, nullptr);
    if (mTextureSampler) vkDestroySampler(mVulkanDevice->device, mTextureSampler, nullptr);

    // Mesh original
	mVertexBuffer.reset();
	mIndexBuffer.reset();

    // Graphics pipelines
    mWireframePipeline.destroy(mVulkanDevice->device); 
    mOneMeshPipeline.destroy(mVulkanDevice->device);
    mvLODPatches.clear();
    mLODPipeline.destroy(mVulkanDevice->device);
	mPipelineTexture.destroy(mVulkanDevice->device);

    // Frames
    for (int i = 0; i < g_FramesInFlight; i++)
		mFrames[i].ubo.reset();

    // Displacement pixels
    if (mReadbackFence) vkDestroyFence(mVulkanDevice->device, mReadbackFence, nullptr);
    if (mStagingMem) vkUnmapMemory(mVulkanDevice->device, mStagingMem);
    if (mStagingBuffer) vkDestroyBuffer(mVulkanDevice->device, mStagingBuffer, nullptr);
    if (mStagingMem) vkFreeMemory(mVulkanDevice->device, mStagingMem, nullptr);
}

void Ocean::Init(vec2 wind)
{
    // Sort the ocean colors by their hue
    //int color = 0;
    //for (auto& c : vOceanColors)
    //{
    //    float h, s, l;
    //    rgb_to_hsl(c, h, s, l);
    //    cout << color++ << " : " << h << endl;
    //}

    SetWind(wind);
    EvaluatePersistence(PersistenceSec);
    SetSpectrum(9);

    // All meshes (normal and lod sizes)
    CreateMesh();
    CreateLODMeshes();

    // FFT
    CreateTextures();
    InitFrequencies();
    InitDisplacementsBuffer();
	InitFoamBuffer();
    CreateTimeBuffer(); 
    CreateSpectrumPipeline();
    CreateSpectrumDescriptors();
    UpdateSpectrumDescriptors();
    CreateIfftPipeline();
    CreateIfftDescriptors();
    PrecomputeAllIfftDescriptors();
    CreateDisplacementsPipeline();
    CreateDisplacementsDescriptors();
    UpdateDisplacementsDescriptors();
    CreateGradientsPipeline();
    CreateGradientsDescriptors();
    UpdateGradientsDescriptors();

    mTextureSampler = CreateTextureSamplerColor(mVulkanDevice->device);
    
    // Ocean colors
    for (auto& c : vOceanColors) c = color_255_to_1(c);
    OceanColor = vOceanColors[iOceanColor];

    // Kelvin waves
    CreateTexture2DArray();

    // Pipelines
    CreatePipeline0();  // Wireframe
    CreatePipeline1();  // One mesh
    CreatePipeline2();  // Lod meshes
    CreatePipeline3();  // Lod with wake around the the ship

    // Timestamps
    CreateQueryPool();

    // Recrée sémaphores et fences compute après un resize
    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < g_FramesInFlight; i++)
    {
        vkCreateSemaphore(mVulkanDevice->device, &semInfo, nullptr, &ComputeFinishedSem[i]);
        vkCreateFence(mVulkanDevice->device, &fenceInfo, nullptr, &mComputeFence[i]);
    }
}
void Ocean::SetWind(vec2 wind)
{
    Wind = wind;

    HeightMax = 0.0f;
    HeightMin = 0.0f;

	// Monahan and O'Brien (2014) : empirical formula for whitecap coverage as a function of wind speed
    float windSpeed = glm::length(Wind);
    WhitecapCoverageTheoretical = 3.84e-6f * powf(windSpeed, 3.41f) * 100.f;
    mvFoamHistory.clear();
};
void Ocean::EvaluatePersistence(float seconds)
{
    PersistenceSec = seconds;
    PersistenceFactor = -std::log(0.01f) / PersistenceSec;
}
void Ocean::SetSpectrum(int spectre)
{
    static const SpectrumFunc spectrumFuncs[] = {
        &Ocean::Phillips,
        &Ocean::Bretschneider,
        &Ocean::PiersonMoskowitz,
        &Ocean::JONSWAP,
        &Ocean::OchiHubble,
        &Ocean::TexelMarsenArsloe,
        &Ocean::DonelanBanner,
        &Ocean::Torsethaugen,
        &Ocean::Elfouhaily,
        &Ocean::Horvath
    };

    if (spectre >= 0 && spectre < (int)std::size(spectrumFuncs))
    {
        Spectre = spectre;
        CurrentSpectrum = spectrumFuncs[spectre];
		Amplitude = 1.0f;
		Lambda = 0.5f;
        mvFoamHistory.clear();
    }
}
void Ocean::ComputeNormFactor()
{
    float savedNorm = mNormFactor;
    mNormFactor = 1.0f;   // calcul brut, sans pondération

    float sumCurrent = 0.0f;
    float sumJONSWAP = 0.0f;
    float dk = 2.0f * M_PI / LengthWave;

    for (int m = 0; m <= FFT_SIZE; ++m)
        for (int n = 0; n <= FFT_SIZE; ++n)
        {
            vec2 kk(dk * (n - FFT_SIZE / 2), dk * (m - FFT_SIZE / 2));  
            sumCurrent += (this->*CurrentSpectrum)(kk);
            sumJONSWAP += JONSWAP(kk);
        }

    mNormFactor = (0.0000375f / 4.0f) * sumJONSWAP / sumCurrent;
}
void Ocean::InitFrequencies()
{
    if (Spectre != 0)
        ComputeNormFactor();

    mt19937 gen(12345); 
    normal_distribution<> gaussian(0.0, 1.0);

    VkDeviceSize h0Size = FFT_SIZE_1 * FFT_SIZE_1 * 2 * sizeof(float);  // complex<float> = 8 bytes
    VkDeviceSize wSize = FFT_SIZE_1 * FFT_SIZE_1 * sizeof(float);       // float = 4 bytes

    complex<float>* h0data = reinterpret_cast<complex<float>*>(mTexInitialSpectrum.cpuData);
    float* wdata = reinterpret_cast<float*>(mTexFrequencies.cpuData);
    
    vec2 k;
    float sqrt_S;

    for (int m = 0; m <= FFT_SIZE; ++m)
    {
        for (int n = 0; n <= FFT_SIZE; ++n) 
        {
            k.x = 2.0 * M_PI * (n - FFT_SIZE / 2) / LengthWave;
            k.y = 2.0 * M_PI * (m - FFT_SIZE / 2) / LengthWave;

            sqrt_S = sqrtf(mNormFactor * (this->*CurrentSpectrum)(k));

            int index = m * FFT_SIZE_1 + n;
            h0data[index].real(gaussian(gen) * sqrt_S);
            h0data[index].imag(gaussian(gen) * sqrt_S);
            wdata[index] = sqrtf(mGravity * glm::length(k));
        }
    }
    mTexInitialSpectrum.CopyStagingToGPU();
    mTexFrequencies.CopyStagingToGPU();
    ClearRecords();
}

float Ocean::Phillips(vec2 k)
{
    float k_length = glm::length(k);
    
    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;
    float k_length4 = k_length2 * k_length2;

    float windSpeed = glm::length(Wind);
    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Spectre de Phillips ───────────────────────────────────────────────
    // S(k) = A · exp(-1 / (k²·L²)) / k⁴
    // L = V² / g : plus grande vague possible pour la vitesse de vent V
    float L = windSpeed * windSpeed / mGravity;
    float L2 = L * L;

    float S_phillips = expf(-1.0f / (k_length2 * L2)) / k_length4;

    // ── Distribution directionnelle cos^n ─────────────────────────────────
    float D = powf(glm::max(k_dot_w, 0.0f), DirSpread);

    // ── Suppression des petites vagues (capillaires) ──────────────────────
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    float S = S_phillips * D * suppress;

    return S;
}
float Ocean::Bretschneider(vec2 k)
{
    // ─── Bretschneider (ISSC, 1959 / 1967) ──────────────────────────────────────
    //
    //  Reformulation paramétrique du Pierson-Moskowitz utilisant la hauteur
    //  significative Hs et la période de pic Tp plutôt que la vitesse du vent.
    //  Standard ISO/ISSC pour l'ingénierie offshore (structures, navires).
    //  Identique à PM quand Hs et Tp sont dérivés de U via les relations P-M,
    //  mais bien plus pratique quand on impose directement l'état de mer.
    //
    //  Formulation en fréquence angulaire ω :
    //    S(ω) = (5/16) · Hs² · ωp⁴ / ω⁵ · exp[-5/4 · (ωp/ω)⁴]
    //  Ici transposée en k via la relation de dispersion ω² = g·k (eau profonde).
    //
    //  Paramètres membres à ajouter :
    //    float Hs   — hauteur significative (m),  ex. 2.5
    //    float Tp   — période de pic (s),          ex. 10.0
    //    (Wind, DirSpread, mGravity : inchangés)
    // ─────────────────────────────────────────────────────────────────────────────

    float windSpeed = glm::length(Wind);
    auto waveParams = JONSWAPModel::GetWaveParameters(windSpeed, Fetch);
    float Hs = waveParams.significantWaveHeight;
    float Tp = waveParams.peakPeriod;

    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;

    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Fréquence angulaire (eau profonde) ────────────────────────────────
    float omega = sqrtf(mGravity * k_length);          // ω = sqrt(g·|k|)
    float omega_p = 2.0f * M_PI / Tp;                    // ωp = 2π / Tp

    // ── Spectre de Bretschneider ──────────────────────────────────────────
    // S(ω) = (5/16) · Hs² · ωp⁴ / ω⁵ · exp[-5/4 · (ωp/ω)⁴]
    // Conversion ω → k : dω/dk = g/(2ω), donc S(k) = S(ω) · dω/dk
    float ratio = omega_p / omega;                     // ωp / ω
    float ratio4 = ratio * ratio * ratio * ratio;
    float S_omega = (5.0f / 16.0f) * Hs * Hs * powf(omega_p, 4.0f) / powf(omega, 5.0f) * expf(-1.25f * ratio4);

    // Jacobien dω/dk = g / (2ω)
    float dw_dk = mGravity / (2.0f * omega);
    float S_k = S_omega * dw_dk;

    // ── Distribution directionnelle cos^n ─────────────────────────────────
    // Bretschneider original est omnidirectionnel ; on ajoute cos^n
    float D = powf(glm::max(k_dot_w, 0.0f), DirSpread);

    // ── Suppression des petites vagues ────────────────────────────────────
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    return S_k * D * suppress;
}
float Ocean::PiersonMoskowitz(vec2 k)
{
    /*
    Pierson-Moskowitz est simple — pas de fetch, pas de pic de résonance γ. 
    La fréquence de pic ωp = 0.855·g/U est uniquement fonction de la vitesse du vent, ce qui correspond à une mer pleinement développée 
    où le vent souffle depuis suffisamment longtemps pour que les vagues aient atteint leur état d'équilibre. 
    C'est pour ça que JONSWAP converge vers Pierson-Moskowitz quand γ = 1 et que le fetch tend vers l'infini. Pierson-Moskowitz est le cas limite de JONSWAP.
    */

    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;
    float k_length4 = k_length2 * k_length2;

    float windSpeed = glm::length(Wind);
    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Fréquence angulaire ───────────────────────────────────────────────
    float omega = sqrtf(mGravity * k_length);
    float omega_p = 0.855f * mGravity / windSpeed;         // fréquence de pic P-M
    // correspond à mer pleinement développée

    // ── Spectre Pierson-Moskowitz ─────────────────────────────────────────
    // S(k) = α·g²/k⁴ · exp[-β·(ωp/ω)⁴]
    // α = 0.0081 (constante de Phillips)
    // β = 1.25   (constante empirique P-M)
    const float alpha = 0.0081f;
    const float beta = 1.25f;

    float S_pm = alpha * mGravity * mGravity / k_length4 * expf(-beta * powf(omega_p / omega, 4.0f));

    // ── Distribution directionnelle cos^n ─────────────────────────────────
    // P-M original est isotrope mais on ajoute une directionnalité pour avoir des vagues alignées sur le vent
    float cosTheta = glm::max(k_dot_w, 0.0f);
    float D = powf(cosTheta, DirSpread);    // exposant 2-6 typique

    // ── Suppression des petites vagues ────────────────────────────────────
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    float S = S_pm * D * suppress;

    return S;
}
float Ocean::JONSWAP(vec2 k)
{
    float k_length = glm::length(k);
    
    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;
    float k_length4 = k_length2 * k_length2;

    // Direction du vent normalisée
    vec2 windDir = glm::normalize(Wind);
    float windSpeed = glm::length(Wind);

    float k_dot_w = glm::dot(glm::normalize(k), windDir);

    // Supprime les vagues allant contre le vent
    if (k_dot_w < 0.0f)
        return 0.0f;

    // Fréquence angulaire de dispersion (deep water)
    float omega = sqrtf(mGravity * k_length);           // ω = sqrt(g * |k|)

    // Fréquence de pic JONSWAP (plus le fetch - en m - est grand, plus les vagues sont longues)
    float omega_p = 22.0f * cbrtf(mGravity * mGravity / (windSpeed * Fetch));

    // Spectre de Pierson-Moskowitz (base de JONSWAP)
    float alpha = 0.0081f;                              // constante de Phillips généralisée
    float S_pm = alpha * mGravity * mGravity / k_length4 * expf(-1.25f * powf(omega_p / omega, 4.0f));

    // Pic de résonance JONSWAP
    float sigma = (omega <= omega_p) ? 0.07f : 0.09f;   // largeur du pic
    float r = expf(-powf(omega - omega_p, 2.0f) / (2.0f * sigma * sigma * omega_p * omega_p));
    float S_jonswap = S_pm * powf(Maturity, r);

    // Distribution directionnelle (même approche que Phillips : cos^n aligné sur le vent)
    float dirSpread = powf(glm::max(k_dot_w, 0.0f), DirSpread); // exposant : 2-6 typique

    // Suppression des petites vagues (capillaires)
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    return S_jonswap * dirSpread * suppress;
}
float Ocean::OchiHubble(vec2 k)
{
    // ─── Ochi-Hubble (1976) ──────────────────────────────────────────────────────
    //
    //  Spectre bimodal à six paramètres (trois par composante), développé par
    //  Michel Ochi et Earl Hubble (SSPA Research, Göteborg) à partir de 800 états
    //  de mer mesurés en Atlantique Nord.
    //  Publication : "Six-Parameter Wave Spectra", Coastal Engineering Conference 1976.
    //
    //  Modélise simultanément :
    //    • La houle longue distante  (swell)  — composante basse fréquence
    //    • La mer de vent locale              — composante haute fréquence
    //  Très utilisé en tenue à la mer et fatigue des structures offshore.
    //
    //  Formulation à deux pics (j = 1 : swell, j = 2 : wind sea) :
    //    S_j(ω) = [ (4λ_j + 1)/4 · ωp_j⁴ ]^λ_j / Γ(λ_j) · Hs_j² / ω^(4λ_j+1) · exp[-(4λ_j+1)/4 · (ωp_j/ω)⁴]
    //
    //  Paramètres membres à ajouter :
    //    float OH_Hs1, OH_Tp1, OH_lambda1   — swell     (ex. 1.5, 15.0, 3.0)
    //    float OH_Hs2, OH_Tp2, OH_lambda2   — mer de vent (ex. 1.0, 8.0,  1.5)
    //    λ typiques : 3.0 (swell bien formé) / 1.5 (mer de vent jeune)
    // ─────────────────────────────────────────────────────────────────────────────

    float windSpeed = glm::length(Wind);
    auto waveParams = JONSWAPModel::GetWaveParameters(windSpeed, Fetch);
    float OH_Hs2 = waveParams.significantWaveHeight;
    float OH_Tp2 = waveParams.peakPeriod;
    float OH_lambda2 = 1.5f;

    float OH_Hs1 = 1.5f * OH_Hs2;
    float OH_Tp1 = 3.0f * OH_Tp2;
    float OH_lambda1 = 3.0f;    // swell bien formé

    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;

    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Fréquence angulaire ───────────────────────────────────────────────
    float omega = sqrtf(mGravity * k_length);

    // ── Composante générique Ochi-Hubble ──────────────────────────────────
    // Calcule S_j(ω) pour un seul pic (Hs_j, Tp_j, λ_j) puis applique le jacobien dω/dk pour obtenir S_j(k)
    auto ochi_component = [&](float Hs_j, float Tp_j, float lambda_j) -> float
        {
            float omega_p_j = 2.0f * M_PI / Tp_j;
            float ratio_j = omega_p_j / omega;                    // ωp_j / ω
            float ratio4_j = powf(ratio_j, 4.0f);

            // Coefficient de forme
            float coeff = powf((4.0f * lambda_j + 1.0f) / 4.0f * ratio4_j, lambda_j) / tgammaf(lambda_j);                         // Γ(λ) via tgamma

            float S_omega_j = coeff * (Hs_j * Hs_j / 4.0f) / powf(omega, 4.0f * lambda_j + 1.0f) * expf(-(4.0f * lambda_j + 1.0f) / 4.0f * ratio4_j);

            // Jacobien dω/dk
            float dw_dk = mGravity / (2.0f * omega);
            return S_omega_j * dw_dk;
        };

    // ── Deux composantes ──────────────────────────────────────────────────
    float S_swell = ochi_component(OH_Hs1, OH_Tp1, OH_lambda1);
    float S_windsea = ochi_component(OH_Hs2, OH_Tp2, OH_lambda2);
    float S_total = S_swell + S_windsea;

    // ── Distribution directionnelle cos^n ─────────────────────────────────
    // Ochi-Hubble original est omnidirectionnel ; on applique la même pondération directionnelle que les autres spectres
    float D = powf(glm::max(k_dot_w, 0.0f), DirSpread);

    return S_total * D;
}
float Ocean::TexelMarsenArsloe(vec2 k)
{
    /*
    La seule différence est Phi_TMA = cg_shallow / cg_deep.
    En eau profonde kh → ∞, tanh(kh) → 1 donc cg_shallow → cg_deep et Phi → 1 — TMA est strictement équivalent à JONSWAP.
    En eau peu profonde kh → 0, tanh(kh) → kh donc cg_shallow → sqrt(g·h) (vitesse en eau peu profonde) et Phi → 0 — le spectre s'effondre.
    C'est physiquement correct : en eau très peu profonde les vagues ne peuvent plus se propager librement et le spectre de surface est atténué.
    En pratique pour SimShip tu obtiendras une mer notablement plus calme dans les zones portuaires si WaterDepth < 20m, et identique à JONSWAP en pleine mer.
    */
    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;
    float k_length4 = k_length2 * k_length2;

    float windSpeed = glm::length(Wind);
    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Profondeur d'eau ──────────────────────────────────────────────────
    // WaterDepth = 0.0 → eau profonde (TMA = JONSWAP)
    // WaterDepth = 10.0 → eau peu profonde (côte, port)

    float h = glm::max(Depth, 0.1f);                  // évite division par zéro

    // ── Relation de dispersion eau peu profonde ───────────────────────────
    // En eau profonde  : ω² = g·k
    // En eau peu profonde : ω² = g·k·tanh(k·h)
    float kh = k_length * h;
    float tanh_kh = tanhf(kh);
    float omega = sqrtf(mGravity * k_length * tanh_kh);  // ← différence vs eau profonde

    // Vitesse de groupe en eau peu profonde
    // cg = dω/dk = (g/2ω) · [tanh(kh) + kh·(1 - tanh²(kh))]
    float cg_deep = mGravity / (2.0f * sqrtf(mGravity * k_length)); // eau profonde
    float cg_shallow = (mGravity / (2.0f * omega)) * (tanh_kh + kh * (1.0f - tanh_kh * tanh_kh));   // eau peu profonde

    // ── Fréquence de pic ──────────────────────────────────────────────────
    float omega_p = 22.0f * cbrtf(mGravity * mGravity / (windSpeed * Fetch));

    // ── Spectre JONSWAP de base ───────────────────────────────────────────
    // TMA est une transformation de JONSWAP → on part de la même base
    const float alpha = 0.0081f;
    float S_pm = alpha * mGravity * mGravity / k_length4 * expf(-1.25f * powf(omega_p / omega, 4.0f));

    // Pic de résonance JONSWAP
    float sigma = (omega <= omega_p) ? 0.07f : 0.09f;
    float r = expf(-powf(omega - omega_p, 2.0f) / (2.0f * sigma * sigma * omega_p * omega_p));
    float peak = powf(Maturity, r);

    float S_jonswap = S_pm * peak;

    // ── Fonction de transformation TMA ────────────────────────────────────
    // C'est LE terme spécifique à TMA (Kitaigородский et al. 1975)
    // Φ(kh) = cg_shallow / cg_deep
    // Atténue le spectre quand kh < π/2 (eau peu profonde)
    // Φ → 1 quand kh → ∞ (eau profonde : TMA = JONSWAP)
    // Φ → 0 quand kh → 0 (très faible profondeur)
    float Phi_TMA = cg_shallow / cg_deep;
    Phi_TMA = glm::clamp(Phi_TMA, 0.0f, 1.0f);

    // ── Distribution directionnelle Donelan-Banner ────────────────────────
    float theta = acosf(glm::clamp(k_dot_w, -1.0f, 1.0f));
    float ratio = omega / omega_p;
    float beta_dir;

    if (ratio < 0.95f)
        beta_dir = 2.61f * powf(ratio, 1.3f);
    else if (ratio <= 1.6f)
        beta_dir = 2.28f * powf(ratio, -1.3f);
    else
        beta_dir = powf(10.0f, -0.4f + 0.8393f * expf(-0.567f * logf(ratio * ratio)));

    float sech_val = 2.0f / (expf(beta_dir * theta) + expf(-beta_dir * theta));
    float D = (beta_dir / 2.0f) * sech_val * sech_val;

    // ── Suppression des petites vagues ────────────────────────────────────
    float L = windSpeed * windSpeed / mGravity;
    float l2 = L * L * 0.0001f * 0.0001f;
    float suppress = expf(-k_length2 * l2);

    // ── Résultat final ────────────────────────────────────────────────────
    // Φ atténue progressivement le spectre quand la profondeur diminue
    float S = S_jonswap * Phi_TMA * D * suppress;

    return S;
}
float Ocean::DonelanBanner(vec2 k)
{
    float k_length = glm::length(k);
    if (k_length < 0.000001f)
        return 0.0f;

    float k_length2 = k_length * k_length;
    float k_length4 = k_length2 * k_length2;

    float windSpeed = glm::length(Wind);
    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Fréquence angulaire ───────────────────────────────────────────────
    float omega = sqrtf(mGravity * k_length);
    float omega_p = 22.0f * cbrtf(mGravity * mGravity / (windSpeed * Fetch));

    // ── Spectre de base Pierson-Moskowitz (même base que JONSWAP) ─────────
    float alpha = 0.0081f;
    float L = windSpeed * windSpeed / mGravity;
    float L2 = L * L;
    float S_pm = alpha * mGravity * mGravity / k_length4 * expf(-1.25f * powf(omega_p / omega, 4.0f));

    // ── Pic de résonance JONSWAP (inchangé) ───────────────────────────────
    float sigma = (omega <= omega_p) ? 0.07f : 0.09f;
    float r = expf(-powf(omega - omega_p, 2.0f)
        / (2.0f * sigma * sigma * omega_p * omega_p));
    float peak = powf(Maturity, r);

    float S_base = S_pm * peak;

    // ── Distribution directionnelle Donelan-Banner ────────────────────────
    // Remplace le cos^n de JONSWAP par sech²(β·θ) (carré de la sécante hyperbolique)
    // β varie selon ω/ωp pour coller aux mesures expérimentales
    float theta = acosf(glm::clamp(k_dot_w, -1.0f, 1.0f));

    float ratio = omega / omega_p;
    float beta_dir;

    if (ratio < 0.95f)
        beta_dir = 2.61f * powf(ratio, 1.3f);
    else if (ratio <= 1.6f)
        beta_dir = 2.28f * powf(ratio, -1.3f);
    else
        beta_dir = powf(10.0f, -0.4f + 0.8393f * expf(-0.567f * logf(ratio * ratio)));

    // sech²(β·θ) — distribution directionnelle normalisée
    float sech_val = 2.0f / (expf(beta_dir * theta) + expf(-beta_dir * theta));
    float D = (beta_dir / 2.0f) * sech_val * sech_val;

    // ── Suppression des petites vagues ────────────────────────────────────
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    // ── Résultat ──────────────────────────────────────────────────────────
    // On utilise D directement à la place de cos^n, sans conversion dω/dk
    // pour garder la même échelle que JONSWAP
    float S = S_base * D * suppress;

    return S;
}
float Ocean::Torsethaugen(vec2 k)
{
    // ─── Torsethaugen (1993 / 1996) ──────────────────────────────────────────────
    //
    //  Spectre bimodal entièrement paramétrique développé par Knut Torsethaugen
    //  (SINTEF, Trondheim) à partir de mesures en mer de Norvège et mer du Nord.
    //  Publication : "Two Peak Wave Spectrum Model", OMAE 1993 + rapport SINTEF 1996.
    //
    //  Conçu pour les conditions nord-atlantiques sévères où swell et mer de vent
    //  coexistent fréquemment. Contrairement à Ochi-Hubble, tous les paramètres
    //  se dérivent automatiquement de Hs et Tp — un seul état de mer à fournir.
    //
    //  Architecture :
    //    • Pic primaire   (j=1) : composante dominante selon régime de mer
    //    • Pic secondaire (j=2) : composante sous-dominante (swell ou vent)
    //  Le régime est déterminé par Tp vs Tf = 6.6·Hs^(1/3) (période limite)
    //
    //  Paramètres membres à ajouter :
    //    float Hs   — hauteur significative totale (m), ex. 4.0
    //    float Tp   — période de pic dominante (s),     ex. 12.0
    // ─────────────────────────────────────────────────────────────────────────────

    float windSpeed = glm::length(Wind);
    auto waveParams = JONSWAPModel::GetWaveParameters(windSpeed, Fetch);
    float Hs = waveParams.significantWaveHeight;
    float Tp = waveParams.peakPeriod;

    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;

    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Fréquence angulaire ───────────────────────────────────────────────
    float omega = sqrtf(mGravity * k_length);

    // ── Période seuil et régime ───────────────────────────────────────────
    // Tf = 6.6 · Hs^(1/3) : séparation régime vent / régime swell
    float Tf = 6.6f * cbrtf(Hs);
    bool  swell_dominated = (Tp > Tf);              // swell domine si Tp > Tf

    // ── Paramétrage du pic primaire ───────────────────────────────────────
    // Calibration empirique SINTEF (valeurs tableau Torsethaugen 1996)
    float Hs1, Tp1, gamma1;

    if (swell_dominated)
    {
        // Régime swell : pic primaire = swell longue période
        float Rp = Tp / Tf;
        Hs1 = Hs * (1.0f - 0.35f * (Rp - 1.0f));        // Hs réduit pour le vent
        Hs1 = glm::clamp(Hs1, 0.3f * Hs, Hs);
        Tp1 = Tp;
        gamma1 = glm::max(1.0f + 6.0f * powf(Hs / (Tp * Tp), 0.3f), 1.0f);
    }
    else
    {
        // Régime mer de vent : pic primaire = mer de vent courte période
        Hs1 = Hs * 0.95f;
        Tp1 = Tp;
        gamma1 = glm::max(35.0f * powf(Hs / (Tp * Tp), 0.5f), 1.0f);
    }

    float Hs2 = sqrtf(glm::max(Hs * Hs - Hs1 * Hs1, 0.0f)); // conservation Hs
    float Tp2 = swell_dominated
        ? 0.7f * Tf                                   // vent sous-dominant
        : 1.3f * Tf;                                  // swell sous-dominant
    float gamma2 = 1.0f;                                       // pic secondaire élargi

    // ── Forme spectrale JONSWAP généralisée pour chaque pic ───────────────
    // S_j(ω) = alpha_j · g² / ω⁵ · exp[-5/4·(ωp_j/ω)⁴] · γ_j^r_j avec alpha_j calibré à partir de Hs_j et Tp_j
    auto tors_component = [&](float Hs_j, float Tp_j, float gamma_j) -> float
        {
            float omega_p_j = 2.0f * M_PI / Tp_j;

            // α normalisé sur Hs_j (intégrale du spectre = Hs²/16)
            // α_j = (5π⁴/g²) · Hs_j² · ωp_j⁴
            float alpha_j = (5.0f * powf(M_PI, 4.0f) / (mGravity * mGravity))
                * Hs_j * Hs_j
                * powf(omega_p_j, 4.0f);

            // Forme P-M
            float S_pm_j = alpha_j * mGravity * mGravity
                / powf(omega, 5.0f)
                * expf(-1.25f * powf(omega_p_j / omega, 4.0f));

            // Pic de résonance JONSWAP
            float sigma_j = (omega <= omega_p_j) ? 0.07f : 0.09f;
            float r_j = expf(-powf(omega - omega_p_j, 2.0f)
                / (2.0f * sigma_j * sigma_j * omega_p_j * omega_p_j));
            float peak_j = powf(glm::max(gamma_j, 1.0f), r_j);

            float S_omega_j = S_pm_j * peak_j;

            // Jacobien dω/dk
            float dw_dk = mGravity / (2.0f * omega);
            return S_omega_j * dw_dk;
        };

    float S_primary = tors_component(Hs1, Tp1, gamma1);
    float S_secondary = tors_component(Hs2, Tp2, gamma2);
    float S_total = S_primary + S_secondary;

    // ── Distribution directionnelle Donelan-Banner ────────────────────────
    // Torsethaugen est conçu pour des conditions nord-atlantiques sévères ;
    // on conserve la directionnalité Donelan-Banner (sech²) comme pour TMA
    float omega_p_dom = 2.0f * M_PI / Tp1;
    float theta = acosf(glm::clamp(k_dot_w, -1.0f, 1.0f));
    float ratio_dir = omega / omega_p_dom;
    float beta_dir;

    if (ratio_dir < 0.95f)
        beta_dir = 2.61f * powf(ratio_dir, 1.3f);
    else if (ratio_dir <= 1.6f)
        beta_dir = 2.28f * powf(ratio_dir, -1.3f);
    else
        beta_dir = powf(10.0f, -0.4f + 0.8393f * expf(-0.567f * logf(ratio_dir * ratio_dir)));

    float sech_val = 2.0f / (expf(beta_dir * theta) + expf(-beta_dir * theta));
    float D = (beta_dir / 2.0f) * sech_val * sech_val;

    // ── Suppression des petites vagues ────────────────────────────────────
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    return S_total * D * suppress;
}
float Ocean::Elfouhaily(vec2 k)
{
    /*
    La différence fondamentale est la séparation en deux spectres Bl et Bh qui couvrent des gammes de fréquences différentes.
    
    Bl (grandes vagues de gravité) reprend la structure JONSWAP avec le pic Gamma_j et Lpm, mais avec α variable comme Horvath. 
    Le terme cp/c module l'énergie selon le rapport des vitesses de phase — les vagues lentes (longues) reçoivent plus d'énergie que les rapides.
    
    Bh (capillaires) est piloté par ux/c — le rapport vent de friction / vitesse de phase.
    Les capillaires sont générés directement par le frottement du vent sur la surface, pas par résonance comme les grandes vagues.
    Le terme Fm centre ce spectre autour de k_m ≈ 363 rad / m, la fréquence où la tension de surface domine la gravité.

    La transition entre les deux régimes est continue — il n'y a pas de coupure artificielle comme dans les autres modèles. 
    C'est ce qui rend Elfouhaily particulièrement adapté pour les simulations radar et optiques, 
    et c'est pourquoi il est utilisé dans les productions Pixar et ILM où les détails de surface à toutes les échelles sont visibles.
    */
    
    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_min = 2.0f * M_PI / 100.0f;
    if (k_length < k_min)
        return 0.0f;

    float k_length2 = k_length * k_length;

    float windSpeed = glm::length(Wind);
    vec2  windDir = glm::normalize(Wind);

    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    const float sigma_t = 0.074f;
    const float rho = 1025.0f;
    const float Omega_c = 0.84f;

    float c_phase = sqrtf(mGravity / k_length + sigma_t * k_length / rho);

    float u_star = 0.025f * windSpeed;
    u_star = glm::max(u_star, 0.001f);

    float omega_p = 22.0f * cbrtf(mGravity * mGravity / (windSpeed * Fetch));
    float k_p = omega_p * omega_p / mGravity;

    float c_p = sqrtf(mGravity / k_p + sigma_t * k_p / rho);

    float Omega = windSpeed / c_p;
    Omega = glm::clamp(Omega, 0.84f, 5.0f);

    float alpha_e = 0.006f * sqrtf(Omega);
    alpha_e = glm::clamp(alpha_e, 0.0028f, 0.015f);

    float beta_e = 0.229f * expf(-0.4f * powf(Omega / Omega_c - 1.0f, 2.0f));

    float Lpm = expf(-1.25f * powf(k_p / k_length, 2.0f));

    float gamma = Maturity;
    float sigma_j = (k_length <= k_p) ? 0.07f : 0.09f;
    float r = expf(-powf(sqrtf(k_length / k_p) - 1.0f, 2.0f) / (2.0f * sigma_j * sigma_j));
    float Gamma_j = powf(gamma, r);

    float Bl = 0.5f * alpha_e * (c_p / c_phase) * Lpm * Gamma_j;

    const float k_m = sqrtf(rho * mGravity / sigma_t);
    float Fm = expf(-0.25f * powf(k_length / k_m - 1.0f, 2.0f));
    float Bh = 0.5f * beta_e * (u_star / c_phase) * Fm;
    float cap_sat = expf(-k_length2 / (k_m * k_m));
    Bh *= cap_sat;

    // ── Conversion vers densité d'énergie ─────────────────────────────────
    // Elfouhaily est formulé en saturation B(k) adimensionnelle
    // On convertit vers S(k) en densité d'énergie comme JONSWAP : S(k) = B(k) / k⁴  (au lieu de / k² dans la formulation saturation)
    float k_length4 = k_length2 * k_length2;
    //float W_k = (Bl + Bh) / (k_length2 * k_length); // k³ (formule originale)
    float W_k = (Bl + Bh) / k_length4;  // k⁴

    // ── Distribution directionnelle Donelan-Banner ────────────────────────
    float omega = sqrtf(mGravity * k_length);
    float theta = acosf(glm::clamp(k_dot_w, -1.0f, 1.0f));
    float ratio = omega / omega_p;
    float beta_dir;

    if (ratio < 0.95f)
        beta_dir = 2.61f * powf(ratio, 1.3f);
    else if (ratio <= 1.6f)
        beta_dir = 2.28f * powf(ratio, -1.3f);
    else
        beta_dir = powf(10.0f, -0.4f + 0.8393f * expf(-0.567f * logf(ratio * ratio)));

    float sech_val = 2.0f / (expf(beta_dir * theta) + expf(-beta_dir * theta));
    float D = (beta_dir / 2.0f) * sech_val * sech_val;

    // ── Suppression des petites vagues ────────────────────────────────────
    float L = windSpeed * windSpeed / mGravity;
    float l2 = L * L * 0.0001f * 0.0001f;
    float suppress = expf(-k_length2 * l2);

    // Coupure à k_cutoff = π / résolution_texel = π / 0.39 ≈ 8 rad/m
    float k_cutoff = M_PI * FFT_SIZE / LengthWave * 0.25f;  // 25% de la fréquence max
    float lowpass = expf(-k_length2 / (k_cutoff * k_cutoff));
    float S = W_k * D * suppress * lowpass;

    return S;
}
float Ocean::Horvath(vec2 k)
{
    float k_length = glm::length(k);

    // Avoid division by zero
    if (k_length < 1e-6f)
        return 0.0f;

    float k_length2 = k_length * k_length;
    float k_length4 = k_length2 * k_length2;

    float windSpeed = glm::length(Wind);
    vec2  windDir = glm::normalize(Wind);

    // Direction : supprime les vagues contre le vent
    float k_dot_w = glm::dot(glm::normalize(k), windDir);
    if (k_dot_w < 0.0f)
        return 0.0f;

    // ── Fréquence angulaire ───────────────────────────────────────────────
    float omega = sqrtf(mGravity * k_length);           // ω = sqrt(g·|k|)
    float omega2 = omega * omega;

    // ── Vitesse de phase et nombre de Froude local ────────────────────────
    float cp = omega / k_length;                      // vitesse de phase c = ω/k
    float Omega = windSpeed / cp;                        // inverse Froude : U/c

    // ── Fréquence de pic (Donelan empirique) ─────────────────────────────
    float omega_p = 22.0f * cbrtf(mGravity * mGravity / (windSpeed * Fetch));    // ωp dépend du fetch via la relation de similitude
    float cp_p = mGravity / omega_p;                    // vitesse de phase au pic

    // ── Coefficient α de Horvath (remplace α=0.0081 fixe) ────────────────
    // α varie avec le développement de la mer (Omega = U/cp) - Calibré sur données JONSWAP + mesures Donelan
    float alpha_h = 0.006f * sqrtf(Omega);                // α = 0.006 * sqrt(U/cp)
    alpha_h = glm::clamp(alpha_h, 0.0028f, 0.015f); // bornes physiques

    // ── Spectre de base Horvath ───────────────────────────────────────────
    float S_base = alpha_h * mGravity * mGravity / k_length4;    // Même forme que P-M mais avec α variable et exposant corrigé

    // ── Pic de résonance (JONSWAP γ) ─────────────────────────────────────
    float gamma = Maturity;
    float sigma = (omega <= omega_p) ? 0.07f : 0.09f;
    float r = expf(-powf(omega - omega_p, 2.0f) / (2.0f * sigma * sigma * omega_p * omega_p));
    float peak = powf(gamma, r);

    // ── Terme de vieillissement de la mer (swell aging) ──────────────────
    // Horvath introduit un terme L_pm qui modélise la saturation du spectre quand la mer est pleinement développée (Omega → 1)
    float L_pm = expf(-1.25f * powf(omega_p / omega, 4.0f)); // Pierson-Moskowitz shape
    float Gamma_h = expf(-powf(Omega - 1.0f, 2.0f) / 0.04f);   // saturation curve
    float J_p = powf(gamma, Gamma_h);                        // pic de Horvath

    float S_horvath = S_base * J_p * L_pm;

    // ── Distribution directionnelle Donelan-Banner (Horvath la conserve) ──
    float theta = acosf(glm::clamp(k_dot_w, -1.0f, 1.0f));
    float ratio = omega / omega_p;
    float beta_dir;

    if (ratio < 0.95f)
        beta_dir = 2.61f * powf(ratio, 1.3f);
    else if (ratio <= 1.6f)
        beta_dir = 2.28f * powf(ratio, -1.3f);
    else
        beta_dir = powf(10.0f, -0.4f + 0.8393f * expf(-0.567f * logf(ratio * ratio)));

    float sech_val = 2.0f / (expf(beta_dir * theta) + expf(-beta_dir * theta));
    float D = (beta_dir / 2.0f) * sech_val * sech_val;

    // ── Correction de fetch (Horvath spécifique) ──────────────────────────
    // Modélise l'évolution du spectre avec la distance parcourue par le vent (fetch court → spectre étroit et haut, fetch long → spectre large et bas)
    float fetch_factor = tanhf(powf(mGravity * Fetch / (windSpeed * windSpeed), 0.33f));
    fetch_factor = glm::clamp(fetch_factor, 0.1f, 1.0f);

    // ── Terme de capillaires / tension de surface ─────────────────────────
    // Horvath ajoute une correction haute fréquence pour les capillaires
    // σ_t = 0.074 N/m (tension de surface de l'eau)
    // k_c = sqrt(ρg / σ_t) ≈ 363 rad/m (nombre d'onde capillaire)
    const float sigma_t = 0.074f;
    const float rho = 1025.0f;                         // densité eau de mer kg/m³
    float k_c = sqrtf(rho * mGravity / sigma_t); // ~363 rad/m
    float cap_suppress = expf(-k_length2 / (k_c * k_c));    // Atténuation exponentielle au-delà du nombre d'onde capillaire

    // ── Suppression des petites vagues (même que JONSWAP) ─────────────────
    float l_small = 0.01f * windSpeed * windSpeed / mGravity;
    float suppress = expf(-k_length2 * l_small * l_small);

    // ── Résultat final ────────────────────────────────────────────────────
    float S = S_horvath * D * fetch_factor * cap_suppress * suppress;

    return S;
}

bool Ocean::GetVerticeXZ(vec2 pos, vec3& output)
{
    int x = (static_cast<int>(pos.x * MESH_SIZE / PATCH_SIZE) + MESH_SIZE / 2) % MESH_SIZE;
    if (x < 0) x += MESH_SIZE;

    int z = (static_cast<int>(pos.y * MESH_SIZE / PATCH_SIZE) + MESH_SIZE / 2) % MESH_SIZE;
    if (z < 0) z += MESH_SIZE;

    int xFft = (x - MESH_SIZE / 2) * FFT_SIZE / MESH_SIZE + FFT_SIZE / 2;
    int yFft = (z - MESH_SIZE / 2) * FFT_SIZE / MESH_SIZE + FFT_SIZE / 2;
    int index = 4 * (yFft * FFT_SIZE + xFft);

    // Outside the map
    if (index < 0 || index >= FFT_SIZE * FFT_SIZE * 4)
        return false;

    output = { pos.x + mPixelsDisplacements[index + 0], mPixelsDisplacements[index + 1] , pos.y + mPixelsDisplacements[index + 2] };
    return true;
}
bool Ocean::GetVerticeXYZ(vec3 pos, vec3& output)
{
    int x = (static_cast<int>(pos.x * MESH_SIZE / PATCH_SIZE) + MESH_SIZE / 2) % MESH_SIZE;
    if (x < 0) x += MESH_SIZE;

    int z = (static_cast<int>(pos.z * MESH_SIZE / PATCH_SIZE) + MESH_SIZE / 2) % MESH_SIZE;
    if (z < 0) z += MESH_SIZE;

    int xFft = (x - MESH_SIZE / 2) * FFT_SIZE / MESH_SIZE + FFT_SIZE / 2;
    int yFft = (z - MESH_SIZE / 2) * FFT_SIZE / MESH_SIZE + FFT_SIZE / 2;
    int index = 4 * (yFft * FFT_SIZE + xFft);

    // Outside the map
    if (index < 0 || index >= FFT_SIZE * FFT_SIZE * 4)
        return false;

    output = { pos.x + mPixelsDisplacements[index + 0], mPixelsDisplacements[index + 1] , pos.z + mPixelsDisplacements[index + 2] };
    return true;
}

// Analysis
vector<vec2> Ocean::GetCut(int xN)
{
    int index;
    vector<vec2> vHeights(FFT_SIZE_1);
    int count = FFT_SIZE_1 - 1;
    for (int m_prime = 0; m_prime < FFT_SIZE; m_prime++)
    {
        index = 4 * (m_prime * FFT_SIZE + xN);
        vHeights[count--] = vec2(mPixelsDisplacements[index + 2], mPixelsDisplacements[index + 1]);
    }
    return vHeights;
}
void Ocean::GetRecordFromBuoy(vec2 pos, float t)
{
    // Get index
    int x = (MESH_SIZE / 2 + pos.x) * FFT_SIZE / MESH_SIZE;
    int z = (MESH_SIZE / 2 + pos.y) * FFT_SIZE / MESH_SIZE;
    int index = 4 * (z * FFT_SIZE + x);

    // Store data (time, dx, dy, dz)
    WaveData wd;
    wd.time = t;
    wd.dx = mPixelsDisplacements[index + 0];
    wd.dy = mPixelsDisplacements[index + 1];
    wd.dz = mPixelsDisplacements[index + 2];

    if (vWaveData.size() < 2 || wd.dy != vWaveData[vWaveData.size() - 1].dy)
        vWaveData.push_back(wd);

    // Store the maximum and minimum
    if (wd.dy > HeightMax)  HeightMax = wd.dy;
    if (wd.dy < HeightMin)  HeightMin = wd.dy;

    // Tells the other functions that new data has been added
    bNewData = true;
}
void Ocean::ClearRecords()
{
    vWaveData.clear();
    bNewData = false;
    HeightMax = 0.0f;
    HeightMin = 0.0f;
}
bool Ocean::GetWaveByWaveAnalysis(float& waves1_3, float& waveMax, int& nWaves, float& average_period)
{
    if (!bNewData)
        return false;

    const size_t size = vWaveData.size();
    if (size < 3)
        return false;

    vector<size_t> crest_indices;
    vector<size_t> trough_indices;

    static vector<float> vWaves;
    static vector<float> vPeriods;

    // Peak and trough detection
    for (size_t i = 1; i < size - 1; ++i)
    {
        float prev = vWaveData[i - 1].dy;
        float curr = vWaveData[i].dy;
        float next = vWaveData[i + 1].dy;

        if (prev < curr && curr > next)         // curr is at the top
            crest_indices.push_back(i);
        else if (prev > curr && curr < next)    // curr is at the bottom
            trough_indices.push_back(i);
    }

    if (crest_indices.size() < 2 || trough_indices.empty())
        return false;

    vWaves.clear();
    vPeriods.clear();

    for (size_t i = 0; i < crest_indices.size() - 1; ++i)
    {
        size_t crest1 = crest_indices[i];
        size_t crest2 = crest_indices[i + 1];

        // Find the deepest trough between the two crests
        float best_dy = FLT_MAX;
        size_t best_idx = SIZE_MAX;
        for (size_t ti : trough_indices)
        {
            if (ti > crest1 && ti < crest2 && vWaveData[ti].dy < best_dy)
            {
                best_dy = vWaveData[ti].dy;
                best_idx = ti;
            }
        }

        if (best_idx == SIZE_MAX)
            continue;

        float height = vWaveData[crest1].dy - best_dy;
        if (height > 0)
        {
            vWaves.push_back(height);
            float period = vWaveData[crest2].time - vWaveData[crest1].time;
            vPeriods.push_back(period);
        }
    }

    nWaves = (int)vWaves.size();
    if (nWaves == 0)
        return false;

    average_period = std::accumulate(vPeriods.begin(), vPeriods.end(), 0.0f) / vPeriods.size();

    std::sort(vWaves.begin(), vWaves.end(), std::greater<float>());
    waveMax = vWaves[0];

    size_t nbSignificantWaves = nWaves / 3;
    if (nbSignificantWaves == 0)
        return false;

    waves1_3 = std::accumulate(vWaves.begin(), vWaves.begin() + nbSignificantWaves, 0.0f) / nbSignificantWaves;

    bNewData = false;
    return true;
}
void Ocean::GetSpectrumStats(vector<float>& vS)
{
    float Sum = 0.0f;
    for (const auto& valeur : vS)
        Sum += valeur;

    float moyenne = Sum / (FFT_SIZE_1 * FFT_SIZE_1);

    std::sort(vS.begin(), vS.end());
    float mediane;
    if (vS.size() % 2 == 0)
        mediane = (vS[vS.size() / 2 - 1] + vS[vS.size() / 2]) / 2.0f;
    else
        mediane = vS[vS.size() / 2];

    float ecart_type = 0.0f;
    for (const auto& valeur : vS)
        ecart_type += std::pow(valeur - moyenne, 2);

    ecart_type = std::sqrt(ecart_type / vS.size());

    float minimum = *std::min_element(vS.begin(), vS.end());
    float maximum = *std::max_element(vS.begin(), vS.end());

    std::map<int, int> frequences;
    for (const auto& valeur : vS)
        frequences[static_cast<int>(valeur)]++;

    cout << "= S P E C T R E  I N I T I A L =================" << endl;
    std::cout << "Moyenne : " << moyenne << std::endl;
    std::cout << "Mediane : " << mediane << std::endl;
    std::cout << "Ecart-type : " << ecart_type << std::endl;
    std::cout << "Minimum : " << minimum << std::endl;
    std::cout << "Maximum : " << maximum << std::endl;

    std::cout << "Frequences :" << std::endl;
    for (const auto& paire : frequences)
        std::cout << "Valeur " << paire.first << " : " << paire.second << " occurrences" << std::endl;
}
double GetPeriodPeak(const vector<double>& densiteSpectrale, const vector<double>& frequences)
{
    if (densiteSpectrale.size() < 3 || frequences.size() < 3)
        return 0.0;

    // Ignore DC ET dernière position (out of bounds)
    auto it_max = std::max_element(densiteSpectrale.begin() + 1, densiteSpectrale.end() - 1);
    size_t i = std::distance(densiteSpectrale.begin(), it_max);

    // Fallback si pas interpolable
    if (i < 1 || i >= frequences.size() - 1 || frequences[i] <= 0.0)
        return frequences.size() > i ? 1.0 / frequences[i] : 1.0;

    // Interpolation parabolique (maintenant SAFE)
    double f1 = frequences[i - 1], S1 = densiteSpectrale[i - 1];
    double f2 = frequences[i], S2 = *it_max;
    double f3 = frequences[i + 1], S3 = densiteSpectrale[i + 1];

    double denom = S1 - 2 * S2 + S3;
    if (fabs(denom) < 1e-10)  // Évite division par zéro
        return 1.0 / f2;

    double delta_f = 0.5 * (S1 - S3) / denom;
    double f_peak = f2 + delta_f * (f3 - f2);

    // Clamp dans bin (stabilité numérique)
    f_peak = glm::clamp(f_peak, f1, f3);

    return 1.0 / f_peak;
}
double Ocean::GetSpectralMoment(const vector<double>& densiteSpectrale, double df, int ordre)
{
    double moment = 0.0;
    const auto& frequences = a_Frequences;  // ← Récup vrai f[]

    for (size_t i = 1; i < densiteSpectrale.size() && i < frequences.size(); i++)
    {
        double f = frequences[i];  // ← VRAIES fréquences FFT
        moment += std::pow(f, ordre) * densiteSpectrale[i] * df;
    }
    return moment;
}
pair<vector<double>, vector<double>> Ocean::GetFrequencies()
{
    int N = vWaveData.size();

    double dt = 0;
    for (int i = 1; i < N; ++i)
        dt += vWaveData[i].time - vWaveData[i - 1].time;
    dt /= (N - 1);

    double df = 1.0 / (N * dt);

    double mean = 0.0;
    for (int i = 0; i < N; ++i)
        mean += vWaveData[i].dy;
    mean /= N;

    vector<double> heights(N);
    for (int i = 0; i < N; ++i)
        heights[i] = vWaveData[i].dy - mean;

    double windowEnergyCorrection = 0.0;
    for (int n = 0; n < N; ++n)
    {
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * n / (N - 1)));
        heights[n] *= w;
        windowEnergyCorrection += w * w;
    }
    windowEnergyCorrection /= N;

    fftwf_complex* in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    fftwf_complex* out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);

    for (int i = 0; i < N; ++i)
    {
        in[i][0] = (float)heights[i];
        in[i][1] = 0.0f;
    }

    fftwf_plan p = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    fftwf_execute(p);

    vector<double> frequences(N / 2 + 1);
    vector<double> densiteSpectrale(N / 2 + 1);

    for (int i = 0; i <= N / 2; ++i)
    {
        frequences[i] = i * df;
        double re = out[i][0];
        double im = out[i][1];
        densiteSpectrale[i] = (re * re + im * im) * dt / (N * windowEnergyCorrection);
    }

    for (int i = 1; i < N / 2; ++i)
        densiteSpectrale[i] *= 2.0;

    fftwf_destroy_plan(p);
    fftwf_free(in);
    fftwf_free(out);

    // Lissage exponentiel
    const double alpha = 0.1;
    if (a_SpectreAccumule.empty())
    {
        // Premier appel après reset : initialisation directe
        a_SpectreAccumule = densiteSpectrale;
    }
    else if (a_SpectreAccumule.size() != densiteSpectrale.size())
    {
        // Taille différente (ne devrait plus arriver) : reset propre
        a_SpectreAccumule = densiteSpectrale;
    }
    else
    {
        for (size_t i = 0; i < densiteSpectrale.size(); ++i)
            a_SpectreAccumule[i] = (1.0 - alpha) * a_SpectreAccumule[i]
            + alpha * densiteSpectrale[i];
    }

    // *** Stocker les fréquences ET le spectre lissé ***
    a_Frequences = frequences;
    a_DensiteSpectrale = a_SpectreAccumule;

    return make_pair(frequences, a_SpectreAccumule);
}
vector<sResultData> Ocean::SpectralAnalysis()
{
    GetFrequencies(); // met à jour a_Frequences, a_SpectreAccumule, a_DensiteSpectrale

    const auto& frequences = a_Frequences;
    const auto& densiteSpectrale = a_SpectreAccumule;
    double df = frequences.size() > 1 ? frequences[1] : 1.0;

    double m0 = GetSpectralMoment(densiteSpectrale, df, 0);
    double m1 = GetSpectralMoment(densiteSpectrale, df, 1);
    double m2 = GetSpectralMoment(densiteSpectrale, df, 2);

    vector<sResultData> vResults;
    sResultData rd;

    double Tm = (m1 > 0) ? m0 / m1 : 0.0;
    double Hm0 = 4.0 * sqrt(m0);
    double Hmax = (vWaveData.size() > 1) ? Hm0 * sqrt(0.5 * log((double)vWaveData.size())) : 0.0;    
    double L = (9.81 * Tm * Tm) / (2.0 * M_PI);
    double cambrure = (L > 0) ? Hm0 / L : 0.0;
    double Tp = GetPeriodPeak(densiteSpectrale, frequences);
    double rho = 1025.0;
    double P = (rho * 9.81 * 9.81 * m0 * Tm) / (64.0 * M_PI);
    double Tm02 = (m2 > 0) ? sqrt(m0 / m2) : 0.0;
    double Tp_shallow = (m0 > 0) ? 2.0 * M_PI * sqrt(L / mGravity) : 0.0;
    double m4 = GetSpectralMoment(densiteSpectrale, df, 4);
    double epsilon = (m0 * m4 > 0) ? sqrtf(1.0f - m2 * m2 / (m0 * m4)) : 0.0;

    // 1. IDENTIFICATION & VALIDITÉ
    rd.variable = "Set of"; 
    rd.value = vWaveData.size();
    rd.decimal = 0; 
    rd.unit = "pts";
    vResults.push_back(rd);
    
    rd.variable = "Record duration"; 
    rd.value = vWaveData.back().time - vWaveData.front().time;
    rd.decimal = 0;
    rd.unit = "s"; 
    vResults.push_back(rd);

    // 2. HAUTEURS (primaires - ce que voient les marins)
    rd.variable = "Hm0 (spectral)"; 
    rd.value = Hm0; 
    rd.decimal = 4; 
    rd.unit = "m";
    vResults.push_back(rd);
    
    rd.variable = "Hmax (theoritical)";
    rd.value = Hmax; 
    rd.decimal = 4; 
    rd.unit = "m";
    vResults.push_back(rd);

    // 3. PÉRIODES (crucial pour navigation/propagation)
    rd.variable = "Peak period Tp"; 
    rd.value = Tp; 
    rd.decimal = 4;
    rd.unit = "s";
    vResults.push_back(rd);

    rd.variable = "Tm01 (average)"; 
    rd.value = Tm; 
    rd.decimal = 4;
    rd.unit = "s";
    vResults.push_back(rd);

    rd.variable = "Tm02 (zero-cross)"; 
    rd.value = Tm02;
    rd.decimal = 4;
    rd.unit = "s";
    vResults.push_back(rd);

    // 4. CARACTÉRISTIQUES GÉOMÉTRIQUES
    rd.variable = "Wavelength"; 
    rd.value = L; 
    rd.decimal = 2;
    rd.unit = "m";
    vResults.push_back(rd);

    rd.variable = "Steepness"; 
    rd.value = cambrure; 
    rd.decimal = 4;
    rd.unit = "";
    vResults.push_back(rd);  // "Camber" → "Steepness"

    // 5. ÉNERGIE & PUISSANCE (ingénierie)
    rd.variable = "Total energy"; 
    rd.value = m0;
    rd.decimal = 4;
    rd.unit = "m2";
    vResults.push_back(rd);

    rd.variable = "Wave power"; 
    rd.value = P / 1000.0; 
    rd.decimal = 4;
    rd.unit = "kW/m";
    vResults.push_back(rd);  // /1000 pour kW

    // 6. DIAGNOSTIC SPECTRAL (avancé)
    rd.variable = "Spectral width"; 
    rd.value = epsilon; 
    rd.decimal = 4;
    rd.unit = "";
    vResults.push_back(rd);

    rd.variable = "Tp shallow"; 
    rd.value = Tp_shallow; 
    rd.decimal = 4;
    rd.unit = "s";
    vResults.push_back(rd);

    return vResults;
}
vector<sResultData> Ocean::DirectionalAnalysis()
{
    int N = vWaveData.size();

    if (N < 2) return vector<sResultData>();

    // Allocate tables for FFTW
    fftwf_complex* in_z, * out_z, * in_x, * out_x, * in_y, * out_y;
    fftwf_plan p_z, p_x, p_y;

    in_z = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    out_z = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    in_x = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    out_x = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    in_y = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);
    out_y = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * N);

    // Copy input vWaveData
    for (int i = 0; i < N; i++) {
        in_z[i][0] = vWaveData[i].dz; in_z[i][1] = 0.0;
        in_x[i][0] = vWaveData[i].dx; in_x[i][1] = 0.0;
        in_y[i][0] = vWaveData[i].dy; in_y[i][1] = 0.0;
    }

    // Create and execute FFT plans
    p_z = fftwf_plan_dft_1d(N, in_z, out_z, FFTW_FORWARD, FFTW_ESTIMATE);
    p_x = fftwf_plan_dft_1d(N, in_x, out_x, FFTW_FORWARD, FFTW_ESTIMATE);
    p_y = fftwf_plan_dft_1d(N, in_y, out_y, FFTW_FORWARD, FFTW_ESTIMATE);

    fftwf_execute(p_z);
    fftwf_execute(p_x);
    fftwf_execute(p_y);

    // Calculate the directional spectrum
    vector<double> frequences(N / 2 + 1);
    vector<double> densiteSpectrale(N / 2 + 1);
    vector<double> directions(N / 2 + 1);
    vector<double> etalement(N / 2 + 1);

    double dt = 0;
    for (int i = 1; i < N; ++i)
        dt += vWaveData[i].time - vWaveData[i - 1].time;
    dt /= (N - 1);
    double df = 1.0 / (N * dt);

    for (int i = 0; i <= N / 2; i++)
    {
        frequences[i] = i * df;
        double Czz = out_z[i][0] * out_z[i][0] + out_z[i][1] * out_z[i][1];
        double Cxx = out_x[i][0] * out_x[i][0] + out_x[i][1] * out_x[i][1];
        double Cyy = out_y[i][0] * out_y[i][0] + out_y[i][1] * out_y[i][1];
        double Qxz = out_x[i][0] * out_z[i][1] - out_x[i][1] * out_z[i][0];
        double Qyz = out_y[i][0] * out_z[i][1] - out_y[i][1] * out_z[i][0];

        densiteSpectrale[i] = Czz / (N * N);

        // Corrected direction calculation for OpenGL
        double dir = atan2(Qxz, Qyz);
        dir = fmod(dir + 2 * M_PI, 2 * M_PI);  // Ensure that dir is between 0 and 2π
        dir = dir * 180 / M_PI;  // Convert to degrees

        // Adjust so that 0° is south and 90° is east
        directions[i] = fmod(540 - dir, 360);  // 450 = 360 + 90, to shift the 0 to the south

        // Calculation of the spread
        double denom = Czz * (Cxx + Cyy);
        double r = (denom > 1e-30) ? sqrt((Qxz * Qxz + Qyz * Qyz) / denom) : 0.0;
        etalement[i] = sqrt(2 * (1 - r)) * 180 / M_PI;  // Convert to degrees
    }

    // Store the vectors for rendering
    a_Directions = directions;

    // Compute Hm0
    double m0 = 0.0;
    for (int i = 0; i <= N / 2; i++)
        m0 += densiteSpectrale[i] * df;

    double Hm0 = 4 * sqrt(m0);

    // Find corresponding Tp and Dir
    auto it_max = std::max_element(densiteSpectrale.begin(), densiteSpectrale.end());
    int index_pic = std::distance(densiteSpectrale.begin(), it_max);
    double Tp = 1.0 / frequences[index_pic];
    double Dir = directions[index_pic];

    // Calculate Energy Weighted Average
    double Etal = 0.0;
    double totalEnergy = 0.0;
    for (int i = 0; i <= N / 2; i++)
    {
        Etal += etalement[i] * densiteSpectrale[i];
        totalEnergy += densiteSpectrale[i];
    }
    Etal /= totalEnergy;

    // Cleaning
    fftwf_destroy_plan(p_z);
    fftwf_destroy_plan(p_x);
    fftwf_destroy_plan(p_y);
    fftwf_free(in_z); fftwf_free(out_z);
    fftwf_free(in_x); fftwf_free(out_x);
    fftwf_free(in_y); fftwf_free(out_y);

    vector<sResultData> vResults;
    sResultData rd;

    rd.variable = "Dir";
    Dir = fmod(450.0f - Dir, 360.0f);
    if (Dir < 0.0f)
        Dir += 360.0f;
    rd.value = Dir;
    rd.decimal = 2;
    rd.unit = "\xc2\xb0";
    vResults.push_back(rd);

    rd.variable = "Spread";
    rd.value = Etal;
    rd.decimal = 2;
    rd.unit = "\xc2\xb0";
    vResults.push_back(rd);

    return vResults;
}

// Textures
void Ocean::CreateTextures()
{
    // Initial spectrum & frequencies
    mTexInitialSpectrum.Create(mVulkanDevice, FFT_SIZE_1, FFT_SIZE_1, 1, VK_FORMAT_R32G32_SFLOAT, true);
    mTexFrequencies.Create(mVulkanDevice, FFT_SIZE_1, FFT_SIZE_1, 1, VK_FORMAT_R32_SFLOAT, true);

    // Updated spectra [2]
    mTexUpdatedSpectra[0].Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R32G32_SFLOAT, false);
    mTexUpdatedSpectra[1].Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R32G32_SFLOAT, false);

    // Temp data for IFFT
    mTexTempData.Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R32G32_SFLOAT, false);

    // Displacements
    mTexDisplacements.Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R32G32B32A32_SFLOAT, false);

    // Gradients 
    mTexGradients.Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R16G16B16A16_SFLOAT, false);

    // Foam accumulation [2]
    mTexFoamAcc[0].Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R32_SFLOAT, false);
    mTexFoamAcc[1].Create(mVulkanDevice, FFT_SIZE, FFT_SIZE, 1, VK_FORMAT_R32_SFLOAT, false);
    mTexFoamBuffer = &mTexFoamAcc[0];

    mPixelsDisplacements = make_unique<float[]>(FFT_SIZE * FFT_SIZE * 4);
    mPixelsFoam = std::make_unique<float[]>(FFT_SIZE * FFT_SIZE);

    // Environment map
    mTexEnvmap.CreateFromFile(mVulkanDevice, "Resources/Textures/kloofendal_misty_morning_puresky_2k.hdr");

    // Foam textures
    mTexFoamIntensity.CreateFromFile(mVulkanDevice, "Resources/Textures/foam_intensity.png");
    mTexFoamBubbles.CreateFromFile(mVulkanDevice, "Resources/Textures/foam_bubbles.png");
    mTexFoamTexture.CreateFromFile(mVulkanDevice, "Resources/Textures/seamless-seawater-with-foam-1.jpg");
    mTexWaterdUdV.CreateFromFile(mVulkanDevice, "Resources/Textures/waterDUDV.png");
}
void Ocean::CreateTexture2DArray()
{
	mTexKelvinArray.CreateTexture2DArray(mVulkanDevice, "Resources/Kelvin/", "Kelvin-1024_Fr-", 100, 1024, 1024);
}

// Mesh
void Ocean::CreateMesh()
{
    // 1. GÉNÉRATION DES DONNÉES (identique OpenGL)
    sVertexOcean* vdata = new sVertexOcean[MESH_SIZE_1 * MESH_SIZE_1];
    for (int z = 0; z <= MESH_SIZE; ++z)
    {
        for (int x = 0; x <= MESH_SIZE; ++x)
        {
            int index = z * MESH_SIZE_1 + x;
            vdata[index].position.x = (x - MESH_SIZE / 2.0f) * PATCH_SIZE / MESH_SIZE;
            vdata[index].position.y = 0.0f;
            vdata[index].position.z = (z - MESH_SIZE / 2.0f) * PATCH_SIZE / MESH_SIZE;
            vdata[index].texCoord.x = (float)x / (float)MESH_SIZE;
            vdata[index].texCoord.y = (float)z / (float)MESH_SIZE;
        }
    }

    unsigned int* idata = new unsigned int[MESH_SIZE * MESH_SIZE * 6];
    mIndicesCount = 0;
    for (int z = 0; z < MESH_SIZE; ++z)
    {
        for (int x = 0; x < MESH_SIZE; ++x)
        {
            int index = z * MESH_SIZE_1 + x;
            // Two triangles (identique)
            idata[mIndicesCount++] = index;
            idata[mIndicesCount++] = index + MESH_SIZE_1;
            idata[mIndicesCount++] = index + MESH_SIZE_1 + 1;
            idata[mIndicesCount++] = index;
            idata[mIndicesCount++] = index + MESH_SIZE_1 + 1;
            idata[mIndicesCount++] = index + 1;
        }
    }

    // Vertex buffer
    VkDeviceSize size = MESH_SIZE_1 * MESH_SIZE_1 * sizeof(sVertexOcean);
    VulkanBuffer vertexBuffer = VulkanBuffer(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data;
    vkMapMemory(mVulkanDevice->device, vertexBuffer.bufferMemory, 0, size, 0, &data);
    memcpy(data, vdata, (size_t)size);
    vkUnmapMemory(mVulkanDevice->device, vertexBuffer.bufferMemory);

    mVertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mVertexBuffer->CopyIntoBuffer(vertexBuffer, size);

	// Index buffer
    size = mIndicesCount * sizeof(uint32_t);
    VulkanBuffer indexBuffer = VulkanBuffer(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    vkMapMemory(mVulkanDevice->device, indexBuffer.bufferMemory, 0, size, 0, &data);
    memcpy(data, idata, (size_t)size);
    vkUnmapMemory(mVulkanDevice->device, indexBuffer.bufferMemory);
    
    mIndexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mIndexBuffer->CopyIntoBuffer(indexBuffer, size);

    // Cleanup
    delete[] vdata;
	delete[] idata;

#ifdef INFO_INIT    // Structures.h
    wcout << L"✅ Mesh : " << mIndicesCount << L" indices" << endl;
#endif
}
void Ocean::CreateLODMesh(int meshSize, vector<sVertexOcean>& vertices, vector<uint32_t>& indices) 
{
    int meshSize1 = meshSize + 1;
    vertices.resize(meshSize1 * meshSize1);
    indices.clear();

    // Création des vertices
    for (int z = 0; z <= meshSize; ++z) 
    {
        for (int x = 0; x <= meshSize; ++x) 
        {
            int index = z * meshSize1 + x;
            vertices[index].position.x = (x - meshSize / 2.0f) * PATCH_SIZE / meshSize;
            vertices[index].position.y = 0.0f;
            vertices[index].position.z = (z - meshSize / 2.0f) * PATCH_SIZE / meshSize;
            vertices[index].texCoord.x = (float)x / (float)meshSize;
            vertices[index].texCoord.y = (float)z / (float)meshSize;
        }
    }

    // Création des indices
    for (int z = 0; z < meshSize; ++z) 
    {
        for (int x = 0; x < meshSize; ++x) 
        {
            int index = z * meshSize1 + x;
            indices.push_back(index);
            indices.push_back(index + meshSize1);
            indices.push_back(index + meshSize1 + 1);
            indices.push_back(index);
            indices.push_back(index + meshSize1 + 1);
            indices.push_back(index + 1);
        }
    }
}
void Ocean::CreateLODMeshes() 
{
    mvLODPatches.clear();

    for (int meshSize : mvMeshSizes)
    {
        vector<sVertexOcean> vertices;
        vector<uint32_t> indices;

        // Génération mesh identique OpenGL
        CreateLODMesh(meshSize, vertices, indices);

        sLODPatch patch{};
        patch.indexCount = static_cast<uint32_t>(indices.size());

        // Vertex buffer
        VkDeviceSize size = vertices.size() * sizeof(sVertexOcean);
        VulkanBuffer vertexBuffer = VulkanBuffer(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        void* data;
        vkMapMemory(mVulkanDevice->device, vertexBuffer.bufferMemory, 0, size, 0, &data);
        memcpy(data, vertices.data(), (size_t)size);
        vkUnmapMemory(mVulkanDevice->device, vertexBuffer.bufferMemory);

        patch.vertexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        patch.vertexBuffer->CopyIntoBuffer(vertexBuffer, size);

        // Index buffer
		size = indices.size() * sizeof(uint32_t);
		VulkanBuffer indexBuffer = VulkanBuffer(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		
        vkMapMemory(mVulkanDevice->device, indexBuffer.bufferMemory, 0, size, 0, &data);
		memcpy(data, indices.data(), (size_t)size);
		vkUnmapMemory(mVulkanDevice->device, indexBuffer.bufferMemory);
		
        patch.indexBuffer = make_unique<VulkanBuffer>(mVulkanDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		patch.indexBuffer->CopyIntoBuffer(indexBuffer, size);

        mvLODPatches.push_back(std::move(patch));
    }

    // 3. Create instance buffers pour positions des patches (Structure: vec2 position, float lodLevel, float padding)
    const uint32_t MAX_INSTANCES_PER_FRAME = 100000;
    VkDeviceSize bufferSize = MAX_INSTANCES_PER_FRAME * sizeof(sInstanceData);

	mFrames.resize(g_FramesInFlight);
    for (int i = 0; i < g_FramesInFlight; i++)
        mFrames[i].ubo = make_unique<VulkanUBO>(mVulkanDevice, MAX_INSTANCES_PER_FRAME * sizeof(sInstanceData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}
void Ocean::CreateTimeBuffer()
{
    VkDeviceSize bufferSize = sizeof(float);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(mVulkanDevice->device, &bufferInfo, nullptr, &mTimeBuffer);

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(mVulkanDevice->device, mTimeBuffer, &memReq);
    uint32_t memType = FindMemoryType(mVulkanDevice->physicalDevice, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memType;
    vkAllocateMemory(mVulkanDevice->device, &allocInfo, nullptr, &mTimeMemory);
    vkBindBufferMemory(mVulkanDevice->device, mTimeBuffer, mTimeMemory, 0);

    vkMapMemory(mVulkanDevice->device, mTimeMemory, 0, bufferSize, 0, &mTimeData);
}

// Spectrum pipeline
void Ocean::CreateSpectrumPipeline() 
{
    // Suppression de l'ancien pipelineLayout d'abord
    if (mSpectrumDescriptorSetLayout)   vkDestroyDescriptorSetLayout(mVulkanDevice->device, mSpectrumDescriptorSetLayout, nullptr);
    if (mSpectrumPipelineLayout)        vkDestroyPipelineLayout(mVulkanDevice->device, mSpectrumPipelineLayout, nullptr);
    if (mSpectrumPipeline)              vkDestroyPipeline(mVulkanDevice->device, mSpectrumPipeline, nullptr);
    
    // 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_spectrum.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (4 texture + 1 UBO)
    VkDescriptorSetLayoutBinding bindings[5] = {
        {  0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mSpectrumDescriptorSetLayout);
    
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = 16;  // float time + padding std140

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mSpectrumDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1; 
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant; 
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mSpectrumPipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mSpectrumPipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mSpectrumPipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);
}
void Ocean::CreateSpectrumDescriptors() 
{
    VkDescriptorPoolSize poolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 } 
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mSpectrumDescriptorPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mSpectrumDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mSpectrumDescriptorSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mSpectrumDescriptorSet);
}
void Ocean::UpdateSpectrumDescriptors() 
{
    // Images 0-3
    array<VkDescriptorImageInfo, 4> imageInfos;
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[0].imageView = mTexInitialSpectrum.imageView;
    imageInfos[0].sampler = VK_NULL_HANDLE;

    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[1].imageView = mTexFrequencies.imageView;
    imageInfos[1].sampler = VK_NULL_HANDLE;

    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[2].imageView = mTexUpdatedSpectra[0].imageView;
    imageInfos[2].sampler = VK_NULL_HANDLE;

    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[3].imageView = mTexUpdatedSpectra[1].imageView;
    imageInfos[3].sampler = VK_NULL_HANDLE;

    // UBO binding 4
    VkDescriptorBufferInfo uboInfo{};
    uboInfo.buffer = mTimeBuffer;
    uboInfo.offset = 0;
    uboInfo.range = VK_WHOLE_SIZE;

    // 5 WRITES
    array<VkWriteDescriptorSet, 5> descriptorWrites{};

    // Images 0-3
    for (int i = 0; i < 4; i++) 
    {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = mSpectrumDescriptorSet;
        descriptorWrites[i].dstBinding = static_cast<uint32_t>(i);
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[i].pImageInfo = &imageInfos[i];
    }

    // UBO binding 4
    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = mSpectrumDescriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[4].pBufferInfo = &uboInfo;

    // Update 5 bindings
    vkUpdateDescriptorSets(mVulkanDevice->device, 5, descriptorWrites.data(), 0, nullptr);
}

// FFT inverse pipeline
void Ocean::CreateIfftPipeline()
{
    // Suppression de l'ancien pipelineLayout d'abord
    if (mIfftDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mVulkanDevice->device, mIfftDescriptorSetLayout, nullptr);
    if (mIfftPipelineLayout != VK_NULL_HANDLE)      vkDestroyPipelineLayout(mVulkanDevice->device, mIfftPipelineLayout, nullptr);
    if (mIfftPipeline != VK_NULL_HANDLE)            vkDestroyPipeline(mVulkanDevice->device, mIfftPipeline, nullptr);

    // 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_fft.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (2 textures)
    array<VkDescriptorSetLayoutBinding, 2> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mIfftDescriptorSetLayout);

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1; pipelineLayoutInfo.pSetLayouts = &mIfftDescriptorSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mIfftPipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mIfftPipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mIfftPipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);
}
void Ocean::CreateIfftDescriptors() 
{
    // Pool élargi : 4 sets × 2 STORAGE_IMAGE = 8 descripteurs
    VkDescriptorPoolSize poolSizes[1] = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 4; 
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Optionnel, pour debug
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mIfftDescriptorPool);

    // 4 layouts identiques (tous les sets ont la même structure)
    VkDescriptorSetLayout layouts[4];
    for (int i = 0; i < 4; i++) 
        layouts[i] = mIfftDescriptorSetLayout;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mIfftDescriptorPool;
    allocInfo.descriptorSetCount = 4;  // ← 4 sets
    allocInfo.pSetLayouts = layouts;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mIfftDescriptorSets);  // Remplit mIfftDescriptorSets[0..3]
}
void Ocean::UpdateIfftDescriptors(VulkanTexture& readTex, VulkanTexture& writeTex, int setIndex)
{
    // Pré-remplit mIfftImageInfo[setIndex] pour les 2 bindings (read+write)
    mIfftImageInfo[setIndex * 2 + 0] = {};  // Binding 0 : read
    mIfftImageInfo[setIndex * 2 + 0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    mIfftImageInfo[setIndex * 2 + 0].imageView = readTex.imageView;
    mIfftImageInfo[setIndex * 2 + 0].sampler = VK_NULL_HANDLE;

    mIfftImageInfo[setIndex * 2 + 1] = {};  // Binding 1 : write  
    mIfftImageInfo[setIndex * 2 + 1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    mIfftImageInfo[setIndex * 2 + 1].imageView = writeTex.imageView;
    mIfftImageInfo[setIndex * 2 + 1].sampler = VK_NULL_HANDLE;
}
void Ocean::PrecomputeAllIfftDescriptors()
{
    // Prépare les 8 imageInfos via 4 appels UpdateIfftDescriptors
    UpdateIfftDescriptors(mTexUpdatedSpectra[0], mTexTempData, 0);        // Set 0: H1
    UpdateIfftDescriptors(mTexTempData, mTexUpdatedSpectra[0], 1);        // Set 1: V1  
    UpdateIfftDescriptors(mTexUpdatedSpectra[1], mTexTempData, 2);        // Set 2: H2
    UpdateIfftDescriptors(mTexTempData, mTexUpdatedSpectra[1], 3);        // Set 3: V2

    // UN SEUL batch vkUpdateDescriptorSets pour TOUS les bindings
    VkWriteDescriptorSet writes[8];  // 4 sets × 2 bindings
    for (int setIndex = 0; setIndex < 4; setIndex++) 
    {
        for (int binding = 0; binding < 2; binding++) 
        {
            int writeIndex = setIndex * 2 + binding;
            writes[writeIndex] = {};
            writes[writeIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeIndex].dstSet = mIfftDescriptorSets[setIndex];
            writes[writeIndex].dstBinding = binding;  // 0=read, 1=write
            writes[writeIndex].descriptorCount = 1;
            writes[writeIndex].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[writeIndex].pImageInfo = &mIfftImageInfo[writeIndex];
        }
    }
    vkUpdateDescriptorSets(mVulkanDevice->device, 8, writes, 0, nullptr);  // Batch 8 writes !
}

// Displacement map pipeline
void Ocean::CreateDisplacementsPipeline()
{
    // Nettoyage précédent
    if (mDisplacementsPipeline) vkDestroyPipeline(mVulkanDevice->device, mDisplacementsPipeline, nullptr);
    if (mDisplacementsPipelineLayout) vkDestroyPipelineLayout(mVulkanDevice->device, mDisplacementsPipelineLayout, nullptr);
    if (mDisplacementsDescSetLayout) vkDestroyDescriptorSetLayout(mVulkanDevice->device, mDisplacementsDescSetLayout, nullptr);

    // 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_displacement.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (3 textures)
    array<VkDescriptorSetLayoutBinding, 3> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 0: heightfield (RG32F) 
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 1: choppyfield (RG32F)
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 2: displacement (RGBA32F)
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mDisplacementsDescSetLayout);

    // 4. PipelineLayout
    VkDescriptorSetLayout layouts[1] = { mDisplacementsDescSetLayout };
    
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(sDispPC);  // lambda + amplitude

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mDisplacementsPipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mDisplacementsPipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mDisplacementsPipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);
}
void Ocean::CreateDisplacementsDescriptors()
{
    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 };  // 3 images seulement
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mDisplacementsDescPool);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mDisplacementsDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &mDisplacementsDescSetLayout;
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, &mDisplacementsDescSet);
}
void Ocean::UpdateDisplacementsDescriptors()
{
    // DescriptorImageInfo
    VkDescriptorImageInfo imageInfos[3];
    imageInfos[0] = {}; 
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfos[0].imageView = mTexUpdatedSpectra[0].imageView; 
    imageInfos[0].sampler = VK_NULL_HANDLE;
    
    imageInfos[1] = {}; 
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos[1].imageView = mTexUpdatedSpectra[1].imageView; 
    imageInfos[1].sampler = VK_NULL_HANDLE;
   
    imageInfos[2] = {}; 
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos[2].imageView = mTexDisplacements.imageView; 
    imageInfos[2].sampler = VK_NULL_HANDLE;

    // WriteDescriptorSet initialisées
    VkWriteDescriptorSet writes[3] = {};

    // Write 0: heightfield
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = mDisplacementsDescSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &imageInfos[0];

    // Write 1: choppyfield  
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = mDisplacementsDescSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &imageInfos[1];

    // Write 2: displacement
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = mDisplacementsDescSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &imageInfos[2];

    vkUpdateDescriptorSets(mVulkanDevice->device, 3, writes, 0, nullptr);
}

// Gradients pipeline
void Ocean::CreateGradientsPipeline()
{
    if (mGradientsDescriptorSetLayout)  vkDestroyDescriptorSetLayout(mVulkanDevice->device, mGradientsDescriptorSetLayout, nullptr);
    if (mGradientsPipelineLayout)       vkDestroyPipelineLayout(mVulkanDevice->device, mGradientsPipelineLayout, nullptr);
    if (mGradientsPipeline)             vkDestroyPipeline(mVulkanDevice->device, mGradientsPipeline, nullptr);

	// 1. Shader
    auto shaderCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_gradients.comp");
    VkShaderModule shaderModule = CreateShaderModule(mVulkanDevice->device, shaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";

    // 3. DescriptorSetLayout (4 images : displacement, gradients, accfoam1, accfoam2)
    array<VkDescriptorSetLayoutBinding, 4> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 0: displacement 
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 1: gradients
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 2: accfoam1
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },   // Binding 3: accfoam2
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mGradientsDescriptorSetLayout);

    // Push constants : 2 floats (dt, persistenceFactor)
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 2;

    // 4. PipelineLayout
    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &mGradientsDescriptorSetLayout;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipeLayoutInfo, nullptr, &mGradientsPipelineLayout);

    // 12. Pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = mGradientsPipelineLayout;
    vkCreateComputePipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mGradientsPipeline);

    vkDestroyShaderModule(mVulkanDevice->device, shaderModule, nullptr);
}
void Ocean::CreateGradientsDescriptors()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 8;  // 4×2
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mGradientsDescriptorPool);

	// Tableau de 2 layouts identiques
    VkDescriptorSetLayout layouts[2] = { mGradientsDescriptorSetLayout, mGradientsDescriptorSetLayout };

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mGradientsDescriptorPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;

    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mGradientsDescriptorSets);
}
void Ocean::UpdateGradientsDescriptors()
{
    // Set 0 : accfoam1=Tex0(read), accfoam2=Tex1(write)
    VkDescriptorImageInfo imageInfos0[4];
    imageInfos0[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos0[0].imageView = mTexDisplacements.imageView;
    
    imageInfos0[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos0[1].imageView = mTexGradients.imageView;
   
    imageInfos0[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos0[2].imageView = mTexFoamAcc[0].imageView;  // read
   
    imageInfos0[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos0[3].imageView = mTexFoamAcc[1].imageView;  // write

    // Set 1 : accfoam1=Tex1(read), accfoam2=Tex0(write)
    VkDescriptorImageInfo imageInfos1[4];
    imageInfos1[0] = imageInfos0[0]; 
    
    imageInfos1[1] = imageInfos0[1];  // displacement/gradients identiques
    
    imageInfos1[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos1[2].imageView = mTexFoamAcc[1].imageView;  // read
   
    imageInfos1[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL; 
    imageInfos1[3].imageView = mTexFoamAcc[0].imageView;  // write

    VkWriteDescriptorSet writes[8];

    // Set 0
    for (int i = 0; i < 4; i++) 
    {
        writes[i * 2 + 0] = {};
        writes[i * 2 + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i * 2 + 0].dstSet = mGradientsDescriptorSets[0];
        writes[i * 2 + 0].dstBinding = i;
        writes[i * 2 + 0].descriptorCount = 1;
        writes[i * 2 + 0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i * 2 + 0].pImageInfo = &imageInfos0[i];
    }

    // Set 1
    for (int i = 0; i < 4; i++) 
    {
        writes[i * 2 + 1] = {};
        writes[i * 2 + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i * 2 + 1].dstSet = mGradientsDescriptorSets[1];
        writes[i * 2 + 1].dstBinding = i;
        writes[i * 2 + 1].descriptorCount = 1;
        writes[i * 2 + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i * 2 + 1].pImageInfo = &imageInfos1[i];
    }

    vkUpdateDescriptorSets(mVulkanDevice->device, 8, writes, 0, nullptr);
}

// Get displacement
void Ocean::InitDisplacementsBuffer()
{
    // Taille RGBA32F
    mStagingSize = mTexDisplacements.extent.width * mTexDisplacements.extent.height * 4ULL * sizeof(float);

    CreateBuffer(mVulkanDevice->device, mVulkanDevice->physicalDevice, mStagingSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		mStagingBuffer, mStagingMem);

    vkMapMemory(mVulkanDevice->device, mStagingMem, 0, mStagingSize, 0, &mStagingData);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;
    vkCreateFence(mVulkanDevice->device, &fenceInfo, nullptr, &mReadbackFence);
}
void Ocean::QueueAsyncReadback()
{
    if (mReadbackPending)
        return;

    // 1. Allouer command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = mVulkanDevice->graphicsCommandPool;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(mVulkanDevice->device, &allocInfo, &mReadbackCmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(mReadbackCmd, &beginInfo);

    // 2. Copy region
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { mTexDisplacements.extent.width, mTexDisplacements.extent.height, 1 };

    // 3. Copy GENERAL → staging slot
    vkCmdCopyImageToBuffer(mReadbackCmd, mTexDisplacements.image, VK_IMAGE_LAYOUT_GENERAL, mStagingBuffer, 1, &region);

    // 1. Terminer enregistrement
    vkEndCommandBuffer(mReadbackCmd);

    // Submit ASYNCHRONE
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mReadbackCmd;
    vkQueueSubmit(mVulkanDevice->graphicsQueue, 1, &submitInfo, mReadbackFence);

    mReadbackPending = true;
}
void Ocean::CheckReadbackCompletion()
{
    if (!mReadbackPending || vkGetFenceStatus(mVulkanDevice->device, mReadbackFence) != VK_SUCCESS)
        return;

    memcpy(mPixelsDisplacements.get(), mStagingData, mStagingSize);
    vkFreeCommandBuffers(mVulkanDevice->device, mVulkanDevice->graphicsCommandPool, 1, &mReadbackCmd);
    vkResetFences(mVulkanDevice->device, 1, &mReadbackFence);
    mReadbackPending = false;
}

// Get foam (whitecap coverage)
void Ocean::InitFoamBuffer()  // Appelez une fois
{
    mFoamStagingSize = FFT_SIZE * FFT_SIZE * sizeof(float);  // R32F = 1 float/pixel

    CreateBuffer(mVulkanDevice->device, mVulkanDevice->physicalDevice, mFoamStagingSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        mFoamStagingBuffer, mFoamStagingMem);

    vkMapMemory(mVulkanDevice->device, mFoamStagingMem, 0, mFoamStagingSize, 0, &mFoamStagingData);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;
    vkCreateFence(mVulkanDevice->device, &fenceInfo, nullptr, &mFoamReadbackFence);
}
void Ocean::QueueAsyncFoamReadback(uint32_t foamSlot)
{
    if (mFoamReadbackPending)
        return;

    // 1. Allouer command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = mVulkanDevice->graphicsCommandPool;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(mVulkanDevice->device, &allocInfo, &mFoamReadbackCmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(mFoamReadbackCmd, &beginInfo);

    // 2. Copy region
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { mTexFoamAcc[foamSlot].extent.width, mTexFoamAcc[foamSlot].extent.height, 1 };

    // 3. Copy GENERAL → staging slot
    vkCmdCopyImageToBuffer(mFoamReadbackCmd, mTexFoamAcc[foamSlot].image, VK_IMAGE_LAYOUT_GENERAL, mFoamStagingBuffer, 1, &region);

    // 1. Terminer enregistrement
    vkEndCommandBuffer(mFoamReadbackCmd);

    // Submit ASYNCHRONE
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &mFoamReadbackCmd;
    vkQueueSubmit(mVulkanDevice->graphicsQueue, 1, &submitInfo, mFoamReadbackFence);

    mFoamReadbackPending = true;
}
void Ocean::CheckFoamReadbackCompletion()
{
    if (!mFoamReadbackPending || vkGetFenceStatus(mVulkanDevice->device, mFoamReadbackFence) != VK_SUCCESS) return;

    memcpy(mPixelsFoam.get(), mFoamStagingData, mFoamStagingSize);

    // Calcul W !
    float sum = 0.f;
    for (uint32_t i = 0; i < FFT_SIZE * FFT_SIZE; i++)
        sum += mPixelsFoam[i];
    sum = sum / (float)(FFT_SIZE * FFT_SIZE);

    // Reset si changement de vent
	static float prevLambda = Lambda;
    static vec2 prevWind = Wind;
    static float cumulativeSum = 0.f;
    static uint32_t sampleCount = 0;
    if (Wind != prevWind || Lambda != prevLambda)
    {
        cumulativeSum = 0.f;
        sampleCount = 0;
        prevWind = Wind;
        prevLambda = Lambda;
    }

    // Moyenne cumulative sans parcourir l'historique
    cumulativeSum += sum;
    sampleCount++;

    static double prevTime = 0.0;
    bool bUpdate = false;
    double t = glfwGetTime();
    if (t - prevTime > 1.0)
    {
        bUpdate = true;
        prevTime = t;
    }

    if (bUpdate)
        WhitecapCoverageReal = (cumulativeSum / (float)sampleCount) * 100.f;

    vkFreeCommandBuffers(mVulkanDevice->device, mVulkanDevice->graphicsCommandPool, 1, &mFoamReadbackCmd);
    vkResetFences(mVulkanDevice->device, 1, &mFoamReadbackFence);
    mFoamReadbackPending = false;
}

// Update
void Ocean::Update(float t, uint32_t currentFrame)
{
    if (NeedsReinitFrequencies.exchange(false))
        InitFrequencies();
    if (NeedsClearRecords.exchange(false))
        ClearRecords();

    ComputeWasPending = false;

    // Attendre que le compute précédent de ce slot soit fini
    vkWaitForFences(mVulkanDevice->device, 1, &mComputeFence[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(mVulkanDevice->device, 1, &mComputeFence[currentFrame]);

    // Libérer le command buffer du slot précédent (maintenant safe)
    if (mComputeCmd[currentFrame] != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(mVulkanDevice->device, mVulkanDevice->graphicsCommandPool, 1, &mComputeCmd[currentFrame]);
        mComputeCmd[currentFrame] = VK_NULL_HANDLE;
    }

    memcpy(mTimeData, &t, sizeof(float));

    // Allouer le command buffer 
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = mVulkanDevice->graphicsCommandPool;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(mVulkanDevice->device, &allocInfo, &mComputeCmd[currentFrame]);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(mComputeCmd[currentFrame], &beginInfo);

    VkCommandBuffer cmd = mComputeCmd[currentFrame];

    vkCmdResetQueryPool(cmd, mQueryPool, 0, 8);

#pragma region SpectrumUpdate
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mQueryPool, 0);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mSpectrumPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mSpectrumPipelineLayout, 0, 1, &mSpectrumDescriptorSet, 0, nullptr);
    
    vkCmdPushConstants(cmd, mSpectrumPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &t);
    
    vkCmdDispatch(cmd, FFT_SIZE >> 4, FFT_SIZE >> 4, 1);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQueryPool, 1);
#pragma endregion

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mQueryPool, 2);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mIfftPipeline);

    VkMemoryBarrier barrierFull{};
    barrierFull.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrierFull.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrierFull.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

#pragma region Ifft0
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, nullptr, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mIfftPipelineLayout, 0, 1, &mIfftDescriptorSets[0], 0, nullptr);
    vkCmdDispatch(cmd, FFT_SIZE, 1, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, nullptr, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mIfftPipelineLayout, 0, 1, &mIfftDescriptorSets[1], 0, nullptr);
    vkCmdDispatch(cmd, FFT_SIZE, 1, 1);
#pragma endregion

#pragma region Ifft1
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, nullptr, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mIfftPipelineLayout, 0, 1, &mIfftDescriptorSets[2], 0, nullptr);
    vkCmdDispatch(cmd, FFT_SIZE, 1, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, nullptr, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mIfftPipelineLayout, 0, 1, &mIfftDescriptorSets[3], 0, nullptr);
    vkCmdDispatch(cmd, FFT_SIZE, 1, 1);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, nullptr, 0, nullptr);
#pragma endregion

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQueryPool, 3);

#pragma region Displacement
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mQueryPool, 4);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mDisplacementsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mDisplacementsPipelineLayout, 0, 1, &mDisplacementsDescSet, 0, nullptr);
   
    sDispPC pc;
    pc.Lambda = Lambda;
    pc.Amplitude = Amplitude;
    vkCmdPushConstants(cmd, mDisplacementsPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(sDispPC), &pc);
   
    vkCmdDispatch(cmd, FFT_SIZE / 16, FFT_SIZE / 16, 1);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQueryPool, 5);
#pragma endregion

#pragma region Gradients
    static float tOld = t;
    float dt = t - tOld;
    tOld = t;

    uint32_t currentSet = mFoamPingPong ? 1 : 0;
    mFoamPingPong = !mFoamPingPong;
    mTexFoamBuffer = &mTexFoamAcc[currentSet];

    struct { float dt; float persistenceFactor; } pcData{ dt, PersistenceFactor };

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, mQueryPool, 6);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrierFull, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mGradientsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, mGradientsPipelineLayout, 0, 1, &mGradientsDescriptorSets[currentSet], 0, nullptr);
    
    vkCmdPushConstants(cmd, mGradientsPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcData), &pcData);
    
    vkCmdDispatch(cmd, FFT_SIZE / 16, FFT_SIZE / 16, 1);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, mQueryPool, 7);
#pragma endregion

#pragma region FoamReadback
    // Transition foam écrit → TRANSFER_SRC
    VkImageMemoryBarrier foam_barrier{};
    foam_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    foam_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    foam_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    foam_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    foam_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    foam_barrier.image = mTexFoamAcc[currentSet].image;  // foam FRESH
    foam_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &foam_barrier);

    // Copy foam → staging (R32F)
    VkBufferImageCopy foam_region{};
    foam_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    foam_region.imageSubresource.mipLevel = 0;
    foam_region.imageSubresource.baseArrayLayer = 0;
    foam_region.imageSubresource.layerCount = 1;
    foam_region.imageExtent = { (uint32_t)FFT_SIZE, (uint32_t)FFT_SIZE, 1 };
    vkCmdCopyImageToBuffer(cmd, mTexFoamAcc[currentSet].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mFoamStagingBuffer, 1, &foam_region);

    // Transition back GENERAL
    foam_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    foam_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    foam_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    foam_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &foam_barrier);
#pragma endregion
    
    vkEndCommandBuffer(cmd);

    // Submit ASYNC avec signal du sémaphore
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &ComputeFinishedSem[currentFrame];

    vkQueueSubmit(mVulkanDevice->graphicsQueue, 1, &submitInfo, mComputeFence[currentFrame]);   // mComputeCmd[currentFrame] sera libéré au prochain Update de ce slot, après attente de la fence

    // Housekeeping
    mCurrentSpectrum = 1 - mCurrentSpectrum;
    QueueAsyncReadback();
    CheckReadbackCompletion();
    GetTimestamps();

    QueueAsyncFoamReadback(currentSet);
    CheckFoamReadbackCompletion();

    ComputeWasPending = true;
}
void Ocean::CreateQueryPool()
{
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = 8;   // 8 timestamps correspondant à 4 paires T0/T1

    if (vkCreateQueryPool(mVulkanDevice->device, &queryPoolInfo, nullptr, &mQueryPool) != VK_SUCCESS)
        throw runtime_error("Échec création query pool");

    vkResetQueryPool(mVulkanDevice->device, mQueryPool, 0, 8);

}
void Ocean::GetTimestamps()
{
    auto now = chrono::high_resolution_clock::now();

    if (mFirstRun)
    {
        mLastUpdateTime = now;
        mFirstRun = false;
    }

    // Vérifier si 1 seconde s'est écoulée
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - mLastUpdateTime);
    bool shouldUpdate = (elapsed.count() >= 1);

    uint64_t mTimestamps[8];
    VkResult result = vkGetQueryPoolResults(mVulkanDevice->device, mQueryPool, 0, 8, 8 * sizeof(uint64_t), mTimestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

    if (result == VK_SUCCESS) 
    {
        const char* names[] = { "Spectrum", "iFFT", "Displacement", "Gradient", "= COMPUTE ="};

        for (int i = 0; i < 4; i++) 
        {
            uint64_t deltaTicks = mTimestamps[2 * i + 1] - mTimestamps[2 * i];
            uint64_t ns = uint64_t(double(deltaTicks) * mVulkanDevice->timestampPeriod);

            // Toujours accumuler
            mTimestampAccumulators[i] += ns;
            mTimestampCounts[i]++;

            // Mettre à jour seulement si 1s écoulée
            if (shouldUpdate) 
            {
                double averageNs = static_cast<double>(mTimestampAccumulators[i]) / mTimestampCounts[i];
                OceanTimeStamps[i] = std::make_pair(std::string(names[i]), static_cast<uint64_t>(averageNs));

                // Reset accumulateurs
                mTimestampAccumulators[i] = 0;
                mTimestampCounts[i] = 0;
            }
        }
        uint64_t deltaTicks = mTimestamps[7] - mTimestamps[0];
        uint64_t ns = uint64_t(double(deltaTicks) * mVulkanDevice->timestampPeriod);
        mTimestampAccumulators[4] += ns;
        mTimestampCounts[4]++;
        if (shouldUpdate)
        {
            double averageNs = static_cast<double>(mTimestampAccumulators[4]) / mTimestampCounts[4];
            OceanTimeStamps[4] = std::make_pair(std::string(names[4]), static_cast<uint64_t>(averageNs));

            // Reset accumulateurs
            mTimestampAccumulators[4] = 0;
            mTimestampCounts[4] = 0;
        }

        // Mettre à jour le temps de référence après traitement
        if (shouldUpdate) 
            mLastUpdateTime = now;
    }
}

// Ocean pipeline (1 wireframe mesh)
void Ocean::CreatePipeline0()
{
    // Nettoyage précédent
	mWireframePipeline.destroy(mVulkanDevice->device);
    
    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_wireframe.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_wireframe.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(sVertexOcean);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    array<VkVertexInputAttributeDescription, 2> attribs = {};
    attribs[0].binding = 0;
    attribs[0].location = 0;  // sVertexOcean.position
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = 0;
   
    attribs[1].binding = 0;
    attribs[1].location = 1;  // sVertexOcean.texCoord
    attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[1].offset = offsetof(sVertexOcean, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout (2 bindings)
    array<VkDescriptorSetLayoutBinding, 2> bindings = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mWireframePipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mWireframePipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mWireframePipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissors
    VkViewport viewport = { 0.0f, 0.0f, (float)mExtent.width, (float)mExtent.height, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, mExtent };
  
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling
    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state

    // 12. Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = mWireframePipeline.pipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mWireframePipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateDescriptors0();
}
void Ocean::CreateDescriptors0()
{
    mWireframePipeline.descSet.resize(g_FramesInFlight);
    mWireframePipeline.ubo.resize(g_FramesInFlight);
    
    // 1. UBO Buffer
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mWireframePipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sMatrixColorUBO));
  
    // 2. Descriptor Pool: 1 UBO + 1 sampler par set, et N sets (1 par frame in flight)
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * g_FramesInFlight}, // 1 sampler par set
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 * g_FramesInFlight}  // 1 UBO   par set
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight; // 1 descriptor set par frame in flight
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mWireframePipeline.descPool);

    // 3. Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mWireframePipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mWireframePipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mWireframePipeline.descSet.data());

    // 5. Update les descripteurs (appel à la fonction dédiée, adaptée)
    UpdateDescriptors0();
}
void Ocean::UpdateDescriptors0()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        // Textures
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = mTexDisplacements.imageView;
        imageInfo.sampler = mTextureSampler;  // Votre sampler linéaire

        // UBO
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = mWireframePipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(sMatrixColorUBO);

        // 2 writes
        VkWriteDescriptorSet descriptorWrites[2] = {};

        // Image (displacement)
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mWireframePipeline.descSet[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].pImageInfo = &imageInfo;

        // UBO (matrices)
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = mWireframePipeline.descSet[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(mVulkanDevice->device, 2, descriptorWrites, 0, nullptr);
    }
}
void Ocean::RenderWireframe(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera)
{
    if (!bVisible)
        return;
    
    // 1. Update UBO
    sMatrixColorUBO ubo = {};
    ubo.model = glm::mat4(1.0f);  // Identité (grille au centre)
    ubo.view = camera.GetView();
    ubo.proj = camera.GetProjection();
    OceanColor = OceanColor;
    ubo.color = vec4(OceanColor.r, OceanColor.g, OceanColor.b, 1.0);
    memcpy(mWireframePipeline.ubo[currentFrame]->data, &ubo, sizeof(ubo));

    // 2. Bind pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframePipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mWireframePipeline.pipelineLayout, 0, 1, &mWireframePipeline.descSet[currentFrame], 0, nullptr);

    // 3. Bind mesh
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    // 4. Draw !
    vkCmdDrawIndexed(cmd, mIndicesCount, 1, 0, 0, 0);
}

// Ocean pipeline (1 mesh)
void Ocean::CreatePipeline1()
{
    /*
    binding 0: UBO                      → VERTEX + FRAGMENT
    binding 1: sampler2D displacement   → VERTEX
    binding 2: sampler2D gradients      → FRAGMENT
    binding 3: sampler2D foamBuffer     → FRAGMENT
    binding 4: sampler2D foamDesign     → FRAGMENT
    binding 5: sampler2D foamBubbles    → FRAGMENT
    binding 6: sampler2D foamTexture    → FRAGMENT
    binding 7: sampler2D envmap         → FRAGMENT
    binding 8: sampler2D reflectionTex  → FRAGMENT
    binding 9: sampler2D waterDUDV      → FRAGMENT
    binding 10: sampler2D shadowMap     → FRAGMENT
	binding 11: sampler2D contourShip   → FRAGMENT
    */

    // Nettoyage précédent
	mOneMeshPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_1mesh.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_1mesh.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(sVertexOcean);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    array<VkVertexInputAttributeDescription, 2> attribs{};
    attribs[0].binding = 0;
    attribs[0].location = 0;    // sVertexOcean.position
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = 0;

    attribs[1].binding = 0;
    attribs[1].location = 1;    // sVertexOcean.texCoord
    attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[1].offset = offsetof(sVertexOcean, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout (12 bindings)
    array<VkDescriptorSetLayoutBinding, 12> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // UBO → VERTEX + FRAGMENT 
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },       // sampler2D displacement → VERTEX
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D gradients → FRAGMENT
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamBuffer → FRAGMENT
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamIntensity → FRAGMENT
        { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamBubbles → FRAGMENT
        { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamTexture → FRAGMENT
        { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D envmap → FRAGMENT
        { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D reflectionTex → FRAGMENT
        { 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D waterDUDV → FRAGMENT
        { 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },    // sampler2D shadowMap → FRAGMENT
        { 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }     // sampler2D contourShip → FRAGMENT
} };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mOneMeshPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mOneMeshPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mOneMeshPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissor
    VkViewport viewport{ 0.0f, 0.0f, (float)mExtent.width, (float)mExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, mExtent };
   
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling 8x
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_DEPTH_BIAS };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    // 12. Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mOneMeshPipeline.pipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mOneMeshPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateDescriptors1();
}
void Ocean::CreateDescriptors1()
{
    mOneMeshPipeline.descSet.resize(g_FramesInFlight);
    mOneMeshPipeline.ubo.resize(g_FramesInFlight);
    
    // 1. UBO BUFFER
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mOneMeshPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sOceanUBO));

    // 2. DESCRIPTOR POOL + SET
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 11 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mOneMeshPipeline.descPool);

    // 3. Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mOneMeshPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mOneMeshPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mOneMeshPipeline.descSet.data());

    //UpdateDescriptors1();
}
void Ocean::UpdateDescriptors1()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        // UBO
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mOneMeshPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        // Textures
        array<VkDescriptorImageInfo, 11> imageInfo{};

        // displacement
        imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[0].imageView = mTexDisplacements.imageView;
        imageInfo[0].sampler = mTextureSampler;

        // gradients
        imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[1].imageView = mTexGradients.imageView;
        imageInfo[1].sampler = mTextureSampler;

        // foamBuffer
        imageInfo[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[2].imageView = mTexFoamBuffer->imageView;
        imageInfo[2].sampler = mTextureSampler;

        // foamIntensity
        imageInfo[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[3].imageView = mTexFoamIntensity.imageView;
        imageInfo[3].sampler = mTextureSampler;

        // foamBubbles
        imageInfo[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[4].imageView = mTexFoamBubbles.imageView;
        imageInfo[4].sampler = mTextureSampler;

        // foamTexture
        imageInfo[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[5].imageView = mTexFoamTexture.imageView;
        imageInfo[5].sampler = mTextureSampler;

        // envmap
        imageInfo[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[6].imageView = mTexEnvmap.imageView;
        imageInfo[6].sampler = mTextureSampler;

        // reflectionTex
        imageInfo[7].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[7].imageView = g_TexReflectionColor->imageView;
        imageInfo[7].sampler = mTextureSampler;

        // waterDUDV
        imageInfo[8].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[8].imageView = mTexWaterdUdV.imageView;
        imageInfo[8].sampler = mTextureSampler;

        // shadowMap
        imageInfo[9].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo[9].imageView = g_TexShadowDepth->imageView;
        imageInfo[9].sampler = g_TexShadowDepthSampler;

        // contourShip
        imageInfo[10].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[10].imageView = TexContourShip.imageView;
        imageInfo[10].sampler = mTextureSampler;

        // WRITES
        array<VkWriteDescriptorSet, 12> writes{};

        // binding 0: UBO
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mOneMeshPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        // binding 1: displacement
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mOneMeshPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imageInfo[0];

        // binding 2: gradients
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mOneMeshPipeline.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &imageInfo[1];

        // binding 3: foamBuffer
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = mOneMeshPipeline.descSet[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo = &imageInfo[2];

        // binding 4: foamIntensity
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = mOneMeshPipeline.descSet[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].pImageInfo = &imageInfo[3];

        // binding 5: foamBubbles
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = mOneMeshPipeline.descSet[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].pImageInfo = &imageInfo[4];

        // binding 6: foamTexture
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = mOneMeshPipeline.descSet[i];
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &imageInfo[5];

        // binding 7: envmap
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = mOneMeshPipeline.descSet[i];
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].pImageInfo = &imageInfo[6];

        // binding 8: reflectionTex
        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = mOneMeshPipeline.descSet[i];
        writes[8].dstBinding = 8;
        writes[8].descriptorCount = 1;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[8].pImageInfo = &imageInfo[7];

        // binding 9: waterdUdV
        writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet = mOneMeshPipeline.descSet[i];
        writes[9].dstBinding = 9;
        writes[9].descriptorCount = 1;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].pImageInfo = &imageInfo[8];

        // binding 10: shadowMap
        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = mOneMeshPipeline.descSet[i];
        writes[10].dstBinding = 10;
        writes[10].descriptorCount = 1;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].pImageInfo = &imageInfo[9];

        // binding 11: contourShip
        writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet = mOneMeshPipeline.descSet[i];
        writes[11].dstBinding = 11;
        writes[11].descriptorCount = 1;
        writes[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[11].pImageInfo = &imageInfo[10];

        vkUpdateDescriptorSets(mVulkanDevice->device, writes.size(), writes.data(), 0, nullptr);
    }
}
void Ocean::RenderOneMesh(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec3& ShipPosition, float ShipRotation)
{
    if (!bVisible)
        return;
    
    if (NeedsUpdateDescriptors)
    {
        UpdateDescriptors1();
        UpdateDescriptors3();
        NeedsUpdateDescriptors = false;
    }
    
    // 1. Update UBO
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();
    mat4 viewProj = proj * view;

    sOceanUBO ubo = {};
    ubo.matViewProj = viewProj;
    ubo.eyePos = camera.GetPosition();
    ubo.bEnvmap = (int)bEnvMap;
    mat4 model(1.0f);
    ubo.lightSpaceMatrix = LightViewProjection * model;
    ubo.oceanColor = OceanColor;
    ubo.transparency = Transparency;
    ubo.sunColor = sky->SunDiffuse;
    ubo.time = 0.01f * glfwGetTime();
    ubo.sunDir = glm::normalize(sky->SunPosition);
    ubo.exposure = sky->Exposure;
    ubo.shipPosition = ShipPosition;
    ubo.shipRotation = -ShipRotation;
    ubo.bKelvinWakes = 0;
    ubo.amplitude = Amplitude;
    ubo.kelvinScale = 0.0f;
    ubo.centerFore = 0.0f;
    ubo.bShowPatches = (int)(g_bShip && bShowPatches);
    ubo.bShowShadow = (int)(g_bShip && g_bShipShadow);
    ubo.bShowReflection = (int)(g_bShip && g_bShipReflection);
    ubo.bShowWake = (int)(g_bShip && g_bShipWake);
    ubo.shipSize = vec2(TexContourShipW, TexContourShipH);
    ubo.shipPivot = vec2(ShipPosition.x, ShipPosition.z);
    ubo.mistDensity = sky->MistDensity;

    memcpy(mOneMeshPipeline.ubo[currentFrame]->data, &ubo, sizeof(sOceanUBO));

    // 2. Bind & Draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mOneMeshPipeline.pipeline);
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 0.025f);  // Anti-acné
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mOneMeshPipeline.pipelineLayout, 0, 1, &mOneMeshPipeline.descSet[currentFrame], 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mVertexBuffer->buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mIndexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mIndicesCount, 1, 0, 0, 0);
}

// Instances
void Ocean::GetPatchesDecal(vec2 Position, float Yaw)
{
    // Get the corners of the decal
    float cosYaw = cos(Yaw);
    float sinYaw = sin(Yaw);
    const float halfWakeSize = g_WakeSize / 2.0f;

    vec2 coins[4] = {
        vec2(-halfWakeSize * cosYaw - halfWakeSize * sinYaw, -halfWakeSize * sinYaw + halfWakeSize * cosYaw),
        vec2(halfWakeSize * cosYaw - halfWakeSize * sinYaw,  halfWakeSize * sinYaw + halfWakeSize * cosYaw),
        vec2(halfWakeSize * cosYaw + halfWakeSize * sinYaw,  halfWakeSize * sinYaw - halfWakeSize * cosYaw),
        vec2(-halfWakeSize * cosYaw + halfWakeSize * sinYaw, -halfWakeSize * sinYaw - halfWakeSize * cosYaw)
    };

    // Initialize the limits
    float xMin = FLT_MAX, xMax = -FLT_MAX, zMin = FLT_MAX, zMax = -FLT_MAX;

    // Find the limits of the decal
    for (int k = 0; k < 4; k++)
    {
        vec2 coinGlobal = Position + coins[k];
        xMin = std::min(xMin, coinGlobal.x);
        xMax = std::max(xMax, coinGlobal.x);
        zMin = std::min(zMin, coinGlobal.y);
        zMax = std::max(zMax, coinGlobal.y);
    }

    // Convert limits to mvIndices of patches
    iMinPatchDecal = floor((xMin + PATCH_SIZE / 2) / PATCH_SIZE);
    iMaxPatchDecal = ceil((xMax + PATCH_SIZE / 2) / PATCH_SIZE) - 1;
    jMinPatchDecal = floor((zMin + PATCH_SIZE / 2) / PATCH_SIZE);
    jMaxPatchDecal = ceil((zMax + PATCH_SIZE / 2) / PATCH_SIZE) - 1;
}
void Ocean::UpdateInstanceBuffer(Camera& camera, uint32_t frameIndex, bool bWithPatchdecal)
{
    auto& frame = mFrames[frameIndex];

    for (int lod = 0; lod < 5; lod++)
        mvInstanceData[lod].clear();
    mvInstanceWakeData.clear();

    int cameraPatchX = static_cast<int>(std::round(camera.GetPosition().x / PATCH_SIZE));
    int cameraPatchZ = static_cast<int>(std::round(camera.GetPosition().z / PATCH_SIZE));

#ifdef _DEBUG
    int nGrids = 50;
#else
    int nGrids = 400;
#endif

    mat4 view = camera.GetView();
    float zNear = camera.GetNearPlane();
    float zFar = camera.GetFarPlane();
    float tanY = tanf(glm::radians(camera.GetZoom()) * 0.5f);
    float tanX = tanY * camera.GetViewportWidth() / camera.GetViewportHeight();
    float radiusWS = PATCH_SIZE * 1.414f;
    float margin = 1.1f;
    float r = radiusWS * margin;

    // ─── Pre-calculation: radius of an entire row ──────────────────────────
    
    // A row j covers (nGrids+1) patches in X → its "half-width" in WS
    float rowHalfWidth = (nGrids / 2 + 0.5f) * PATCH_SIZE + r;      // bounding sphere approx
    float rowRadius = sqrtf(rowHalfWidth * rowHalfWidth + r * r);   // encompassing sphere

    for (int j = -nGrids / 2; j <= nGrids / 2; j++)
    {
        // ── Test frustum on the entire row (center of the row) ──────
        
        // Center of the row in world space: X=cameraPatchX (middle), Z=jj*PATCH_SIZE
        int jj = j + cameraPatchZ;
        vec3 rowCenterWS(PATCH_SIZE * cameraPatchX, 0.0f, PATCH_SIZE * jj);
        vec4 rowCenterVS4 = view * vec4(rowCenterWS, 1.0f);
        vec3 rowCenterVS = vec3(rowCenterVS4);

        // Behind the camera ?
        if (rowCenterVS.z > rowRadius)                          continue;
        // In front of far plane ?
        if (rowCenterVS.z < -(zFar + rowRadius))                continue;
        // Excluding high/low frustum ?
        float maxYRow = -rowCenterVS.z * tanY + rowRadius;
        float minYRow = rowCenterVS.z * tanY - rowRadius;
        if (rowCenterVS.y < minYRow || rowCenterVS.y > maxYRow) continue;
        // Note: we are not testing X here because the row spans the entire width

        // ── The row is (partially) visible → test patch by patch ──
        for (int i = -nGrids / 2; i <= nGrids / 2; i++)
        {
            int ii = i + cameraPatchX;
            bool isWakePatch = bWithPatchdecal && (ii >= iMinPatchDecal && ii <= iMaxPatchDecal && jj >= jMinPatchDecal && jj <= jMaxPatchDecal);

            vec3 centerWS(PATCH_SIZE * ii, 0.0f, PATCH_SIZE * jj);
            vec4 centerVS4 = view * vec4(centerWS, 1.0f);
            vec3 centerVS = vec3(centerVS4);

            if (centerVS.z > r)                                 continue;
            if (centerVS.z < -(zFar + r))                       continue;

            float maxX = -centerVS.z * tanX + r;
            float minX = centerVS.z * tanX - r;
            if (centerVS.x < minX || centerVS.x > maxX)         continue;

            float maxY = -centerVS.z * tanY + r;
            float minY = centerVS.z * tanY - r;
            if (centerVS.y < minY || centerVS.y > maxY)         continue;

            float dist = glm::length2(centerWS - camera.GetPosition());
            int lod = 4;
            if (dist < 600.0f * 600.0f)   lod = 0;
            else if (dist < 1200.0f * 1200.0f)  lod = 1;
            else if (dist < 2400.0f * 2400.0f)  lod = 2;
            else if (dist < 4800.0f * 4800.0f)  lod = 3;

            sInstanceData data;
            data.modelMatrix = mat4(
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                centerWS.x, centerWS.y, centerWS.z, 1
			);  // Equivalent to translate but faster to compute

            if (isWakePatch)
            {
                data.lod = 0;
                mvInstanceWakeData.push_back(data);
            }
            else
            {
                data.lod = lod;
                mvInstanceData[lod].push_back(data);
            }
        }
    }

    // ── Copy to the buffer ──────────────────────────────────
    frame.instanceCount = 0;
    int offset = 0;

    for (int lod = 0; lod < 5; lod++)
    {
        uint32_t count = mvInstanceData[lod].size();
        memcpy((char*)frame.ubo->data + offset * sizeof(sInstanceData), mvInstanceData[lod].data(), count * sizeof(sInstanceData));
        offset += count;
        frame.instanceCount += count;
    }

    uint32_t wakeCount = mvInstanceWakeData.size();
    memcpy((char*)frame.ubo->data + offset * sizeof(sInstanceData), mvInstanceWakeData.data(), wakeCount * sizeof(sInstanceData));
    frame.instanceCount += wakeCount;
}

// Ocean pipeline (LOD instancing)
void Ocean::CreatePipeline2()
{
    /*
    binding 0: UBO                      → VERTEX + FRAGMENT
    binding 1: sampler2D displacement   → VERTEX
    binding 2: sampler2D gradients      → FRAGMENT
    binding 3: sampler2D foamBuffer     → FRAGMENT
    binding 4: sampler2D foamDesign     → FRAGMENT
    binding 5: sampler2D foamBubbles    → FRAGMENT
    binding 6: sampler2D foamTexture    → FRAGMENT
    binding 7: sampler2D envmap         → FRAGMENT
    binding 8: sampler2D reflectionTex  → FRAGMENT
    binding 9: sampler2D waterDUDV      → FRAGMENT
    binding 10: sampler2D shadowMap     → FRAGMENT
    */

    // Nettoyage précédent
	mLODPipeline.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_lod.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_lod.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding[2]{};
    binding[0].binding = 0;                    // sVertexOcean
    binding[0].stride = sizeof(sVertexOcean);
    binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    binding[1].binding = 1;                    // sInstanceData
    binding[1].stride = sizeof(sInstanceData);
    binding[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    array<VkVertexInputAttributeDescription, 7> attribs{};

    // 0,1 : sVertexOcean
    attribs[0].binding = 0;
    attribs[0].location = 0;    // sVertexOcean.position
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = 0;

    attribs[1].binding = 0;
    attribs[1].location = 1;    // sVertexOcean.texCoord
    attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[1].offset = offsetof(sVertexOcean, texCoord);

    // mat4 modelMatrix (locations 2,3,4,5)
    attribs[2].binding = 1;
    attribs[2].location = 2;    // sInstanceData.modelMatrix0
    attribs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[2].offset = 0;

    attribs[3].binding = 1;
    attribs[3].location = 3;    // sInstanceData.modelMatrix1
    attribs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[3].offset = 16;

    attribs[4].binding = 1;
    attribs[4].location = 4;    // sInstanceData.modelMatrix2
    attribs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[4].offset = 32;

    attribs[5].binding = 1;
    attribs[5].location = 5;    // sInstanceData.modelMatrix3
    attribs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[5].offset = 48;

    // lod
    attribs[6].binding = 1;
    attribs[6].location = 6;    // sInstanceData.lod
    attribs[6].format = VK_FORMAT_R32_SINT;
    attribs[6].offset = 64;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout (8 bindings)
    array<VkDescriptorSetLayoutBinding, 8> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // UBO → VERTEX + FRAGMENT 
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },       // sampler2D displacement → VERTEX
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D gradients → FRAGMENT
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamBuffer → FRAGMENT
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamIntensity → FRAGMENT
        { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamBubbles → FRAGMENT
        { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamTexture → FRAGMENT
        { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D envmap → FRAGMENT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mLODPipeline.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mLODPipeline.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mLODPipeline.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissor
    VkViewport viewport{ 0.0f, 0.0f, (float)mExtent.width, (float)mExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, mExtent };
   
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling 8x
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_DEPTH_BIAS,
                                        VK_DYNAMIC_STATE_POLYGON_MODE_EXT,
                                        VK_DYNAMIC_STATE_LINE_WIDTH };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    // 12. Pipeline creation
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mLODPipeline.pipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mLODPipeline.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateDescriptors2();
}
void Ocean::CreateDescriptors2()
{
    mLODPipeline.descSet.resize(g_FramesInFlight);
    mLODPipeline.ubo.resize(g_FramesInFlight);
    
    // UBO Buffer
    for (size_t i = 0; i < g_FramesInFlight; i++)
        mLODPipeline.ubo[i] = make_unique<VulkanUBO>(mVulkanDevice, sizeof(sOceanUBO));

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mLODPipeline.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mLODPipeline.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mLODPipeline.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mLODPipeline.descSet.data());

    UpdateDescriptors2();
}
void Ocean::UpdateDescriptors2()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        // UBO
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mLODPipeline.ubo[i]->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        // Textures
        array<VkDescriptorImageInfo, 7> imageInfo{};

        // displacement
        imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[0].imageView = mTexDisplacements.imageView;
        imageInfo[0].sampler = mTextureSampler;

        // gradients
        imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[1].imageView = mTexGradients.imageView;
        imageInfo[1].sampler = mTextureSampler;

        // foamBuffer
        imageInfo[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[2].imageView = mTexFoamBuffer->imageView;
        imageInfo[2].sampler = mTextureSampler;

        // foamIntensity
        imageInfo[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[3].imageView = mTexFoamIntensity.imageView;
        imageInfo[3].sampler = mTextureSampler;

        // foamBubbles
        imageInfo[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[4].imageView = mTexFoamBubbles.imageView;
        imageInfo[4].sampler = mTextureSampler;

        // foamTexture
        imageInfo[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[5].imageView = mTexFoamTexture.imageView;
        imageInfo[5].sampler = mTextureSampler;

        // envmap
        imageInfo[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[6].imageView = mTexEnvmap.imageView;
        imageInfo[6].sampler = mTextureSampler;

        // 7 WRITES
        array<VkWriteDescriptorSet, 8> writes{};

        // binding 0: UBO
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mLODPipeline.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        // binding 1: displacement
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mLODPipeline.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imageInfo[0];

        // binding 2: gradients
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mLODPipeline.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &imageInfo[1];

        // binding 3: foamBuffer
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = mLODPipeline.descSet[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo = &imageInfo[2];

        // binding 4: foamIntensity
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = mLODPipeline.descSet[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].pImageInfo = &imageInfo[3];

        // binding 5: foamBubbles
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = mLODPipeline.descSet[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].pImageInfo = &imageInfo[4];

        // binding 6: foamTexture
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = mLODPipeline.descSet[i];
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &imageInfo[5];

        // binding 7: envmap
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = mLODPipeline.descSet[i];
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].pImageInfo = &imageInfo[6];

        vkUpdateDescriptorSets(mVulkanDevice->device, 8, writes.data(), 0, nullptr);
    }
}
void Ocean::RenderInstancedMeshs(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky)
{
    if (!bVisible)
        return;
    
    Chronos[0].NameAndStart("LODs");

    UpdateInstanceBuffer(camera, currentFrame, false);  // Met à jour les données d'instance selon la position de la caméra

    Chronos[0].Stop();

    // 1. Update UBO
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();
    mat4 viewProj = proj * view;

    sOceanUBO ubo = {};
    ubo.matViewProj = viewProj;
    ubo.eyePos = camera.GetPosition();
    ubo.bEnvmap = (int)bEnvMap;
    ubo.lightSpaceMatrix = mat4(1.0f);  // Optional
    ubo.oceanColor = OceanColor;
    ubo.transparency = Transparency;
    ubo.sunColor = sky->SunDiffuse;
    ubo.time = 0.0f;                    // Optional
    ubo.sunDir = glm::normalize(sky->SunPosition);
    ubo.exposure = sky->Exposure;
    // Optional
    ubo.shipPosition = vec3(0.0f);
    ubo.shipRotation = 0.0f;
    ubo.bKelvinWakes = 0;
    ubo.amplitude =Amplitude;
    ubo.kelvinScale = 0.0f;
    ubo.centerFore = 0.0f;
    ubo.bShowPatches = (int)bShowPatches;
    ubo.bShowShadow = (int)g_bShipShadow;
    ubo.bShowReflection = (int)g_bShipReflection;
    ubo.bShowWake = (int)g_bShipWake;
    ubo.mistDensity = sky->MistDensity;

    memcpy(mLODPipeline.ubo[currentFrame]->data, &ubo, sizeof(sOceanUBO));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mLODPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mLODPipeline.pipelineLayout, 0, 1, &mLODPipeline.descSet[currentFrame], 0, nullptr);
    
    auto& frame = mFrames[currentFrame];
    size_t globalInstanceOffset = 0;  // Offset cumulé
    
    for (int lod = 0; lod < 5 && lod < mvLODPatches.size(); lod++)
    {
        VkDeviceSize offsets[2] = { 0, globalInstanceOffset };  // Vertex=0, Instance=cumulé

        VkBuffer buffers[] = { mvLODPatches[lod].vertexBuffer->buffer, frame.ubo->buffer };
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, mvLODPatches[lod].indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        uint32_t instanceCount = mvInstanceData[lod].size();
        vkCmdDrawIndexed(cmd, mvLODPatches[lod].indexCount, instanceCount, 0, 0, 0);

        globalInstanceOffset += instanceCount * sizeof(sInstanceData);  // Prochain LOD
    }
}

// Ocean pipeline (LOD instancing with wake)
void Ocean::CreatePipeline3()
{
    /*
    binding 0: UBO                      → VERTEX + FRAGMENT
    binding 1: sampler2D displacement   → VERTEX
    binding 2: sampler2D gradients      → FRAGMENT
    binding 3: sampler2D foamBuffer     → FRAGMENT
    binding 4: sampler2D foamDesign     → FRAGMENT
    binding 5: sampler2D foamBubbles    → FRAGMENT
    binding 6: sampler2D foamTexture    → FRAGMENT
    binding 7: sampler2D envmap         → FRAGMENT
    binding 8: sampler2D reflectionTex  → FRAGMENT
    binding 9: sampler2D waterDUDV      → FRAGMENT
    binding 10: sampler2D shadowMap     → FRAGMENT
  	binding 11: sampler2D contourShip   → FRAGMENT
  */

    // Nettoyage précédent
	mPipelineTexture.destroy(mVulkanDevice->device);

    // 1. Shaders
    auto vertCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_wake.vert");
    auto fragCode = CompileShaderRuntime("Resources/Shaders/Ocean/ocean_wake.frag");
    VkShaderModule vertModule = CreateShaderModule(mVulkanDevice->device, vertCode);
    VkShaderModule fragModule = CreateShaderModule(mVulkanDevice->device, fragCode);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
    };

    // 2. Vertex input
    VkVertexInputBindingDescription binding[2]{};
    binding[0].binding = 0;                    // sVertexOcean
    binding[0].stride = sizeof(sVertexOcean);
    binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    binding[1].binding = 1;                    // sInstanceData
    binding[1].stride = sizeof(sInstanceData);
    binding[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    array<VkVertexInputAttributeDescription, 7> attribs{};

    // 0,1 : sVertexOcean
    attribs[0].binding = 0;
    attribs[0].location = 0;    // sVertexOcean.position
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = 0;

    attribs[1].binding = 0;
    attribs[1].location = 1;    // sVertexOcean.texCoord
    attribs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attribs[1].offset = offsetof(sVertexOcean, texCoord);

    // mat4 modelMatrix (locations 2,3,4,5)
    attribs[2].binding = 1;
    attribs[2].location = 2;    // sInstanceData.modelMatrix0
    attribs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[2].offset = 0;

    attribs[3].binding = 1;
    attribs[3].location = 3;    // sInstanceData.modelMatrix1
    attribs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[3].offset = 16;

    attribs[4].binding = 1;
    attribs[4].location = 4;    // sInstanceData.modelMatrix2
    attribs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[4].offset = 32;

    attribs[5].binding = 1;
    attribs[5].location = 5;    // sInstanceData.modelMatrix3
    attribs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[5].offset = 48;

    // lod
    attribs[6].binding = 1;
    attribs[6].location = 6;    // sInstanceData.lod
    attribs[6].format = VK_FORMAT_R32_SINT;
    attribs[6].offset = 64;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions = attribs.data();

    // 3. Descriptor Set Layout (14 bindings)
    array<VkDescriptorSetLayoutBinding, 14> bindings = { {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },  // UBO → VERTEX + FRAGMENT 
        
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },       // sampler2D displacement → VERTEX
        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },       // sampler2DArray kelvin → VERTEX
        
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D gradients → FRAGMENT
        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamBuffer → FRAGMENT
        { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamIntensity → FRAGMENT
        { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamBubbles → FRAGMENT
        { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D foamTexture → FRAGMENT
        { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D envmap → FRAGMENT
        { 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D reflectionTex → FRAGMENT
        { 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },     // sampler2D waterDUDV → FRAGMENT
        { 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },    // sampler2D shadowMap → FRAGMENT
        { 12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },    // sampler2D contourShip → FRAGMENT
        { 13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }     // sampler2D wake → FRAGMENT
    } };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(mVulkanDevice->device, &layoutInfo, nullptr, &mPipelineTexture.descSetLayout);

    // 4. Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mPipelineTexture.descSetLayout;
    vkCreatePipelineLayout(mVulkanDevice->device, &pipelineLayoutInfo, nullptr, &mPipelineTexture.pipelineLayout);

    // 5. Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 6. Viewport & scissor
    VkViewport viewport{ 0.0f, 0.0f, (float)mExtent.width, (float)mExtent.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0, 0}, mExtent };
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 7. Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // 8. Multisampling 8x
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mVulkanDevice->msaaSamples;

    // 9. Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 10. Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 11. Dynamic state
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_DEPTH_BIAS, 
                                        VK_DYNAMIC_STATE_POLYGON_MODE_EXT, 
                                        VK_DYNAMIC_STATE_LINE_WIDTH };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    // 12. Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = mPipelineTexture.pipelineLayout;
    pipelineInfo.renderPass = mRenderPass;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(mVulkanDevice->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &mPipelineTexture.pipeline);

    vkDestroyShaderModule(mVulkanDevice->device, vertModule, nullptr);
    vkDestroyShaderModule(mVulkanDevice->device, fragModule, nullptr);

    CreateDescriptors3();
}
void Ocean::CreateDescriptors3()
{
    mPipelineTexture.descSet.resize(g_FramesInFlight);
    mPipelineTexture.ubo.resize(g_FramesInFlight);
    
    // UBO Buffer (same ubo as LOD pipeline)
    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 13 * g_FramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * g_FramesInFlight}
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = g_FramesInFlight;
    vkCreateDescriptorPool(mVulkanDevice->device, &poolInfo, nullptr, &mPipelineTexture.descPool);

    // Allocate 1 descriptor set per frame in flight
    vector<VkDescriptorSetLayout> layouts(g_FramesInFlight, mPipelineTexture.descSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = mPipelineTexture.descPool;
    allocInfo.descriptorSetCount = layouts.size();
    allocInfo.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(mVulkanDevice->device, &allocInfo, mPipelineTexture.descSet.data());
}
void Ocean::UpdateDescriptors3()
{
    for (size_t i = 0; i < g_FramesInFlight; ++i)
    {
        // UBO
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mLODPipeline.ubo[i]->buffer;    // Same ubo as LOD pipeline
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        // Textures
        array<VkDescriptorImageInfo, 13> imageInfo{};

        // displacement
        imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[0].imageView = mTexDisplacements.imageView;
        imageInfo[0].sampler = mTextureSampler;

        // kelvin array
        imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[1].imageView = mTexKelvinArray.imageView;
        imageInfo[1].sampler = mTextureSampler;

        // gradients
        imageInfo[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[2].imageView = mTexGradients.imageView;
        imageInfo[2].sampler = mTextureSampler;

        // foamBuffer
        imageInfo[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[3].imageView = mTexFoamBuffer->imageView;
        imageInfo[3].sampler = mTextureSampler;

        // foamIntensity
        imageInfo[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[4].imageView = mTexFoamIntensity.imageView;
        imageInfo[4].sampler = mTextureSampler;

        // foamBubbles
        imageInfo[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[5].imageView = mTexFoamBubbles.imageView;
        imageInfo[5].sampler = mTextureSampler;

        // foamTexture
        imageInfo[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[6].imageView = mTexFoamTexture.imageView;
        imageInfo[6].sampler = mTextureSampler;

        // envmap
        imageInfo[7].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[7].imageView = mTexEnvmap.imageView;
        imageInfo[7].sampler = mTextureSampler;

        // reflectionTex
        imageInfo[8].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[8].imageView = g_TexReflectionColor->imageView;
        imageInfo[8].sampler = mTextureSampler;

        // waterDUDV
        imageInfo[9].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[9].imageView = mTexWaterdUdV.imageView;
        imageInfo[9].sampler = mTextureSampler;

        // shadowMap
        imageInfo[10].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfo[10].imageView = g_TexShadowDepth->imageView;
        imageInfo[10].sampler = g_TexShadowDepthSampler;

        // contourShip
        imageInfo[11].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[11].imageView = TexContourShip.imageView;
        imageInfo[11].sampler = mTextureSampler;

        // wake
        imageInfo[12].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo[12].imageView = g_TexWake2->imageView;
        imageInfo[12].sampler = mTextureSampler;

        // WRITES
        array<VkWriteDescriptorSet, 14> writes{};

        // binding 0: UBO
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = mPipelineTexture.descSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;

        // binding 1: displacement
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = mPipelineTexture.descSet[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imageInfo[0];

        // binding 2: kelvin array
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = mPipelineTexture.descSet[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &imageInfo[1];
        
        // binding 3: gradients
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = mPipelineTexture.descSet[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo = &imageInfo[2];

        // binding 4: foamBuffer
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = mPipelineTexture.descSet[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].pImageInfo = &imageInfo[3];

        // binding 5: foamIntensity
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = mPipelineTexture.descSet[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].pImageInfo = &imageInfo[4];

        // binding 6: foamBubbles
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = mPipelineTexture.descSet[i];
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &imageInfo[5];

        // binding 7: foamTexture
        writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[7].dstSet = mPipelineTexture.descSet[i];
        writes[7].dstBinding = 7;
        writes[7].descriptorCount = 1;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[7].pImageInfo = &imageInfo[6];

        // binding 8: envmap
        writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[8].dstSet = mPipelineTexture.descSet[i];
        writes[8].dstBinding = 8;
        writes[8].descriptorCount = 1;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[8].pImageInfo = &imageInfo[7];

        // binding 9: reflectionTex
        writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[9].dstSet = mPipelineTexture.descSet[i];
        writes[9].dstBinding = 9;
        writes[9].descriptorCount = 1;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[9].pImageInfo = &imageInfo[8];

        // binding 10: waterdUdV
        writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[10].dstSet = mPipelineTexture.descSet[i];
        writes[10].dstBinding = 10;
        writes[10].descriptorCount = 1;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[10].pImageInfo = &imageInfo[9];

        // binding 11: shadowMap
        writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[11].dstSet = mPipelineTexture.descSet[i];
        writes[11].dstBinding = 11;
        writes[11].descriptorCount = 1;
        writes[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[11].pImageInfo = &imageInfo[10];
        
        // binding 12: contourShip
        writes[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[12].dstSet = mPipelineTexture.descSet[i];
        writes[12].dstBinding = 12;
        writes[12].descriptorCount = 1;
        writes[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[12].pImageInfo = &imageInfo[11];

        // binding 13: wake
        writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[13].dstSet = mPipelineTexture.descSet[i];
        writes[13].dstBinding = 13;
        writes[13].descriptorCount = 1;
        writes[13].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[13].pImageInfo = &imageInfo[12];

        vkUpdateDescriptorSets(mVulkanDevice->device, writes.size(), writes.data(), 0, nullptr);
    }
}
void Ocean::RenderFull(VkCommandBuffer cmd, uint32_t currentFrame, Camera& camera, Sky* sky, vec3& ShipPosition, float ShipRotation, bool bKelvinWakes, float LWL, float kelvinScale, float shipVelocity, float centerFore, int baseFroude)
{
    if (!bVisible)
        return;

    Chronos[0].NameAndStart("LODs");

    if (NeedsUpdateDescriptors)
    {
        UpdateDescriptors1();
        UpdateDescriptors3();
        NeedsUpdateDescriptors = false;
    }

    GetPatchesDecal(vec2(ShipPosition.x, ShipPosition.z), ShipRotation);
    UpdateInstanceBuffer(camera, currentFrame, true);  // Met à jour les données d'instance selon la position de la caméra

    Chronos[0].Stop();

    // 1. Update UBO
    mat4 view = camera.GetView();
    mat4 proj = camera.GetProjection();
    mat4 viewProj = proj * view;

    sOceanUBO ubo = {};
    ubo.matViewProj = viewProj;
    ubo.eyePos = camera.GetPosition();
    ubo.bEnvmap = (int)bEnvMap;
    mat4 model(1.0f);
    ubo.lightSpaceMatrix = LightViewProjection * model;
    ubo.oceanColor = OceanColor;
    ubo.transparency = Transparency;
    ubo.sunColor = sky->SunDiffuse;
    ubo.time = 0.01f * glfwGetTime();
    ubo.sunDir = glm::normalize(sky->SunPosition);
    ubo.exposure = sky->Exposure;
    ubo.shipPosition = ShipPosition;
    ubo.shipRotation = -ShipRotation;
    ubo.bKelvinWakes = (int)(g_bShip && bKelvinWakes);
    ubo.amplitude = 0.15f * shipVelocity;
    ubo.kelvinScale = kelvinScale;
    ubo.centerFore = centerFore;
    ubo.bShowPatches = (int)(g_bShip && bShowPatches);
    ubo.bShowShadow = (int)(g_bShip && g_bShipShadow);
    ubo.bShowReflection = (int)(g_bShip && g_bShipReflection);
    ubo.bShowWake = (int)(g_bShip && g_bShipWake);
    ubo.shipSize = vec2(TexContourShipW, TexContourShipH);
    ubo.shipPivot = vec2(ShipPosition.x, ShipPosition.z);
    ubo.wakeSize = vec2(g_WakeSize, g_WakeSize);
    int layer = int(100.0f * fabs(shipVelocity) / sqrt(9.81f * LWL)) + baseFroude;   // Froude is (layer + 1) / 100
    layer = glm::clamp(layer, 0, 100);
    ubo.texLayer = layer;
    ubo.mistDensity = sky->MistDensity;
    
    ubo.windDir = glm::normalize(Wind);  // Example wind direction
    ubo.windRippleStr = 0.1f;  // Example ripple strength
    ubo.windSpeed = 0.2f;      // Example wind speed

    auto& frame = mFrames[currentFrame];

    memcpy(mLODPipeline.ubo[currentFrame]->data, &ubo, sizeof(sOceanUBO));
    mLODPipeline.ubo[currentFrame]->Flush();

    // 1. Rendu LODs normaux (mLODPipeline)
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mLODPipeline.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mLODPipeline.pipelineLayout, 0, 1, &mLODPipeline.descSet[currentFrame], 0, nullptr);

    if (bWireframe)
        mVulkanDevice->getCmdSetPolygonModeEXT()(cmd, VK_POLYGON_MODE_LINE);
    else
        mVulkanDevice->getCmdSetPolygonModeEXT()(cmd, VK_POLYGON_MODE_FILL);
    mVulkanDevice->getCmdSetLineWidth()(cmd, 1.0f);

    size_t normalOffset = 0;  // Début des LODs normaux
    for (int lod = 0; lod < 5 && lod < mvLODPatches.size(); lod++) 
    {
        VkDeviceSize offsets[2] = { 0, normalOffset * sizeof(sInstanceData) };

        VkBuffer buffers[] = { mvLODPatches[lod].vertexBuffer->buffer, frame.ubo->buffer };
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, mvLODPatches[lod].indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

        uint32_t instanceCount = mvInstanceData[lod].size();
        vkCmdDrawIndexed(cmd, mvLODPatches[lod].indexCount, instanceCount, 0, 0, 0);

        normalOffset += instanceCount;  // Offset cumulé normal
    }

    // 2. Rendu wake patches (mPipelineTexture, LOD 0 seulement)

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineTexture.pipeline);
    vkCmdSetDepthBias(cmd, 1.25f, 0.0f, 0.025f);  // Anti-acné
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelineTexture.pipelineLayout, 0, 1, &mPipelineTexture.descSet[currentFrame], 0, nullptr);

    if (bWireframe)
        mVulkanDevice->getCmdSetPolygonModeEXT()(cmd, VK_POLYGON_MODE_LINE);
    else
        mVulkanDevice->getCmdSetPolygonModeEXT()(cmd, VK_POLYGON_MODE_FILL);

    VkDeviceSize wakeOffsets[2] = { 0, normalOffset * sizeof(sInstanceData) };  // Après les normaux
    VkBuffer wakeBuffers[] = { mvLODPatches[0].vertexBuffer->buffer, frame.ubo->buffer };  // LOD 0 mesh
    vkCmdBindVertexBuffers(cmd, 0, 2, wakeBuffers, wakeOffsets);
    vkCmdBindIndexBuffer(cmd, mvLODPatches[0].indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);

    uint32_t wakeInstanceCount = mvInstanceWakeData.size();
    vkCmdDrawIndexed(cmd, mvLODPatches[0].indexCount, wakeInstanceCount, 0, 0, 0);
}

// Recreate all pipelines if SwapChain has changed

void Ocean::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    mRenderPass = renderPass;
    mExtent = newExtent;

    CreatePipeline0();  // Wireframe
    CreatePipeline1();  // One mesh
    UpdateDescriptors1();
    CreatePipeline2();  // LOD instancing
    CreatePipeline3();  // LOD with wake
    UpdateDescriptors3();

    // Recrée sémaphores et fences compute après un resize
    VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < g_FramesInFlight; i++)
    {
        vkCreateSemaphore(mVulkanDevice->device, &semInfo, nullptr, &ComputeFinishedSem[i]);
        vkCreateFence(mVulkanDevice->device, &fenceInfo, nullptr, &mComputeFence[i]);
    }
}