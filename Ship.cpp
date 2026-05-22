/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Ship.h"

#include "clipper/clipper.h"        // Used to get front and lateral areas by clipping everything below the waterline
#ifdef _DEBUG
#pragma comment(lib, "clipper/Debug/clipper.lib")
#else
#pragma comment(lib, "clipper/Release/clipper.lib")
#endif
using namespace Clipper2Lib;

extern uint32_t         g_FramesInFlight;
extern sChrono          Chronos[10];
extern float            g_TWS_Kt;
extern float            g_TWS_Deg;
extern vec2             g_Wind;
extern SoundManager   * g_SoundMgr;
extern bool             g_bPause;
extern Camera           g_Camera;
extern bool             g_bShowShipForcesWindows;
extern unique_ptr<VulkanTexture>    g_TexWake0;
extern unique_ptr<VulkanTexture>    g_TexWake1;
extern unique_ptr<VulkanTexture>    g_TexWake2;
extern int						    g_WakeSize;
extern bool				g_bShipShadow;
bool                    bTexWakeByVAO = true;
VulkanTexture           TexContourShip;
int                     TexContourShipW;
int                     TexContourShipH;

Ship::Ship(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent, VkRenderPass renderPassReflection, VkRenderPass randerPassShadow, uint32_t shadowWidth, uint32_t shadowHeight, 
    VkRenderPass randerPassBridgeMask, VkRenderPass randerPassWake, sShip& ship, Ocean* ocean, Camera& camera)
{
    mVulkanDevice = vulkanDevice;

    chrono.start();
    this->ship = ship;

    // Read the mvVertices and the faces
    igl::readOBJ(ship.PathnameHull.c_str(), mV, mF);
    stringstream ssHull;

    // Vertices
    mvVertices.resize(mV.rows());
    mvVerticesInitial.resize(mV.rows());
    for (int i = 0; i < mV.rows(); i++)
        mvVerticesInitial[i] = vec3(mV(i, 0), mV(i, 1), mV(i, 2));
    mvVertSubmerged.resize(mV.rows());
    mvVertWaterHeight.resize(mV.rows());
    mWorld = mat4(1.0f);
    TransformVertices();

    SetOcean(ocean);
    InitModels(vulkanDevice, renderPassScene, extent, renderPassReflection, randerPassShadow, shadowWidth, shadowHeight, randerPassBridgeMask);

    // Get data
    InitDimensions();
    UpdateWorldMatrix();                        // Necessary for several calculations to come

    mvTris.resize(mF.rows());
    InitTriangles();                            // Create the list of the triangles
    InitCentroid();                             // Compute the centre of the volume
    InitSurfaces();                             // Certain surfaces
    InitInertia();                              // Compute volume & all moments of inertia (Ixx, Iyy, Izz, Ixy, Ixz, Iyz)

    // Info to display with interface
    ssHull << "Length     : " << std::fixed << std::setprecision(2) << mLength << " m" << endl;
    ssHull << "Width      : " << std::fixed << std::setprecision(2) << mWidth << " m" << endl;
    ssHull << "Draft      : " << std::fixed << std::setprecision(2) << mDraft << " m" << endl;
    ssHull << "Mass       : " << std::setprecision(0) << int(ship.Mass_t) << " t" << endl;
	ssHull << "Power      : " << std::setprecision(0) << int(ship.PowerkW) << " kW" << endl;
	ssHull << "Max speed  : " << std::fixed << std::setprecision(2) << ship.SpeedMaxKt << " kt" << endl;
	ssHull << "Test speed : " << std::fixed << std::setprecision(2) << ship.SpeedEcoKt << " kt" << endl;
    InfoModel = ssHull.str();

    InitWaterVertices();                        // Create the list of water vertices in the reference patch
    InitHullMesh(vulkanDevice, renderPassScene, extent);    // Create the VAO of the colored hull
    InitPressureMesh(vulkanDevice, renderPassScene, extent);
    InitContours(vulkanDevice, renderPassScene, extent);
	InitWakeMesh(vulkanDevice, renderPassScene, extent, randerPassWake);

    InitSounds(camera);

    // Particles
    mSmoke = make_unique<Smoke>(mVulkanDevice, renderPassScene, extent);
	mSpray = make_unique<Spray>(mVulkanDevice, renderPassScene, extent);
    if (ship.bFlag)
    {
        float spacing = ship.DimXFlag / 15.0f;
        mFlag = make_unique<Flag>(vulkanDevice, renderPassScene, extent, 15, 10, spacing, ship.PathnameFlag.c_str());
    }
    mLight = make_unique<Light>(mVulkanDevice, renderPassScene, extent);

    ResetVelocities();
    bMotion = false;
    bSound = true;

    mArchimede.Name = "Archimede";
    mGravity.Name = "Gravity";
    mHeaveDrag.Name = "Heave Drag";
    mThrust1.Name = "Thrust1";
    mThrust2.Name = "Thrust2";
    mPropDrag1.Name = "Prop Drag 1";
    mPropDrag2.Name = "Prop Drag 2";
    mViscousDrag.Name = "Viscous Drag";
    mWavesDrag.Name = "Waves Drag";
    mBowThrust.Name = "Bow Thrust";
    mSternThrust.Name = "Stern Thrust";
    mRudderLift.Name = "Rudder Lift";
    mRudderDrag.Name = "Rudder Drag";
    mAirDrag.Name = "Air Drag";
    mWindTorque.Name = "Wind Rotation";
    mWindDrift.Name = "Wind Drift";
    mCentrifugalTorque.Name = "Centrifugal";

    ComputeEquilibriumDraft();
    ComputeMaxSpeed();
}
Ship::~Ship()
{
    mModelFull.reset();
    mPropeller1.reset();
    mPropeller2.reset();
    mRudder1.reset();
    mRudder2.reset();
    mRadar1.reset();
    mRadar2.reset();

    mHullMesh.reset();
	mContourMesh1.reset();
	mContourMesh2.reset();
	mWakeMesh.reset();
    mPressureMesh.reset();

    mSoundThrust1.reset();
    mSoundThrust2.reset();
    mSoundBowThruster.reset();
    mSoundSternThruster.reset();

    mSmoke.reset();
    mSpray.reset();
    mFlag.reset();
}

