/* SimShip by Edouard Halbert
This work is licensed under a Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License
http://creativecommons.org/licenses/by-nc-nd/4.0/ */

#include "Traffic.h"

Traffic::Traffic(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, const pugi::xml_node& node)
{
    // Load xml data
    auto modelNode = node.child(L"Model");
    if (modelNode)
        ModelName = modelNode.child_value();

    auto nameNode = node.child(L"Name");
    if (nameNode)
        Name = nameNode.child_value();

    wstring wtype;
    auto shipTypeNode = node.child(L"Type");
    if (shipTypeNode)
        wtype = shipTypeNode.child_value();
    string type = wstring_to_utf8(wtype);
    ShipType = stringToShipType(type);

    auto mmsiNode = node.child(L"MMSI");
    if (mmsiNode)
        MMSI = mmsiNode.child_value();

    auto callSignNode = node.child(L"CallSign");
    if (callSignNode)
        CallSign = callSignNode.child_value();

    auto toBowNode = node.child(L"to_bow");
    if (toBowNode)
        ToBow = toBowNode.attribute(L"value").as_float();

    auto toSternNode = node.child(L"to_stern");
    if (toSternNode)
        ToStern = toSternNode.attribute(L"value").as_float();

    auto toPortNode = node.child(L"to_port");
    if (toPortNode)
        ToPort = toPortNode.attribute(L"value").as_float();

    auto toStarboardNode = node.child(L"to_starboard");
    if (toStarboardNode)
        ToStarboard = toStarboardNode.attribute(L"value").as_float();

    auto draughtNode = node.child(L"Draught");
    if (draughtNode)
        Draught = draughtNode.attribute(L"value").as_float();

    auto epfdNode = node.child(L"epfd");
    if (epfdNode)
        EPFD = epfdNode.attribute(L"value").as_int();

    float lastSpeed = 0.0f;
    for (auto ptNode : node.children(L"Point"))
    {
        TrafficPoint pt;
        float x = ptNode.attribute(L"lon").as_float();
        float y = ptNode.attribute(L"lat").as_float();
        vec3 pos = lonlat_to_opengl(x, y);
        pt.position = vec2(pos.x, pos.z);
        // Check if the speed attribute exists and is not null
        if (ptNode.attribute(L"speed"))
        {
            pt.speed = ptNode.attribute(L"speed").as_float();
            lastSpeed = pt.speed; // update to the latest known speed
        }
        else
        {
            pt.speed = lastSpeed;  // resume last known speed
        }
        if (ptNode.attribute(L"arc"))
            pt.bArc = (bool)ptNode.attribute(L"arc").as_int();
        vRoute.emplace_back(pt);
    }
    mvLightPositions.clear();
    mvLightColors.clear();
    for (pugi::xml_node light : node.child(L"Lights").children(L"Light"))
    {
        // Read the position
        pugi::xml_node posNode = light.child(L"Position");
        float x = posNode.attribute(L"x").as_float();
        float y = posNode.attribute(L"y").as_float();
        float z = posNode.attribute(L"z").as_float();
        mvLightPositions.push_back(glm::vec3(x, y, z));

        // Read the color
        pugi::xml_node colorNode = light.child(L"Color");
        float r = colorNode.attribute(L"r").as_float();
        float g = colorNode.attribute(L"g").as_float();
        float b = colorNode.attribute(L"b").as_float();
        mvLightColors.push_back(glm::vec3(r, g, b));
    }
    ConstructSegmentsFromRoute();

    // Init the model
    Ship = make_unique<Model>(vulkanDevice);
    Ship->LoadModel(wstring_to_utf8(ModelName).c_str());
    Ship->CreateMsPipeline(renderPass, extent);

    mLight = make_unique<Light>(vulkanDevice, renderPass, extent);

    CurPos = vRoute[0].position;
    CurYaw = std::atan2(-(vRoute[1].position.y - vRoute[0].position.y), vRoute[1].position.x - vRoute[0].position.x);

    // Build VAO
    BuildSegmentsVAO(vulkanDevice, renderPass, extent);
    BuildBezierVAO(vulkanDevice, renderPass, extent);

    // Init the sound of the engine
    wstring wsound;
    auto soundNode = node.child(L"Sound");
    if (soundNode)
        wsound = soundNode.child_value();
    string sound = wstring_to_utf8(wsound);
    mSoundThrust = make_unique<Sound>(sound);
    mSoundThrust->setVolume(0.25f);
    vec3 pos = vec3(CurPos.x, 0.0f, CurPos.y);
    mSoundThrust->setPosition(pos);
    mSoundThrust->setLooping(true);
    mSoundThrust->adjustDistances();
    if (bSound)
        mSoundThrust->play();

    auto speedmaxNode = node.child(L"SpeedMax");
    if (speedmaxNode)
        mSpeedMaxKt = speedmaxNode.attribute(L"value").as_float();
}
Traffic::~Traffic()
{
    mSoundThrust->stop();
    mSoundThrust.reset();
}

