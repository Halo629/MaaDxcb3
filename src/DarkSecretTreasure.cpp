#include "Comm.h"

// 线索地图名 → SceneEnterXXXNoReset 节点映射（地图名支持 "a|b|c" 多别名，任一命中即可）
static const std::vector<std::pair<const char*, const char*>> kClueMapNodeTable = {
    { "时之尽头的孤岛|时之尽头|孤岛", "SceneEnterIsleOfTimesEndNoReset" },
    { "新月郡|新月", "SceneEnterCrescentCountyNoReset" },
    { "圣城修道院|圣城|修道院", "SceneEnterHolyCityMonasteryNoReset" },
    { "格雷米德|格雷", "SceneEnterGremidNoReset" },
    { "熔炉之城|熔炉", "SceneEnterFurnaceCityNoReset" },
    { "蛮牛谷|蛮牛", "SceneEnterWildBullValleyNoReset" },
    { "银月谷", "SceneEnterSilverMoonValleyNoReset" },
    { "伊瑟瑞尔|伊瑟|瑞尔", "SceneEnterIserelNoReset" },
};

// 自定义动作：读取 GoToClueMap 的 OCR 文本，识别线索所在地图，把 next 定向到对应 SceneEnterXXXNoReset 节点
MaaBool goToClueMap(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    ScreenCap cap(context);
    std::string text = ocrText(context, cap.img, "GoToClueMap", "{}", nullptr);

    for (const auto& [mapName, nextNode] : kClueMapNodeTable) {
        for (const auto& alt : split(mapName, '|')) {
            if (text.find(alt) != std::string::npos) {
                LogUtils::info(fmt("当前秘宝线索在地图[%1]", alt));
                setNextNode(context, node_name, nextNode);
                return true;
            }
        }
    }

    LogUtils::warn(fmt("GoToClueMap: 未识别到线索地图，OCR 文本: %1", text));
    return false;
}

// ──── 寻找线索宝藏 ────

// 罗盘 12 方位（每格 30°，0°=正北，顺时针）
enum class CompassDir {
    North = 0,        // 正北
    NorthByEast = 1,  // 北偏东
    EastByNorth = 2,  // 东偏北
    East = 3,         // 正东
    EastBySouth = 4,  // 东偏南
    SouthByEast = 5,  // 南偏东
    South = 6,        // 正南
    SouthByWest = 7,  // 南偏西
    WestBySouth = 8,  // 西偏南
    West = 9,         // 正西
    WestByNorth = 10, // 西偏北
    NorthByWest = 11, // 北偏西
    Unknown = 12,
};

static CompassDir parseDirection(const std::string& text)
{
    // 复合方位（X偏Y）优先，再匹配 正X，最后单字兜底
    static const std::vector<std::pair<const char*, CompassDir>> kDirTable = {
        { "北偏东", CompassDir::NorthByEast },
        { "北偏西", CompassDir::NorthByWest },
        { "东偏北", CompassDir::EastByNorth },
        { "东偏南", CompassDir::EastBySouth },
        { "南偏东", CompassDir::SouthByEast },
        { "南偏西", CompassDir::SouthByWest },
        { "西偏北", CompassDir::WestByNorth },
        { "西偏南", CompassDir::WestBySouth },
        { "正北", CompassDir::North },
        { "正东", CompassDir::East },
        { "正南", CompassDir::South },
        { "正西", CompassDir::West },
        { "西", CompassDir::West },
        { "北", CompassDir::North },
        { "东", CompassDir::East },
        { "南", CompassDir::South },
    };
    for (const auto& [kw, dir] : kDirTable) {
        if (text.find(kw) != std::string::npos) {
            return dir;
        }
    }
    return CompassDir::Unknown;
}

// 世界坐标向量 → 12 方位（+x=东，+y=北，0°=北顺时针）
static CompassDir vectorToDirection(int dx, int dy)
{
    if (dx == 0 && dy == 0) return CompassDir::Unknown;
    double angle = std::atan2(static_cast<double>(dx), static_cast<double>(dy)) * 180.0 / 3.14159265358979323846;
    if (angle < 0) angle += 360.0;
    int idx = static_cast<int>(std::lround(angle / 30.0)) % 12;
    return static_cast<CompassDir>(idx);
}

static void clickPoint(MaaContext* context, int x, int y)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, x, y);
    MaaControllerWait(ctrl, id);
}