void Ship::SetOcean(Ocean* ocean)
{
    mOcean = ocean;
    pDisplacement = ocean->GetPixelsDisplacement();
};
void Ship::InitModels(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent, VkRenderPass renderPassReflection, VkRenderPass randerPassShadow, uint32_t shadowWidth, uint32_t shadowHeight, VkRenderPass randerPassBridgeMask)
{
    // Load models
    mModelFull = make_unique<Model>(vulkanDevice);
    mModelFull->LoadModel(ship.PathnameFull.c_str());
    stringstream ssFull;
    ssFull << "Full: " << mModelFull->NbVertices << " vert. & " << mModelFull->NbFaces << " faces" << endl;
    ssFull << "Hull: " << mV.rows() << " vert. & " << mF.rows() << " faces" << endl;
    Info3D = ssFull.str();
    mModelFull->CreateMsPipeline(renderPassScene, extent);
    mModelFull->CreateReflectionPipeline(renderPassReflection, extent);
    mModelFull->CreateShadowPipeline(randerPassShadow, { shadowWidth, shadowHeight });
    mModelFull->CreateCxPipeline(renderPassScene, extent);
    mModelFull->CreateBboxPipeline(renderPassScene, extent);
    mModelFull->CreateBridgeMaskPipeline(randerPassBridgeMask, extent);
	mModelFull->CreateWireframeMsPipeline(renderPassScene, extent);
	mModelFull->CreateWireframeColorPipeline(renderPassScene, extent);

    if (ship.PathnamePropeller1.length())
    {
        mPropeller1 = make_unique<Model>(vulkanDevice);
        mPropeller1->LoadModel(ship.PathnamePropeller1.c_str());
        mPropeller1->CreateMsPipeline(renderPassScene, extent);
		mPropeller1->CreateWireframeMsPipeline(renderPassScene, extent);
    }
    if (ship.PathnamePropeller2.length())
    {
        mPropeller2 = make_unique<Model>(vulkanDevice);
        mPropeller2->LoadModel(ship.PathnamePropeller2.c_str());
        mPropeller2->CreateMsPipeline(renderPassScene, extent);
        mPropeller2->CreateWireframeMsPipeline(renderPassScene, extent);
    }
    if (ship.PathnameRudder.length())
    {
        mRudder1 = make_unique<Model>(vulkanDevice);
        mRudder1->LoadModel(ship.PathnameRudder.c_str());
        mRudder1->CreateMsPipeline(renderPassScene, extent);
        mRudder1->CreateWireframeMsPipeline(renderPassScene, extent);
        if (ship.nRudder == 2)
        {
            mRudder2 = make_unique<Model>(vulkanDevice);
            mRudder2->LoadModel(ship.PathnameRudder.c_str());
            mRudder2->CreateMsPipeline(renderPassScene, extent);
            mRudder2->CreateWireframeMsPipeline(renderPassScene, extent);
        }
    }
    if (ship.PathnameRadar1.length())
    {
        mRadar1 = make_unique<Model>(vulkanDevice);
        mRadar1->LoadModel(ship.PathnameRadar1.c_str());
        mRadar1->CreateMsPipeline(renderPassScene, extent);
        mRadar1->CreateWireframeMsPipeline(renderPassScene, extent);
    }
    if (ship.nRadar > 1 && ship.PathnameRadar2.length())
    {
        mRadar2 = make_unique<Model>(vulkanDevice);
        mRadar2->LoadModel(ship.PathnameRadar2.c_str());
        mRadar2->CreateMsPipeline(renderPassScene, extent);
        mRadar2->CreateWireframeMsPipeline(renderPassScene, extent);
    }

    mAxis = make_unique<Model>(vulkanDevice);
    mAxis->LoadModel("Resources/Interface/Axis.glb");
    mAxis->CreateMsPipeline(renderPassScene, extent);
    mAxis->bVisible = true;
}
void Ship::InitDimensions()
{
    mBbox = mModelFull->GetBoundingBox();
    mMass = ship.Mass_t * 1000.0f;              // t -> kg for all physical calculations
    mPowerW = ship.PowerkW * 1000.0f * 0.5f;    // kW -> W for all physical calculations, 90% sur l'arbre et 55% d'efficacité de l'hélice, soit 0.5 au total
    if (ship.nPropeller == 2) mPowerW *= 0.5f;
    mLength = fabs(mBbox.max.x - mBbox.min.x);  // Overall length
    mLength3 = mLength * mLength * mLength;     // Length3
    mWidth = fabs(mBbox.max.z - mBbox.min.z);   // Overall width
    mHeight = fabs(mBbox.max.y - mBbox.min.y);  // Overall height
    if (mLength < mWidth) std::swap(mLength, mWidth);
    mDraft = -mBbox.min.y;                      // Below the water level
    mAirDraft = mBbox.max.y;                    // Above the water level
    mBow = vec3(mBbox.max.x, 0.0f, 0.0f);       // Distance to the centre
    mStern = vec3(mBbox.min.x, 0.0f, 0.0f);     // Distance to the centre
    mWakePivot = vec3(mBbox.min.x + 0.5f * mWidth, 0.0f, 0.0f);
    mRudderArea = (mLength * mDraft * 0.01f) * (1.0f + 0.25f * (mWidth / mDraft) * (mWidth / mDraft));    // DNV2 formula for the area of the rudder in m²
    mBowThrustMax = ship.BowThrusterPowerW * ship.BowThrusterPerf;
    mSternThrustMax = ship.SternThrusterPowerW * ship.SternThrusterPerf;
}
void Ship::InitTriangles()
{
    sTriangle tri;
    for (int i = 0; i < mF.rows(); ++i)
    {
        tri.I[0] = mF(i, 0);
        tri.I[1] = mF(i, 1);
        tri.I[2] = mF(i, 2);
        vec3 u = mvVertices[tri.I[1]] - mvVertices[tri.I[0]];
        vec3 v = mvVertices[tri.I[2]] - mvVertices[tri.I[0]];
        vec3 a = glm::cross(v, u);
        tri.Area = 0.5 * sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
        tri.NormalInitial = glm::normalize(a);
		tri.Color = vec3(0.5f, 0.5f, 0.5f);
        mvTris[i] = tri;
    }
}
void Ship::InitCentroid()
{
    mCentroid = vec3(0.0f);
    for (const auto& tri : mvTris)
        mCentroid += mvVertices[tri.I[0]] + mvVertices[tri.I[1]] + mvVertices[tri.I[2]];

    if (mvTris.size())
        mCentroid /= (mvTris.size() * 3.0f);

#ifdef PROPERTIES
    cout << "Centroid : ( " << mCentroid.x << ", " << mCentroid.y << ", " << mCentroid.z << " )" << endl;
#endif
}
bool IsPolygonClockwise(const Clipper2Lib::Path64& polygon)
{
    int64_t sum = 0;
    int n = (int)polygon.size();
    for (int i = 0; i < n; ++i)
    {
        int j = (i + 1) % n;
        sum += (polygon[j].x - polygon[i].x) * (polygon[j].y + polygon[i].y);
    }
    return sum > 0; // true if clockwise
}
void Ship::InitSurfaces()
{
    // Area
    mAreaXZ = mLength * mWidth;
    // Cube root of the area in the XZ plane
    mAreaXZ_RacCub = cbrtf(mAreaXZ);
    // Wet area
    mAreaWettedMax = 0.0f;
    for (auto& tri : mvTris) mAreaWettedMax += tri.Area;
    // Area of the propeller
    mAreaPropeller = M_PI * 0.25f * ship.PropDiameter * ship.PropDiameter;

#ifdef PROPERTIES   
    cout << "Surface XZ : " << mAreaXZ << " m2" << endl;
    cout << "Surface : " << mAreaWettedMax << " m2" << endl;
#endif

    if (ship.AreaFront != 0.0f && ship.AreaLat != 0.0f)
        return;

    vector<Mesh>& vMeshes = mModelFull->GetMesh();

    const double scale = 1e4;
    const double scale2 = scale * scale;

    Clipper2Lib::Paths64 projectedFront;  // projection on (Y, Z) plane — front silhouette
    Clipper2Lib::Paths64 projectedLat;    // projection on (X, Y) plane — lateral silhouette

    for (const auto& mesh : vMeshes)
    {
        for (size_t i = 0; i < mesh.vIndices.size(); i += 3)
        {
            const sVertex& v0 = mesh.vVertices[mesh.vIndices[i]];
            const sVertex& v1 = mesh.vVertices[mesh.vIndices[i + 1]];
            const sVertex& v2 = mesh.vVertices[mesh.vIndices[i + 2]];

            // Front silhouette: project onto (Y=height, Z=width) plane
            Clipper2Lib::Path64 triFront;
            triFront.push_back({ (int64_t)(v0.pos.y * scale), (int64_t)(v0.pos.z * scale) });
            triFront.push_back({ (int64_t)(v1.pos.y * scale), (int64_t)(v1.pos.z * scale) });
            triFront.push_back({ (int64_t)(v2.pos.y * scale), (int64_t)(v2.pos.z * scale) });
            if (IsPolygonClockwise(triFront)) std::reverse(triFront.begin(), triFront.end());
            projectedFront.push_back(triFront);

            // Lateral silhouette: project onto (X=length, Y=height) plane
            Clipper2Lib::Path64 triLat;
            triLat.push_back({ (int64_t)(v0.pos.x * scale), (int64_t)(v0.pos.y * scale) });
            triLat.push_back({ (int64_t)(v1.pos.x * scale), (int64_t)(v1.pos.y * scale) });
            triLat.push_back({ (int64_t)(v2.pos.x * scale), (int64_t)(v2.pos.y * scale) });
            if (IsPolygonClockwise(triLat)) std::reverse(triLat.begin(), triLat.end());
            projectedLat.push_back(triLat);
        }
    }

    // ── Clip polygon : keep only the part above waterline (y >= 0) ────────────

    const int64_t INF = std::numeric_limits<int64_t>::max() / 2;
    const int64_t waterlineScaled = (int64_t)(ship.PositionY * scale);  // waterline at y=0 in model space

    Clipper2Lib::Path64 clipAboveWaterline;
    clipAboveWaterline.push_back({ -INF, waterlineScaled });
    clipAboveWaterline.push_back({ INF, waterlineScaled });
    clipAboveWaterline.push_back({ INF, INF });
    clipAboveWaterline.push_back({ -INF, INF });

    // ── Helper lambda : union of triangles → clip above waterline → area + centroid ──

    auto computeSilhouette = [&]( Clipper2Lib::Paths64& projected, float& outArea, vec2& outCenter)
        {
            outArea = 0.0f;
            outCenter = vec2(0.0f);

            // Step 1 : union all projected triangles → true 2D silhouette (no double-counting)
            Clipper2Lib::Paths64 united;
            Clipper2Lib::Clipper64 unionClipper;
            unionClipper.AddSubject(projected);
            unionClipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, united);

            // Step 2 : intersect with above-waterline clip polygon
            Clipper2Lib::Clipper64 clipper;
            clipper.AddSubject(united);
            clipper.AddClip(Clipper2Lib::Paths64{ clipAboveWaterline });

            Clipper2Lib::Paths64 solution;
            clipper.Execute(Clipper2Lib::ClipType::Intersection, Clipper2Lib::FillRule::Positive, solution);

            // Step 3 : accumulate area and centroid over solution polygons
            double totalArea = 0.0;
            double cx = 0.0, cy = 0.0;

            for (const auto& poly : solution)
            {
                double polyArea = Clipper2Lib::Area(poly) / scale2;
                if (polyArea < 1e-12) continue;

                // Polygon centroid via shoelace formula
                double pcx = 0.0, pcy = 0.0;
                int n = (int)poly.size();
                for (int k = 0; k < n; ++k)
                {
                    int     l = (k + 1) % n;
                    double  xk = (double)poly[k].x / scale;
                    double  yk = (double)poly[k].y / scale;
                    double  xl = (double)poly[l].x / scale;
                    double  yl = (double)poly[l].y / scale;
                    double  f = (xk * yl - xl * yk);
                    pcx += (xk + xl) * f;
                    pcy += (yk + yl) * f;
                }
                pcx /= (6.0 * polyArea);
                pcy /= (6.0 * polyArea);

                cx += pcx * polyArea;
                cy += pcy * polyArea;
                totalArea += polyArea;
            }

            if (totalArea > 0.0)
            {
                outArea = (float)totalArea;
                outCenter.x = (float)(cx / totalArea);
                outCenter.y = (float)(cy / totalArea);
            }
        };

    // ── Front silhouette (projection on Y,Z) → AreaFront, AreaFrontCenter ────

    vec2 frontCenter2D;
    float frontArea = 0.0f;
    computeSilhouette(projectedFront, frontArea, frontCenter2D);

    ship.AreaFront = frontArea;
    ship.AreaFrontCenter.y = frontCenter2D.x;   // Y=height
    ship.AreaFrontCenter.z = frontCenter2D.y;   // Z=width

    // ── Lateral silhouette (projection on X,Y) → AreaLat, AreaLatCenter ──────

    vec2 latCenter2D;
    float latArea = 0.0f;
    computeSilhouette(projectedLat, latArea, latCenter2D);

    ship.AreaLat = latArea;
    ship.AreaLatCenter.x = latCenter2D.x;     // X=length
    ship.AreaLatCenter.y = latCenter2D.y;     // Y=height

    cout << "AreaFront : " << ship.AreaFront << " m2  center=";
    PrintGlmVec3(ship.AreaFrontCenter);
    cout << "AreaLat   : " << ship.AreaLat << " m2  center=";
    PrintGlmVec3(ship.AreaLatCenter);
    cout << endl;
}
void Ship::InitInertia()
{
    // Empirical radii of gyration (fraction of L or B), ITTC, Clarke 1983, Brix 1993
    float kyy = 0.25f;  // yaw   : 0.22-0.28 × L
    float kxx = 0.35f;  // roll  : 0.30-0.40 × B  
    float kzz = 0.25f;  // pitch : 0.22-0.28 × L

    switch (ship.Class)
    {
    case eClass::FastBoat:    kyy = 0.26f; kxx = 0.38f; kzz = 0.26f; break;
    case eClass::Corvette:    kyy = 0.25f; kxx = 0.36f; kzz = 0.25f; break;
    case eClass::Frigate:     kyy = 0.25f; kxx = 0.35f; kzz = 0.25f; break;
    case eClass::Fishing:     kyy = 0.27f; kxx = 0.40f; kzz = 0.27f; break;
    case eClass::Submarine:   kyy = 0.24f; kxx = 0.32f; kzz = 0.24f; break;
    case eClass::Ferry:       kyy = 0.26f; kxx = 0.38f; kzz = 0.26f; break;
    case eClass::Tugboat:     kyy = 0.27f; kxx = 0.42f; kzz = 0.27f; break;
    case eClass::Cargo:       kyy = 0.25f; kxx = 0.35f; kzz = 0.25f; break;
    case eClass::Supertanker: kyy = 0.24f; kxx = 0.33f; kzz = 0.24f; break;
    }

    mIyy = mMass * (kyy * mLength) * (kyy * mLength);   // Yaw   (autour Y, longueur X)
    mIzz = mMass * (kzz * mLength) * (kzz * mLength);   // Pitch (autour Z, longueur X)
    mIxx = mMass * (kxx * mWidth) * (kxx * mWidth);     // Roll  (autour X, largeur Z)

    mVolume = mMass / mWATER_DENSITY;

#ifdef PROPERTIES
    // Displaying results
    cout << "Volume : " << mVolume << " m3" << endl;
    cout << "============================" << endl;
    cout << "Moments d'inertie volumiques" << endl;
    cout << "Volume total : " << mVolume << " m3" << endl;
    cout << "Ixx = " << mIxx << " kg/m2" << endl;
    cout << "Iyy = " << mIyy << " kg/m2" << endl;
    cout << "Izz = " << mIzz << " kg/m2" << endl;
    cout << endl;
#endif
}
void Ship::InitWaterVertices()
{
    // Positions
    for (int z = 0; z <= mOcean->MESH_SIZE; ++z)
    {
        vector<vec3> vPos;
        for (int x = 0; x <= mOcean->MESH_SIZE; ++x)
        {
            int index = z * mOcean->MESH_SIZE_1 + x;
            vec3 v;
            v.x = (x - mOcean->MESH_SIZE / 2.0f) * mOcean->PATCH_SIZE / mOcean->MESH_SIZE;
            v.y = 0.0f;
            v.z = (z - mOcean->MESH_SIZE / 2.0f) * mOcean->PATCH_SIZE / mOcean->MESH_SIZE;
            vPos.push_back(v);
        }
        mvWaterPos.push_back(vPos);
    }
}
void Ship::InitHullMesh(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent)
{
    // Converting mvVertices, Normals and Colors
    mvVertexColored = vector<float>(mvTris.size() * 3 * 6);
    int index = 0;
    for (const auto& tri : mvTris)
    {
        for (int j = 0; j < 3; ++j)
        {
            // Position
            mvVertexColored[index++] = mvVertices[tri.I[j]].x;   // x
            mvVertexColored[index++] = mvVertices[tri.I[j]].y;   // y
            mvVertexColored[index++] = mvVertices[tri.I[j]].z;   // z

            // Color
            mvVertexColored[index++] = tri.Color.r;   // r
            mvVertexColored[index++] = tri.Color.g;   // g
            mvVertexColored[index++] = tri.Color.b;   // b
        }
    }

    // Generation of mvIndices
    vector<unsigned int> indices(mF.rows() * 3);
    for (unsigned int i = 0; i < indices.size(); ++i)
        indices[i] = i;

    mHullMesh = make_unique<HullMesh>(vulkanDevice, mvVertexColored, indices);
	mHullMesh->CreatePipeline(renderPassScene, extent);
}
void Ship::InitPressureMesh(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent)
{
    float coeff = 0.001f * (200000.0f / mMass) * (6000.0f / mF.rows());

    vector<vec3> linePoints;
    vec3 start = vec3(0.0f);
    vec3 end = vec3(1.0f);

    for (auto& tri : mvTris)
    {
        linePoints.push_back(start);
        linePoints.push_back(end);
    }

    mPressureMesh = make_unique<LineMesh>(mVulkanDevice, linePoints);
    mPressureMesh->CreatePipeline(renderPassScene, extent, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    mPressureMesh->UpdateVertices(linePoints);
}
void Ship::InitContours(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent)
{
	// Create 2 contours, the first for the spray, the second for the foam, which is a bit more expanded
    
    // Contour
    vector<vec3> contour = ComputeContour();    // Intersect the mesh with the ocean (Y = 0)
    if (contour.size() == 0)
        return;

    // Sort the points
    contour = ArrangeContour(contour);          
	mContourMesh1 = make_unique<LineMesh>(vulkanDevice, contour);
    mContourMesh1->CreatePipeline(renderPassScene, extent);

	// Put a small offset to the contour to create the spray, otherwise it would be exactly on the waterline and cause z-fighting
    vector<vec3> contourSpray = OffsetContour(contour, 0.1f);
    InitSpray(contourSpray);

	// Close the contour by adding the first point at the end, otherwise the offset contour would be open and not form a closed loop
    contour.push_back(contour.front());

    // Expand the contour with a constant offset
    contour = OffsetContour(contour, 0.5f);     
	// Create the second for visual debugging, but also to create the texture of the foam inside the expanded contour
    mContourMesh2 = make_unique<LineMesh>(vulkanDevice, contour);
    mContourMesh2->CreatePipeline(renderPassScene, extent);

    CreateTextureOfContour(vulkanDevice, contour);      // Create the texture formed with foam inside the exapnded contour
}
void Ship::InitWakeMesh(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPassScene, VkExtent2D extent, VkRenderPass randerPassWake)
{
    mWakeMesh = make_unique<WakeMesh>(vulkanDevice, vWakeVertices);
    mWakeMesh->CreatePipeline(renderPassScene, extent);
	mWakeMesh->CreatePipelineTexture(randerPassWake, VkExtent2D(g_WakeSize, g_WakeSize));
    mWakeMesh->CreateBlurPipelines();
}
void Ship::InitSounds(Camera& camera)
{
    // Sounds
    g_SoundMgr->setListenerPosition(camera.GetPosition());
    g_SoundMgr->setListenerOrientation(camera.GetAt(), camera.GetUp());

    // Power 1
    mSoundThrust1 = make_unique<Sound>(ship.ThrustSound);
    mSoundThrust1->setVolume(0.25f);
    mSoundThrust1->setPosition(ship.Position + ship.PosPropeller1);
    mSoundThrust1->setLooping(true);
    mSoundThrust1->adjustDistances();

    // Power 2
    mSoundThrust2 = make_unique<Sound>(ship.ThrustSound);
    mSoundThrust2->setVolume(0.25f);
    mSoundThrust2->setPosition(ship.Position + ship.PosPropeller2);
    mSoundThrust2->setLooping(true);
    mSoundThrust2->adjustDistances();

    if (bSound)
    {
        mSoundThrust1->play();
        mSoundThrust2->play();
    }

    // Bow thruster
    if (ship.HasBowThruster)
    {
        mSoundBowThruster = make_unique<Sound>(ship.BowThrusterSound);
        mSoundBowThruster->setVolume(0.25f);
        mSoundBowThruster->setPosition(ship.Position + ship.PosBowThruster);
        mSoundBowThruster->setLooping(true);
        mSoundBowThruster->adjustDistances();
    }

    // Stern thruster
    if (ship.HasSternThruster)
    {
        mSoundSternThruster = make_unique<Sound>(ship.SternThrusterSound);
        mSoundSternThruster->setVolume(0.25f);
        mSoundSternThruster->setPosition(ship.Position + ship.PosSternThruster);
        mSoundSternThruster->setLooping(true);
        mSoundSternThruster->adjustDistances();
    }
}
void FilterClosePoints(vector<sSprayPt>& pts)
{
    if (pts.size() < 2)
        return;

    float totalDist = glm::length(pts.front().p - pts.back().p);
    float threshold = totalDist / pts.size();

    // New filtered list
    vector<sSprayPt> filtered;
    filtered.reserve(pts.size());

    filtered.push_back(pts[0]); // Always keep the first point
    vec3 lastPos = pts[0].p;

    for (size_t i = 1; i < pts.size(); ++i)
    {
        float dist = glm::length(pts[i].p - lastPos);
        if (dist >= threshold)
        {
            filtered.push_back(pts[i]);
            lastPos = pts[i].p;
        }
        // Otherwise ignore the point that is too close
    }

    pts = std::move(filtered);
}
void Ship::InitSpray(vector<vec3>& contour)
{
    if (contour.size() == 0)
        return;

    float maxForward = contour[0].x;
    int frontIndex = 0;
    for (int i = 1; i < (int)contour.size(); i++)
    {
        if (contour[i].x > maxForward)
        {
            maxForward = contour[i].x;
            frontIndex = i;
        }
    }

    vec3 frontPoint = contour[frontIndex];

    mLeft.clear();
    mRight.clear();

    float dist = mLength * ship.SprayLength;

    for (size_t i = 0; i < contour.size(); ++i)
    {
        vec3 p = contour[i];
        float d = glm::length(p - frontPoint);

        if (d <= dist)
        {
            // Find the point before and the point after on the contour
            int idxPrev = (i == 0) ? (int)contour.size() - 1 : (int)i - 1;
            int idxNext = (i == contour.size() - 1) ? 0 : (int)i + 1;

            vec3 prev = contour[idxPrev];
            vec3 next = contour[idxNext];

            // Tangent vector (direction of the contour at point p)
            vec3 tangent = glm::normalize(next - prev);

            // For the x/z plane: take the outside
            vec3 toPrev = prev - p;
            vec3 toNext = next - p;
            // Lateral vector in the plane (here (toNext - toPrev))
            vec3 lateral = toNext - toPrev;
            // Normal: perpendicular to tangent, oriented outward
            vec3 n = glm::normalize(glm::cross(tangent, vec3(0, 1, 0)));

            // We want n.z to have the same sign as p.z 
            if ((p.z < 0.0f && n.z < 0.0f) || (p.z > 0.0f && n.z > 0.0f))
            {
                // n already on the right side
            }
            else
                n = -n;

            sSprayPt pt;
            pt.p = p;
            pt.n = n;

            if (p.z < 0.0f)
                mLeft.push_back(pt);
            else if (p.z > 0.0f)
                mRight.push_back(pt);
        }
    }

    // Comparator to sort in ascending order of distance on the X axis from frontPoint
    auto compareNearToFarX = [frontPoint](const sSprayPt& a, const sSprayPt& b) {
        float distA = frontPoint.x - a.p.x;  // the "distance" in x from frontPoint
        float distB = frontPoint.x - b.p.x;
        return distA < distB;
        };

    std::sort(mLeft.begin(), mLeft.end(), compareNearToFarX);
    std::sort(mRight.begin(), mRight.end(), compareNearToFarX);

    FilterClosePoints(mLeft);
    FilterClosePoints(mRight);

    // Ajout de points extrapolés en avant (axe +X)
    auto addForwardPoints = [this](vector<sSprayPt>& pts, int numExtra)
        {
            if (pts.size() < 2) return;

            const sSprayPt& p0 = pts[0];
            const sSprayPt& p1 = pts[1];

            // Pas uniquement en X, ignorant l'écart transversal
            float step = ship.Length / 200.0f;

            // Direction purement axiale vers l'avant
            vec3 dir = vec3(1.0f, 0.0f, 0.0f);

            vector<sSprayPt> extra;
            for (int i = 1; i <= numExtra; ++i)
            {
                sSprayPt pt;
                // Z et Y restent ceux de p0 : le point reste sur le bord latéral de la proue
                pt.p = vec3(p0.p.x + dir.x * step * (float)i, p0.p.y, p0.p.z);
                pt.n = p0.n;
                extra.push_back(pt);
            }

            std::reverse(extra.begin(), extra.end());
            pts.insert(pts.begin(), extra.begin(), extra.end());
        };
    const int NUM_EXTRA_POINTS = 2;
    addForwardPoints(mLeft, NUM_EXTRA_POINTS);
    addForwardPoints(mRight, NUM_EXTRA_POINTS);

    if (mLeft.size() > 1 && mRight.size() > 1)
    {
        mRandomOffsetRange = 0.0f;
        for (size_t i = 0; i < mLeft.size() - 1; ++i)
            mRandomOffsetRange += glm::length(mLeft[i + 1].p - mLeft[i].p);

        for (size_t i = 0; i < mRight.size() - 1; ++i)
            mRandomOffsetRange += glm::length(mRight[i + 1].p - mRight[i].p);

        mRandomOffsetRange /= (mLeft.size() - 1 + mRight.size() - 1);
        mRandomOffsetRange *= 0.1f;
    }
}
vector<vec3> Ship::ComputeContour()
{
    vector<vec3> contour;
    contour.reserve(64);

    for (const sTriangle& tri : mvTris)
    {
        const vec3& v0 = mvVertices[tri.I[0]];
        const vec3& v1 = mvVertices[tri.I[1]];
        const vec3& v2 = mvVertices[tri.I[2]];

        float y0 = v0.y, y1 = v1.y, y2 = v2.y;
        int sign0 = (y0 > 1e-6f) ? 1 : ((y0 < -1e-6f) ? -1 : 0);
        int sign1 = (y1 > 1e-6f) ? 1 : ((y1 < -1e-6f) ? -1 : 0);
        int sign2 = (y2 > 1e-6f) ? 1 : ((y2 < -1e-6f) ? -1 : 0);

        if ((sign0 == 1 && sign1 == 1 && sign2 == 1) || (sign0 == -1 && sign1 == -1 && sign2 == -1)) continue;

        // Points exactly on the plan (strict tolerance)
        if (abs(y0) < 1e-6f) contour.push_back(v0);
        if (abs(y1) < 1e-6f) contour.push_back(v1);
        if (abs(y2) < 1e-6f) contour.push_back(v2);

        // Edge 0-1: avoid division by zero
        if (sign0 * sign1 < 0 && abs(y1 - y0) > 1e-6f)
        {
            float t = glm::clamp(-y0 / (y1 - y0), 0.0f, 1.0f);
            contour.push_back(glm::mix(v0, v1, t));
        }

        // Edge 1-2
        if (sign1 * sign2 < 0 && abs(y2 - y1) > 1e-6f)
        {
            float t = glm::clamp(-y1 / (y2 - y1), 0.0f, 1.0f);
            contour.push_back(glm::mix(v1, v2, t));
        }

        // Edge 2-0
        if (sign2 * sign0 < 0 && abs(y0 - y2) > 1e-6f)
        {
            float t = glm::clamp(-y2 / (y0 - y2), 0.0f, 1.0f);
            contour.push_back(glm::mix(v2, v0, t));
        }
    }

    // Sorting
    std::sort(contour.begin(), contour.end(), [](const vec3& a, const vec3& b) {
            return a.x < b.x || (a.x == b.x && a.z < b.z);
        });

    // Remove duplicates
    vector<vec3> uniqueContour;
    uniqueContour.reserve(contour.size());
    for (size_t i = 0; i < contour.size(); ++i)
        if (i == 0 || abs(contour[i].x - contour[i - 1].x) > 1e-4f || abs(contour[i].z - contour[i - 1].z) > 1e-4f)
            uniqueContour.push_back(contour[i]);

    return uniqueContour;
}
vector<vec3> Ship::ArrangeByCoordinates(const vector<vec3>& contourUnordered)
{
    vector<vec3> left;
    vector<vec3> right;
    for (const vec3& p : contourUnordered)
    {
        if (p.z <= 0.0f)        left.push_back(p);
        else if (p.z > 0.0f)    right.push_back(p);
    }

    auto compareNearToFarX = [](const vec3& a, const vec3& b) {
        if (abs(a.x - b.x) < 1e-4f)
            return a.z > b.z; // sort by z if x difference is very small
        return a.x < b.x;
        };
    sort(left.begin(), left.end(), compareNearToFarX);
    sort(right.begin(), right.end(), compareNearToFarX);
    vector<vec3> ordered;
    ordered.reserve(left.size() + right.size());
    ordered.insert(ordered.end(), left.begin(), left.end());
    ordered.insert(ordered.end(), right.rbegin(), right.rend()); // right in reverse order
    return ordered;
}
vector<vec3> Ship::ArrangeByPolarAngle(const vector<vec3>& contourUnordered)
{
    if (contourUnordered.empty()) return {};

    // 1. Calculate the centroid (in the xz plane)
    float cx = 0.f, cz = 0.f;
    for (const vec3& p : contourUnordered) { cx += p.x; cz += p.z; }
    cx /= contourUnordered.size();
    cz /= contourUnordered.size();

    // 2. Sort by polar angle around the centroid
    vector<vec3> ordered = contourUnordered;
    std::sort(ordered.begin(), ordered.end(), [cx, cz](const vec3& a, const vec3& b) {
            float angleA = std::atan2(a.z - cz, a.x - cx);
            float angleB = std::atan2(b.z - cz, b.x - cx);
            return angleA < angleB;
        });

    // 3. Close the polygon
    ordered.push_back(ordered.front());

    return ordered;
}
vector<vec3> Ship::ArrangeContour(const vector<vec3>& contourUnordered)
{
    if (ship.ContourType == 1)
		return ArrangeByCoordinates(contourUnordered);
    else
		return ArrangeByPolarAngle(contourUnordered);
}
vector<vector<vec2>> offsetContourWithClipper(const vector<vec2>& contour, float offset, JoinType joinType = JoinType::Round, EndType endType = EndType::Polygon)
{
    const double scaleClipper = 1e6; // scale factor to maintain accuracy of clipper (calculations on int64)

    // Convert contour to Clipper int64
    Path64 path;
    path.reserve(contour.size());
    for (const auto& pt : contour)
        path.emplace_back(static_cast<int64_t>(pt.x * scaleClipper), static_cast<int64_t>(pt.y * scaleClipper));

    // ClipperOffset manages multiple paths, we create a vector of paths
    Paths64 paths;
    paths.push_back(path);

    // Offset in integer units, we convert the float offset to int64_t
    int64_t intOffset = static_cast<int64_t>(offset * scaleClipper);

    ClipperOffset offsetter;
    offsetter.AddPaths(paths, joinType, endType);

    Paths64 solution;
    offsetter.Execute(intOffset, solution);

    // Convert each solution polygon to glm::vec2 (float)
    vector<vector<vec2>> result;
    result.reserve(solution.size());
    for (const Path64& p : solution)
    {
        vector<vec2> res;
        result.reserve(p.size());
        for (const auto& pt : p)
            res.emplace_back(static_cast<float>(pt.x) / static_cast<float>(scaleClipper), static_cast<float>(pt.y) / static_cast<float>(scaleClipper));
        result.push_back(res);
    }

    return result;
}
vector<vec3> Ship::OffsetContour(const vector<vec3>& contour, float offset)
{
    // Convert contour in 3D to contour 2D
    vector<vec2> contour2d;
    for (const auto& v : contour)
        contour2d.push_back(vec2(v.x, v.z));

    // Execute the offset with clipper library
    auto newContour = offsetContourWithClipper(contour2d, offset);

    // Convert the result to 3D
    vector<vec3> result;
    size_t totalSize = 0;
    for (const auto& contour : newContour)
        totalSize += contour.size();
    result.reserve(totalSize);

    for (const auto& contour : newContour)
        for (const vec2& p : contour)
            result.emplace_back(p.x, 0.f, p.y); // Level of the water

    return result;
}
bool isPointInPolygon(const vec2& pt, const vector<vec2>& contour2D)
{
    // Tests if a point (x, z) is inside a polygon (ray algorithm)

    bool inside = false;
    size_t n = contour2D.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const vec2& vi = contour2D[i];
        const vec2& vj = contour2D[j];
        if (((vi.y > pt.y) != (vj.y > pt.y)) && (pt.x < (vj.x - vi.x) * (pt.y - vi.y) / (vj.y - vi.y + 1e-8f) + vi.x))
            inside = !inside;
    }
    return inside;
}
float minDistanceToContour(const vec2& pt, const vector<vec2>& contour2D)
{
    // Minimum distance between a point and a polyline (to draw the outline in white)

    float minDist = numeric_limits<float>::max();
    size_t n = contour2D.size();
    for (size_t i = 0; i < n; ++i)
    {
        const vec2& a = contour2D[i];
        const vec2& b = contour2D[(i + 1) % n];
        vec2 ab = b - a;
        vec2 ap = pt - a;
        float t = glm::clamp(glm::dot(ap, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        vec2 proj = a + t * ab;
        float dist = glm::distance(pt, proj);
        if (dist < minDist) minDist = dist;
    }
    return minDist;
}
void Ship::CreateTextureOfContour(shared_ptr<VulkanDevice>& vulkanDevice, const vector<vec3>& contour)
{
    const int SCALE = 4;

    float metersW = std::ceil((mLength + 5.0f) / 10.0f) * 10.0f;
    float metersH = std::ceil((mWidth + 5.0f) / 10.0f) * 10.0f;

    TexContourShipW = (int)metersW * SCALE;
    TexContourShipH = (int)metersH * SCALE;

    float halfW = TexContourShipW / 2.0f;
    float halfH = TexContourShipH / 2.0f;

    vector<vec2> contourHi;
    for (const auto& v : contour)
        contourHi.emplace_back(v.x * SCALE + halfW, v.z * SCALE + halfH);

    float edgeWidth = 1.0f * SCALE;

    vector<float> mask(TexContourShipW * TexContourShipH);
    for (int j = 0; j < TexContourShipH; ++j)
    {
        for (int i = 0; i < TexContourShipW; ++i)
        {
            vec2 pt(i + 0.5f, j + 0.5f);
            bool inside = isPointInPolygon(pt, contourHi);
            float dist = minDistanceToContour(pt, contourHi);
            float sd = inside ? -dist : dist;

            if (sd <= 0.0f)          mask[j * TexContourShipW + i] = 1.0f;
            else if (sd < edgeWidth) mask[j * TexContourShipW + i] = 1.0f - sd / edgeWidth;
            else                     mask[j * TexContourShipW + i] = 0.0f;
        }
    }

    TexContourShip.Create(vulkanDevice, TexContourShipW, TexContourShipH, 1, VK_FORMAT_R32_SFLOAT, true);
    memcpy(TexContourShip.cpuData, mask.data(), TexContourShipW * TexContourShipH * sizeof(float));
    TexContourShipW = metersW;
    TexContourShipH = metersH;
    TexContourShip.CopyStagingToGPU();
}

// Support
void Ship::ComputeEquilibriumDraft()
{
    if (ship.AreaWetted != 0.0f)
        return;

    // ── Save current state ─────────────────────────────────────────

    float savedPositionY = ship.Position.y;
    float savedHeaveVel = HeaveVelocity;
    float savedHeaveAcc = HeaveAcceleration;
    float savedPitch = Pitch;
    float savedRoll = Roll;
    float savedPitchVel = PitchVelocity;
    float savedRollVel = RollVelocity;
    float savedPitchAcc = PitchAcceleration;
    float savedRollAcc = RollAcceleration;
    float savedAreaWetted = AreaWetted;
    vec2  savedGWind = g_Wind;

    // ── Configuration ─────────────────────────────────────────────────────────

    // Zero wind, no waves
    g_Wind = vec2(0.0f);

    // Ship flat (no heel or trim)
    Pitch = 0.0f;
    Roll = 0.0f;
    PitchVelocity = 0.0f;
    RollVelocity = 0.0f;
    PitchAcceleration = 0.0f;
    RollAcceleration = 0.0f;

    // Zero initial heave velocity
    HeaveVelocity = 0.0f;

    // ── Simulation parameters ──────────────────────────────────────────────

    const float dt = 0.05f;    // fixed time step (50ms)
    const int   maxSteps = 1000;    // max 5 seconds of simulation
    const float tolForce = mMass * mGRAVITY * 0.0001f;  // 0.01% of weight
    const float tolVelocity = 0.0001f;  // 0.1 mm/s

    // Calculate gravity (constant)
    ComputeGravity();

    // ── Initial estimation of vertical position ──────────────────────────

    ship.Position.y = 0.0f;  // initial approximation

    // ── Convergence loop ─────────────────────────────────────────────────
    int   step = 0;
    float prevPositionY = ship.Position.y;
    bool  converged = false;

    // Artificial damping to accelerate convergence (avoids heave oscillations)
    const float dampingFactor = 0.85f;

    while (step < maxSteps)
    {
        // Update world matrix with current position
        UpdateWorldMatrix();
        TransformVertices();
        GetHeightOfAllVertices();
        GetTrisUnderWater();
        ComputeArchimede();

        // Net heave force
        float HeaveForce = mArchimede.Magnitude - mGravity.Magnitude;

        // Artificial critical damping for rapid convergence
        
        // F_damp = -2 × sqrt(K × M) × v  with K = ρgA
        float K = mWATER_DENSITY * mGRAVITY * mAreaXZ;
        float C_crit = 2.0f * sqrt(K * mMass);
        HeaveForce -= C_crit * HeaveVelocity;  // critical damping

        // Integration
        HeaveAcceleration = HeaveForce / mMass;
        HeaveVelocity += HeaveAcceleration * dt;
        HeaveVelocity *= dampingFactor;         // additional damping
        ship.Position.y += HeaveVelocity * dt;

        // ── Convergence test ───────────────────────────────────────────────

        float forceResidual = fabs(mArchimede.Magnitude - mGravity.Magnitude);
        float velocityResidual = fabs(HeaveVelocity);

        if (forceResidual < tolForce && velocityResidual < tolVelocity)
        {
            converged = true;
            break;
        }

        // Divergence protection
        if (fabs(ship.Position.y - prevPositionY) > mLength)
        {
            cout << "[ComputeEquilibriumDraft] Divergence detected at step=" << step << endl;
            ship.Position.y = savedPositionY;
            break;
        }

        prevPositionY = ship.Position.y;
        step++;
    }

    // ── Results ─────────────────────────────────────────────────────────────

    if (converged)
    {
        // Last calculation to get clean final values
        UpdateWorldMatrix();
        TransformVertices();
        GetHeightOfAllVertices();
        GetTrisUnderWater();
        ComputeArchimede();

        mAreaWettedMax = AreaWetted;

        float archimedeError = fabs(mArchimede.Magnitude - mGravity.Magnitude) / mGravity.Magnitude * 100.0f;

        cout << fixed << setprecision(2)
            << "[ComputeEquilibriumDraft] " << ship.ShortName << endl
            << "  Converged in " << step << " steps (" << step * dt << " s)" << endl
            << "  Position Y   = " << ship.Position.y << " m" << endl
            << "  AreaWetted   = " << AreaWetted << " m²" << endl
            << "  Error        = " << archimedeError << " %" << endl
            << "  LWL          = " << LWL << " m" << endl;
    }
    else
    {
        cout << "[ComputeEquilibriumDraft] " << ship.ShortName << " — not converged after " << maxSteps << " steps" << endl
            << "  AreaWetted   = " << AreaWetted << " m² (approximate value)" << endl;
    }

    // ── State restoration ────────────────────────────────────────────────
    
    HeaveVelocity = 0.0f;
    HeaveAcceleration = 0.0f;
    Pitch = savedPitch;
    Roll = savedRoll;
    PitchVelocity = savedPitchVel;
    RollVelocity = savedRollVel;
    PitchAcceleration = savedPitchAcc;
    RollAcceleration = savedRollAcc;
    AreaWetted = savedAreaWetted;
    g_Wind = savedGWind;

    // Final update of the world matrix with the equilibrium position
    UpdateWorldMatrix();
}
void Ship::ComputeMaxSpeed()
{
    if (ship.SpeedMaxKt != 0.0f)
        return;

    // ── Save current state ─────────────────────────────────────────

    float savedSurgeVelocity = SurgeVelocity;
    float savedPropRpm1 = PropRpm1;
    float savedPropRpm2 = PropRpm2;
    float savedPowerApplied1 = PowerApplied1;
    float savedPowerApplied2 = PowerApplied2;
    int   savedPowerStep1 = PowerCurrentStep1;
    int   savedPowerStep2 = PowerCurrentStep2;
    vec2  savedVCOG = vCOG;
    vec2  savedGWind = g_Wind;

    // ── Configuration for calculation ─────────────────────────────────────────

    // Zero wind
    g_Wind = vec2(0.0f);
    vCOG = vec2(0.0f);

    // Full power
    PowerCurrentStep1 = ship.PowerStepMax;
    PowerCurrentStep2 = (ship.nPropeller == 2) ? ship.PowerStepMax : 0;
    PropRpm1 = ship.PropRpmMax;
    PropRpm2 = (ship.nPropeller == 2) ? ship.PropRpmMax : 0.0f;
    PowerApplied1 = mPowerW;
    PowerApplied2 = (ship.nPropeller == 2) ? mPowerW : 0.0f;

	// Wetted surface and LWL at equilibrium
	AreaWetted = ship.AreaWetted;
    LWL = ship.LWL;

    // ── Function to compute forward force at a given speed ──────────────────

    auto computeNetForce = [&](float v) -> float
        {
            SurgeVelocity = v;
            vCOG = vec2(0.0f);

            // Thrust
            ComputeMainThrust(0.0f);

            // Drags
            ComputeViscousDrag();
            ComputeWavesDrag();
            ComputeWind();   // zero wind → AirDrag only (aerodynamic resistance)

            float thrust = mThrust1.Magnitude + mThrust2.Magnitude;
            float drag = mViscousDrag.Magnitude + mWavesDrag.Magnitude + mAirDrag.Magnitude;

            return thrust + drag;  // thrust positive, drag negative 
        };

    // ── Bisection search ───────────────────────────────────────────────
    
    // Find the speed where forceFWD = 0 (equilibrium)

    auto findEquilibriumSpeed = [&](float powerFactor) -> float
        {
            PowerApplied1 = mPowerW * powerFactor;
            PowerApplied2 = (ship.nPropeller == 2) ? mPowerW * powerFactor : 0.0f;
            PropRpm1 = ship.PropRpmMax * powerFactor;
            PropRpm2 = (ship.nPropeller == 2) ? ship.PropRpmMax * powerFactor : 0.0f;

            const float vMin = 0.1f;
            const float vMax = 30.0f;
            const int   nScan = 300;
            const float dv = (vMax - vMin) / nScan;

            // ── Full scan to find ALL sign changes ──────────
            struct ZeroCrossing { float vLow; float vHigh; bool risingToFalling; };
            vector<ZeroCrossing> crossings;

            float fPrev = computeNetForce(vMin);
            if (fPrev <= 0.0f)
                return 0.0f;  // not enough power

            for (int i = 1; i <= nScan; i++)
            {
                float v = vMin + i * dv;
                float f = computeNetForce(v);

                if (fPrev > 0.0f && f <= 0.0f)
                    crossings.push_back({ v - dv, v, true });   // positive → negative
                else if (fPrev <= 0.0f && f > 0.0f)
                    crossings.push_back({ v - dv, v, false });  // negative → positive (planing recovery)

                fPrev = f;
            }

            if (crossings.empty())
                return ms_to_knot(vMax);  // no equilibrium in the range

            // ── Search for the LAST positive→negative zero (true equilibrium) ────────
            // A false zero is followed by a negative→positive zero (recovery)
            // The true equilibrium is the last positive→negative zero without recovery after
            float vEquilibrium = -1.0f;

            for (int i = 0; i < (int)crossings.size(); i++)
            {
                if (!crossings[i].risingToFalling)
                    continue;  // looking for positive→negative

                // Check if after this zero, the force becomes positive again (false zero)
                bool isFalseZero = false;
                for (int j = i + 1; j < (int)crossings.size(); j++)
                {
                    if (!crossings[j].risingToFalling)  // negative→positive after
                    {
                        isFalseZero = true;
                        break;
                    }
                }

                if (!isFalseZero)
                {
                    // This is the true final equilibrium
                    vEquilibrium = crossings[i].vLow;
                    float vH = crossings[i].vHigh;

                    // Bisection
                    const int   maxIter = 64;
                    const float tol = 0.001f;
                    for (int k = 0; k < maxIter; k++)
                    {
                        float vMid = 0.5f * (vEquilibrium + vH);
                        float fMid = computeNetForce(vMid);
                        if (fabs(fMid) < tol || (vH - vEquilibrium) < tol)
                            return ms_to_knot(vMid);
                        if (fMid > 0.0f) vEquilibrium = vMid;
                        else             vH = vMid;
                    }
                    return ms_to_knot(0.5f * (vEquilibrium + vH));
                }
            }

            // All zeros are false → no stable equilibrium found
            return ms_to_knot(vMax);
        };

    // ── Calculation at 100% and 70% power ────────────────────────────────────

    ship.SpeedMaxKt = findEquilibriumSpeed(1.0f);
    ship.SpeedEcoKt = findEquilibriumSpeed(0.7f);
    cout << fixed << setprecision(1) << "SpeedMax=" << ship.SpeedMaxKt << " kt" << "  SpeedEco=" << ship.SpeedEcoKt << " kt" << endl;

    // ── Restoration of the state ────────────────────────────────────────────────

    SurgeVelocity = savedSurgeVelocity;
    PropRpm1 = savedPropRpm1;
    PropRpm2 = savedPropRpm2;
    PowerApplied1 = savedPowerApplied1;
    PowerApplied2 = savedPowerApplied2;
    PowerCurrentStep1 = savedPowerStep1;
    PowerCurrentStep2 = savedPowerStep2;
    vCOG = savedVCOG;
    g_Wind = savedGWind;

    ComputeMainThrust(0.0f);  // reset thrust to their initial state
}

void Ship::ResetVelocities()
{
    Pitch = 0.0f;
    Roll = 0.0f;
    PitchCouple = 0.0f;
    RollCouple = 0.0f;
    PitchAcceleration = 0.0f;
    RollAcceleration = 0.0f;
    PitchVelocity = 0.0f;
    RollVelocity = 0.0f;
    HeaveAcceleration = 0.0f;
    HeaveVelocity = 0.0f;
    SurgeAcceleration = 0.0f;
    SurgeVelocity = 0.0f;
    YawAcceleration = 0.0f;
    YawVelocity = 0.0f;
    WindAcceleration = 0.0f;
    WindVelocity = 0.0f;
    LinearVelocity = 0.0f;
    DriftVelocity = 0.0f;
    DriftAngleDeg = 0.0f;
    SOG = 0.0f;
}
vec3 Ship::TransformPosition(vec3 v)
{
    return vec3(mWorld * vec4(v, 1.0f));
}
vec3 Ship::TransformVector(vec3 v)
{
    return vec3(mWorld * vec4(v, 0.0f));
}
void Ship::SetYawFromHDG(float hdg)
{
    float deg_Yaw = fmod(450.0f - hdg, 360.0f);
    if (deg_Yaw < 0.0f)
        deg_Yaw += 360.0f;
    Yaw = glm::radians(deg_Yaw);
}

// Creation of all the Kelvin wake textures 
constexpr int IMAGE_HEIGHT = 1024;
constexpr int IMAGE_WIDTH = IMAGE_HEIGHT / 2;
constexpr int IMAGE_WIDTH_2SIDES = IMAGE_HEIGHT;
constexpr float MAX_Y_TILDE = 10.0f;
constexpr float PI = 3.14159265358979323846f;
float pressure_field(float theta, float FR)
{
    // Adaptation of pressure_field

    float K0_inv = (FR * std::cos(theta)) * (FR * std::cos(theta));
    float denom = 2.0f * M_PI * K0_inv;
    float exponent = -1.0f / (denom * denom);
    return std::exp(exponent);
}
float fonction_a_integrer(float theta, float x_tilde, float y_tilde, float froude_nbr)
{
    // Function under the integral (fonction_a_integrer)

    float sin_num = 2.0f * M_PI * (std::cos(theta) * x_tilde - std::sin(theta) * y_tilde);
    float sin_den = std::pow(std::cos(theta), 2);
    float numerator = std::sin(sin_num / sin_den);
    float denominator = std::pow(std::cos(theta), 4);

    return pressure_field(theta, froude_nbr) * numerator / denominator;
}
float integrate(std::function<float(float)> f, float a, float b, int n = 1000)
{
    // Simple numerical integration using the trapezoidal method

    float h = (b - a) / n;
    float sum = 0.5f * (f(a) + f(b));
    for (int i = 1; i < n; ++i)
    {
        float x = a + i * h;
        sum += f(x);
    }
    return sum * h;
}
float surface_displacement(float x_tilde, float y_tilde, float froude_nbr)
{
    // Rotation 90 degrees
    std::swap(x_tilde, y_tilde);

    auto integrand = [&](float theta) {
        return fonction_a_integrer(theta, x_tilde, y_tilde, froude_nbr);
        };

    float val = integrate(integrand, -M_PI_2, M_PI_2, 1000);
    return -val;
}
void wake_simulation(float froude_nbr, vector<float>& buffer)
{
    // Calculation of the entire simulation in a 2D buffer

    buffer.resize(IMAGE_HEIGHT * IMAGE_WIDTH);

    float subpixel_nbr = IMAGE_HEIGHT / MAX_Y_TILDE;
    int y_offset = 0;// static_cast<int>(IMAGE_HEIGHT * 0.05f); // ex: offset vertical (-44, -22)

    int x1 = 20, y1 = IMAGE_HEIGHT - 973;
    int x2 = 160, y2 = IMAGE_HEIGHT - 1007;
    y_offset = y1 + (y2 - y1) * (100.0f * froude_nbr - x1) / (x2 - x1);
    y_offset = -y_offset;
    //cout << int(100.0f * froude_nbr) << "  " << y_offset << endl;

    for (int y_img = 0; y_img < IMAGE_HEIGHT; ++y_img)
    {
        for (int x_img = 0; x_img < IMAGE_WIDTH; ++x_img)
        {
            float x_tilde = x_img / subpixel_nbr;
            float y_tilde = (y_img - y_offset) / subpixel_nbr;
            buffer[y_img * IMAGE_WIDTH + x_img] = surface_displacement(x_tilde, y_tilde, froude_nbr);
        }
    }
}
void normalizeBuffer(vector<float>& buffer)
{
    if (buffer.empty()) return;

    // Find current min, max, and mean
    float min_val = FLT_MAX;
    float max_val = FLT_MIN;
    double sum = 0.0;

    for (float v : buffer)
    {
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
        sum += v;
    }

    float range = max_val - min_val;
    if (range == 0)
    {
        // All values are identical, set to 0.5
        for (auto& v : buffer) v = 0.5f;
        return;
    }

    double mean = sum / buffer.size();

    // Step 1: normalize between 0 and 1
    for (auto& v : buffer)
        v = (v - min_val) / range;

    // Step 2: adjust so that the mean is 0.5
    
    // Calculate the mean after normalization
    double new_sum = 0.0;
    for (float v : buffer)
    {
        new_sum += v;
    }

    double new_mean = new_sum / buffer.size();

    // Offset necessary to make the mean 0.5
    float offset = 0.5f - static_cast<float>(new_mean);

    for (auto& v : buffer)
    {
        v += offset;
        // Clamp between 0 and 1
        if (v < 0.0f) v = 0.0f;
        else if (v > 1.0f) v = 1.0f;
    }
}
void Ship::CreateKelvinImages()
{
    for (int fr = 0; fr < 61; fr++)
    {
        vector<float> buffer;
        float froude = 0.01f * fr;
        wake_simulation(froude, buffer);
        normalizeBuffer(buffer);

        // Preparation of a complete buffer with both sides
        vector<float> buffer_two_sides(IMAGE_HEIGHT * IMAGE_WIDTH_2SIDES);
        for (int y = 0; y < IMAGE_HEIGHT; ++y)
        {
            for (int x = 0; x < IMAGE_WIDTH_2SIDES; ++x)
            {
                if (x < IMAGE_WIDTH)
                {
                    // Mirror part on the left: horizontal mirror of the left edge of the original image
                    int mirror_x = IMAGE_WIDTH - 1 - x;
                    buffer_two_sides[y * IMAGE_WIDTH_2SIDES + x] = buffer[y * IMAGE_WIDTH + mirror_x];
                }
                else
                {
                    // Original part on the right, placed starting from IMAGE_WIDTH
                    int original_x = x - IMAGE_WIDTH;
                    buffer_two_sides[y * IMAGE_WIDTH_2SIDES + x] = buffer[y * IMAGE_WIDTH + original_x];
                }
            }
        }

        string name;
        if (fr > 99)
            name = "Outputs/Kelvin-512_Fr-" + to_string(fr) + ".png";
        else if (fr < 10)
            name = "Outputs/Kelvin-512_Fr-00" + to_string(fr) + ".png";
        else
            name = "Outputs/Kelvin-512_Fr-0" + to_string(fr) + ".png";

        VulkanTexture tex;
        tex.CreateFromData(mVulkanDevice, IMAGE_WIDTH_2SIDES, IMAGE_HEIGHT, 1, VK_FORMAT_R32_SFLOAT, true, buffer_two_sides.data());
		tex.Save(name);
    }
}

// Update
void Ship::Update(float time)
{
    UpdateSounds();

    if (g_bPause)
        return;

    if (!bVisible)
        return;

    // CPU
    Chronos[1].NameAndStart("Ship motion"); 

    static float prevTime = 0.0f;
    float dt = mSmoothDt.update(time - prevTime);
    prevTime = time;
    mDt = dt;

    // Preparation
    UpdateWorldMatrix();
    TransformVertices();
    // Computation
    GetHeightOfAllVertices();
    GetTrisUnderWater();
    // Forces
    ComputeArchimede();
    ComputeGravity();
    ComputeHeaveDrag();
    ComputeMainThrust(dt);
    ComputePropellersDrag();
    ComputeViscousDrag();
    ComputeWavesDrag();
    ComputeBowThrust(dt);
    ComputeSternThrust(dt);
    ComputeRudder(dt);
    ComputeWind();
    ComputeCentrifugal();
    // Result
    ComputeForces(dt);
    ComputeTurningCircle(dt);
    ComputeAutopilot(dt);

    Chronos[1].Stop();

    // GPU
    if (bPressure) 
        UpdatePressureMesh();
    UpdateWakeMesh();
}
void Ship::UpdateWorldMatrix()
{
    mWorld = mat4(1.0f);
    mWorld = glm::translate(mWorld, ship.Position);
    mWorld = glm::rotate(mWorld, Yaw, vec3(0.0f, 1.0f, 0.0f));      // Axis Y
    mWorld = glm::rotate(mWorld, Roll, vec3(1.0f, 0.0f, 0.0f));     // Axis X
    mWorld = glm::rotate(mWorld, Pitch, vec3(0.0f, 0.0f, 1.0f));    // Axis Z

    mCosYaw = cos(Yaw);
	mSinYaw = sin(Yaw);
}
void Ship::TransformVertices()
{
    for (size_t i = 0; i < mvVerticesInitial.size(); i++)
        mvVertices[i] = vec3(mWorld * vec4(mvVerticesInitial[i], 1.0f));
}
vec3 Ship::GetVerticeAtMeshIndex(int x, int z)
{
    // Patch offset (how many times we have exceeded the grid)
    int i = 0, j = 0;

    // Euclidean division to bring into [0, MESH_SIZE[
    if (x < 0 || x >= mOcean->MESH_SIZE)
    {
        // floor division
        i = (int)floor((float)x / mOcean->MESH_SIZE);
        x = x - i * mOcean->MESH_SIZE;
    }
    if (z < 0 || z >= mOcean->MESH_SIZE)
    {
        j = (int)floor((float)z / mOcean->MESH_SIZE);
        z = z - j * mOcean->MESH_SIZE;
    }

    // Correspondence MESH → FFT
    int xFft = (x - mOcean->MESH_SIZE / 2) * mOcean->FFT_SIZE / mOcean->MESH_SIZE + mOcean->FFT_SIZE / 2;
    int yFft = (z - mOcean->MESH_SIZE / 2) * mOcean->FFT_SIZE / mOcean->MESH_SIZE + mOcean->FFT_SIZE / 2;

    // Safety clamp
    xFft = glm::clamp(xFft, 0, mOcean->FFT_SIZE - 1);
    yFft = glm::clamp(yFft, 0, mOcean->FFT_SIZE - 1);

    int index = 4 * (yFft * mOcean->FFT_SIZE + xFft);

    vec3 pos;
    pos.x = mvWaterPos[z][x].x + i * mOcean->PATCH_SIZE + pDisplacement[index + 0];
    pos.y = pDisplacement[index + 1];
    pos.z = mvWaterPos[z][x].z + j * mOcean->PATCH_SIZE + pDisplacement[index + 2];

    return pos;
}
bool InterpolateTriangle(const vec3& p1, const vec3& p2, const vec3& p3, vec3& pos)
{
    // Calculation of barycentric coordinates
    float det = ((p2.z - p3.z) * (p1.x - p3.x) + (p3.x - p2.x) * (p1.z - p3.z));
    float w1 = ((p2.z - p3.z) * (pos.x - p3.x) + (p3.x - p2.x) * (pos.z - p3.z)) / det;
    float w2 = ((p3.z - p1.z) * (pos.x - p3.x) + (p1.x - p3.x) * (pos.z - p3.z)) / det;
    float w3 = 1.0f - w1 - w2;

    // Check if the point is inside the triangle
    if (w1 >= 0 && w2 >= 0 && w3 >= 0 && w1 <= 1 && w2 <= 1 && w3 <= 1)
    {
        // Interpolation of the y value
        pos.y = w1 * p1.y + w2 * p2.y + w3 * p3.y;
        return true;
    }
    else
    {
        // The point is outside the triangle
        return false;
    }
}
int Ship::GetHeightFast(vec3& pos)
{
    // Update pos vector and return the number of computations (higher the lambda is, higher the computations are)

    int x = pos.x * mOcean->MESH_SIZE / mOcean->PATCH_SIZE + mOcean->MESH_SIZE / 2;
    int z = pos.z * mOcean->MESH_SIZE / mOcean->PATCH_SIZE + mOcean->MESH_SIZE / 2;

    vec3 posR = pos;

    if (x < 0 || x >= mOcean->MESH_SIZE)
    {
        int rep = (int)floor((float)x / mOcean->MESH_SIZE);
        posR.x -= rep * mOcean->PATCH_SIZE;
        x -= rep * mOcean->MESH_SIZE;
    }
    if (z < 0 || z >= mOcean->MESH_SIZE)
    {
        int rep = (int)floor((float)z / mOcean->MESH_SIZE);
        posR.z -= rep * mOcean->PATCH_SIZE;
        z -= rep * mOcean->MESH_SIZE;
    }

    int n = 0;
    float xOrig, xReal;
    float zOrig, zReal;
    vec3 p1, p2, p3, p4;

    // First level of searching, step by step, x then z

    // X to the left
    int xGrid;
    for (xGrid = x; xGrid >= 0; xGrid--)
    {
        n++;
        xReal = GetVerticeAtMeshIndex(xGrid, z).x;
        if (xReal < posR.x)
            break;
    }
    int indexX1 = xGrid;
    if (x == indexX1)
    {
        // X to the right
        for (xGrid = x; xGrid < mOcean->MESH_SIZE_1; xGrid++)
        {
            n++;
            xReal = GetVerticeAtMeshIndex(xGrid, z).x;
            if (xReal > posR.x)
                break;
        }
        indexX1 = xGrid - 1;
    }


    // Z to the bottom
    int zGrid;
    for (zGrid = z; zGrid >= 0; zGrid--)
    {
        n++;
        zReal = GetVerticeAtMeshIndex(x, zGrid).z;
        if (zReal < posR.z)
            break;
    }
    int indexZ1 = zGrid;
    if (z == indexZ1)
    {
        // Z to the top
        for (zGrid = z; zGrid < mOcean->MESH_SIZE_1; zGrid++)
        {
            n++;
            zReal = GetVerticeAtMeshIndex(x, zGrid).z;
            if (zReal > posR.z)
                break;
        }
        indexZ1 = zGrid - 1;
    }
    if (indexZ1 == -1)  indexZ1 = 0;

    // Try with these indexes
    p1 = GetVerticeAtMeshIndex(indexX1, indexZ1);          // Bottom Left
    p2 = GetVerticeAtMeshIndex(indexX1 + 1, indexZ1);      // Bottom Right
    p3 = GetVerticeAtMeshIndex(indexX1 + 1, indexZ1 + 1);  // Top Right

    if (InterpolateTriangle(p1, p2, p3, posR))
    {
        pos.y = posR.y;
        return n;
    }

    p4 = GetVerticeAtMeshIndex(indexX1, indexZ1 + 1);      // Top Left
    if (InterpolateTriangle(p1, p3, p4, posR))
    {
        pos.y = posR.y;
        return n;
    }

    // Second level of searching. From the indexes, start around the last indexes on a 5 x 5 basis
    for (int i = -2; i <= 2; i++)
    {
        for (int j = -2; j <= 2; j++)
        {
            n++;
            p1 = GetVerticeAtMeshIndex(indexX1 + i, indexZ1 + j);          // Bottom Left
            p2 = GetVerticeAtMeshIndex(indexX1 + 1 + i, indexZ1 + j);      // Bottom Right
            p3 = GetVerticeAtMeshIndex(indexX1 + 1 + i, indexZ1 + 1 + j);  // Top Right

            if (InterpolateTriangle(p1, p2, p3, posR))
            {
                pos.y = posR.y;
                return n;
            }

            p4 = GetVerticeAtMeshIndex(indexX1 + i, indexZ1 + 1 + j);      // Top Left
            if (InterpolateTriangle(p1, p3, p4, posR))
            {
                pos.y = posR.y;
                return n;
            }
        }
    }

    // Second level of searching, extend the grid in another method
    n += GetHeightSlow(pos);

    return n;
}
int Ship::GetHeightSlow(vec3& pos)
{
    int x = pos.x * mOcean->MESH_SIZE / mOcean->PATCH_SIZE + mOcean->MESH_SIZE / 2;
    int z = pos.z * mOcean->MESH_SIZE / mOcean->PATCH_SIZE + mOcean->MESH_SIZE / 2;

    int n = 0;

    // Find the index in pDisplacement

    vec3 p1, p2, p3, p4;
    int xGrid, zGrid;
    for (xGrid = x - 20; xGrid <= x + 20; xGrid++)
    {
        for (zGrid = z - 20; zGrid <= z + 20; zGrid++)
        {
            n++;
            p1 = GetVerticeAtMeshIndex(xGrid, zGrid);          // Bottom Left
            p2 = GetVerticeAtMeshIndex(xGrid + 1, zGrid);      // Bottom Right
            p3 = GetVerticeAtMeshIndex(xGrid + 1, zGrid + 1);  // Top Right

            if (InterpolateTriangle(p1, p2, p3, pos))
                return n;

            p4 = GetVerticeAtMeshIndex(xGrid, zGrid + 1);      // Top Left
            if (InterpolateTriangle(p1, p3, p4, pos))
                return n;
        }
    }

    // No triangle found
    pos.y = 0.0f;
    return 0;
}
void Ship::GetHeightOfAllVertices()
{
    int nSearch = 0;
    vec3 pWater;

    for (unsigned int i = 0; i < mvVertices.size(); i++)
    {
        pWater = mvVertices[i];
        nSearch += GetHeightFast(pWater);
        mvVertSubmerged[i] = (mvVertices[i].y < pWater.y) ? 1 : 0;  // 0 = under water, 1 = above
        mvVertWaterHeight[i] = mvVertices[i].y - pWater.y;
    }
    if (mvVertices.size())
        WaterSearch = int(nSearch / mvVertices.size());
}
void Ship::GetTrisUnderWater()
{
    int vertex = 0;
    for (auto& tri : mvTris)
    {
        tri.WaterStatus = 0;
        for (unsigned int i = 0; i < 3; i++)
        {
            tri.bUnder[i] = mvVertSubmerged[tri.I[i]];
            tri.WaterStatus += tri.bUnder[i];
        }
    }

    if (bHullMesh)
    {
        // Color of the triangles
        for (auto& tri : mvTris)
        {
            switch (tri.WaterStatus)
            {
            case 0: tri.Color = vec3(0.5f, 0.5f, 0.5f); break;   // Above water 0/3
            case 1: tri.Color = vec3(0.6f, 0.6f, 1.0f); break;   // Under water 1/3
            case 2: tri.Color = vec3(0.3f, 0.3f, 1.0f); break;   // Under water 2/3
            case 3: tri.Color = vec3(0.0f, 0.0f, 1.0f); break;   // Under water 3/3
            }
        }
        // Update the vertex array object
        int index = 0;
        for (const auto& tri : mvTris)
        {
            for (int j = 0; j < 3; ++j)
            {
                index += 3;
                // Color
                mvVertexColored[index++] = tri.Color.r;   // r
                mvVertexColored[index++] = tri.Color.g;   // g
                mvVertexColored[index++] = tri.Color.b;   // b
            }
        }

        mHullMesh->UpdateVertexBuffer(mvVertexColored);
    }
}
void Ship::ComputeArchimede()
{
    AreaWetted = 0.0f;
    float tmpSumPressure = 0.0f;

    mArchimede.Magnitude = 0.0f;
    mArchimede.Vector = vec3(0.0f);
    mArchimede.Position = vec3(0.0f);

    vec3 Min = vec3(FLT_MAX);
    vec3 Max = vec3(-FLT_MAX);

    // Longitudinal axis of the ship in world coordinates (after Yaw rotation)
    vec3 shipAxisFull = glm::normalize(vec3(mWorld[0]));  // column 0 = transformed X axis
    vec2 shipAxis = glm::normalize(vec2(shipAxisFull.x, shipAxisFull.z));

    float projMin = FLT_MAX;
    float projMax = -FLT_MAX;

    const mat3 normalMatrix = mat3(mWorld);

    for (auto& tri : mvTris)
    {
        if (tri.WaterStatus == 0)
            continue;

        // Normal and CoG in world space
        tri.Normal = glm::normalize(normalMatrix * tri.NormalInitial);
        tri.CoG = (mvVertices[tri.I[0]] + mvVertices[tri.I[1]] + mvVertices[tri.I[2]]) / 3.0f;

        // LWL footprint
        if (tri.CoG.x < Min.x) Min.x = tri.CoG.x;
        if (tri.CoG.z < Min.z) Min.z = tri.CoG.z;
        if (tri.CoG.x > Max.x) Max.x = tri.CoG.x;
        if (tri.CoG.z > Max.z) Max.z = tri.CoG.z;

        // Average depth of submerged vertices (replaces the switch)
        float depthSum = 0.0f;
        int   count = 0;
        for (int i = 0; i < 3; i++)
        {
            if (tri.bUnder[i])
            {
                depthSum -= mvVertWaterHeight[tri.I[i]];
                count++;
            }
        }
        tri.Depth = depthSum / float(count);  // count >= 1 guaranteed by WaterStatus != 0

        // Hydrostatic pressure
        float intensity = float(tri.WaterStatus) / 3.0f;
        tri.fPressure = intensity * mWATER_DENSITY * mGRAVITY * tri.Depth * tri.Area;
        tri.vPressure = tri.Normal * tri.fPressure;

        mArchimede.Vector += tri.vPressure;
        mArchimede.Position += tri.CoG * tri.fPressure;
        tmpSumPressure += tri.fPressure;

        AreaWetted += intensity * tri.Area;

        // Projection of CoG onto the longitudinal axis
        float proj = tri.CoG.x * shipAxis.x + tri.CoG.z * shipAxis.y;
        if (proj < projMin) projMin = proj;
        if (proj > projMax) projMax = proj;
    }

    mArchimede.Magnitude = std::max(mArchimede.Vector.y, 0.0f);
    mArchimede.Vector = { 0.0f, mArchimede.Magnitude, 0.0f };
    mArchimede.Position = (tmpSumPressure > 0.0f) ? mArchimede.Position / tmpSumPressure : vec3(0.0f);

    LWL = projMax - projMin;
}
void Ship::ComputeGravity()
{
    mGravity.Magnitude = mMass * mGRAVITY;
    mGravity.Vector = vec3(0.0f, -mGravity.Magnitude, 0.0f);  // Always vertical
    mGravity.Position = TransformPosition(ship.PosGravity);
}
void Ship::ComputeHeaveDrag()
{
    if (fabs(HeaveVelocity) < 1e-4f)
    {
        mHeaveDrag.Magnitude = 0.0f;
        mHeaveDrag.Vector = vec3(0.0f);
        mHeaveDrag.Position = mArchimede.Position;
        return;
    }

    // Damping ratio ζ by ship class : ζ < 1.0 → underdamped (bounces), ζ = 1.0 → critically damped (no bounce), ζ > 1.0 → overdamped (slow return)
    float zeta = 0.40f;
    switch (ship.Class)
    {
    case eClass::FastBoat:    zeta = 0.20f; break;  // bounces on waves
    case eClass::Corvette:    zeta = 0.25f; break;
    case eClass::Frigate:     zeta = 0.30f; break;
    case eClass::Fishing:     zeta = 0.40f; break;
    case eClass::Submarine:   zeta = 0.60f; break;  // heavily damped
    case eClass::Ferry:       zeta = 0.35f; break;
    case eClass::Tugboat:     zeta = 0.45f; break;
    case eClass::Cargo:       zeta = 0.40f; break;
    case eClass::Supertanker: zeta = 0.50f; break;  // very heavy, little bounce
    }

    // Hydrostatic stiffness (N/m) : K = ρ × g × A_waterplane
    float K = mWATER_DENSITY * mGRAVITY * mAreaXZ;

    // Critical damping coefficient (N·s/m) : C_crit = 2 × sqrt(K × M)
    float C_critique = 2.0f * sqrt(K * mMass);

    // Damping force (N) — linear in velocity, signed : F = ζ × C_crit × v
    mHeaveDrag.Magnitude = zeta * C_critique * HeaveVelocity;

    // Vector opposing HeaveVelocity
    float absMag = fabs(mHeaveDrag.Magnitude);
    if (mArchimede.Magnitude > mGravity.Magnitude)
        mHeaveDrag.Vector = TransformVector(vec3(0.0f, -absMag, 0.0f));
    else
        mHeaveDrag.Vector = TransformVector(vec3(0.0f, absMag, 0.0f));

    mHeaveDrag.Position = mArchimede.Position;
}
void Ship::ComputeMainThrust(float dt)
{
    float v = fabs(SurgeVelocity);

    // Threshold speed: below this, interpolate towards static thrust
    const float vThreshold = glm::clamp(0.5f * sqrt(mLength / 50.0f), 0.3f, 3.0f);

    // Propeller area (m²)
    float S = mAreaPropeller;

    // ── Engine 1 ──────────────────────────────────────────────────────────

    float limitRpm1 = ((float)PowerCurrentStep1 / (float)ship.PowerStepMax) * ship.PropRpmMax;

    if (PropRpm1 < limitRpm1 - ship.PropRpmIncrement * dt)      PropRpm1 += ship.PropRpmIncrement * dt;
    else if (PropRpm1 > limitRpm1 + ship.PropRpmIncrement * dt) PropRpm1 -= ship.PropRpmIncrement * dt;

    PowerApplied1 = mPowerW * PropRpm1 / ship.PropRpmMax;

    if (PropRpm1 > -ship.PropRpmIncrement * dt && PropRpm1 < ship.PropRpmIncrement * dt)
    {
        PropRpm1 = 0.0f;
        PowerApplied1 = 0.0f;
    }

    // Thrust: interpolate between static and dynamic below vThreshold
    if (fabs(PowerApplied1) > 0.0f)
    {
        float absP1 = fabs(PowerApplied1);

        // Static thrust (actuator disk theory): F = (2ρ·S·P²)^(1/3)
        float F_static_max = mMass * 0.1f;
        float F_static1 = cbrtf(2.0f * mWATER_DENSITY * S * absP1 * absP1);
        F_static1 = std::min(F_static1, F_static_max);

        // Dynamic thrust at vThreshold
        float F_dynamic1 = absP1 / vThreshold;

        if (v >= vThreshold)
            mThrust1.Magnitude = absP1 / v;
        else
        {
            // Linear blend between static (v=0) and dynamic (v=vThreshold)
            float blend = v / vThreshold;
            mThrust1.Magnitude = (1.0f - blend) * F_static1 + blend * F_dynamic1;
        }

        mThrust1.Magnitude *= Sign(PropRpm1);  // sign according to rotation direction
    }
    else
    {
        mThrust1.Magnitude = 0.0f;
    }

    mThrust1.Vector = TransformVector(vec3(mThrust1.Magnitude, 0.0f, 0.0f));
    mThrust1.Position = TransformPosition(ship.PosPropeller1);

    // ── Engine 2 ──────────────────────────────────────────────────────────

    if (ship.nPropeller < 2)
    {
        mThrust2.Magnitude = 0.0f;
        mPropDrag2.Magnitude = 0.0f;
        return;
    }

    float limitRpm2 = ((float)PowerCurrentStep2 / (float)ship.PowerStepMax) * ship.PropRpmMax;

    if (PropRpm2 < limitRpm2 - ship.PropRpmIncrement * dt)      PropRpm2 += ship.PropRpmIncrement * dt;
    else if (PropRpm2 > limitRpm2 + ship.PropRpmIncrement * dt) PropRpm2 -= ship.PropRpmIncrement * dt;

    PowerApplied2 = mPowerW * PropRpm2 / ship.PropRpmMax;

    if (PropRpm2 > -ship.PropRpmIncrement * dt && PropRpm2 < ship.PropRpmIncrement * dt)
    {
        PropRpm2 = 0.0f;
        PowerApplied2 = 0.0f;
    }

    if (fabs(PowerApplied2) > 0.0f)
    {
        float absP2 = fabs(PowerApplied2);
        float F_static2_max = mMass * 0.1f;
        float F_static2 = cbrtf(2.0f * mWATER_DENSITY * S * absP2 * absP2);
        F_static2 = std::min(F_static2, F_static2_max);
        float F_dynamic2 = absP2 / vThreshold;

        if (v >= vThreshold)
            mThrust2.Magnitude = absP2 / v;
        else
        {
            float blend = v / vThreshold;
            mThrust2.Magnitude = (1.0f - blend) * F_static2 + blend * F_dynamic2;
        }

        mThrust2.Magnitude *= Sign(PropRpm2);
    }
    else
    {
        mThrust2.Magnitude = 0.0f;
    }

    mThrust2.Vector = TransformVector(vec3(mThrust2.Magnitude, 0.0f, 0.0f));
    mThrust2.Position = TransformPosition(ship.PosPropeller2);
}
void Ship::ComputePropellersDrag()
{
    float Cx = 1.0f; // disc drag coefficient for blocked or dragging propeller
    float v = fabs(SurgeVelocity);

    // Propeller 1 ======================

    float drag1 = 0.0f;

    // Case 1: propeller in opposition
    if ((SurgeVelocity > 0.0f && PropRpm1 < 0.0f) || (SurgeVelocity < 0.0f && PropRpm1 > 0.0f))
    {
        float max_drag = 0.5f * mWATER_DENSITY * Cx * mAreaPropeller * SurgeVelocity * fabs(SurgeVelocity);
        float thrustN1 = (v > 0.1f) ? fabs(PowerApplied1) / v : fabs(PowerApplied1) / 0.1f;
        if (thrustN1 < fabs(max_drag))
            drag1 = -copysign(max_drag - thrustN1, SurgeVelocity);
    }
    // Case 2 : Insufficient thrust to maintain speed → the propeller brakes
    else
    {
        float F_available = (v > 0.1f) ? fabs(PowerApplied1) / v : fabs(PowerApplied1) / 0.1f;      
        float F_resistance = (fabs(mViscousDrag.Magnitude) + fabs(mWavesDrag.Magnitude)) / float(ship.nPropeller);

        // Margin: do not brake if already at equilibrium (avoids oscillating at cruising speed)
        if (F_available < F_resistance)
        {
            float coverageRatio = (F_resistance > 0.0f) ? glm::clamp(F_available / F_resistance, 0.0f, 1.0f) : 1.0f;
            float F_hull = 0.5f * mWATER_DENSITY * Cx * mAreaPropeller * SurgeVelocity * fabs(SurgeVelocity);
            // dragFactor: 0 when thrust = resistance, 0.30 when propeller is cut off
            float dragFactor = (1.0f - coverageRatio) * 0.30f;
            drag1 = -Sign(SurgeVelocity) * fabs(F_hull) * dragFactor;
        }
    }

    mPropDrag1.Magnitude = drag1;
    mPropDrag1.Vector = TransformVector(vec3(drag1, 0.0f, 0.0f));
    mPropDrag1.Position = TransformPosition(ship.PosPropeller1);

    // Propeller 2 ======================

    if (ship.nPropeller == 1)
    {
        mPropDrag2.Magnitude = 0.0f;
        mPropDrag2.Vector = vec3(0.0f);
        mPropDrag2.Position = TransformPosition(ship.PosPropeller2);
        return;
    }
    
    float drag2 = 0.0f;

    // Case 1: propeller in opposition
    if ((SurgeVelocity > 0.0f && PropRpm2 < 0.0f) || (SurgeVelocity < 0.0f && PropRpm2 > 0.0f))
    {
        float max_drag = 0.5f * mWATER_DENSITY * Cx * mAreaPropeller * SurgeVelocity * fabs(SurgeVelocity);
        float thrustN2 = (v > 0.1f) ? fabs(PowerApplied2) / v : fabs(PowerApplied2) / 0.1f;
        if (thrustN2 < fabs(max_drag))
            drag2 = -copysign(max_drag - thrustN2, SurgeVelocity);
    }
    // Case 2 : Insufficient thrust to maintain speed → the propeller brakes
    else
    {
        float F_available = (v > 0.1f) ? fabs(PowerApplied2) / v : fabs(PowerApplied2) / 0.1f;
        float F_resistance = (fabs(mViscousDrag.Magnitude) + fabs(mWavesDrag.Magnitude)) / float(ship.nPropeller);
        
        // Margin: do not brake if already at equilibrium (avoids oscillating at cruising speed)
        if (F_available < F_resistance)
        {
            float coverageRatio = (F_resistance > 0.0f) ? glm::clamp(F_available / F_resistance, 0.0f, 1.0f) : 1.0f;
            float F_hull = 0.5f * mWATER_DENSITY * Cx * mAreaPropeller * SurgeVelocity * fabs(SurgeVelocity);
            // dragFactor: 0 when thrust = resistance, 0.30 when propeller is cut off
            float dragFactor = (1.0f - coverageRatio) * 0.30f;
            drag2 = -Sign(SurgeVelocity) * fabs(F_hull) * dragFactor;
        }
    }
    mPropDrag2.Magnitude = drag2;
    mPropDrag2.Vector = TransformVector(vec3(drag2, 0.0f, 0.0f));
    mPropDrag2.Position = TransformPosition(ship.PosPropeller2);
}
void Ship::ComputeViscousDrag()
{
    if (fabs(SurgeVelocity) < 0.01f)
    {
        mViscousDrag.Magnitude = 0.0f;
        mViscousDrag.Vector = vec3(0.0f);
        mViscousDrag.Position = mArchimede.Position;
        return;
    }

    // Reynolds number (ITTC-1957)
    float Re = (fabs(SurgeVelocity) * LWL) / mKINEMATIC_VISCOSITY;

    // Friction coefficient (ITTC-1957)
    float Cf = 0.075f / pow(log10(Re) - 2.0f, 2);

    // Form factor by ship class
    float k_form = 0.30f;
    switch (ship.Class)
    {
    case eClass::FastBoat:    k_form = 0.10f; break;
    case eClass::Corvette:    k_form = 0.12f; break;
    case eClass::Frigate:     k_form = 0.13f; break;
    case eClass::Fishing:     k_form = 0.30f; break;
    case eClass::Submarine:   k_form = 0.08f; break;
    case eClass::Ferry:       k_form = 0.18f; break;
    case eClass::Tugboat:     k_form = 0.35f; break;
    case eClass::Cargo:       k_form = 0.25f; break;
    case eClass::Supertanker: k_form = 0.15f; break;
    }
    // Penalty for reversing: non-hydrodynamic hull
    if (SurgeVelocity < 0.0f)
        k_form *= 2.5f;

    float Cv = Cf * (1.0f + k_form);

    float S_appendages = mRudderArea * ship.nPropeller;

    // Viscous resistance (in Newtons)
    float resVisc = 0.5f * mWATER_DENSITY * (AreaWetted + S_appendages) * pow(fabs(SurgeVelocity), 2) * Cv;

    if (isnan(resVisc) || isinf(resVisc))
        resVisc = 0.0f;

    mViscousDrag.Magnitude = -Sign(SurgeVelocity) * resVisc;
    mViscousDrag.Vector = TransformVector(vec3(mViscousDrag.Magnitude, 0.0f, 0.0f));
    mViscousDrag.Position = mArchimede.Position;
}
void Ship::ComputeWavesDrag()
{
    if (fabs(SurgeVelocity) < 0.01f)
    {
        mWavesDrag.Magnitude = 0.0f;
        mWavesDrag.Vector = vec3(0.0f);
        mWavesDrag.Position = mArchimede.Position;
        return;
    }

    // Froude number
    float Fn = fabs(SurgeVelocity) / sqrt(mGRAVITY * LWL);

    // Wave resistance parameters by ship class
    float Fr0 = 0.27f;
    float Sigma = 0.07f;
    float Cw0 = 0.010f;
    switch (ship.Class)
    {
    case eClass::FastBoat:    Fr0 = 0.55f; Sigma = 0.10f; Cw0 = 0.004f; break;
    case eClass::Corvette:    Fr0 = 0.45f; Sigma = 0.09f; Cw0 = 0.006f; break;
    case eClass::Frigate:     Fr0 = 0.40f; Sigma = 0.08f; Cw0 = 0.007f; break;
    case eClass::Fishing:     Fr0 = 0.41f; Sigma = 0.08f; Cw0 = 0.010f; break;
    case eClass::Submarine:   Fr0 = 0.28f; Sigma = 0.06f; Cw0 = 0.003f; break;
    case eClass::Ferry:       Fr0 = 0.35f; Sigma = 0.08f; Cw0 = 0.008f; break;
    case eClass::Tugboat:     Fr0 = 0.25f; Sigma = 0.06f; Cw0 = 0.012f; break;
    case eClass::Cargo:       Fr0 = 0.27f; Sigma = 0.07f; Cw0 = 0.010f; break;
    case eClass::Supertanker: Fr0 = 0.25f; Sigma = 0.07f; Cw0 = 0.0003f; break;
    }

    // Wave resistance coefficient (Gaussian peak)
    float exponent = -pow(Fn - Fr0, 2) / (2.0f * pow(Sigma, 2));
    float Cw = Cw0 * exp(exponent);

    // Wave resistance (in Newtons)
    float resWav = 0.5f * mWATER_DENSITY * AreaWetted * pow(fabs(SurgeVelocity), 2) * Cw;

    if (isnan(resWav) || isinf(resWav))
        resWav = 0.0f;

    if (SurgeVelocity < 0.0f)
        resWav *= 10.0f;  // Empirical factor for astern resistance

    mWavesDrag.Magnitude = -Sign(SurgeVelocity) * resWav;
    mWavesDrag.Vector = TransformVector(vec3(mWavesDrag.Magnitude, 0.0f, 0.0f));
    mWavesDrag.Position = mArchimede.Position;
}
void Ship::ComputeBowThrust(float dt)
{
    if (!ship.HasBowThruster)
    {
        mBowThrust.Magnitude = 0.0f;
        return;
    }

    // Force as a result of the bow thruster

    float limitRpm = 0.0f;
    if (BowThrusterCurrentStep >= 0)
        limitRpm = ship.BowThrusterRpmMin + ((float)BowThrusterCurrentStep / (float)ship.BowThrusterStepMax) * ((float)ship.BowThrusterRpmMax - (float)ship.BowThrusterRpmMin);
    else
        limitRpm = -ship.BowThrusterRpmMin + ((float)BowThrusterCurrentStep / (float)ship.BowThrusterStepMax) * ((float)ship.BowThrusterRpmMax - (float)ship.BowThrusterRpmMin);

    if (BowThrusterRpm < limitRpm - ship.BowThrusterRpmIncrement * dt)      BowThrusterRpm += ship.BowThrusterRpmIncrement * dt;
    else if (BowThrusterRpm > limitRpm + ship.BowThrusterRpmIncrement * dt) BowThrusterRpm -= ship.BowThrusterRpmIncrement * dt;

    if (BowThrusterRpm >= 0)    BowThrusterApplied = ship.BowThrusterPowerW * (BowThrusterRpm - ship.BowThrusterRpmMin) / (ship.BowThrusterRpmMax - ship.BowThrusterRpmMin);
    else                        BowThrusterApplied = ship.BowThrusterPowerW * (BowThrusterRpm + ship.BowThrusterRpmMin) / (ship.BowThrusterRpmMax - ship.BowThrusterRpmMin);

    // Stop to 0 if close to zero and target is zero
    if (BowThrusterRpm > -ship.BowThrusterRpmIncrement * dt && BowThrusterRpm < ship.BowThrusterRpmIncrement * dt)
        BowThrusterRpm = 0.0f;

    mBowThrust.Magnitude = BowThrusterApplied * ship.BowThrusterPerf;
    mBowThrust.Vector = TransformVector(vec3(0.0f, 0.0f, mBowThrust.Magnitude));
    mBowThrust.Position = TransformPosition(ship.PosBowThruster);

    mSoundBowThruster->setPitch(1.0f + 0.5f * fabs(BowThrusterApplied) / ship.BowThrusterPowerW);
    if (g_Camera.GetPosition().y < 0.0f)    mSoundBowThruster->setVolume(0.01f + 0.25f * fabs(BowThrusterApplied) / ship.BowThrusterPowerW);
    else                                    mSoundBowThruster->setVolume(0.25f + 0.25f * fabs(BowThrusterApplied) / ship.BowThrusterPowerW);
}
void Ship::ComputeSternThrust(float dt)
{
    if (!ship.HasSternThruster)
    {
        mSternThrust.Magnitude = 0.0f;
        return;
    }

    // Force as a result of the stern thruster

    float limitRpm = 0.0f;
    if (SternThrusterCurrentStep >= 0)
        limitRpm = ship.SternThrusterRpmMin + ((float)SternThrusterCurrentStep / (float)ship.SternThrusterStepMax) * ((float)ship.SternThrusterRpmMax - (float)ship.SternThrusterRpmMin);
    else
        limitRpm = -ship.SternThrusterRpmMin + ((float)SternThrusterCurrentStep / (float)ship.SternThrusterStepMax) * ((float)ship.SternThrusterRpmMax - (float)ship.SternThrusterRpmMin);

    if (SternThrusterRpm < limitRpm - ship.SternThrusterRpmIncrement * dt)      SternThrusterRpm += ship.SternThrusterRpmIncrement * dt;
    else if (SternThrusterRpm > limitRpm + ship.SternThrusterRpmIncrement * dt) SternThrusterRpm -= ship.SternThrusterRpmIncrement * dt;

    if (SternThrusterRpm >= 0)  SternThrusterApplied = ship.SternThrusterPowerW * (SternThrusterRpm - ship.SternThrusterRpmMin) / (ship.SternThrusterRpmMax - ship.SternThrusterRpmMin);
    else                        SternThrusterApplied = ship.SternThrusterPowerW * (SternThrusterRpm + ship.SternThrusterRpmMin) / (ship.SternThrusterRpmMax - ship.SternThrusterRpmMin);

    // Stop to 0 if close to zero and target is zero
    if (SternThrusterRpm > -ship.SternThrusterRpmIncrement * dt && SternThrusterRpm < ship.SternThrusterRpmIncrement * dt)
        SternThrusterRpm = 0.0f;

    mSternThrust.Magnitude = SternThrusterApplied * ship.SternThrusterPerf;
    mSternThrust.Vector = TransformVector(vec3(0.0f, 0.0f, mSternThrust.Magnitude));
    mSternThrust.Position = TransformPosition(ship.PosSternThruster);

    mSoundSternThruster->setPitch(1.0f + 0.5f * fabs(SternThrusterApplied) / ship.SternThrusterPowerW);
    if (g_Camera.GetPosition().y < 0.0f)    mSoundSternThruster->setVolume(0.01f + 0.25f * fabs(SternThrusterApplied) / ship.SternThrusterPowerW);
    else                                    mSoundSternThruster->setVolume(0.25f + 0.25f * fabs(SternThrusterApplied) / ship.SternThrusterPowerW);
}
void Ship::ComputeRudder(float dt)
{
    // Rudder movement
    float maxAngleDeg = RudderCurrentStep * ship.RudderIncrement;
    if (bAutopilot)
    {
        // Autopilot: direct command in degrees, without going through increments
        maxAngleDeg = std::clamp(RudderTargetDeg, -(float)ship.RudderStepMax, (float)ship.RudderStepMax);
    }
    else
    {
        // Helmsman: command in incremental steps
        maxAngleDeg = RudderCurrentStep * ship.RudderIncrement;
        if (fabs(maxAngleDeg) > ship.RudderStepMax)
            maxAngleDeg = ship.RudderStepMax * Sign(RudderCurrentStep);
    }

    float delta = ship.RudderRotSpeed * dt;

    if (RudderAngleDeg < maxAngleDeg - delta)       RudderAngleDeg += delta;
    else if (RudderAngleDeg > maxAngleDeg + delta)  RudderAngleDeg -= delta;
    else                                            RudderAngleDeg = maxAngleDeg;

    if (fabs(RudderAngleDeg) > ship.RudderStepMax)
        RudderAngleDeg = ship.RudderStepMax * Sign(RudderAngleDeg);

    if (maxAngleDeg == 0.0f && fabs(RudderAngleDeg) < delta)
        RudderAngleDeg = 0.0f;

    // Convert rudder angle to radians
    float angle = glm::radians(RudderAngleDeg);

    // Lift force: sign of v² preserves direction in reverse : F = ½ρ × v|v| × A × sin(α) × perf
    float force = 0.5f * mWATER_DENSITY * SurgeVelocity * fabs(SurgeVelocity) * mRudderArea * sin(angle);

    // Lateral component (yaw torque)
    mRudderLift.Magnitude = force;  // sign already included
    mRudderLift.Vector = TransformVector(vec3(0.0f, 0.0f, mRudderLift.Magnitude));
    mRudderLift.Position = TransformPosition(ship.PosRudder);

    // Axial drag — realistically 5-15% of total resistance at max angle
    float x = fabs(RudderAngleDeg) / ship.RudderStepMax;
    if (x > 1.0f) x = 1.0f;
    
    // Rudder drag ratio is the drag at max angle as a fraction of the lift force
    float dragFactor = 0.8f * pow(x, 2.0f);
    mRudderDrag.Magnitude = -Sign(SurgeVelocity) * fabs(force) * dragFactor;
    mRudderDrag.Vector = TransformVector(vec3(mRudderDrag.Magnitude, 0.0f, 0.0f));
    mRudderDrag.Position = TransformPosition(ship.PosRudder);
}
void Ship::ComputeWind()
{
    float windSpeed = glm::length(g_Wind);
    if (windSpeed < 0.01f)
    {
        mAirDrag.Magnitude = 0.0f;
        mAirDrag.Vector = vec3(0.0f);
        mWindTorque.Magnitude = 0.0f;
        mWindTorque.Vector = vec3(0.0f);
        mWindDrift.Magnitude = 0.0f;
        mWindDrift.Vector = vec3(0.0f);
        return;
    }

    // ── Angles ──────────────────────────────────────────────────────────────

    vec3 heading = TransformPosition(mBow) - ship.Position;
    vec2 headingXZ = glm::normalize(vec2(heading.x, heading.z));
    vec2 windXZ = glm::normalize(g_Wind);
    vec2 windAppXZ = g_Wind + vCOG;
    float AWS_speed = glm::length(windAppXZ);

    float angleWind = atan2(windXZ.y, windXZ.x);
    float angleHeading = atan2(headingXZ.y, headingXZ.x);
    float angle = atan2(sin(angleWind - angleHeading), cos(angleWind - angleHeading));  // [-π, π]

    AWS = AWS_speed;
    AWD = wind_to_dirdeg(windXZ);
    AWA = glm::degrees(angle);
    WindLeftRight = (angle < 0) ? 'L' : 'R';

    float dynPressure = 0.5f * mAIR_DENSITY * mPLATE_DRAG_COEFF;
    mCosPhi = cos(angle);
    mSinPhi = sin(angle);

    // ── Effect 1 : Axial resistance — true wind (TWS / TWA) ───────────────────
    
    // Angle is already calculated relative to the heading with the true wind (windXZ)

    float cosTWA = cos(angle);   // TWA angle
    float frontAxial = ship.AreaFront * std::max(cosTWA, 0.0f);
    float rearAxial = ship.AreaLat * std::max(-cosTWA, 0.0f);
    float axialArea = frontAxial + rearAxial;

    float airDragForce = dynPressure * windSpeed * windSpeed * axialArea;

    mAirDrag.Magnitude = -Sign(cosTWA) * airDragForce;
    mAirDrag.Vector = TransformVector(vec3(mAirDrag.Magnitude, 0.0f, 0.0f));
    mAirDrag.Position = TransformPosition(ship.AreaLatCenter);

    // ── Effect 2 : Yaw torque (weathercocking) ───────────────────────────
    
    // Use TWS: This is a force on the hull/superstructure. sin(2×angle): zero when facing/downwind, maximum at 45°

    float alpha = sin(2.0f * angle);
    float windForce = dynPressure * ship.AreaLat * windSpeed * windSpeed * alpha;
    float leverArm = fabs(ship.AreaLatCenter.x);

    mWindTorque.Magnitude = windForce * leverArm;
    mWindTorque.Vector = TransformVector(vec3(0.0f, mWindTorque.Magnitude, 0.0f));
    mWindTorque.Position = TransformPosition(ship.AreaLatCenter);

    // ── Effect 3 : Drift (TWS, box model) ────────────────────────────────
    
    float driftAreaFront = ship.AreaFront * fabs(mCosPhi);
    float driftAreaLat = ship.AreaLat * fabs(mSinPhi);
    float driftArea = driftAreaFront + driftAreaLat;

    mWindDrift.Magnitude = 0.5f * mAIR_DENSITY * mPLATE_DRAG_COEFF * windSpeed * windSpeed * driftArea;
    mWindDrift.Vector = mWindDrift.Magnitude * glm::normalize(-vec3(g_Wind.x, 0.0f, g_Wind.y));
    mWindDrift.Position = TransformPosition(ship.AreaLatCenter);
}
void Ship::ComputeCentrifugal()
{
    // Centrifugal force during a turn that causes the ship to roll outward
    
    // F = m × v × ω  (visually scaled by CentrifugalPerf)

    mCentrifugalTorque.Magnitude = mMass
        * fabs(SurgeVelocity)   // roll zero at v=0
        * fabs(YawVelocity)
        * ship.CentrifugalPerf;

    // Sign : turn right (YawVelocity > 0) → roll left (negative Z torque)
    mCentrifugalTorque.Magnitude *= -Sign(YawVelocity);

    mCentrifugalTorque.Vector = TransformVector( vec3(0.0f, 0.0f, mCentrifugalTorque.Magnitude));
    mCentrifugalTorque.Position = TransformPosition(ship.PosGravity);
}
float Ship::ComputePivotPosition()
{
    // Return a distance from midship:
    // positive = forward, negative = aft
    // At rest    : pivot at midship (0)
    // Fwd motion : pivot moves forward  (toward bow,  ~0.3L from midship)
    // Bwd motion : pivot moves aft      (toward stern, ~0.2L from midship)

    float maxSpeed = knot_to_ms(ship.SpeedMaxKt);  // plus static
    float vNorm = (maxSpeed > 0.01f) ? fabs(SurgeVelocity) / maxSpeed : 0.0f;

    float P0 = 0.5f * mLength;   // midship (reference)
    float alpha = 3.0f;             // 1-exp(-3) ≈ 0.95 at full speed
    float blend = 1.0f - exp(-alpha * vNorm);

    float pivotPos;

    if (SurgeVelocity >= 0.0f)
    {
        // Forward: pivot moves toward bow
        float PmaxFwd = ship.PivotFwd * mLength;   // e.g. 0.2 × L = 20m
        pivotPos = P0 + (PmaxFwd - P0) * blend;
        pivotPos = glm::clamp(pivotPos, PmaxFwd, P0);
    }
    else
    {
        // Astern: pivot moves toward stern
        float PmaxBwd = ship.PivotBwd * mLength;   // e.g. 0.7 × L = 70m
        pivotPos = P0 + (PmaxBwd - P0) * blend;
        pivotPos = glm::clamp(pivotPos, P0, PmaxBwd);
    }

    // Convert to signed distance from midship
    return 0.5f * mLength - pivotPos;
}

void Ship::ComputeForces(float dt)
{
    if (!bMotion)
        return;

    if (dt == 0.0f)
        return;

    if (fabs(YawVelocity) <= glm::radians(0.1f / 60.0f))
        mTurnEntrySpeed = fabs(SurgeVelocity);  // memorize the speed outside the turn

    auto normalizeAngle = [](float angle) -> float {
        angle = fmod(angle, 2.0f * M_PI);
        if (angle > M_PI) angle -= 2.0f * M_PI;
        if (angle < -M_PI) angle += 2.0f * M_PI;
        return angle;
        };

    // Previous position of the ship
    vec2 prevPosition = { ship.Position.x, ship.Position.z };

    // Previous position of the bow
    vec3 PrevPosBow = TransformPosition(mBow);

    // Previous position of the stern
    vec3 PrevPosStern = TransformPosition(mStern);

    // ── HEAVE ────────────────────────────────────────────────────────────────
    
    float HeaveForce = mArchimede.Magnitude - mGravity.Magnitude;
    if (mArchimede.Magnitude != 0.0f)
        HeaveForce -= mHeaveDrag.Magnitude;
    
    HeaveAcceleration = HeaveForce / mMass;
    HeaveVelocity += HeaveAcceleration * dt;
    ship.Position.y += HeaveVelocity * dt;

    // ── PITCH ────────────────────────────────────────────────────────────────

    float volume = mMass / mWATER_DENSITY;

    // Pitch/Roll speed factor : 1.0 = natural, 0.5 = half speed, 2.0 = double speed
    float pitchRollFactor = ship.PitchRollFactor;  // parameter per ship

    float zeta_pitch = 0.35f;
    switch (ship.Class)
    {
    case eClass::FastBoat:    zeta_pitch = 0.15f; break;
    case eClass::Corvette:    zeta_pitch = 0.20f; break;
    case eClass::Frigate:     zeta_pitch = 0.25f; break;
    case eClass::Fishing:     zeta_pitch = 0.35f; break;
    case eClass::Submarine:   zeta_pitch = 0.50f; break;
    case eClass::Ferry:       zeta_pitch = 0.30f; break;
    case eClass::Tugboat:     zeta_pitch = 0.40f; break;
    case eClass::Cargo:       zeta_pitch = 0.35f; break;
    case eClass::Supertanker: zeta_pitch = 0.45f; break;
    }

    // Hydrostatic pitch stiffness (N·m/rad)
    float IL = 0.75f * (mLength3 * mWidth) / 12.0f;
    float BML = IL / volume;
    float K_pitch = (mWATER_DENSITY * mGRAVITY * volume * BML / mLength) * pitchRollFactor;

    // Critical damping coefficient (N·m·s/rad)
    float C_pitch_crit = 2.0f * sqrt(K_pitch * mIzz);

    // ── ROLL ─────────────────────────────────────────────────────────────────

    float zeta_roll = 0.20f;
    switch (ship.Class)
    {
    case eClass::FastBoat:    zeta_roll = 0.10f; break;
    case eClass::Corvette:    zeta_roll = 0.12f; break;
    case eClass::Frigate:     zeta_roll = 0.15f; break;
    case eClass::Fishing:     zeta_roll = 0.25f; break;
    case eClass::Submarine:   zeta_roll = 0.40f; break;
    case eClass::Ferry:       zeta_roll = 0.18f; break;
    case eClass::Tugboat:     zeta_roll = 0.30f; break;
    case eClass::Cargo:       zeta_roll = 0.20f; break;
    case eClass::Supertanker: zeta_roll = 0.25f; break;
    }

    float GM = 1.0f;
    switch (ship.Class)
    {
    case eClass::FastBoat:    GM = 1.50f; break;
    case eClass::Corvette:    GM = 1.20f; break;
    case eClass::Frigate:     GM = 1.00f; break;
    case eClass::Fishing:     GM = 0.60f; break;
    case eClass::Submarine:   GM = 2.00f; break;
    case eClass::Ferry:       GM = 1.50f; break;
    case eClass::Tugboat:     GM = 1.20f; break;
    case eClass::Cargo:       GM = 0.80f; break;
    case eClass::Supertanker: GM = 0.50f; break;
    }

    // Hydrostatic roll stiffness (N·m/rad)
    float IT = 0.75f * (mWidth * mWidth * mWidth * mLength) / 12.0f;
    float BM = IT / volume;
    if (GM > BM) GM = BM * 0.9f;  // safety : GM cannot exceed BM
    float K_roll = (mWATER_DENSITY * mGRAVITY * volume * GM) * pitchRollFactor;

    // Critical damping coefficient (N·m·s/rad)
    float C_roll_crit = 2.0f * sqrt(K_roll * mIxx);

    // ── Archimede / Gravity ──────────────────────────────────────────────────

    float dx = mArchimede.Position.x - mGravity.Position.x;
    float dz = mArchimede.Position.z - mGravity.Position.z;
    float PitchOffset = dx * cos(Yaw) - dz * sin(Yaw);
    float RollOffset = dx * sin(Yaw) + dz * cos(Yaw);

    // Convert AG offset to equivalent excitation angle

    // = what angle the ship would reach at static equilibrium under this offset
    float pitchExcitationAngle = (mArchimede.Magnitude * PitchOffset) / K_pitch;
    float rollExcitationAngle = (mArchimede.Magnitude * (-RollOffset)) / K_roll;

    // Limit to ±20° to avoid wave-driven instability
    pitchExcitationAngle = glm::clamp(pitchExcitationAngle, glm::radians(-20.0f), glm::radians(20.0f));
    rollExcitationAngle = glm::clamp(rollExcitationAngle, glm::radians(-20.0f), glm::radians(20.0f));

    // ── PITCH ──────────────────────────────────────────────────────────────

    float pitchRestoring = -K_pitch * (Pitch - pitchExcitationAngle);

    PitchAcceleration = pitchRestoring / mIzz;
    PitchAcceleration -= (zeta_pitch * C_pitch_crit * PitchVelocity) / mIzz;

    PitchVelocity += PitchAcceleration * dt;
    Pitch += PitchVelocity * dt;
    Pitch = normalizeAngle(Pitch);

    // ── ROLL ───────────────────────────────────────────────────────────────

    float rollRestoring = -K_roll * (Roll - rollExcitationAngle);

    RollAcceleration = rollRestoring / mIxx;
    RollAcceleration -= (zeta_roll * C_roll_crit * RollVelocity) / mIxx;

    // Centrifugal effect during a turn (ship rolls outward)
    if (fabs(SurgeVelocity) > 0.01f)
        RollAcceleration += Sign(YawVelocity) * mCentrifugalTorque.Magnitude / mIxx;

    RollVelocity += RollAcceleration * dt;
    Roll += RollVelocity * dt;
    Roll = normalizeAngle(Roll);

    // ── SURGE ────────────────────────────────────────────────────────────────
    
    float forceFWD = mThrust1.Magnitude + mThrust2.Magnitude
        + mPropDrag1.Magnitude + mPropDrag2.Magnitude
        + mViscousDrag.Magnitude
        + mWavesDrag.Magnitude
        + mAirDrag.Magnitude
        + mRudderDrag.Magnitude;

    SurgeAcceleration = forceFWD / mMass;

    // ── Turning resistance ───────────────────────────────────────────────────
    {
        // ── Low-speed attenuation ────────────────────────────────────────

        // Fn > 0.10 : full drift resistance
        float Fn = fabs(SurgeVelocity) / sqrt(mGRAVITY * LWL);
        float lowSpeedFade = std::clamp((Fn - 0.1f) / 0.1f, 0.0f, 1.0f);

        float driftDeg = fabs(DriftAngleDeg);          // suppression du x1.4 non physique
        float driftRad = glm::radians(driftDeg);
        float sinB = sin(driftRad);
        float cosB = cos(driftRad);
        float latArea = mLength * mDraft;

        // ── Attenuation : drift due to wind vs drift due to turning ────────────

        float yawNorm = glm::clamp(fabs(YawVelocity) / glm::radians(1.0f / 60.0f), 0.0f, 1.0f);
        float windDriftNorm = (mWindDrift.Magnitude > 0.0f && glm::length(vCOG) > 0.01f)
            ? glm::clamp(mWindDrift.Magnitude / (mMass * 0.01f), 0.0f, 1.0f) : 0.0f;
        float windDriftFactor = glm::clamp(1.0f - windDriftNorm * (1.0f - yawNorm), 0.2f, 1.0f);

        // F_drift : lateral resistance due to drift angle
        
        // Typical lateral Cd for a hull = 0.15–0.25
        float F_drift = 0.5f * mWATER_DENSITY * 0.2f * latArea * SurgeVelocity * fabs(SurgeVelocity) * sinB * cosB;
        F_drift *= windDriftFactor * lowSpeedFade;

        // F_rot : rotational resistance
        
        // Cd = 0.4 (value from literature for hull in yaw), surface = draft * L
        float V_rot = YawVelocity * mLength * 0.5f;
        float F_rot = 0.5f * mWATER_DENSITY * 0.4f * mDraft * mLength * V_rot * fabs(V_rot);

        float thrustMax = fabs(mThrust1.Magnitude) + fabs(mThrust2.Magnitude);
        float F_rot_max = 0.08f * thrustMax;
        F_rot = glm::clamp(F_rot, -F_rot_max, F_rot_max);

        SurgeAcceleration -= (F_drift + F_rot) / mMass;

        // ── Speed loss ─────────────────────────────────────────────────────────

        // Accumulation of heading angle in a turn
        if (fabs(YawVelocity) > glm::radians(0.1f / 60.0f))
        {
            float rotIntensity = glm::clamp(fabs(YawVelocity) / (ship.RoTMax * ((float)M_PI / 180.0f) / 60.0f), 0.0f, 1.0f);
            mTurnYawAccum += fabs(YawVelocity) * dt * rotIntensity;
        }
        else
        {
            mTurnYawAccum = 0.0f;
            mTurnPhase2 = false;
            mTurnEntrySpeed = fabs(SurgeVelocity);
        }

        // Smoothed speed derivative
        float speedDot = (fabs(SurgeVelocity) - mPrevSurgeVelocity) / dt;
        mPrevSurgeVelocity = fabs(SurgeVelocity);

        // Switch to phase 2 as soon as the speed no longer decreases (slightly positive threshold to avoid oscillations)
        if (!mTurnPhase2 && speedDot >= -0.002f && mTurnYawAccum > glm::radians(20.0f))
        {
            mTurnPhase2 = true;
            mSpeedAtPhase2 = fabs(SurgeVelocity);   // actual speed at the moment of the switch
            mYawAtPhase2 = mTurnYawAccum;
        }

        float speedLossFactor;
        float targetSpeed;

        if (!mTurnPhase2)
        {
            // Segment 1 : quadratic: rises faster at the beginning
            float u = glm::clamp(mTurnYawAccum / glm::radians(35.0f), 0.0f, 1.0f);
            float s = u * (2.0f - u);          // courbe ease-out : mord fort au début
            speedLossFactor = 1.0f - s * (1.0f - 0.55f);  // perte max phase 1 = 45%
            targetSpeed = mTurnEntrySpeed * speedLossFactor;
        }
        else
        {
			// Segment 2 : linear from actual switch speed → 1/2 of entry speed over remaining 270°
            float speedTargeted = mTurnEntrySpeed * 0.55f;
            float yawSincePhase2 = mTurnYawAccum - mYawAtPhase2;
			float u = glm::clamp(yawSincePhase2 / glm::radians(90.0f), 0.0f, 1.0f); // param 90° to be calibrated
            float vTarget = speedTargeted / mTurnEntrySpeed;                        // absolute target ratio
            speedLossFactor = 1.0f - u * (1.0f - (mSpeedAtPhase2 / mTurnEntrySpeed) - u * ((mSpeedAtPhase2 / mTurnEntrySpeed) - vTarget));
            targetSpeed = mSpeedAtPhase2 - u * (mSpeedAtPhase2 - speedTargeted);
        }

        // Application
        float speedError = fabs(SurgeVelocity) - targetSpeed;
        if (speedError > 0.0f)
        {
            // Braking force proportional to the mass, but capped at a percentage of the available thrust to never block the ship
            float thrustAvailable = fabs(mThrust1.Magnitude) + fabs(mThrust2.Magnitude);
            float Fn_entry = (mTurnEntrySpeed > 0.1f) ? mTurnEntrySpeed / sqrt(mGRAVITY * LWL) : 0.0f;
            float highSpeedBrakeFactor = glm::clamp((Fn_entry - 0.15f) / 0.15f, 0.0f, 1.0f);

            float capLow = 0.25f * thrustAvailable;
            float capHigh = 0.30f * thrustAvailable + mMass * 0.015f * fabs(SurgeVelocity);
            float brakeCap = glm::mix(capLow, capHigh, highSpeedBrakeFactor);

            float brakingForce = glm::clamp(mMass * 0.5f * speedError, 0.0f, brakeCap);
            SurgeAcceleration -= brakingForce / mMass;
        }
    }
   
    // ── Slam and Surge ───────────────────────────────────────────────────────
    if (AreaWetted > 0.0f)
    {
        // Slam decreases the speed
        if (SurgeAcceleration > 0.0f && PitchVelocity > 0.0f) SurgeAcceleration -= 0.1f * PitchVelocity;

        // Surge: Positive pitch decreases the speed, negative pitch increases the speed
        SurgeAcceleration -= 0.1f * (AreaWetted / mAreaWettedMax) * mGRAVITY * sin(Pitch);
    }

    // ── Integration ─────────────────────────────────────────────────────────
    
    SurgeVelocity += SurgeAcceleration * dt;

    // Low-speed damping: only if the engine is off
    float vAbs = fabs(SurgeVelocity);
    if (vAbs > 0.001f && vAbs < 0.5f)
    {
        bool engineOff = (fabs(PowerApplied1) < 1.0f && fabs(PowerApplied2) < 1.0f);
        bool decelerating = (SurgeAcceleration * SurgeVelocity < 0.0f);  // acc opposite to v

        if (engineOff || decelerating)
        {
            float stopBlend = 1.0f - (vAbs / 0.5f);
            float stopForce = -Sign(SurgeVelocity) * mMass * 0.02f * stopBlend;
            SurgeVelocity += (stopForce / mMass) * dt;
        }
    }

    // ── YAW ────────────────────────────────────────────────────────────────
   
    YawAcceleration = 0.0f;

    // Thrusters
    float bowComponent = mBowThrust.Magnitude * fabs(ship.PosBowThruster.x) / mIyy;
    float sternComponent = mSternThrust.Magnitude * fabs(ship.PosSternThruster.x) / mIyy;
    if (mBowThrust.Magnitude != 0.0f)    YawAcceleration -= bowComponent;
    if (mSternThrust.Magnitude != 0.0f)  YawAcceleration += sternComponent;
    float yawAccelerationBeforeThrusters = YawAcceleration;

	// 2 propellers with different thrusts create a yaw torque
    if (ship.nPropeller == 2 && mThrust1.Magnitude != mThrust2.Magnitude)
    {
        float couplePropellers = -(mThrust1.Magnitude - mThrust2.Magnitude) * glm::length(ship.PosPropeller1 - ship.PosPropeller2);
        YawAcceleration += 0.25f * couplePropellers / mIyy;
    }

    // Rudder (Increased effect to counter yaw damping)
    float pivot = ComputePivotPosition() * 0.2f;
    YawAcceleration += 10.0f * mRudderLift.Magnitude * (fabs(pivot - ship.PosRudder.x)) / mIyy;
    
	// Yaw damping (Enought damping to cancel yaw when no rudder)
    float yawDampingQ = 100.0f * mWATER_DENSITY * mDraft * (mLength3) / 12.0f * YawVelocity * fabs(YawVelocity);
    YawAcceleration -= yawDampingQ / mIyy;

    // Wind
    YawAcceleration += mWindTorque.Magnitude / mIyy;

    // Influence of the pivot
    float pivotFactorDrag = fabs(pivot) / (0.5f * mLength);
    float pivotEffect = 1.0f - 0.5f * pivotFactorDrag;
    YawAcceleration *= pivotEffect;

    // ── Integration ─────────────────────────────────────────────────────────
    
    YawVelocity += YawAcceleration * dt;

    // Limit the rate of turn ─────────────────────────────────────────────────
    
    const float speedHigh = knot_to_ms(ship.SpeedEcoKt);
    const float speedLow = 0.5f * speedHigh;
    float t = glm::clamp((SurgeVelocity - speedLow) / (speedHigh - speedLow), 0.0f, 1.0f);
    float s = t * t * (3.0f - 2.0f * t);
    float limitFactor = (1.0f - s) + s * ship.TurnabilityAtSpeed;
    float rateOfTurn_rad_s = limitFactor * ship.RoTMax * (M_PI / 180.0f) / 60.0f;

    if (fabs(SurgeVelocity) > 0.1f)  // only when underway
        if (fabs(YawVelocity) > rateOfTurn_rad_s)
            YawVelocity = rateOfTurn_rad_s * Sign(YawVelocity);

    // ── Integration ─────────────────────────────────────────────────────────
    
    Yaw += YawVelocity * dt;

    // ── NEW POSITION ─────────────────────────────────────────────────────────

    // Displacement by the thrusters
    float thrusterDisp = 0.0f;

    if (mBowThrust.Magnitude != 0.0f && mSternThrust.Magnitude != 0.0f && mBowThrust.Magnitude * mSternThrust.Magnitude > 0.0f)
    {
		float sinBow = sin((yawAccelerationBeforeThrusters - bowComponent) * dt);
        float sinStern = sin((yawAccelerationBeforeThrusters + sternComponent) * dt);

        float b = mBow.x * sinBow * fabs(mBowThrust.Magnitude / mBowThrustMax);
        float s = mStern.x * sinStern * fabs(mSternThrust.Magnitude / mSternThrustMax);

        thrusterDisp = -(b + s);
    }
    else
    {
        float sinYawVelocity_dt = sin(YawVelocity * dt);
        if (mBowThrust.Magnitude != 0.0f)   thrusterDisp -= mBow.x * sinYawVelocity_dt * fabs(mBowThrust.Magnitude / mBowThrustMax);
		if (mSternThrust.Magnitude != 0.0f) thrusterDisp -= mStern.x * sinYawVelocity_dt * fabs(mSternThrust.Magnitude / mSternThrustMax);
    }
    float thrusterVel = (dt > 0.0f) ? thrusterDisp / dt : 0.0f;


    // Displacement by the rudder
    float rudderVel = 0.0f;
    if (fabs(YawVelocity) > 1e-6f)
        rudderVel = pivot * YawVelocity;

    // Displacement by the wind
    float resistAreaFront = mWidth * mDraft * fabs(mCosPhi);
    float resistAreaSide = LWL * mDraft * fabs(mSinPhi);
    float totalResistArea = resistAreaFront + resistAreaSide;
    const float Cd_hydro_lateral = 1.2f; 
    float resistDrag = 0.5f * mWATER_DENSITY * Cd_hydro_lateral * totalResistArea * WindVelocity * fabs(WindVelocity);
    WindAcceleration = (mWindDrift.Magnitude - resistDrag) / mMass;
    WindVelocity += WindAcceleration * dt;
    WindVelocity *= (1.0f - 0.5f * (1.0f - fabs(mSinPhi)) * dt);    // Residual damping: returns to zero if no more force

    // ── VELOCITIES ────────────────────────────────────────────────────────────

    mat4 rotationMatrix = glm::rotate(mat4(1.0f), Yaw, vec3(0.0f, 1.0f, 0.0f));
    vec3 forward = vec3(rotationMatrix * vec4(1.0f, 0.0f, 0.0f, 0.0f));
    vec3 perpend = vec3(rotationMatrix * vec4(0.0f, 0.0f, 1.0f, 0.0f));
    vec3 wind = glm::normalize(-vec3(g_Wind.x, 0.0f, g_Wind.y));

    vCOG = vec2(forward.x, forward.z) * SurgeVelocity               // Velocity
		 + vec2(perpend.x, perpend.z) * (rudderVel + thrusterVel)   // Rudder and thrusters
         + vec2(wind.x, wind.z)       * WindVelocity;               // Wind

    vec2 dPos = dt * vCOG;
    dPos = mSmoothDpos.update(dPos);
    ship.Position.x += dPos.x;
    ship.Position.z += dPos.y;

    LinearVelocity = dot(normalize(vec2(forward.x, forward.z)), dPos) / dt;
    DriftVelocity = dot(normalize(vec2(perpend.x, perpend.z)), dPos) / dt;

    SOG = glm::length(vCOG);

    // HDG, COG, SOG
    HDG = fmod(450.0f - glm::degrees(Yaw), 360.0f);
    while (HDG < 0.0f)      HDG += 360.0f;
    while (HDG > 360.0f)    HDG -= 360.0f;

    if (SOG == 0.0f)        COG = HDG;
    else                    COG = glm::degrees(std::atan2(dPos.y, dPos.x)) + 90;
    while (COG < 0.0f)      COG += 360.0f;
    while (COG > 360.0f)    COG -= 360.0f;

    DriftAngleDeg = DifferenceDEG(HDG, COG);

    // SOG at the bow and the stern
    mat4 world = mat4(1.0f);
    world = glm::translate(world, ship.Position);
    world = glm::rotate(world, Yaw, vec3(0.0f, 1.0f, 0.0f));
    world = glm::rotate(world, Roll, vec3(1.0f, 0.0f, 0.0f));
    world = glm::rotate(world, Pitch, vec3(0.0f, 0.0f, 1.0f));

    vec3 PosBow = mBow;
    PosBow = vec3(world * vec4(PosBow, 1.0f));
    float d = (PosBow.x - PrevPosBow.x) * mSinYaw + (PosBow.z - PrevPosBow.z) * mCosYaw;
    if (dt != 0.0f) SOGbow = d / dt;
    SOGbow = mSmoothSogBow.update(SOGbow);

    vec3 PosStern = mStern;
    PosStern = vec3(world * vec4(PosStern, 1.0f));
    d = (PosStern.x - PrevPosStern.x) * mSinYaw + (PosStern.z - PrevPosStern.z) * mCosYaw;
    if (dt != 0.0f) SOGstern = d / dt;
    SOGstern = mSmoothSogStern.update(SOGstern);

    if (fabs(YawVelocity) > 1e-6f)
    {
        TurnDiameter_m = 2.0f * fabs(SurgeVelocity) / fabs(YawVelocity);
        TurnDiameter_L = TurnDiameter_m / ship.Length;
    }

    // ── Data filling ─────────────────────────────────────────────────────────

    auto fillResultData = [](const sForce& force) -> sResultData {
        sResultData result;
        result.variable = force.Name;
        result.value = static_cast<double>(force.Magnitude);
        result.decimal = force.Decimal;
        result.unit = force.Unit;
        return result;
        };

    if (g_bShowShipForcesWindows)
    {
        vForcesData.clear();
        vForcesData.push_back(fillResultData(mArchimede));
        vForcesData.push_back(fillResultData(mGravity));
        vForcesData.push_back(fillResultData(mHeaveDrag));
        vForcesData.push_back(fillResultData(mThrust1));
        if (ship.nPropeller == 2) vForcesData.push_back(fillResultData(mThrust2));
        vForcesData.push_back(fillResultData(mPropDrag1));
        if (ship.nPropeller == 2)  vForcesData.push_back(fillResultData(mPropDrag2));
        vForcesData.push_back(fillResultData(mViscousDrag));
        vForcesData.push_back(fillResultData(mWavesDrag));
        vForcesData.push_back(fillResultData(mRudderLift));
        vForcesData.push_back(fillResultData(mRudderDrag));
        vForcesData.push_back(fillResultData(mAirDrag));
        vForcesData.push_back(fillResultData(mWindTorque));
        vForcesData.push_back(fillResultData(mWindDrift));
        vForcesData.push_back(fillResultData(mCentrifugalTorque));
        if (ship.HasBowThruster) vForcesData.push_back(fillResultData(mBowThrust));
        if (ship.HasSternThruster) vForcesData.push_back(fillResultData(mSternThrust));
    }
//#define DEBUG_FORCES
#ifdef DEBUG_FORCES
    static int _dbgCounter = 0;
    if (++_dbgCounter % 100 == 0)   // affiche 1 fois par seconde à 60 fps
    {
        std::cout << std::fixed << std::setprecision(2)
            << "--- FORCES [N] --- dt=" << dt << " ---\n"
            << "  mThrust1          : " << mThrust1.Magnitude << "\n"
            << "  mThrust2          : " << mThrust2.Magnitude << "\n"
            << "  mPropDrag1        : " << mPropDrag1.Magnitude << "\n"
            << "  mPropDrag2        : " << mPropDrag2.Magnitude << "\n"
            << "  mViscousDrag      : " << mViscousDrag.Magnitude << "\n"
            << "  mWavesDrag        : " << mWavesDrag.Magnitude << "\n"
            << "  mBowThrust        : " << mBowThrust.Magnitude << "\n"
            << "  mSternThrust      : " << mSternThrust.Magnitude << "\n"
            << "  mRudderLift       : " << mRudderLift.Magnitude << "\n"
            << "  mRudderDrag       : " << mRudderDrag.Magnitude << "\n"
            << "  mAirDrag          : " << mAirDrag.Magnitude << "\n"
            << "  mWindTorque       : " << mWindTorque.Magnitude << "\n"
            << "  mWindDrift        : " << mWindDrift.Magnitude << "\n"
            << "  mCentrifugalTorque: " << mCentrifugalTorque.Magnitude << "\n"
            << "  >> SurgeAcceleration : " << SurgeAcceleration << " m/s²\n"
            << "  >> SurgeVelocity     : " << SurgeVelocity << " m/s\n"
            << "  >> YawVelocity       : " << glm::degrees(YawVelocity) << " °/s\n"
            << "  >> YawAcceleration   : " << YawAcceleration << " rad/s²\n"
            << std::endl;
    }
#endif
}

void Ship::ComputeTurningCircle(float dt)   
{
#ifdef _DEBUG

    // ── Turning circle metrics ───────────────────────────────────────────────

    float rudderAngle = fabs(RudderAngleDeg);

    // Detection of the first order of rudder (0° → 1°) — start the timer
    if (!mTurnArmed && !mTurnStarted && mPrevRudderAngle < 1.0f && rudderAngle >= 1.0f && fabs(ms_to_knot(SurgeVelocity) - ship.SpeedEcoKt) <= 1.0f) 
    {
        mTurnArmed = true;
        mTurnStartSpeed = ms_to_knot(SurgeVelocity);
        mTurnElapsedTime = 0.0f;
        mTurnStartPos = vec2(ship.Position.x, ship.Position.z);
        mTurnStartHDG = HDG;
        mPrevHDG = HDG;
    }

    // If the rudder returns to 0 before reaching the max → cancellation
    if (mTurnArmed && !mTurnStarted && rudderAngle < 1.0f)
    {
        mTurnArmed = false;
        mTurnElapsedTime = 0.0f;
    }

    // Confirmation: rudder at maximum → turning circle confirmed → display 000
    if (mTurnArmed && !mTurnStarted && rudderAngle >= ship.RudderStepMax - 1)
    {
        mTurnStarted = true;
        mTurnHdgAccum = 0.0f;
        mTurnStopFrames = 0;
        mTurnMetricsDone = 0;
        mTC_090_Printed = false;
        mTC_180_Printed = false;
        mTC_270_Printed = false;
        mTC_360_Printed = false;

        cout << "000 : " << fixed << setprecision(2) << mTurnStartSpeed << " kt | 0 s | TURNING CIRCLE" << endl;
    }

    mPrevRudderAngle = rudderAngle;

    // Timer — starts from the first order of rudder
    if (mTurnArmed && !(mTurnMetricsDone & 8))
        mTurnElapsedTime += dt;

    // Completion of the full turn
    if (mTurnMetricsDone == 15)
    {
        mTurnStarted = false;
        mTurnArmed = false;
    }

    // Frame counter outside of turn
    if (fabs(YawVelocity) <= glm::radians(0.1f / 60.0f))
        mTurnStopFrames++;
    else
        mTurnStopFrames = 0;

    // Reset only after 60 consecutive frames outside of turn
    if (mTurnStopFrames >= 60 && mTurnMetricsDone == 0)
    {
        mTurnStartPos = vec2(ship.Position.x, ship.Position.z);
        mTurnStartHDG = HDG;
        mTurnHdgAccum = 0.0f;
        mPrevHDG = HDG;
    }

    // New turn after a complete turn
    if (mTurnStopFrames >= 60 && mTurnMetricsDone == 15)
    {
        mTurnMetricsDone = 0;
        mTurnHdgAccum = 0.0f;
        mTurnStartPos = vec2(ship.Position.x, ship.Position.z);
        mTurnStartHDG = HDG;
        mPrevHDG = HDG;
        mTC_090_Printed = false;
        mTC_180_Printed = false;
        mTC_270_Printed = false;
        mTC_360_Printed = false;
    }

    // Active turn only if YawVelocity exceeds a significant threshold
    bool bTurning = fabs(YawVelocity) > glm::radians(0.5f / 60.0f);

    // Accumulation of the heading angle traveled
    if (mTurnStarted && bTurning)
    {
        float dHDG = HDG - mPrevHDG;
        if (dHDG > 180.0f) dHDG -= 360.0f;
        if (dHDG < -180.0f) dHDG += 360.0f;
        mTurnHdgAccum += fabs(dHDG);
    }
    mPrevHDG = HDG;

    // Current position
    vec2 curPos = vec2(ship.Position.x, ship.Position.z);

    // ── 090° ────────────────────────────────────────────────────────────────
    if (mTurnStarted && bTurning && !(mTurnMetricsDone & 1) && mTurnHdgAccum >= 90.0f)
    {
        mTurnMetricsDone |= 1;

        float hdgRad = glm::radians(mTurnStartHDG);
        vec2  fwd = vec2(sin(hdgRad), cos(hdgRad));
        vec2  perp = vec2(cos(hdgRad), -sin(hdgRad));
        float advance = dot(curPos - mTurnStartPos, fwd);
        float transfer = fabs(dot(curPos - mTurnStartPos, perp));

        mTC_090_Speed = ms_to_knot(SurgeVelocity);
        mTC_090_Advance = advance / ship.Length;
        mTC_090_Transfer = transfer / ship.Length;
    }

    // ── 180° ────────────────────────────────────────────────────────────────
    if (mTurnStarted && bTurning && !(mTurnMetricsDone & 2) && mTurnHdgAccum >= 180.0f)
    {
        mTurnMetricsDone |= 2;

        float hdgRad = glm::radians(mTurnStartHDG);
        vec2  perp = vec2(cos(hdgRad), -sin(hdgRad));
        float tactDiam = fabs(dot(curPos - mTurnStartPos, perp));

        mTC_180_Speed = ms_to_knot(SurgeVelocity);
        mTC_180_TactDiam = tactDiam / ship.Length;
    }

    // ── 270° ────────────────────────────────────────────────────────────────
    if (mTurnStarted && bTurning && !(mTurnMetricsDone & 4) && mTurnHdgAccum >= 270.0f)
    {
        mTurnMetricsDone |= 4;
        mTC_270_Speed = ms_to_knot(SurgeVelocity);
    }

    // ── 360° ────────────────────────────────────────────────────────────────
    if (mTurnStarted && bTurning && !(mTurnMetricsDone & 8) && mTurnHdgAccum >= 355.0f)
    {
        mTurnMetricsDone |= 8;
        mTC_360_Speed = ms_to_knot(SurgeVelocity);
        mTC_360_TurnDiam = TurnDiameter_L;
    }

    // ── Display ───────────────────────────────────────────────────────────────
    if (mTurnStarted && bTurning && (mTurnMetricsDone & 1) && !mTC_090_Printed)
    {
        mTC_090_Printed = true;
        cout << "090 : " << fixed << setprecision(2) << mTC_090_Speed
            << " kt | " << setprecision(1) << mTurnElapsedTime
            << " s | Avance " << setprecision(2) << mTC_090_Advance
            << " L | Transfert " << mTC_090_Transfer
            << " L | Drift " << setprecision(1) << DriftAngleDeg << "°" << endl;
    }
    if (mTurnStarted && bTurning && (mTurnMetricsDone & 2) && !mTC_180_Printed)
    {
        mTC_180_Printed = true;
        cout << "180 : " << fixed << setprecision(2) << mTC_180_Speed
            << " kt | " << setprecision(1) << mTurnElapsedTime
            << " s | Tactical Diameter " << setprecision(2) << mTC_180_TactDiam
            << " L | Drift " << setprecision(1) << DriftAngleDeg << "°" << endl;
    }
    if (mTurnStarted && bTurning && (mTurnMetricsDone & 4) && !mTC_270_Printed)
    {
        mTC_270_Printed = true;
        cout << "270 : " << fixed << setprecision(2) << mTC_270_Speed
            << " kt | " << setprecision(1) << mTurnElapsedTime
            << " s | Drift " << setprecision(1) << DriftAngleDeg << "°" << endl;
    }
    if (mTurnStarted && bTurning && (mTurnMetricsDone & 8) && !mTC_360_Printed)
    {
        mTC_360_Printed = true;
        cout << "360 : " << fixed << setprecision(2) << mTC_360_Speed
            << " kt | " << setprecision(1) << mTurnElapsedTime
            << " s | Turning Diameter " << setprecision(2) << mTC_360_TurnDiam
            << " L | Drift " << setprecision(1) << DriftAngleDeg << "°" << endl;
    }

#endif
}
void Ship::ComputeAutopilot(float dt)
{
    /*
    This implementation uses a PID (Proportional-Integral-Derivative) controller to calculate the optimal rudder angle.
    The function first calculates the heading error, taking into account that heading angles are cyclical (0-360°).

    The proportional (P) component acts on a PREDICTED error rather than the current error.
    Because a heavy vessel has significant rotational inertia and the rudder has a slow mechanical response time,
    the controller anticipates where the ship will be once the rudder becomes effective, not where it is now.
    The look-ahead time (rudderLag + inertiaLag) is scaled dynamically: small for minor corrections, large for major course changes.

    The integral (I) component accumulates the current error over time, correcting persistent offsets such as wind or current drift.

    The derivative (D) component acts as a pure rotation brake: it opposes the current yaw rate to prevent overshoot.
    Its gain is scaled down for small errors so it does not suppress minor course corrections.

    The final rudder angle is the sum of these three components, clamped to the maximum allowable angle.
    Additionally, rudder travel is limited to 5° while the ship is above half its economical speed,
    to avoid excessive hydrodynamic forces at high speed.

    Rudder Gain (P) – Controls how aggressively the autopilot steers toward the predicted heading.
    Increasing this value makes the autopilot respond more quickly and decisively.
    However, too high a value combined with a large inertiaLag will cause overshoot on large course changes.

    Auto Trim (I) – Corrects slow, persistent heading drift caused by external forces (wind, current, trim).
    Increase BaseI if the ship consistently settles a few degrees off the target heading.
    Keep MaxIntegral low to prevent the integral from winding up during large manoeuvres.

    Counter Rudder (D) – Opposes the current yaw rate to brake the ship's rotation before it reaches the target heading.
    This is the primary anti-overshoot mechanism for large course changes.
    Its influence is reduced automatically on small corrections (< 15°) so they remain crisp and responsive.
    If the ship consistently overshoots: increase BaseD.
    If small corrections feel sluggish: reduce the dFactor threshold in code (default 15°).

    Tuning parameters:
    ship.BaseP          Proportional gain (acts on predicted error)
    ship.BaseI          Integral gain (acts on accumulated current error)
    ship.BaseD          Derivative gain (acts on yaw rate, scaled by error magnitude)
    ship.MaxIntegral    Integral wind-up limit

    Internal constants to adjust for a new vessel:
    rudderLag           Mechanical travel time of the rudder to full deflection (seconds, measure empirically)
    inertiaLagMin       Rotation inertia lag for small corrections, < 5° (seconds)
    inertiaLagMax       Rotation inertia lag for large course changes, > 30° (seconds)
    */

    static float integral = 0.0f;
    static float lastError = 0.0f;
    static bool  bPrevAutopilot = false;

    if (!bPrevAutopilot && bAutopilot)
    {
        integral = 0.0f;
        lastError = 0.0f;
    }
    bPrevAutopilot = bAutopilot;
    if (!bAutopilot) return;

    float error = std::fmod(HDGInstruction - HDG + 540.0f, 360.0f) - 180.0f;
    error = -error;

    float errorAbs = std::abs(error);
    float yawVelocityDeg = YawVelocity * (180.0f / M_PI);

    // --- Dynamic anticipation based on heading change amplitude ---
    // Small change: little inertia to absorb, anticipate little → reactive
    // Large change: lots of inertia, anticipate early → no overshoot
    float rudderLag = 17.5f;
    float inertiaLagMin = 5.0f;   // for small corrections (< 5°)
    float inertiaLagMax = 60.0f;  // for large changes (> 30°)  ← your current inertiaLag
    float errorScaleMin = 5.0f;   // below: inertiaLagMin
    float errorScaleMax = 30.0f;  // above: inertiaLagMax

    float inertiaFactor = std::clamp((errorAbs - errorScaleMin) / (errorScaleMax - errorScaleMin), 0.0f, 1.0f);
    float inertiaLag = inertiaLagMin + inertiaFactor * (inertiaLagMax - inertiaLagMin);
    float totalLag = rudderLag + inertiaLag;

    float coastDeg = yawVelocityDeg * totalLag;
    coastDeg = std::clamp(coastDeg, -errorAbs, errorAbs); 
    float predictedError = error - coastDeg;

    // --- P on predicted error ---
    
    float p = ship.BaseP * predictedError;

    // --- I on current error ---
    integral += error * dt;
    integral = std::clamp(integral, -ship.MaxIntegral, ship.MaxIntegral);
    
    // Progressive discharge of I when close to the heading
    float iDecay = std::clamp(1.0f - errorAbs / 2.0f, 0.0f, 1.0f);  // 1.0 at 0°, 0.0 at 2°
    integral *= (1.0f - 0.02f * iDecay);   
    
    float i = ship.BaseI * integral;

    // --- D : braking on rotation speed ---
    float dFactor = std::clamp(errorAbs / 15.0f, 0.3f, 1.0f); 
    float d = -ship.BaseD * dFactor * yawVelocityDeg;

    // --- Integration ---
    float rudderAngle = p + i + d;

    // --- Rudder limit according to Froude -------------------------------------------
    // Fn < FnLow  : maximum allowed angle (maneuvering, low speed)
    // Fn > FnHigh : minimum angle (high speed, hydrodynamic safety)
    // Entre les deux : interpolation lisse (smoothstep)

    float Fn = fabs(SurgeVelocity) / sqrt(mGRAVITY * LWL);

    float FnLow = 0.05f;    // below: maximum allowed angle
    float FnHigh = 0.20f;   // above: angle reduced

    float rudderAtLowSpeed = (float)ship.RudderStepMax; // 35° typically
    float rudderAtHighSpeed = 5.0f;                     // 5° at high speed

    float t = std::clamp((Fn - FnLow) / (FnHigh - FnLow), 0.0f, 1.0f);
    float s = t * t * (3.0f - 2.0f * t);  // smoothstep

    float maxRudderAngle = rudderAtLowSpeed + s * (rudderAtHighSpeed - rudderAtLowSpeed);

    rudderAngle = std::clamp(rudderAngle, -maxRudderAngle, maxRudderAngle);

    // --- Minimum correction if persistent error ---
    float minCorrection = 0.2f;
    if (std::abs(rudderAngle) < minCorrection && errorAbs > 1.5f)
        rudderAngle = (rudderAngle >= 0 ? 1.0f : -1.0f) * minCorrection;

    rudderAngle = std::clamp(rudderAngle, -(float)ship.RudderStepMax, (float)ship.RudderStepMax);

    // --- Filtered application to the rudder ---
    float rudderTolerance = 1.0f;
    bool  rudderReady = std::abs(RudderAngleDeg - RudderTargetDeg) < rudderTolerance;
    bool  oppositeDirection = Sign(rudderAngle) != Sign(RudderTargetDeg);

    if (rudderReady || !oppositeDirection)
        RudderTargetDeg = rudderAngle;

    lastError = error;
}
void Ship::UpdatePressureMesh()
{
    float coeff = 0.001f * (200000.0f / mMass) * (6000.0f / mF.rows());

    vector<vec3> linePoints;
    for (auto& tri : mvTris)
    {
        if (tri.WaterStatus != 0) // at least, 1 pt under water
        {
            linePoints.push_back(tri.CoG);
            linePoints.push_back(tri.CoG - tri.Normal * tri.fPressure * coeff);
        }
    }
    mPressureMesh->UpdateVertices(linePoints);
}
void Ship::UpdateSounds()
{
    bool sound = bSound && g_SoundMgr->bSound && bVisible && !g_bPause;

    // Power 1
    mSoundThrust1->setPitch(2.0f + 0.25f * fabs(PowerApplied1) / mPowerW);
    if (g_Camera.GetPosition().y < 0.0f)
        mSoundThrust1->setVolume(0.02f + 0.25f * fabs(PowerApplied1) / mPowerW);
    else
        mSoundThrust1->setVolume(0.25f + 0.25f * fabs(PowerApplied1) / mPowerW);
    mSoundThrust1->setPosition(TransformPosition(ship.PosPropeller1));

    // Power 2
    mSoundThrust2->setPitch(2.0f + 0.25f * fabs(PowerApplied2) / mPowerW);
    if (g_Camera.GetPosition().y < 0.0f)
        mSoundThrust2->setVolume(0.02f + 0.25f * fabs(PowerApplied2) / mPowerW);
    else
        mSoundThrust2->setVolume(0.25f + 0.25f * fabs(PowerApplied2) / mPowerW);
    mSoundThrust2->setPosition(TransformPosition(ship.PosPropeller2));

    // Bow thruster
    if (ship.HasBowThruster)
    {
        mSoundBowThruster->setPosition(TransformPosition(ship.PosBowThruster));
        if (!mbSoundBowThrusterPlaying)
        {
            if (BowThrusterRpm != ship.BowThrusterRpmMin)
            {
                alGetError(); // clear error state
                mSoundBowThruster->play();
                ALenum error;
                if ((error = alGetError()) != AL_NO_ERROR)
                    cout << "alSourcef 0 AL_PITCH : " << error << endl;
                mbSoundBowThrusterPlaying = true;
            }
        }
        else
        {
            if (BowThrusterRpm == ship.BowThrusterRpmMin)
            {
                mSoundBowThruster->pause();
                mbSoundBowThrusterPlaying = false;
            }
        }
    }

    // Stern thruster
    if (ship.HasSternThruster)
    {
        mSoundSternThruster->setPosition(TransformPosition(ship.PosSternThruster));
        if (!mbSoundSternThrusterPlaying)
        {
            if (SternThrusterRpm != ship.SternThrusterRpmMin)
            {
                alGetError(); // clear error state
                mSoundSternThruster->play();
                ALenum error;
                if ((error = alGetError()) != AL_NO_ERROR)
                    cout << "alSourcef 0 AL_PITCH : " << error << endl;
                mbSoundSternThrusterPlaying = true;
            }
        }
        else
        {
            if (SternThrusterRpm == ship.SternThrusterRpmMin)
            {
                mSoundSternThruster->pause();
                mbSoundSternThrusterPlaying = false;
            }
        }
    }

    static bool bPause = false;
    if (!sound)
    {
        mSoundThrust1->pause();
        mSoundThrust2->pause();
        if (ship.HasBowThruster)
            mSoundBowThruster->pause();
        if (ship.HasSternThruster)
            mSoundSternThruster->pause();
        bPause = true;
    }
    if (sound && bPause)
    {
        mSoundThrust1->play();
        mSoundThrust2->play();
        if (ship.HasBowThruster && BowThrusterRpm != ship.BowThrusterRpmMin)
            mSoundBowThruster->play();
        if (ship.HasSternThruster && SternThrusterRpm != ship.SternThrusterRpmMin)
            mSoundSternThruster->play();
        bPause = false;
    }
}

// Wake mesh (for the wake texture)
float calcAlpha(float pointTime, float now)
{
    const float start = 4.0f;
    const float end = 30.0f;

    float elapsed = now - pointTime;
    if (elapsed <= start)
        return 1.0f;
    if (elapsed >= end)
        return 0.0f;

    // logarithmic interpolation inverse: alpha=1 at start, alpha=0 at end
    float t = (elapsed - start) / (end - start); // t in [0,1]
    // small epsilon to avoid log(0)
    float eps = 1e-5f;
    t = glm::clamp(t, 0.0f, 1.0f - eps);
    // Compute
    float logAlpha = 1.0f - log(t + 1.0f) / log(2.0f);
    return glm::clamp(logAlpha, 0.0f, 1.0f);
}
void Ship::UpdateWakeMesh()
{    
    // Add point to wake every 100 frames
    static int compteurSillage = 0;
    compteurSillage++;
    if (compteurSillage % 100 == 0)
    {
        sFoamPts sfp;
        sfp.pos = TransformPosition(mWakePivot);
        sfp.pos.y = 1.0f;
        sfp.time = glfwGetTime();
        vWakePoints.push_back(sfp);
        // Cleaning to not exceed a limit
        if (vWakePoints.size() > 500) vWakePoints.erase(vWakePoints.begin());
        compteurSillage = 0;
    }

    // Temporarily adds the current position
    sFoamPts sfp;
    sfp.pos = TransformPosition(mWakePivot);
    sfp.pos.y = 1.0f;
    sfp.time = glfwGetTime();
    vWakePoints.push_back(sfp);

    size_t n = vWakePoints.size();
    if (n < 2) return;

    mWakeSideLeft.resize(n);
    mWakeSideRight.resize(n);
    vWakeVertices.reserve((n - 1) * 12);// pre-allocate the exact count

    float widthHalf = (mWidth * ship.WakeWidth) * 0.5f;

    // Calculation of the “joined” side ends
    for (size_t i = 0; i < n; ++i)
    {
        vec3 p = vWakePoints[i].pos;

        // Forward (next), backward (prev)
        vec2 dirPrev, dirNext;

        if (i == 0)     dirPrev = glm::normalize(vec2(vWakePoints[i + 1].pos.x - p.x, vWakePoints[i + 1].pos.z - p.z));
        else            dirPrev = glm::normalize(vec2(p.x - vWakePoints[i - 1].pos.x, p.z - vWakePoints[i - 1].pos.z));
        if (i == n - 1) dirNext = glm::normalize(vec2(p.x - vWakePoints[i - 1].pos.x, p.z - vWakePoints[i - 1].pos.z));
        else            dirNext = glm::normalize(vec2(vWakePoints[i + 1].pos.x - p.x, vWakePoints[i + 1].pos.z - p.z));

        // Normals to the left of each segment
        vec2 nPrev(-dirPrev.y, dirPrev.x);
        vec2 nNext(-dirNext.y, dirNext.x);

        // Standard bisector (except in the case of very tight turns)
        vec2 bisec = glm::normalize(nPrev + nNext);
        float bisecLen = glm::length(nPrev + nNext);
        if (bisecLen < 1e-4f) // super acute angle, we take one of the normals
            bisec = nPrev;

        // Corrects the "big miter" if the turn is very tight (prevents crazy points)
        float dotDir = glm::dot(dirPrev, dirNext);

        // Calculates the distance to neighbors to adjust the width
        float dist = 0.0f;
        if (i + 1 < n)
            dist = glm::distance(vec2(p.x, p.z), vec2(vWakePoints[i + 1].pos.x, vWakePoints[i + 1].pos.z));
        else if (i > 0)
            dist = glm::distance(vec2(p.x, p.z), vec2(vWakePoints[i - 1].pos.x, vWakePoints[i - 1].pos.z));

        // Threshold for “too close” points
        float adjustedWidthHalf = widthHalf;
        float threshold = 0.25f;
        if (dist < threshold)
            adjustedWidthHalf = 0.05f; // Minimum width

        // Use adjustedWidthHalf for the following
        float miterLen = adjustedWidthHalf / glm::max(glm::dot(bisec, nPrev), 0.2f); // clamp min

        vec3 left = p + vec3(bisec.x, 0.0f, bisec.y) * miterLen;
        vec3 right = p - vec3(bisec.x, 0.0f, bisec.y) * miterLen;

        mWakeSideLeft[i] = left;
        mWakeSideRight[i] = right;
    }

    // Generation of triangles (2 per segment)
    vWakeVertices.clear();
    float uv_v = 0.0f, dv = 1.0f / n;

    float now = glfwGetTime();

    // 3 trails (left and right with foam and center without foam)
    for (size_t i = 0; i + 1 < n; ++i)
    {
        float v0 = uv_v, v1 = uv_v + dv;

        float alpha0 = calcAlpha(vWakePoints[i].time, now);
        float alpha1 = calcAlpha(vWakePoints[i + 1].time, now);

        vec3 center0 = vWakePoints[i].pos;       // current centre
        vec3 center1 = vWakePoints[i + 1].pos;   // next centre

        // Triangle left-center
        vWakeVertices.push_back({ mWakeSideLeft[i],  {0, v0}, alpha0 });
        vWakeVertices.push_back({ center0,      {0.5, v0}, 0.0f }); // center without foam, alpha 0
        vWakeVertices.push_back({ mWakeSideLeft[i + 1],  {0, v1}, alpha1 });

        // Triangle center-left
        vWakeVertices.push_back({ mWakeSideLeft[i + 1],  {0, v1}, alpha1 });
        vWakeVertices.push_back({ center0,      {0.5, v0}, 0.0f });
        vWakeVertices.push_back({ center1,      {0.5, v1}, 0.0f });

        // Triangle center-right
        vWakeVertices.push_back({ center0,      {0.5, v0}, 0.0f });
        vWakeVertices.push_back({ mWakeSideRight[i], {1, v0}, alpha0 });
        vWakeVertices.push_back({ center1,      {0.5, v1}, 0.0f });

        // Triangle right-center (continued)
        vWakeVertices.push_back({ center1,      {0.5, v1}, 0.0f });
        vWakeVertices.push_back({ mWakeSideRight[i], {1, v0}, alpha0 });
        vWakeVertices.push_back({ mWakeSideRight[i + 1], {1, v1}, alpha1 });

        uv_v += dv;
    }

    // Remove the temporary stitch after use
    vWakePoints.pop_back();

    // Update
    if (vWakeVertices.size() > 3)
    {
        mWakeMesh->UpdateVertices(vWakeVertices);
        mWakeMesh->UpdateTextureVertices(vWakeVertices);
    }
}

// NMEA
unsigned char calculate_checksum(const std::string& sentence)
{
    // Calculate the NMEA XOR checksum on the string between $ and *
    unsigned char checksum = 0;
    for (size_t i = 1; i < sentence.size(); ++i)
    {
        if (sentence[i] == '*') break;
        checksum ^= sentence[i];
    }
    return checksum;
}
string format_latitude(double latitude)
{
    // Formatting latitude/N/S direction
    char dir = (latitude >= 0) ? 'N' : 'S';
    latitude = std::abs(latitude);
    int deg = static_cast<int>(latitude);
    double min = (latitude - deg) * 60;
    ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << deg << std::fixed << std::setprecision(4) << std::setw(7) << min << "," << dir;
    return ss.str();
}
string format_longitude(double longitude)
{
    // E/W longitude/direction formatting
    char dir = (longitude >= 0) ? 'E' : 'W';
    longitude = std::abs(longitude);
    int deg = static_cast<int>(longitude);
    double min = (longitude - deg) * 60;
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(3) << deg << std::fixed << std::setprecision(4) << std::setw(7) << min << "," << dir;
    return ss.str();
}
string Ship::NMEA_RMC()
{
    // Date and time
    time_t now = time(nullptr);
    struct tm utcTime;
    gmtime_s(&utcTime, &now);
    char dateStr[7]; // "ddmmyy"
    strftime(dateStr, sizeof(dateStr), "%d%m%y", &utcTime);
    string date = string(dateStr);
    char hourStr[7]; // "hhmmss"
    strftime(hourStr, sizeof(hourStr), "%H%M%S", &utcTime);
    string time = string(hourStr);

    // Ship data
    char status = 'A'; // A = valid, V = invalid
    vec2 p = opengl_to_lonlat(ship.Position.x, ship.Position.z);

    // Construction of the sentence without the checksum or $
    ostringstream sentence;
    sentence << "GPRMC,"
        << time << "," << status << "," << format_latitude(p.y) << "," << format_longitude(p.x) << ","
        << std::fixed << std::setprecision(1)
        << ms_to_knot(fabs(SurgeVelocity)) << "," << COG << "," << date << ",0.0,E";

    // Build the complete sentence with $ and checksum *
    string full_sentence = "$" + sentence.str();
    unsigned char checksum = calculate_checksum(full_sentence);
    ostringstream final_sentence;
    final_sentence << full_sentence << "*" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)checksum << "\r\n";

    //cout << final_sentence.str() << endl;

    return final_sentence.str();
}
string Ship::NMEA_VHW()
{
    // Construction of the sentence without the checksum or $
    ostringstream sentence;
    sentence << "IIVHW,"
        << std::fixed << std::setprecision(1)
        << HDG << ",T," << HDG << ",M," << ms_to_knot(fabs(SurgeVelocity)) << ",N," << "0,K";

    // Build the complete sentence with $ and checksum *
    string full_sentence = "$" + sentence.str();
    unsigned char checksum = calculate_checksum(full_sentence);
    ostringstream final_sentence;
    final_sentence << full_sentence << "*" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)checksum << "\r\n";

    //cout << final_sentence.str();

    return final_sentence.str();
}
string Ship::NMEA_VWR()
{
    // Construction of the sentence without the checksum or $
    ostringstream sentence;
    sentence << "IIVWR,"
        << std::fixed << std::setprecision(1)
        << fabs(AWA) << "," << WindLeftRight << "," << ms_to_knot(AWS) << ",N," << "0.0,M," << "0.0,K";

    // Build the complete sentence with $ and checksum *
    string full_sentence = "$" + sentence.str();
    unsigned char checksum = calculate_checksum(full_sentence);
    ostringstream final_sentence;
    final_sentence << full_sentence << "*" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)checksum << "\r\n";

    //cout << final_sentence.str();

    return final_sentence.str();
}