void Traffic::Update(float dt)
{
    float temp_dt = dt;

    if (mCurSegmentIndex >= mvSegments.size())
        return;

    float speed = 0.0f;

    while (dt > 0 && mCurSegmentIndex < mvSegments.size())
    {
        Segment& seg = mvSegments[mCurSegmentIndex];

        speed = (seg.type == SegmentType::Line) ? glm::mix(seg.line.speedStart, seg.line.speedEnd, mCurT) : glm::mix(seg.bezier.speedStart, seg.bezier.speedEnd, mCurT);

        float segmentLength = (seg.type == SegmentType::Line) ? glm::distance(seg.line.start, seg.line.end) : ApproximateBezierLength(seg.bezier.P0, seg.bezier.P1, seg.bezier.P2, seg.bezier.P3);

        float distRemaining = (1.0f - mCurT) * segmentLength;
        float distToTravel = speed * dt;

        if (distToTravel + 1e-6 < distRemaining)
        {
            float paramDelta = distToTravel / segmentLength;
            mCurT += paramDelta;
            dt = 0.0f; // everything consumed
        }
        else
        {
            dt -= distRemaining / speed; // adjust the time spent
            mCurT = 0.0f;
            mCurSegmentIndex++;
            if (mCurSegmentIndex >= (int)mvSegments.size())
            {
                mCurSegmentIndex = (int)mvSegments.size() - 1;
                dt = 0.0f;
                break;
            }
        }
    }

    // Calculating position and orientation on the current segment
    const Segment& curSeg = mvSegments[mCurSegmentIndex];
    if (curSeg.type == SegmentType::Line)
    {
        CurPos = glm::mix(curSeg.line.start, curSeg.line.end, mCurT);
        mTangent = glm::normalize(curSeg.line.end - curSeg.line.start);
        CurYaw = std::atan2(-mTangent.y, mTangent.x);
    }
    else
    {
        CurPos = EvaluateBezier(curSeg.bezier.P0, curSeg.bezier.P1, curSeg.bezier.P2, curSeg.bezier.P3, mCurT);
        mTangent = EvaluateBezierDerivative(curSeg.bezier.P0, curSeg.bezier.P1, curSeg.bezier.P2, curSeg.bezier.P3, mCurT);
        mTangent = glm::normalize(mTangent);
        CurYaw = std::atan2(-mTangent.y, mTangent.x);
    }
    vec2 lonlat = opengl_to_lonlat(CurPos.x, CurPos.y);
    Longitude = lonlat.x;
    Latitude = lonlat.y;
    COG = get_hdg_from_yaw(CurYaw);
    SOG = ms_to_knot(speed);

    float deltaYaw = CurYaw - PrevYaw;
    while (deltaYaw > M_PI) deltaYaw -= 2.0f * M_PI;
    while (deltaYaw < -M_PI) deltaYaw += 2.0f * M_PI;
    float deltaYawDeg = deltaYaw * 180.0f / static_cast<float>(M_PI);
    ROT = (deltaYawDeg / temp_dt) * 60.0f;
    PrevYaw = CurYaw;

    UpdateSound();
}
string Traffic::NMEA_AIVDM_1()
{
    string bin;
    auto append_bits = [&](uint64_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i)
            bin += ((value >> i) & 1) ? '1' : '0';
        };

    // Construction of the binary message
    append_bits(1, 6);                          // Message Type
    append_bits(0, 2);                          // Repeat indicator
    append_bits(stoull(MMSI), 30);              // MMSI
    append_bits(static_cast<uint8_t>(eNavigationalStatus::UnderWayUsingEngine), 4);

    float rot = ROT;
    append_bits(static_cast<uint8_t>(rot), 8);  // ROT

    float sog = SOG;
    uint16_t sog_val = (sog < 0) ? 1023 : std::min(static_cast<uint16_t>(sog * 10), (uint16_t)1023);
    append_bits(sog_val, 10);

    append_bits(1, 1);                          // Position accuracy

    float longitude = Longitude;
    int32_t lon_val = static_cast<int32_t>(longitude * 600000);
    if (lon_val < 0) lon_val = (1 << 28) + lon_val;
    append_bits(lon_val, 28);

    float latitude = Latitude;
    int32_t lat_val = static_cast<int32_t>(latitude * 600000);
    if (lat_val < 0) lat_val = (1 << 27) + lat_val;
    append_bits(lat_val, 27);

    float cog = COG;
    uint16_t cog_val = std::min(static_cast<uint16_t>(cog * 10), (uint16_t)3599);
    append_bits(cog_val, 12);

    float hdg = COG;
    uint16_t heading_val = (hdg >= 0 && hdg <= 359) ? static_cast<uint16_t>(hdg) : 511;
    append_bits(heading_val, 9);

    // Timestamp
    time_t now = std::time(nullptr);            // Get the current time in seconds since Epoch
    tm* localTime = std::localtime(&now);       // Convert to local time
    append_bits(localTime->tm_sec, 6);          // Second of UTC timestamp

    append_bits(3, 2);                          // Maneuver Indicator
    append_bits(0, 3);                          // Spare (not used)
    append_bits(0, 1);                          // RAIM flag
    append_bits(49287, 19);                     // Communication state (Radio status)

    // Encode in ASCII 6-bit AIS
    string payload_ascii = encodeAISPayloadToNMEAASCII(bin);

    // Get the sentence AIVDM
    int total_fragments = 1;
    int fragment_number = 1;
    int sequence_id = 0;
    char channel = 'A';

    string sentence;

    if (sequence_id > 0)
        sentence = "AIVDM," + to_string(total_fragments) + "," + to_string(fragment_number) + "," + to_string(sequence_id) + "," + channel + "," + payload_ascii + ",0";
    else
        sentence = "AIVDM," + to_string(total_fragments) + "," + to_string(fragment_number) + ",," + channel + "," + payload_ascii + ",0";

    // Checksum
    unsigned char checksum = calculate_checksum(sentence);

    // Final sentence
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", (int)checksum);
    string final_sentence = "!" + sentence + "*" + string(buf) + "\r\n";
    return final_sentence;
}
string Traffic::NMEA_AIVDM_5()
{
    string bin;
    auto append_bits = [&](uint64_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i)
            bin += ((value >> i) & 1) ? '1' : '0';
        };

    auto to_uppercase = [](string& s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::toupper(c); });
        };

    // Construction of message type 5 (424 bits)
    append_bits(5, 6);                          // Message Type = 5
    append_bits(0, 2);                          // Repeat indicator
    append_bits(stoull(MMSI), 30);              // MMSI
    append_bits(0, 2);                          // AIS version
    append_bits(IMO, 30);                       // IMO number

    // CallSign encoded in 6 bits, 7 characters max
    string callsign = wstring_to_utf8(CallSign);
    to_uppercase(callsign);
    for (int i = 0; i < 7; i++) {
        char c = (i < callsign.size()) ? callsign[i] : ' ';
        uint8_t sixbit = encodeAISChar6(c);
        append_bits(sixbit, 6);
    }

    // Ship name encoded in 6 bits, 20 characters max
    string name = wstring_to_utf8(Name);
    to_uppercase(name);
    for (int i = 0; i < 20; i++) {
        char c = (i < name.size()) ? name[i] : ' ';
        uint8_t sixbit = encodeAISChar6(c);
        append_bits(sixbit, 6);
    }

    append_bits(ShipType, 8); // Ship type

    // Dimensions
    append_bits(ToBow, 9);
    append_bits(ToStern, 9);
    append_bits(ToPort, 6);
    append_bits(ToStarboard, 6);

    append_bits(EPFD, 4);                       // EPFD fix type

    // ETA
    append_bits(0, 4);                          // Month
    append_bits(0, 5);                          // Day
    append_bits(0, 5);                          // Hour
    append_bits(0, 6);                          // Minute

    // Draught
    append_bits(static_cast<uint8_t>(Draught * 10.0f / 4), 8);  // Draught

    // Destination encoded in 6 bits, 20 characters max.
    string destination = "FR-HOUAT";
    for (int i = 0; i < 20; i++) {
        char c = (i < destination.size()) ? destination[i] : ' ';
        uint8_t sixbit = encodeAISChar6(c);
        append_bits(sixbit, 6);
    }

    append_bits(1, 1);                          // DTE  0=Data terminal ready, 1=Not ready (default).
    append_bits(0, 1);                          // Spare (Not used)

    // Handling fragmentation: message type 5 is in 2 fragments
    int total_fragments = 2;
    int sequence_id = 0;
    char channel = 'A';

    // Split bin into 2 parts
    string bin1 = bin.substr(0, 168);
    string bin2 = bin.substr(168);

    string payload1 = encodeAISPayloadToNMEAASCII(bin1);
    string payload2 = encodeAISPayloadToNMEAASCII(bin2);

    // Generate NMEA sentences
    string sentence1 = "AIVDM," + to_string(total_fragments) + ",1," + to_string(sequence_id) + "," + channel + "," + payload1 + ",0";
    string sentence2 = "AIVDM," + to_string(total_fragments) + ",2," + to_string(sequence_id) + "," + channel + "," + payload2 + ",0";

    unsigned char checksum1 = calculate_checksum(sentence1);
    unsigned char checksum2 = calculate_checksum(sentence2);

    char buf1[3], buf2[3];
    snprintf(buf1, sizeof(buf1), "%02X", checksum1);
    snprintf(buf2, sizeof(buf2), "%02X", checksum2);

    string final_sentence = "!" + sentence1 + "*" + string(buf1) + "\r\n" + "!" + sentence2 + "*" + string(buf2) + "\r\n";

    return final_sentence;
}