// 朝某方位滑动屏幕（拖拽世界视角：滑动方向与观察方向相反，看北=往下滑、看东=往左滑）
static void swipeToward(MaaContext* context, CompassDir dir, int dist = 400, int duration = 300)
{
    int idx = static_cast<int>(dir);
    if (idx < 0 || idx >= 12) return;

    double angle = idx * 30.0 * 3.14159265358979323846 / 180.0;
    int dx = static_cast<int>(-std::sin(angle) * dist); // 看东=往左滑
    int dy = static_cast<int>(std::cos(angle) * dist);  // 看北=往下滑

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    const int cx = 360, cy = 640; // 屏幕中心（按 720×1280 估算，需按实际分辨率调整）
    auto id = MaaControllerPostSwipe(ctrl, cx, cy, cx + dx, cy + dy, duration);
    MaaControllerWait(ctrl, id);
}

// 解析 "(19,73)" 或 "(19, 73)"
static bool parseCoord(const std::string& text, int& x, int& y)
{
    std::vector<int> nums;
    std::string digits;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
        else if (!digits.empty()) {
            nums.push_back(std::atoi(digits.c_str()));
            digits.clear();
        }
    }
    if (!digits.empty()) nums.push_back(std::atoi(digits.c_str()));
    if (nums.size() >= 2) { x = nums[0]; y = nums[1]; return true; }
    return false;
}

// 读指定 OCR 节点文本
static std::string readText(MaaContext* context, const char* entry)
{
    ScreenCap cap(context);
    return ocrText(context, cap.img, entry, "{}", nullptr);
}

// 屏幕查找文字（直接 OCR），命中返回 box
static bool findTextOnScreen(MaaContext* context, const MaaImageBuffer* img, const std::string& text, MaaRect& outBox)
{
    auto* tasker = MaaContextGetTasker(context);
    std::string param = fmt(R"({"expected":"%1","only_rec":true})", text);
    auto id = MaaContextRunRecognitionDirect(context, "OCR", param.c_str(), img);
    if (id == MaaInvalidId) return false;
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &box, nullptr, nullptr, nullptr);
    if (!hit) return false;
    outBox = box;
    return true;
}

// 读 int 数组（缺省用 def 补齐）
static std::vector<int> readIntArrayLocal(const json::value& v, std::vector<int> def)
{
    if (!v.is_array()) return def;
    auto& arr = v.as_array();
    std::vector<int> out = def;
    for (size_t i = 0; i < arr.size() && i < out.size(); ++i) out[i] = arr[i].as_integer();
    return out;
}

// 判断人物 (cx,cy) 到目标 (tx,ty) 的直线是否穿过横墙（墙在 y=value，x 范围 extent，空=全宽）
static bool crossHorizontalWall(int cx, int cy, int tx, int ty, int value, const json::value& extent)
{
    if ((long long)(cy - value) * (ty - value) >= 0) return false; // 同侧或正好在墙上
    double crossX = cx + (double)(tx - cx) * (value - cy) / (ty - cy);
    if (!extent.is_array() || extent.as_array().size() < 2) return true; // 全宽
    int a = extent.as_array()[0].as_integer();
    int b = extent.as_array()[1].as_integer();
    return crossX >= a && crossX <= b;
}

// 判断人物到目标的直线是否穿过竖墙（墙在 x=value，y 范围 extent，空=全高）
static bool crossVerticalWall(int cx, int cy, int tx, int ty, int value, const json::value& extent)
{
    if ((long long)(cx - value) * (tx - value) >= 0) return false;
    double crossY = cy + (double)(ty - cy) * (value - cx) / (tx - cx);
    if (!extent.is_array() || extent.as_array().size() < 2) return true;
    int a = extent.as_array()[0].as_integer();
    int b = extent.as_array()[1].as_integer();
    return crossY >= a && crossY <= b;
}