// Shadow of the ship
void Ship::UpdateShadowUBOs(uint32_t imageIndex, Camera& camera, Sky* sky)
{
    if (!bVisible) return;
    
    mModelFull->UpdateShadowUBOs(imageIndex, camera, sky, mWorld, ship.Position);
}
void Ship::RenderShadow(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    UpdateWorldMatrix();
    mModelFull->RenderShadow(cmd, iCurrentFrame);
}
// Wake of the ship (1 rendering offscreen + 2 passes of blur)
void Ship::UpdateWakeUBO(uint32_t imageIndex, int sizeW, int sizeH)
{
    if (!bVisible) return;

    if (vWakeVertices.size() > 3)
    {
        float scaleX = 2.0f / sizeW;
        float scaleZ = 2.0f / sizeH;
        float offsetX = 0.0f;
        float offsetZ = 0.0f;
        float originX = ship.Position.x;
        float originZ = ship.Position.z;
        mWakeMesh->UpdateTextureUBO(imageIndex, scaleX, scaleZ, offsetX, offsetZ, originX, originZ);
    }
}
void Ship::RenderWake(VkCommandBuffer cmd, int iCurrentFrame, int imageIndex)
{
    if (!bVisible) return;

    if (vWakeVertices.size() > 3)
    {
        mWakeMesh->RenderTexture(cmd, iCurrentFrame);
        mWakeMesh->ComputeBlur(imageIndex, *g_TexWake0, *g_TexWake1, *g_TexWake2, g_WakeSize);
    }
}
// Reflection of the ship on the water
void Ship::UpdateReflectionUBOs(uint32_t imageIndex, Camera& camera, Sky* sky)
{
    if (!bVisible) return;
    
    mModelFull->UpdateReflectionUBOs(imageIndex, camera, sky, mWorld);
}
void Ship::RenderReflection(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    UpdateWorldMatrix();
    mModelFull->RenderReflection(cmd, iCurrentFrame);
}
// Bridge masks
void Ship::UpdateBridgeMaskUBO(uint32_t imageIndex, Camera& camera)
{
    if (!bVisible) return;
    
    mModelFull->UpdateBridgeMaskUBOs(imageIndex, camera, mWorld);
}
void Ship::RenderOpaqueWalls(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;
    
    mModelFull->RenderOpaqueWalls(cmd, iCurrentFrame);
}
void Ship::RenderWindows(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    mModelFull->RenderWindows(cmd, iCurrentFrame);
}
// Ship model and parts
void Ship::UpdateUBO(VkCommandBuffer cmd,uint32_t imageIndex, Camera& camera, Sky* sky)
{
    if (!bVisible) return;

    if (g_bPause)
        mDt = 0.0f;

    if (bModel)
    {
        if (bWireframe) mModelFull->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld);
        else
        {
            if (g_bShipShadow)
                mModelFull->UpdateCxUBOs(imageIndex, camera, sky, mWorld, ship.EnvMapFactor);
            else
                mModelFull->UpdateMsUBOs(imageIndex, camera, sky, mWorld, ship.EnvMapFactor);
        }

        if (mPropeller1.get())
        {
            float omega1 = PropRpm1 * (2.0f * M_PI) / 60.0f; // radians per second
            static float rotation1 = 0.0f;
            rotation1 += ship.PropTorque1 * omega1 * mDt; // increment rotation each frame
            if (rotation1 > 360.0f)
                rotation1 -= 360.0f;
            mat4 matPropeller1 = mat4(1.0f);
            matPropeller1 = glm::translate(matPropeller1, ship.PosPropeller1);
            matPropeller1 = glm::rotate(matPropeller1, rotation1, vec3(1.0f, 0.0f, 0.0f));
            if (bWireframe) mPropeller1->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld * matPropeller1);
			else            mPropeller1->UpdateMsUBOs(imageIndex, camera, sky, mWorld * matPropeller1);
        }
        if (mPropeller2.get())
        {
            float omega2 = PropRpm2 * (2.0f * M_PI) / 60.0f; // radians per second
            static float rotation2 = 0.0f;
            rotation2 += ship.PropTorque2 * omega2 * mDt; // increment rotation each frame
            if (rotation2 > 360.0f)
                rotation2 -= 360.0f;
            mat4 matPropeller2 = mat4(1.0f);
            matPropeller2 = glm::translate(matPropeller2, ship.PosPropeller2);
            matPropeller2 = glm::rotate(matPropeller2, rotation2, vec3(1.0f, 0.0f, 0.0f));
			if (bWireframe) mPropeller2->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld * matPropeller2);
			else            mPropeller2->UpdateMsUBOs(imageIndex, camera, sky, mWorld * matPropeller2);
        }

        float rotation = -RudderAngleDeg * M_PI / 180.0f;
        if (mRudder1.get())
        {
            mat4 matRudder1 = mat4(1.0f);
            matRudder1 = glm::translate(matRudder1, ship.PosRudder1);
            matRudder1 = glm::rotate(matRudder1, rotation, vec3(0.0f, 1.0f, 0.0f));
			if (bWireframe) mRudder1->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld * matRudder1);
			else            mRudder1->UpdateMsUBOs(imageIndex, camera, sky, mWorld * matRudder1);
        }
        if (mRudder2.get())
        {
            mat4 matRudder2 = mat4(1.0f);
            matRudder2 = glm::translate(matRudder2, ship.PosRudder2);
            matRudder2 = glm::rotate(matRudder2, rotation, vec3(0.0f, 1.0f, 0.0f));
			if (bWireframe) mRudder2->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld * matRudder2);
			else            mRudder2->UpdateMsUBOs(imageIndex, camera, sky, mWorld * matRudder2);
        }
        if (mRadar1.get())
        {
            static float rot1 = 0.0f;
            rot1 -= ship.RotationRadar1 * 6.0f * mDt;
            rot1 = fmod(rot1, 360.0f);
            mat4 matRadar1 = mat4(1.0f);
            matRadar1 = glm::translate(matRadar1, ship.PosRadar1);
            matRadar1 = glm::rotate(matRadar1, glm::radians(rot1), vec3(0.0f, 1.0f, 0.0f));
			if (bWireframe) mRadar1->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld * matRadar1);
			else            mRadar1->UpdateMsUBOs(imageIndex, camera, sky, mWorld * matRadar1);
        }
        if (mRadar2.get())
        {
            static float rot2 = 0.0f;
            rot2 -= ship.RotationRadar2 * 6.0f * mDt;
            rot2 = fmod(rot2, 360.0f);
            mat4 matRadar2 = mat4(1.0f);
            matRadar2 = glm::translate(matRadar2, ship.PosRadar2);
            matRadar2 = glm::rotate(matRadar2, glm::radians(rot2), vec3(0.0f, 1.0f, 0.0f));
			if (bWireframe) mRadar2->UpdateWireframeMsUBOs(imageIndex, camera, sky, mWorld * matRadar2);
			else            mRadar2->UpdateMsUBOs(imageIndex, camera, sky, mWorld * matRadar2);
        }
        
        if (bSmoke && ship.nChimney)
            UpdateSmoke(cmd, imageIndex);
        if (bSpray)
            UpdateSpray(imageIndex);
        if (bFlag)
            UpdateFlag(imageIndex);
    }
    
    if (bHullMesh)
        mHullMesh->UpdateUBO(imageIndex, camera, mWorld);

    if (bPressure)
    {
        mat4 model(1.0f);
        mPressureMesh->UpdateUBO(imageIndex, camera, model, vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    if (bContour)
    {
        mContourMesh1->UpdateUBO(imageIndex, camera, mWorld, vec4(1.0f, 0.0f, 0.0f, 1.0f));
        mContourMesh2->UpdateUBO(imageIndex, camera, mWorld, vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    if (bAxis)
        mAxis->UpdateMsUBOs(imageIndex, camera, sky, mWorld);

    if (bWakeMesh && vWakeVertices.size() > 3)
	    mWakeMesh->UpdateUBO(imageIndex, camera, vec4(0.0f, 1.0f, 1.0f, 1.0f));
}
void Ship::RenderOpaque(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky)
{
    if (!bVisible) return;

    UpdateWorldMatrix();
    if (bModel)
    {
        if (bWireframe) mModelFull->RenderWireframeMs(cmd, iCurrentFrame);
        else
        {
            if (g_bShipShadow)
                mModelFull->RenderCxOpaque(cmd, iCurrentFrame);
            else
                mModelFull->RenderMsOpaque(cmd, iCurrentFrame);
        }

        if (mPropeller1.get())
            if (bWireframe) mPropeller1->RenderWireframeMs(cmd, iCurrentFrame);
			else            mPropeller1->RenderMsOpaque(cmd, iCurrentFrame);
        if (mPropeller2.get())
            if (bWireframe) mPropeller2->RenderWireframeMs(cmd, iCurrentFrame);
			else            mPropeller2->RenderMsOpaque(cmd, iCurrentFrame);
        if (mRudder1.get())
			if (bWireframe) mRudder1->RenderWireframeMs(cmd, iCurrentFrame);
			else            mRudder1->RenderMsOpaque(cmd, iCurrentFrame);
        if (mRudder2.get())
			if (bWireframe) mRudder2->RenderWireframeMs(cmd, iCurrentFrame);
			else            mRudder2->RenderMsOpaque(cmd, iCurrentFrame);
        if (mRadar1.get())
			if (bWireframe) mRadar1->RenderWireframeMs(cmd, iCurrentFrame);
			else            mRadar1->RenderMsOpaque(cmd, iCurrentFrame);
        if (mRadar2.get())
			if (bWireframe) mRadar2->RenderWireframeMs(cmd, iCurrentFrame);
			else            mRadar2->RenderMsOpaque(cmd, iCurrentFrame);
        if (bFlag && mFlag.get())
            RenderFlag(cmd, iCurrentFrame, camera, sky);
    }
    
    if (bHullMesh)
        mHullMesh->Render(cmd, iCurrentFrame);

    if (bPressure)
        mPressureMesh->Render(cmd, iCurrentFrame);

    if (bBbox)
        mModelFull->RenderBbox(cmd, iCurrentFrame, camera, mWorld, vec4(1.0f));

    if (bContour)
    {
        mContourMesh1->Render(cmd, iCurrentFrame);
        mContourMesh2->Render(cmd, iCurrentFrame);
    }

    if (bAxis)
        mAxis->RenderMs(cmd, iCurrentFrame);

    if (bWakeMesh && vWakeVertices.size() > 3)
        mWakeMesh->RenderMesh(cmd, iCurrentFrame);
}
void Ship::RenderTransparent(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;

    UpdateWorldMatrix();

    if (bModel)
    {
		if (g_bShipShadow)
			mModelFull->RenderCxTransparent(cmd, iCurrentFrame);
		else
			mModelFull->RenderMsTransparent(cmd, iCurrentFrame);
    }
}
// Smoke
void Ship::UpdateSmoke(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible) return;
    
    if (!mSmoke.get())
        return;

    vec3 windDirection = 0.25f * vec3(-g_Wind.x, 0.1f * g_TWS_Kt, -g_Wind.y);
    mSmoke->Update(cmd, iCurrentFrame, mDt, ship.nChimney, TransformPosition(ship.PosChimney1), TransformPosition(ship.PosChimney2), windDirection);
}
void Ship::RenderSmoke(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky)
{
    if (!bVisible) return;
    if (!mSmoke.get()) return;

    float rawPower;
    if (ship.nPropeller == 1)
        rawPower = fabs(PowerApplied1);
    else
        rawPower = fabs(0.5f * (PowerApplied1 + PowerApplied2));

    // 400 kW = 0.14, 99000 kW = 0.5
    constexpr float exp = 0.25f;
    float densityMax = InterpolateAValue(powf(400.0f, exp), powf(99000.0f, exp), 0.14f, 0.5f, powf(ship.PowerkW, exp));
    float density = InterpolateAValue(0.0f, mPowerW, 0.01f, densityMax, rawPower);

    mSmoke->Render(cmd, iCurrentFrame, camera, sky, density);
}
// Spray
void Ship::UpdateSpray(int iCurrentFrame)
{
    if (!mSpray.get())
        return;

    // Emits particules of spray from points distributed on the contour

    size_t leftCount = mLeft.size();
    size_t rightCount = mRight.size();

    if (leftCount < 2 || rightCount < 2)
        return;

    float intensity1, intensity2;

    // Lambda function to emit multiple interpolated particles between two points
    auto EmitInterpolatedSpray = [&](const sSprayPt& pt1, const sSprayPt& pt2, float intensity1, float intensity2)
        {
            for (int j = 0; j <= ship.SprayMultiplier; ++j)
            {
                float t = float(j) / float(ship.SprayMultiplier);
                // Local position interpolation
                vec3 interpPos = pt1.p * (1.0f - t) + pt2.p * t;

                vec3 emitPosWorld = TransformPosition(interpPos);
                GetHeightFast(emitPosWorld);

                // Added random offset to start position only
                vec3 randomOffset(mDist(mRng) * mRandomOffsetRange, mDist(mRng) * mRandomOffsetRange, mDist(mRng) * mRandomOffsetRange);
                emitPosWorld += randomOffset;

                // Vertical interpolation factor
                float segmentIntensity = intensity1 * (1.0f - t) + intensity2 * t;

                // Normal interpolation and velocity calculation
                vec3 interpNormal = pt1.n * (1.0f - t) + pt2.n * t;

                vec3 velocity = TransformVector(interpNormal) * segmentIntensity;
                velocity.y += segmentIntensity + 2.0f * ship.SprayVerticalPerf * PitchVelocity;
                velocity.x += 0.01f * vCOG.x;
                velocity.z += 0.01f * vCOG.y;
                velocity *= SOG * 0.5f;
                if (segmentIntensity > 0.0f)
                    mSpray->Emit(emitPosWorld, velocity);
            }
        };

    // Emission on the left with interpolation between points
    for (size_t i = 0; i < leftCount - 1; ++i)
    {
        intensity1 = 1.0f - float(i) / float(leftCount - 1);
        intensity2 = 1.0f - float(i + 1) / float(leftCount - 1);

        if (ship.SprayType == 1)
        {
            // Using a sine in [0, pi/2] to vary from 1.0 to 0
            intensity1 = sinf(1.57079632679f * intensity1);
            intensity2 = sinf(1.57079632679f * intensity2);
        }

        EmitInterpolatedSpray(mLeft[i], mLeft[i + 1], intensity1, intensity2);
    }

    // Emission on the right with interpolation between points
    for (size_t i = 0; i < rightCount - 1; ++i)
    {
        intensity1 = 1.0f - float(i) / float(rightCount - 1);
        intensity2 = 1.0f - float(i + 1) / float(rightCount - 1);

        if (ship.SprayType == 1)
        {
            // Using a sine in [0, pi/2] to vary from 1.0 to 0
            intensity1 = sinf(1.57079632679f * intensity1);
            intensity2 = sinf(1.57079632679f * intensity2);
        }

        EmitInterpolatedSpray(mRight[i], mRight[i + 1], intensity1, intensity2);
    }
    mSpray->Update(mDt);
}
void Ship::RenderSpray(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky)
{
    if (!mSpray.get()) return;
    if (!bSpray) return;

    const float min = 2.0f;     // density starts to increase
    const float range = 4.0f;   // density stops to increase
    float density = std::clamp((SOG - min) / range, 0.0f, 1.0f);

    mSpray->Render(cmd, iCurrentFrame, camera, density, sky->Exposure);
}
// Flag
void Ship::UpdateFlag(int iCurrentFrame)
{
    if (!mFlag.get()) return;
    if (g_bPause) return;
    
    vec2 windApparent = (-vCOG) + (-g_Wind);
    mFlag->Update(iCurrentFrame, mDt, windApparent);
}
void Ship::RenderFlag(VkCommandBuffer cmd, int iCurrentFrame, Camera& camera, Sky* sky)
{
    if (!bVisible) return;
    if (!mFlag.get()) return;

    mat4 model = glm::translate(mat4(1.0f), ship.PosFlag);
    model = glm::rotate(model, -Yaw, vec3(0.0f, 1.0f, 0.0f));
    mFlag->Render(cmd, iCurrentFrame, mWorld * model, camera.GetView(), camera.GetProjection(), sky->Exposure);
}
// Lights
void Ship::RenderOneLight(VkCommandBuffer cmd,Camera& camera, int i)
{
    vec3 pos = TransformPosition(ship.LightPositions[i]);
    mLight->Render(cmd, camera, pos, ship.LightColors[i], 1.0f, 0.1f);
}
void Ship::RenderNavLights(VkCommandBuffer cmd, Camera& camera)
{
    if (!bVisible) return;
    
    if (camera.GetMode() != eCameraMode::BRIDGE)
    {
        if (bLights)
        {
            // Visibility of navigation lights
            vec3 shipForward = TransformPosition(vec3(mLength * 0.5f, 0.0f, 0.0f)) - ship.Position;
            shipForward.y = 0.0f;
            vec3 cameraToShip = camera.GetPosition() - ship.Position;
            cameraToShip.y = 0.0f;
            float angleDeg = degrees(orientedAngle(glm::normalize(shipForward), glm::normalize(cameraToShip), vec3(0.0f, 1.0f, 0.0f)));

            // From starboard (Green)
            if (angleDeg > -112.5f && angleDeg < -3.0f)
                RenderOneLight(cmd, camera, 1);
            // From ahead (Red and Green)
            else if (angleDeg >= -3.0f && angleDeg <= 3.0f)
            {
                RenderOneLight(cmd, camera, 0);  // Red
                RenderOneLight(cmd, camera, 1);  // Green
            }
            // From port (Red)
            else if (angleDeg > 3.0f && angleDeg < 112.5f)
                RenderOneLight(cmd, camera, 0);  // Red
            
			// From astern (White)
            else
                RenderOneLight(cmd, camera, 2);  // White

            // Masthead and stern lights
            if (angleDeg > -112.5f && angleDeg < 112.5f)
            {
                RenderOneLight(cmd, camera, 3);      // White high
                if (ship.LightPositions.size() > 4)
                    RenderOneLight(cmd, camera, 4);  // White high
            }
        }
    }
}

void Ship::RecreatePipelines(VkRenderPass renderPassScene, VkRenderPass renderPassReflection, VkRenderPass renderPassShadow, VkRenderPass randerPassBridgeMask, VkRenderPass randerPassWake, VkExtent2D swapChainExtent)
{
    mHullMesh->RecreatePipelines(renderPassScene, swapChainExtent);
    mContourMesh1->RecreatePipelines(renderPassScene, swapChainExtent);
    mContourMesh2->RecreatePipelines(renderPassScene, swapChainExtent);
    mPressureMesh->RecreatePipelines(renderPassScene, swapChainExtent, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    mModelFull->RecreatePipelines(renderPassScene, renderPassShadow, renderPassReflection, randerPassBridgeMask, swapChainExtent);
    mPropeller1->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    if(ship.nPropeller == 2)
        mPropeller2->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    mRudder1->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    if (ship.nRudder == 2)
        mRudder2->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    if (ship.nRadar <= 2)
        mRadar1->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    if (ship.nRadar == 2)
        mRadar2->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    mAxis->RecreatePipelines(renderPassScene, nullptr, nullptr, nullptr, swapChainExtent);
    
	mWakeMesh.reset(); // Release the old wake mesh
    mWakeMesh = make_unique<WakeMesh>(mVulkanDevice, vWakeVertices);
    mWakeMesh->CreatePipeline(renderPassScene, swapChainExtent);
    mWakeMesh->CreatePipelineTexture(randerPassWake, VkExtent2D(g_WakeSize, g_WakeSize));
    mWakeMesh->CreateBlurPipelines();

    mSmoke.reset();
    mSmoke = make_unique<Smoke>(mVulkanDevice, renderPassScene, swapChainExtent);
    mSpray->RecreatePipelines(renderPassScene, swapChainExtent);
    mFlag->RecreatePipelines(renderPassScene, swapChainExtent);
    mLight->RecreatePipelines(renderPassScene, swapChainExtent);
}