void Traffic::RenderOneLight(VkCommandBuffer cmd, Camera& camera, int i)
{
    vec3 p = vec3(CurPos.x, 0.0f, CurPos.y);
    mat world = glm::translate(mat4(1.0f), p);
    world = glm::rotate(world, CurYaw, vec3(0.0f, 1.0f, 0.0f));
    mat4 model = glm::translate(world, mvLightPositions[i]);

    vec3 camRight = vec3(camera.GetView()[0][0], camera.GetView()[1][0], camera.GetView()[2][0]);
    vec3 camUp = vec3(camera.GetView()[0][1], camera.GetView()[1][1], camera.GetView()[2][1]);

    model[0] = vec4(camRight, 0.0f);
    model[1] = vec4(camUp, 0.0f);

    float dCameraToLight = glm::length(camera.GetPosition() - p);
    // Distance limits and scales
    const float dMin = 0.5f * 1852.0f;      // 1/2 NM
    const float dMax = 9.0f * 1852.0f;      // 9 NM
    const float sMin = 2.0f;
    const float sMax = 20.0f;
    // Linear interpolation with clamp
    float t = (dCameraToLight - dMin) / (dMax - dMin);
    t = glm::clamp(t, 0.0f, 1.0f);          // t in [0,1]
    float scale = sMin + t * (sMax - sMin); // scale in [2,20]
    model = glm::scale(model, glm::vec3(scale));

    mLight->Render(cmd, camera, model, mvLightColors[i], 1.0f, 0.1f);
}
void Traffic::RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera)
{
    // Visibility of navigation lights
    vec3 shipPos = vec3(CurPos.x, 0.0f, CurPos.y);
    vec3 shipForward = vec3(mTangent.x, 0.0f, mTangent.y);
    vec3 cameraToShip = camera.GetPosition() - shipPos;
    cameraToShip.y = 0.0f;
    float angleDeg = degrees(orientedAngle(glm::normalize(shipForward), glm::normalize(cameraToShip), vec3(0.0f, 1.0f, 0.0f)));

    // From starboard
    if (angleDeg > -112.5f && angleDeg < -3.0f)
        RenderOneLight(commandBuffer, camera, 1);    // Green
    // From ahead
    else if (angleDeg >= -3.0f && angleDeg <= 3.0f)
    {
        RenderOneLight(commandBuffer, camera, 0);    // Red
        RenderOneLight(commandBuffer, camera, 1);    // Green
    }
    // From port
    else if (angleDeg > 3.0f && angleDeg < 112.5f)
        RenderOneLight(commandBuffer, camera, 0);    // Red
    // From astern
    else
        RenderOneLight(commandBuffer, camera, 2);    // White

    // Masthead and stern lights
    if (angleDeg > -112.5f && angleDeg < 112.5f)
    {
        RenderOneLight(commandBuffer, camera, 3);        // White high
        if (mvLightPositions.size() > 4)
            RenderOneLight(commandBuffer, camera, 4);    // White high
    }
}