// 自定义动作：寻找线索宝藏
// custom_action_param:
//   obstacles: [{ "axis":"horizontal","value":45,"task":"穿过A任务" }, { "axis":"vertical","value":30,"task":"穿过B任务" }]
//   points: [{ "x":19, "y":73, "text":"避世树屋", "offset":[-47,29,-46,-2] }, ...]
//   判断：人物到目标的直线穿过墙才穿（horizontal 墙在 y=value、extent 是 x 范围；vertical 墙在 x=value、extent 是 y 范围）。
//   extent 可省略=全宽/全高。同一 task 只跑一次。
//   若障碍物一侧有上下分层且两层不连通，额外加一条「虚拟墙」（如 horizontal value=20 extent=[0,30]），
//   其 task 填同一个真实门任务即可。
MaaBool seekClueTreasure(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto param = json::parse(custom_action_param ? custom_action_param : "{}").value_or(json::value { });
    auto points = param.get("points", json::value { });
    auto obstacles = param.get("obstacles", json::value { });
    if (!points.is_array() || points.as_array().empty()) {
        LogUtils::error("SeekClueTreasure: 缺少 points 配置");
        return false;
    }

    // 1. 点罗盘读方位
    {
        ScreenCap cap(context);
        MaaRect compassBox;
        if (doRecognition(context, cap.img, "HasTreasureCompass", "{}", &compassBox, nullptr)) {
            clickPoint(context, compassBox.x + compassBox.width / 2, compassBox.y + compassBox.height / 2);
            Sleep(1000);
        }
    }
    CompassDir compassDir = parseDirection(readText(context, "GetTreasureDirection"));

    // 2. 读人物坐标
    int cx = 0, cy = 0;
    bool hasCoord = parseCoord(readText(context, "SceneMapCharacterPosition"), cx, cy);

    LogUtils::info(fmt("SeekClueTreasure: 方位=%1，坐标=(%2,%3)", static_cast<int>(compassDir), cx, cy));

    // 3. 遍历点位
    for (const auto& pt : points.as_array()) {
        int tx = pt.get("x", json::value(0)).as_integer();
        int ty = pt.get("y", json::value(0)).as_integer();
        std::string text = pt.get("text", json::value("")).as_string();
        auto offset = readIntArrayLocal(pt.get("offset", json::value()), { 0, 0, 0, 0 });

        // 用坐标算出该点位的方向；有罗盘时用罗盘方向过滤不符的点位
        CompassDir pointDir = hasCoord ? vectorToDirection(tx - cx, ty - cy) : compassDir;
        if (compassDir != CompassDir::Unknown && hasCoord && pointDir != compassDir) {
            continue;
        }

        // 人物到目标的直线穿过墙才穿（同一 task 去重）
        if (hasCoord && obstacles.is_array()) {
            std::vector<std::string> tasks;
            for (const auto& ob : obstacles.as_array()) {
                std::string axis = ob.get("axis", json::value("")).as_string();
                int value = ob.get("value", json::value(0)).as_integer();
                std::string task = ob.get("task", json::value("")).as_string();
                auto extent = ob.get("extent", json::value());
                if (task.empty()) continue;

                bool cross = false;
                if (axis == "horizontal") cross = crossHorizontalWall(cx, cy, tx, ty, value, extent);
                else if (axis == "vertical") cross = crossVerticalWall(cx, cy, tx, ty, value, extent);
                if (!cross) continue;

                if (std::find(tasks.begin(), tasks.end(), task) == tasks.end()) {
                    tasks.push_back(task);
                }
            }
            for (const auto& task : tasks) {
                LogUtils::info(fmt("SeekClueTreasure: 穿过障碍物[%1]", task));
                auto* tasker = MaaContextGetTasker(context);
                auto taskId = MaaContextRunTask(context, task.c_str(), "{}");
                if (taskId != MaaInvalidId) {
                    MaaTaskerWait(tasker, taskId);
                }
            }
        }

        // 朝该方向滑动几次，寻找位置名
        for (int attempt = 0; attempt < 5; ++attempt) {
            swipeToward(context, pointDir);

            ScreenCap cap(context);
            MaaRect textBox;
            if (!findTextOnScreen(context, cap.img, text, textBox)) {
                continue;
            }

            // 识别到位置名，点击偏移位置
            int clickX = textBox.x + textBox.width / 2 + offset[0];
            int clickY = textBox.y + textBox.height / 2 + offset[1];
            clickPoint(context, clickX, clickY);
            Sleep(1500); // 等人物走到并出现挖掘图标

            // 检查挖掘图标（模板识别在 pipeline 的 DigTreasure 节点）
            ScreenCap digCap(context);
            MaaRect digBox;
            if (doRecognition(context, digCap.img, "DigTreasure", "{}", &digBox, nullptr)) {
                clickPoint(context, digBox.x + digBox.width / 2, digBox.y + digBox.height / 2);
                LogUtils::info(fmt("SeekClueTreasure: 在[%1]挖掘成功", text));
                return true;
            }
        }
    }

    LogUtils::warn("所有点位尝试完毕，未找到秘宝");
    return false;
}

void registerCustomDarkSecretTreasure(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "GoToClueMap", goToClueMap, user_data);
    registerCustomAction(res, "SeekClueTreasure", seekClueTreasure, user_data);
}