eShipType Traffic::stringToShipType(const std::string& typeStr)
{
    static const std::unordered_map<std::string, eShipType> mapType = {
        {"not_available", not_available},
        {"wing_in_ground", wing_in_ground},
        {"wing_in_ground_hazardous_cat_a", wing_in_ground_hazardous_cat_a},
        {"wing_in_ground_hazardous_cat_b", wing_in_ground_hazardous_cat_b},
        {"wing_in_ground_hazardous_cat_c", wing_in_ground_hazardous_cat_c},
        {"wing_in_ground_hazardous_cat_d", wing_in_ground_hazardous_cat_d},
        {"fishing", fishing},
        {"towing", towing},
        {"towing_large", towing_large},
        {"dredging_or_underwater_ops", dredging_or_underwater_ops},
        {"diving_ops", diving_ops},
        {"military_ops", military_ops},
        {"sailing", sailing},
        {"pleasure_craft", pleasure_craft},
        {"high_speed_craft", high_speed_craft},
        {"high_speed_craft_hazardous_cat_a", high_speed_craft_hazardous_cat_a},
        {"high_speed_craft_hazardous_cat_b", high_speed_craft_hazardous_cat_b},
        {"high_speed_craft_hazardous_cat_c", high_speed_craft_hazardous_cat_c},
        {"high_speed_craft_hazardous_cat_d", high_speed_craft_hazardous_cat_d},
        {"high_speed_craft_no_info", high_speed_craft_no_info},
        {"pilot_vessel", pilot_vessel},
        {"search_and_rescue_vessel", search_and_rescue_vessel},
        {"tug", tug},
        {"port_tender", port_tender},
        {"anti_pollution_equipment", anti_pollution_equipment},
        {"law_enforcement", law_enforcement},
        {"medical_transport", medical_transport},
        {"noncombatant", noncombatant},
        {"passenger", passenger},
        {"passenger_hazardous_cat_a", passenger_hazardous_cat_a},
        {"passenger_hazardous_cat_b", passenger_hazardous_cat_b},
        {"passenger_hazardous_cat_c", passenger_hazardous_cat_c},
        {"passenger_hazardous_cat_d", passenger_hazardous_cat_d},
        {"passenger_no_info", passenger_no_info},
        {"cargo", cargo},
        {"cargo_hazardous_cat_a", cargo_hazardous_cat_a},
        {"cargo_hazardous_cat_b", cargo_hazardous_cat_b},
        {"cargo_hazardous_cat_c", cargo_hazardous_cat_c},
        {"cargo_hazardous_cat_d", cargo_hazardous_cat_d},
        {"cargo_no_info", cargo_no_info},
        {"tanker", tanker},
        {"tanker_hazardous_cat_a", tanker_hazardous_cat_a},
        {"tanker_hazardous_cat_b", tanker_hazardous_cat_b},
        {"tanker_hazardous_cat_c", tanker_hazardous_cat_c},
        {"tanker_hazardous_cat_d", tanker_hazardous_cat_d},
        {"tanker_no_info", tanker_no_info},
        {"other", other},
        {"other_hazardous_cat_a", other_hazardous_cat_a},
        {"other_hazardous_cat_b", other_hazardous_cat_b},
        {"other_hazardous_cat_c", other_hazardous_cat_c},
        {"other_hazardous_cat_d", other_hazardous_cat_d},
        {"other_no_info", other_no_info}
    };

    auto it = mapType.find(typeStr);
    if (it != mapType.end())
        return it->second;
    return not_available; // default value if not found
}
inline uint8_t Traffic::encodeAISChar6(char c)
{
    if (c == ' ') return 32;        // space encoded as 32
    if (c == '@') return 0;         // '@' encoded as 0
    if (c >= 'A' && c <= 'Z')       // Uppercase letters
        return c - 'A' + 1;
    if (c >= '0' && c <= '9')       // Digits '0' -> 16, '1' -> 17, ..., '9' -> 25
        return c - '0' + 48;
    if (c >= '[' && c <= '_')       // Special characters
        return c - '[' + 27;
    return 0;                       // Otherwise code 0
}
unsigned char Traffic::calculate_checksum(const string& sentence)
{
    // Calculate the NMEA XOR checksum on the string between $ and *
    unsigned char checksum = 0;
    for (size_t i = 0; i < sentence.size(); ++i)
        checksum ^= sentence[i];

    return checksum;
}
string Traffic::encodeAISPayloadToNMEAASCII(const string& bin)
{
    /*
    This code divides the binary string into groups of 6 bits.
    For each group, it converts the 6 bits into a value between 0 and 63.
    If the value is less than 40, it adds 48 (the character '0') to obtain the corresponding ASCII character.
    If the value is 40 or more, it adds 8 in addition to 48 to skip the unused range between 88 and 95 and reach characters from '`' to 'w'.
    */
    string asciiPayload;
    for (size_t i = 0; i < bin.size(); i += 6)
    {
        unsigned char val = 0;
        for (int j = 0; j < 6; ++j)
        {
            val <<= 1;
            if (i + j < bin.size())
                val |= (bin[i + j] == '1');
        }
        val &= 0x3F; // 6 bits max

        // Official table NMEA
        if (val < 40)
            asciiPayload += static_cast<char>('0' + val);
        else
            asciiPayload += static_cast<char>(val + 8 + 48); // '`' à 'w'
    }
    return asciiPayload;
}

vec2 Traffic::EvaluateBezier(const vec2& P0, const vec2& P1, const vec2& P2, const vec2& P3, float t)
{
    // Evaluates a point on a cubic Bézier curve with parameter t (0<=t<=1)

    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;

    vec2 point = uuu * P0;           // (1-t)^3 * P0
    point += 3 * uu * t * P1;        // 3*(1-t)^2*t*P1
    point += 3 * u * tt * P2;        // 3*(1-t)*t^2*P2
    point += ttt * P3;               // t^3 * P3

    return point;
}
vec2 Traffic::EvaluateBezierDerivative(const vec2& P0, const vec2& P1, const vec2& P2, const vec2& P3, float t)
{
    // Evaluates the derivative (tangent) of a cubic Bézier curve with parameter t

    float u = 1.0f - t;

    vec2 derivative =
        3.0f * u * u * (P1 - P0) +
        6.0f * u * t * (P2 - P1) +
        3.0f * t * t * (P3 - P2);

    return derivative;
}
float Traffic::ApproximateBezierLength(const vec2& P0, const vec2& P1, const vec2& P2, const vec2& P3, int steps)
{
    // Numerical approximation of the length of a cubic Bézier curve via subdivision into linear segments and sum of Euclidean distances

    float length = 0.0f;
    vec2 prevPoint = P0;

    for (int i = 1; i <= steps; ++i)
    {
        float t = i / (float)steps;
        vec2 currentPoint = EvaluateBezier(P0, P1, P2, P3, t);
        length += glm::distance(currentPoint, prevPoint);
        prevPoint = currentPoint;
    }

    return length;
}
void Traffic::ConstructSegmentsFromRoute()
{
    mvSegments.clear();

    for (size_t i = 0; i < vRoute.size() - 1; ++i)
    {
        // If the following point is an arc (Bézier)
        if (vRoute[i].bArc && i > 0 && i + 2 < vRoute.size())
        {
            const vec2& A = vRoute[i - 1].position;
            const vec2& B = vRoute[i].position;
            const vec2& C = vRoute[i + 1].position;
            const vec2& D = vRoute[i + 2].position;

            float k = glm::length(C - B) * 0.5f;

            SegmentBezier bezierSeg;
            bezierSeg.P0 = B;
            bezierSeg.P3 = C;
            bezierSeg.P1 = B + k * glm::normalize(B - A);
            bezierSeg.P2 = C + k * glm::normalize(C - D);

            bezierSeg.speedStart = vRoute[i].speed;
            bezierSeg.speedEnd = vRoute[i + 1].speed;

            Segment seg;
            seg.type = SegmentType::Bezier;
            seg.bezier = bezierSeg;
            mvSegments.push_back(seg);
        }
        else
        {
            SegmentLine lineSeg;
            lineSeg.start = vRoute[i].position;
            lineSeg.end = vRoute[i + 1].position;
            lineSeg.speedStart = vRoute[i].speed;
            lineSeg.speedEnd = vRoute[i + 1].speed;

            Segment seg;
            seg.type = SegmentType::Line;
            seg.line = lineSeg;
            mvSegments.push_back(seg);
        }
    }
}
void Traffic::BuildSegmentsVAO(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent)
{
    vector<vec3> contour;
    for (const auto& p : vRoute)
        contour.emplace_back(p.position.x, 0.0f, p.position.y);

    MeshSegments = make_unique<LineMesh>(vulkanDevice, contour);
    MeshSegments->CreatePipeline(renderPass, extent);
}
void Traffic::BuildBezierVAO(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent)
{
    vector<vec3> contour;

    for (const auto& seg : mvSegments)
    {
        if (seg.type == SegmentType::Line)
        {
            // Add the two points of the straight segment (start, end)
            contour.push_back(vec3(seg.line.start.x, 0.0f, seg.line.start.y));
            contour.push_back(vec3(seg.line.end.x, 0.0f, seg.line.end.y));
        }
        else if (seg.type == SegmentType::Bezier)
        {
            // Sample the Bézier curve and add the points
            int steps = 20; // or adjust according to precision
            for (int i = 0; i <= steps; ++i)
            {
                float t = i / float(steps);
                vec2 p = EvaluateBezier(seg.bezier.P0, seg.bezier.P1, seg.bezier.P2, seg.bezier.P3, t);
                contour.push_back(vec3(p.x, 0.0f, p.y));
            }
        }
    }

    MeshBeziers = make_unique<LineMesh>(vulkanDevice, contour);
    MeshBeziers->CreatePipeline(renderPass, extent);
}
void Traffic::UpdateSound()
{
    bool sound = bSound && g_SoundMgr->bSound && !g_bPause;

    mSoundThrust->setPitch(2.0f + 0.25f * fabs(SOG) / mSpeedMaxKt);
    if (g_Camera.GetPosition().y < 0.0f)
        mSoundThrust->setVolume(0.02f + 0.25f * fabs(SOG) / mSpeedMaxKt);
    else
        mSoundThrust->setVolume(0.25f + 0.25f * fabs(SOG) / mSpeedMaxKt);
    vec3 shipPos = vec3(CurPos.x, 0.0f, CurPos.y);
    mSoundThrust->setPosition(shipPos);

    static bool bPause = false;
    if (!sound)
    {
        mSoundThrust->pause();
        bPause = true;
    }
    if (sound && bPause)
    {
        mSoundThrust->play();
        bPause = false;
    }
}

void Traffic::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    Ship->RecreatePipelines(renderPass, nullptr, nullptr, nullptr, newExtent);
}

///////////////////////////////////////////////////////////////////////////////////////////

Traffics::Traffics(shared_ptr<VulkanDevice>& vulkanDevice, VkRenderPass renderPass, VkExtent2D extent, const string& filename)
{
    mVulkanDevice = vulkanDevice;
    mRenderPass = renderPass;
    mExtent = extent;
    
    LoadFromFile(filename);
}
Traffics::~Traffics()
{
    for (auto tr : vTraffics)
        delete tr;
}

bool Traffics::LoadFromFile(const string& filename)
{
    pugi::xml_document doc;
    if (!doc.load_file(filename.c_str())) return false;

    auto root = doc.child(L"Traffics");
    if (!root) return false;

    vTraffics.clear();
    for (auto trafficNode : root.children(L"Traffic"))
    {
        Traffic* tr = new Traffic(mVulkanDevice, mRenderPass, mExtent, trafficNode);
        vTraffics.emplace_back(tr);
    }
    return true;
}
float Traffics::smooth_dt(float newVal)
{
    const int nbValues = 500;
    const int nbIgnored = 300;

    static vector<float> values(nbValues, 0.0f);
    static int index = 0;
    static bool filled = false;
    static int callCount = 0;
    static float sum = 0.0f;

    callCount++;

    if (callCount <= nbIgnored)
        return newVal;  // Ignore recording and sum on the first n calls

    if (filled)
        sum -= values[index];

    sum += newVal;
    values[index] = newVal;

    index = (index + 1) % nbValues;
    if (index == 0) filled = true;

    int count = filled ? nbValues : (index);
    return sum / count;
}
void Traffics::Update(uint32_t currentImage, Camera& camera, Sky* sky)
{
    if (!bVisible)
        return;
    
    static float prevTime = 0.0f;
    float t = glfwGetTime();
    float dt = smooth_dt(t - prevTime);
    prevTime = t;

    for (const auto& tr : vTraffics)
    {
        tr->Update(dt);
        vec3 pos = vec3(tr->CurPos.x, 0.0f, tr->CurPos.y);
        mat4 model = glm::translate(mat4(1.0f), pos);
        model = glm::rotate(model, tr->CurYaw, vec3(0.0f, 1.0f, 0.0f));

        tr->Ship->UpdateMsUBOs(currentImage, camera, sky, model);
        if (bShowRoute)
        {
            mat4 model = glm::translate(mat4(1.0f), vec3(0.0f, 0.5f, 0.0f));
            tr->MeshSegments->UpdateUBO(currentImage, camera, model, vec4(0.0f, 1.0f, 0.0f, 1.0f));
            tr->MeshBeziers->UpdateUBO(currentImage, camera, model, vec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }
}
string Traffics::NMEA_AIVDM_1()
{
    string nmeaData;
    for (const auto& tr : vTraffics)
    {
        string sentence = tr->NMEA_AIVDM_1();
        nmeaData += sentence;
    }
    return nmeaData;
}
string Traffics::NMEA_AIVDM_5(int index)
{
    if (index < 0 || index >= vTraffics.size())
        return "";

    return vTraffics[index]->NMEA_AIVDM_5();
}

void Traffics::RenderOpaque(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible)
        return;

    if (bShowRoute)
    {
        for (const auto& tr : vTraffics)
        {
            tr->MeshSegments->Render(cmd, iCurrentFrame);
            tr->MeshBeziers->Render(cmd, iCurrentFrame);
        }
    }

    for (const auto& tr : vTraffics)
        tr->Ship->RenderMsOpaque(cmd, iCurrentFrame);
}
void Traffics::RenderTransparent(VkCommandBuffer cmd, int iCurrentFrame)
{
    if (!bVisible)
        return;
    
    for (const auto& tr : vTraffics)
        tr->Ship->RenderMsTransparent(cmd, iCurrentFrame);
}
void Traffics::RenderLights(VkCommandBuffer commandBuffer, uint32_t frame, Camera& camera, bool bLights)
{
    if (!bVisible)
        return;
    
    if (bLights)
        for (const auto& tr : vTraffics)
            tr->RenderLights(commandBuffer, frame, camera);
}

void Traffics::RecreatePipelines(VkRenderPass renderPass, VkExtent2D newExtent)
{
    mRenderPass = renderPass;
    mExtent = newExtent;
    
    for (const auto& tr : vTraffics)
        tr->RecreatePipelines(renderPass, newExtent);
}
