#include "Comm.h"
#include <algorithm>
#include <fstream>

const int one_second_delay = 1000;
const int two_second_delay = 2000;

ScreenCap::ScreenCap(MaaContext* c)
    : img(MaaImageBufferCreate())
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(c));
    auto id = MaaControllerPostScreencap(ctrl);
    MaaControllerWait(ctrl, id);
    MaaControllerCachedImage(ctrl, img);
}

ScreenCap::~ScreenCap()
{
    MaaImageBufferDestroy(img);
}

bool doRecognition(
    MaaContext* context,
    const MaaImageBuffer* img,
    const char* entry,
    const char* detail_json,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    auto* tasker = MaaContextGetTasker(context);
    auto id = MaaContextRunRecognition(context, entry, detail_json ? detail_json : "{}", img);
    if (id == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, out_box, out_detail, nullptr, nullptr);
    return hit;
}

bool waitUntilRecognitionSuccess(
    MaaContext* context,
    const char* entry,
    const char* detail_json,
    MaaRect* out_box,
    MaaStringBuffer* out_detail,
    int timeoutMs)
{
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        ScreenCap cap(context);
        if (doRecognition(context, cap.img, entry, detail_json, out_box, out_detail)) {
            return true;
        }
        Sleep(one_second_delay);
        elapsed += one_second_delay;
    }
    return false;
}

void clickRandomTarget(MaaContext* context, int x, int y, int w, int h)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    int rx = x + rand() % w;
    int ry = y + rand() % h;
    auto id = MaaControllerPostClick(ctrl, rx, ry);
    MaaControllerWait(ctrl, id);
}

std::string ocrText(MaaContext* context, const MaaImageBuffer* img, const char* entry, const char* detailJson, MaaRect* box)
{
    auto detailBuf = MaaStringBufferCreate();
    if (!doRecognition(context, img, entry, detailJson, box, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail);
    if (!j || !j->contains("all")) {
        return { };
    }
    std::string result;
    for (auto& item : (*j)["all"].as_array()) {
        result += item["text"].as_string();
    }
    return result;
}

json::value getAndSubDetail(MaaContext* context, const MaaImageBuffer* img, const char* entry, const char* subName)
{
    auto detailBuf = MaaStringBufferCreate();
    MaaRect box;
    if (!doRecognition(context, img, entry, "{}", &box, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail).value_or(json::value { });

    if (!j.is_array()) {
        return { };
    }
    for (auto& item : j.as_array()) {
        if (!item.is_object()) {
            continue;
        }
        auto& name = item["name"];
        if (name.is_string() && name.as_string() == subName) {
            auto& sub = item["detail"];
            return sub.is_object() ? sub : json::value { };
        }
    }
    return { };
}

// 从矩形列表里减去一个矩形（矩形差集），返回剩余矩形列表
static std::vector<MaaRect> subtractRects(const std::vector<MaaRect>& areas, const MaaRect& cut)
{
    std::vector<MaaRect> result;
    for (const auto& a : areas) {
        int ix = (std::max)(a.x, cut.x);
        int iy = (std::max)(a.y, cut.y);
        int ir = (std::min)(a.x + a.width, cut.x + cut.width);
        int ib = (std::min)(a.y + a.height, cut.y + cut.height);
        if (ix >= ir || iy >= ib) {
            result.push_back(a); // 不相交，保留
            continue;
        }
        if (a.y < iy) {
            result.push_back({ a.x, a.y, a.width, iy - a.y });
        }
        if (ib < a.y + a.height) {
            result.push_back({ a.x, ib, a.width, a.y + a.height - ib });
        }
        if (a.x < ix) {
            result.push_back({ a.x, iy, ix - a.x, ib - iy });
        }
        if (ir < a.x + a.width) {
            result.push_back({ ir, iy, a.x + a.width - ir, ib - iy });
        }
    }
    return result;
}

// 计算不可操作区域（TaskIcon 动态区 + 左边/底部/顶部/右侧固定区）
static std::vector<MaaRect> getForbiddenAreas(MaaContext* context)
{
    const int screenW = 720;
    const int screenH = 1280;

    // 识别 TaskIcon，收集符合 threshold 的 box
    ScreenCap cap(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect outBox;
    std::vector<MaaRect> taskIcons;
    if (doRecognition(context, cap.img, "TaskIcon", "{}", &outBox, detailBuf)) {
        std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
        auto j = json::parse(detail).value_or(json::value { });
        if (j.is_object()) {
            if (auto& filtered = j["filtered"]; filtered.is_array()) {
                for (auto& item : filtered.as_array()) {
                    auto& box = item["box"];
                    taskIcons.push_back({ box[0].as_integer(), box[1].as_integer(), box[2].as_integer(), box[3].as_integer() });
                }
            }
        }
    }
    MaaStringBufferDestroy(detailBuf);

    std::vector<MaaRect> forbidden;

    // 第一个：TaskIcon 区域（最下面 box 底边 +30，最靠右 box 右边缘 +20）
    if (!taskIcons.empty()) {
        int rightX = 0;
        int bottomY = 0;
        for (auto& r : taskIcons) {
            rightX = (std::max)(rightX, r.x + r.width);
            bottomY = (std::max)(bottomY, r.y + r.height);
        }
        forbidden.push_back({ 0, 0, rightX + 20, bottomY + 30 });
    }

    // 第二个：最左边 130
    forbidden.push_back({ 0, 0, 130, screenH });
    // 第三个：底部 110
    forbidden.push_back({ 0, screenH - 110, screenW, 110 });
    // 第四个：顶部 160
    forbidden.push_back({ 0, 0, screenW, 160 });
    // 第五个：右侧（右上角起，往左 120，往下 365）
    forbidden.push_back({ screenW - 120, 0, 120, 365 });

    return forbidden;
}

std::vector<MaaRect> getClickableAreas(MaaContext* context)
{
    const int screenW = 720;
    const int screenH = 1280;

    auto forbidden = getForbiddenAreas(context);

    // 从屏幕减去不可操作区域
    std::vector<MaaRect> clickable;
    clickable.push_back({ 0, 0, screenW, screenH });
    for (auto& cut : forbidden) {
        clickable = subtractRects(clickable, cut);
    }
    return clickable;
}

// 快速判断一个点是否在可操作区域内（只需判断是否落在任一不可操作区域内）
bool isClickable(MaaContext* context, int x, int y)
{
    auto forbidden = getForbiddenAreas(context);
    for (auto& r : forbidden) {
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            return false;
        }
    }
    return true;
}

// ──── 罗盘方位与滑动/点击/走位 ────

CompassDir parseDirection(const std::string& text)
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

CompassDir vectorToDirection(int dx, int dy)
{
    if (dx == 0 && dy == 0) return CompassDir::Unknown;
    double angle = std::atan2(static_cast<double>(dx), static_cast<double>(dy)) * 180.0 / 3.14159265358979323846;
    if (angle < 0) angle += 360.0;
    int idx = static_cast<int>(std::lround(angle / 30.0)) % 12;
    return static_cast<CompassDir>(idx);
}

bool parseCoord(const std::string& text, int& x, int& y)
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

// 基础点击（复用文件后面已有的 clickPoint 定义，这里前向声明）
static void clickPoint(MaaContext* context, int x, int y);

// 把点 (x,y) 朝屏幕中心滑动，直到落入可操作区域（坐标跟着视角移动近似更新）。
static void adjustPointToClickable(MaaContext* context, int& x, int& y)
{
    if (isClickable(context, x, y)) {
        return;
    }

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    const int cx = 360, cy = 640; // 屏幕中心

    for (int attempt = 0; attempt < 10 && !isClickable(context, x, y); ++attempt) {
        int mx = (cx - x) / 3;
        int my = (cy - y) / 3;
        if (mx == 0 && my == 0) break;
        auto id = MaaControllerPostSwipe(ctrl, cx, cy, cx + mx, cy + my, 200);
        MaaControllerWait(ctrl, id);
        Sleep(500);
        x += mx; // 点击点跟着视角移动（近似）
        y += my;
    }
}

// 判断矩形是否完全落在可操作区域内（不与任何禁区分区相交）
static bool isRectClickable(MaaContext* context, int x, int y, int w, int h)
{
    auto forbidden = getForbiddenAreas(context);
    for (auto& r : forbidden) {
        if (x < r.x + r.width && x + w > r.x && y < r.y + r.height && y + h > r.y) {
            return false;
        }
    }
    return true;
}

// 把矩形裁减到可操作区域内：减去所有禁区分区，取剩余最大的一块。无剩余返回 false。
static bool cropRectToClickable(MaaContext* context, int& x, int& y, int& w, int& h)
{
    auto forbidden = getForbiddenAreas(context);
    std::vector<MaaRect> parts = { { x, y, w, h } };
    for (auto& r : forbidden) {
        parts = subtractRects(parts, r);
        if (parts.empty()) {
            return false;
        }
    }
    MaaRect best = parts[0];
    int bestArea = best.width * best.height;
    for (size_t i = 1; i < parts.size(); ++i) {
        int area = parts[i].width * parts[i].height;
        if (area > bestArea) {
            bestArea = area;
            best = parts[i];
        }
    }
    x = best.x;
    y = best.y;
    w = best.width;
    h = best.height;
    return true;
}

// 计算把框拉进可操作区域需要滑动的方向：看框压到哪个禁区，朝远离该禁区的方向滑（单轴）。
// 返回归一化方向 (dx, dy) ∈ {-1, 0, 1}。
static void computeEscapeDir(MaaContext* context, int x, int y, int w, int h, int& dx, int& dy)
{
    const int cx = 360, cy = 640; // 屏幕中心
    dx = 0;
    dy = 0;
    auto forbidden = getForbiddenAreas(context);
    for (auto& r : forbidden) {
        bool overlap = x < r.x + r.width && x + w > r.x && y < r.y + r.height && y + h > r.y;
        if (!overlap) continue;
        int rcX = r.x + r.width / 2;
        int rcY = r.y + r.height / 2;
        // 禁区在屏幕中心哪一侧，就往反方向滑（远离禁区）
        if (rcX < cx) dx = 1;       // 禁区在左边 → 往右滑
        else if (rcX > cx) dx = -1; // 禁区在右边 → 往左滑
        if (rcY < cy) dy = 1;       // 禁区在上边 → 往下滑
        else if (rcY > cy) dy = -1; // 禁区在下边 → 往上滑
    }
}

// 把矩形调整到可操作区域：朝远离禁区的方向滑（最多 3 次）；仍不完全可点就裁掉不可操作的部分。
static void adjustRectToClickable(MaaContext* context, int& x, int& y, int& w, int& h)
{
    if (isRectClickable(context, x, y, w, h)) {
        return;
    }

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    const int cx = 360, cy = 640; // 屏幕中心
    const int step = 100;         // 每次滑动距离

    for (int attempt = 0; attempt < 3 && !isRectClickable(context, x, y, w, h); ++attempt) {
        int dx = 0, dy = 0;
        computeEscapeDir(context, x, y, w, h, dx, dy);
        if (dx == 0 && dy == 0) break;
        int mx = dx * step;
        int my = dy * step;
        auto ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, cx, cy, cx + mx, cy + my, 200);
        MaaControllerWait(ctrl, id);
        Sleep(500);
        x += mx; // 框跟着视角移动（近似）
        y += my;
    }

    if (!isRectClickable(context, x, y, w, h)) {
        cropRectToClickable(context, x, y, w, h);
    }
}

void clickAdjusted(MaaContext* context, int x, int y)
{
    adjustPointToClickable(context, x, y);
    clickPoint(context, x, y);
}

// 调整位置：按 target_offset 计算目标区域，滑动视角直到该区域落入可操作区域。
static void adjustPositionToClickable(MaaContext* context, const MaaRect& box, const std::vector<int>& target_offset)
{
    MaaRect target = box;
    if (target_offset.size() >= 4) {
        target.x += target_offset[0];
        target.y += target_offset[1];
        target.width += target_offset[2];
        target.height += target_offset[3];
    }
    adjustRectToClickable(context, target.x, target.y, target.width, target.height);
}

// 自定义动作：调整位置（把识别框按 target_offset 偏移后的区域拉到可操作区域）。
// custom_action_param: { "target_offset": [x, y, w, h] }
MaaBool adjustPositionAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    if (!box) return false;
    auto param = json::parse(custom_action_param ? custom_action_param : "{}").value_or(json::value { });
    std::vector<int> target_offset = { 0, 0, 0, 0 };
    if (auto off = param.get("target_offset", json::value()); off.is_array()) {
        auto& arr = off.as_array();
        for (size_t i = 0; i < arr.size() && i < target_offset.size(); ++i) {
            target_offset[i] = arr[i].as_integer();
        }
    }
    adjustPositionToClickable(context, *box, target_offset);
    return true;
}

// 从屏幕中心按向量 (dx,dy) 滑动（内部处理据点弹框）。
static void swipeVec(MaaContext* context, int dx, int dy, int duration)
{
    const int baseX = 360, baseY = 640; // 屏幕中心（720×1280）
    static const int offX[4] = { 0, 0, -1, 1 }; // 上、下、左、右
    static const int offY[4] = { -1, 1, 0, 0 };

    // 是否弹出了据点框（滑动变点击据点的标志：出现返回按钮）
    auto hasOutpostPopup = [&]() {
        ScreenCap cap(context);
        return doRecognition(context, cap.img, "LeaveButton", "{}", nullptr, nullptr);
    };
    // 退出据点弹框
    auto quitOutpost = [&]() {
        auto* tasker = MaaContextGetTasker(context);
        auto id = MaaContextRunTask(context, "QuitMapOutpost", "{}");
        if (id != MaaInvalidId) {
            MaaTaskerWait(tasker, id);
        }
    };
    // 从 (sx,sy) 滑动固定距离（每次重新获取 controller，避免子任务运行后 controller 失效）
    auto swipeFrom = [&](int sx, int sy) {
        auto ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto id = MaaControllerPostSwipe(ctrl, sx, sy, sx + dx, sy + dy, duration);
        MaaControllerWait(ctrl, id);
        Sleep(300);
    };

    // 从屏幕中心滑动固定距离
    swipeFrom(baseX, baseY);
    if (!hasOutpostPopup()) return; // 没弹窗，滑动成功

    // 弹窗了：退出弹框，偏移起点重试（上下左右 100px 递增，偏移上限受屏幕尺寸限制）
    quitOutpost();
    for (int step = 100; step <= 640; step += 100) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return;
        }
        for (int i = 0; i < 4; ++i) {
            int sx = baseX + offX[i] * step;
            int sy = baseY + offY[i] * step;
            if (sx + dx < 0 || sy + dy < 0 || sx + dx >= 720 || sy + dy >= 1280) continue;
            if (!isClickable(context, sx, sy)) continue;
            swipeFrom(sx, sy);
            if (!hasOutpostPopup()) return; // 滑动成功
            quitOutpost();
        }
    }
}

// 朝目标方向 (dx,dy) 连续滑动屏幕（拖拽视角：滑动方向与观察方向相反，不量化到 12 方位）。
void swipeToward(MaaContext* context, int dx, int dy, int dist, int duration)
{
    double len = std::sqrt((double)dx * dx + (double)dy * dy);
    if (len < 1e-6) return; // 已在目标点，不滑动
    double ux = dx / len; // 东分量归一化
    double uy = dy / len; // 北分量归一化
    int sx = static_cast<int>(-ux * dist); // 看东=往左滑
    int sy = static_cast<int>(uy * dist);  // 看北=往下滑
    swipeVec(context, sx, sy, duration);
}

// 朝某方位滑动屏幕（12 方位量化，走位用 swipeToward 更精确）。
void swipeAvoidOutpost(MaaContext* context, CompassDir dir, int dist, int duration)
{
    int idx = static_cast<int>(dir);
    if (idx < 0 || idx >= 12) return;

    double angle = idx * 30.0 * 3.14159265358979323846 / 180.0;
    int dx = static_cast<int>(-std::sin(angle) * dist);
    int dy = static_cast<int>(std::cos(angle) * dist);
    swipeVec(context, dx, dy, duration);
}

// 识别文本 + 框
struct OcrText {
    std::string text;
    MaaRect box;
};

static std::vector<OcrText> readTextFromNode(MaaContext* context, const MaaImageBuffer* img, const char* nodeName, const char* subName = nullptr);

// 识别所有迷宫名字（已完成 + 未完成），返回名字和框。
static std::vector<OcrText> collectMazeNames(MaaContext* context, const MaaImageBuffer* img)
{
    std::vector<OcrText> out;
    auto complete = readTextFromNode(context, img, "CompleteMazeNameRecg", "CompleteMazeName");
    out.insert(out.end(), complete.begin(), complete.end());
    auto incomplete = readTextFromNode(context, img, "IncompleteMazeNameRecg", "IncompleteMazeName");
    out.insert(out.end(), incomplete.begin(), incomplete.end());
    return out;
}

// 屏幕查找文字（迷宫名字）：识别所有迷宫名字，命中目标返回 box。
bool findTextOnScreen(MaaContext* context, const MaaImageBuffer* img, const std::string& text, MaaRect& outBox)
{
    auto mazes = collectMazeNames(context, img);
    for (auto& m : mazes) {
        if (m.text.find(text) != std::string::npos) {
            outBox = m.box;
            LogUtils::info(fmt("findTextOnScreen: 期望[%1] 识别到迷宫[%2]", text, m.text));
            return true;
        }
    }
    return false;
}

// 读取人物世界坐标
static bool readCharacterCoord(MaaContext* context, int& cx, int& cy);

// 单步走位：读人物坐标 → 朝目标方向滑动 → 找位置名。
// Found=找到位置名(outBox)；NotFound=没找到(继续滑动)；Error=读坐标/算方向失败。
WalkStep walkStep(MaaContext* context, int tx, int ty, const std::string& text, MaaRect& outBox)
{
    if (MaaTaskerStopping(MaaContextGetTasker(context))) {
        return WalkStep::Error;
    }

    int cx = 0, cy = 0;
    if (!readCharacterCoord(context, cx, cy)) {
        LogUtils::warn("walkStep: 读取人物坐标失败");
        return WalkStep::Error;
    }

    int dx = tx - cx;
    int dy = ty - cy;
    LogUtils::debug(fmt("walkStep: 人物=(%1,%2) 目标=(%3,%4)", cx, cy, tx, ty));

    {
        ScreenCap cap(context);
        if (findTextOnScreen(context, cap.img, text, outBox)) {
            LogUtils::info(fmt("walkStep: 找到目标位置%1", text));
            return WalkStep::Found;
        }
    }
    swipeToward(context, dx, dy);

    ScreenCap cap(context);
    if (findTextOnScreen(context, cap.img, text, outBox)) {
        LogUtils::info(fmt("walkStep: 找到目标位置%1", text));
        return WalkStep::Found;
    }
    return WalkStep::NotFound;
}

// ──── 穿墙与走位 ────

// 地图名 → 地图配置（walls + islands）
static std::map<std::string, json::value> g_mapConfig;

// 自定义动作：定义地图的墙/孤岛配置。
// custom_action_param: [
//   {
//     "map":"伊瑟瑞尔",
//     "walls":[
//       { "axis":"horizontal","value":45,"extent":[10,60],"doors":[{"task":"IserelWall1","x":20,"y":45}] }
//     ],
//     "islands":[
//       { "points":[[10,10],[50,10],[50,40],[10,40]], "entrance":{"task":"EnterIsland","x":15,"y":8}, "exit":{"task":"ExitIsland","x":15,"y":35} }
//     ]
//   }
// ]
// 墙：axis=horizontal(横墙,value是y)/vertical(竖墙,value是x)；extent=[a,b] 可选；doors 数组，门 task 是识别节点名，可选 x/y。
// 孤岛：points 是岛屿 4 个坐标（顺时针）；entrance 岛外入口、exit 岛内出口，各含 task + 可选 x/y。
MaaBool defineMapWalls(
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
    auto store = [](const json::value& item) {
        std::string map = item.get("map", json::value("")).as_string();
        if (map.empty()) return;
        g_mapConfig[map] = item;
    };
    if (param.is_array()) {
        for (const auto& item : param.as_array()) {
            store(item);
        }
    }
    else if (param.is_object()) {
        store(param);
    }
    return true;
}

// 判断人物 (cx,cy) 到目标 (tx,ty) 的直线是否穿过横墙（墙在 y=value，x 范围 extent，空=全宽）
static bool crossHorizontalWall(int cx, int cy, int tx, int ty, int value, const json::value& extent)
{
    if ((long long)(cy - value) * (ty - value) >= 0) return false;
    double crossX = cx + (double)(tx - cx) * (value - cy) / (ty - cy);
    if (!extent.is_array() || extent.as_array().size() < 2) return true;
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

// 运行一个 pipeline 任务并等待其完成
void runTaskAndWait(MaaContext* context, const std::string& task)
{
    auto* tasker = MaaContextGetTasker(context);
    auto taskId = MaaContextRunTask(context, task.c_str(), "{}");
    if (taskId != MaaInvalidId) {
        MaaTaskerWait(tasker, taskId);
    }
}

// 读人物世界坐标
static bool readCharacterCoord(MaaContext* context, int& cx, int& cy)
{
    ScreenCap cap(context);
    return parseCoord(ocrText(context, cap.img, "SceneMapCharacterPosition", "{}", nullptr), cx, cy);
}

// 门信息：穿门任务（task 也是识别节点名，用于识别门位置）、可选坐标（滑动找门用）
struct DoorInfo {
    std::string task;
    int x = 0;
    int y = 0;
    bool hasPos = false;
};

// 解析单个门
static DoorInfo parseDoor(const json::value& d)
{
    DoorInfo di;
    di.task = d.get("task", json::value("")).as_string();
    if (di.task.empty()) return di;
    auto ox = d.get("x", json::value());
    auto oy = d.get("y", json::value());
    if (ox.is_number() && oy.is_number()) {
        di.x = ox.as_integer();
        di.y = oy.as_integer();
        di.hasPos = true;
    }
    return di;
}

// 解析 doors 数组
static std::vector<DoorInfo> parseDoors(const json::value& doors)
{
    std::vector<DoorInfo> out;
    if (!doors.is_array()) return out;
    for (const auto& d : doors.as_array()) {
        DoorInfo di = parseDoor(d);
        if (!di.task.empty()) out.push_back(di);
    }
    return out;
}

// 选择离人物最近的门。单门直接返回；多门识别 task 节点 + 人物 box，取最近。
static DoorInfo selectClosestDoor(MaaContext* context, const std::vector<DoorInfo>& doors)
{
    if (doors.empty()) return {};
    if (doors.size() == 1) return doors[0];

    ScreenCap cap(context);
    MaaRect charBox;
    int charCx = 0, charCy = 0;
    if (!doRecognition(context, cap.img, "RolePos", "{}", &charBox, nullptr)) {
        return doors[0]; // 识别不到人物，用第一个
    }
    charCx = charBox.x + charBox.width / 2;
    charCy = charBox.y + charBox.height / 2;

    DoorInfo best = doors[0];
    int bestDist = 0x7fffffff;
    for (const auto& door : doors) {
        MaaRect doorBox;
        if (!doRecognition(context, cap.img, door.task.c_str(), "{}", &doorBox, nullptr)) continue;
        int dx = doorBox.x + doorBox.width / 2 - charCx;
        int dy = doorBox.y + doorBox.height / 2 - charCy;
        int dist = dx * dx + dy * dy;
        if (dist < bestDist) {
            bestDist = dist;
            best = door;
        }
    }
    return best;
}

// 执行一个门：有坐标先滑到门（识别到停），再跑任务
static void runDoor(MaaContext* context, int cx, int cy, const DoorInfo& door)
{
    if (door.task.empty()) return;
    if (door.hasPos) {
        for (int i = 0; i < 10; ++i) {
            swipeToward(context, door.x - cx, door.y - cy);
            ScreenCap cap(context);
            if (doRecognition(context, cap.img, door.task.c_str(), "{}", nullptr, nullptr)) {
                break; // 门已出现
            }
        }
    }
    LogUtils::info(fmt("walkToTargetPoint: 穿过[%1]", door.task));
    runTaskAndWait(context, door.task);
}

// 处理穿墙
static void crossWalls(MaaContext* context, int cx, int cy, int tx, int ty, const json::value& walls)
{
    if (!walls.is_array()) return;

    struct WallCross {
        std::string axis;
        int value = 0;
        std::vector<DoorInfo> doors;
    };
    std::vector<WallCross> crossObs;
    for (const auto& ob : walls.as_array()) {
        std::string axis = ob.get("axis", json::value("")).as_string();
        int value = ob.get("value", json::value(0)).as_integer();
        auto extent = ob.get("extent", json::value());
        auto doors = ob.get("doors", json::value());
        if (!doors.is_array() || doors.as_array().empty()) continue;

        bool cross = false;
        if (axis == "horizontal") cross = crossHorizontalWall(cx, cy, tx, ty, value, extent);
        else if (axis == "vertical") cross = crossVerticalWall(cx, cy, tx, ty, value, extent);
        if (!cross) continue;

        bool dup = false;
        for (const auto& c : crossObs) {
            if (c.axis == axis && c.value == value) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        WallCross wc;
        wc.axis = axis;
        wc.value = value;
        wc.doors = parseDoors(doors);
        if (!wc.doors.empty()) crossObs.push_back(wc);
    }

    for (const auto& co : crossObs) {
        runDoor(context, cx, cy, selectClosestDoor(context, co.doors));
    }
}

// 判断点是否在多边形内（射线法）
static bool pointInPolygon(int px, int py, const std::vector<std::pair<int, int>>& pts)
{
    int n = (int)pts.size();
    if (n < 3) return false;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        int xi = pts[i].first, yi = pts[i].second;
        int xj = pts[j].first, yj = pts[j].second;
        if ((yi > py) != (yj > py)) {
            double x = (double)(xj - xi) * (py - yi) / (yj - yi) + xi;
            if (px < x) inside = !inside;
        }
    }
    return inside;
}

// 处理孤岛（岛外→岛内穿过入口，岛内→岛外穿过出口）
static void crossIslands(MaaContext* context, int cx, int cy, int tx, int ty, const json::value& islands)
{
    if (!islands.is_array()) return;

    for (const auto& island : islands.as_array()) {
        auto ptsJson = island.get("points", json::value());
        if (!ptsJson.is_array()) continue;
        std::vector<std::pair<int, int>> pts;
        for (const auto& p : ptsJson.as_array()) {
            if (!p.is_array() || p.as_array().size() < 2) continue;
            pts.push_back({ p.as_array()[0].as_integer(), p.as_array()[1].as_integer() });
        }
        if (pts.size() < 3) continue;

        bool charInside = pointInPolygon(cx, cy, pts);
        bool targetInside = pointInPolygon(tx, ty, pts);

        if (!charInside && targetInside) {
            runDoor(context, cx, cy, parseDoor(island.get("entrance", json::value())));
        }
        else if (charInside && !targetInside) {
            runDoor(context, cx, cy, parseDoor(island.get("exit", json::value())));
        }
    }
}

// 穿墙/孤岛处理：根据当前地图名查配置，判断是否需要穿墙/登岛。
static void crossObstacle(MaaContext* context, int tx, int ty)
{
    std::string map = getCurrentMap(context);
    auto it = g_mapConfig.find(map);
    if (it == g_mapConfig.end()) return;
    const json::value& cfg = it->second;

    int cx = 0, cy = 0;
    if (!readCharacterCoord(context, cx, cy)) return;

    crossWalls(context, cx, cy, tx, ty, cfg.get("walls", json::value()));
    crossIslands(context, cx, cy, tx, ty, cfg.get("islands", json::value()));
}

// 走到目标点位（含穿墙），返回位置名框。找到返回 true。
bool walkToTargetPoint(
    MaaContext* context,
    int tx,
    int ty,
    const std::string& text,
    MaaRect& outBox,
    int maxAttempts)
{
    crossObstacle(context, tx, ty);

    for (int i = 0; i < maxAttempts; ++i) {
        MaaRect box;
        WalkStep step = walkStep(context, tx, ty, text, box);
        if (step == WalkStep::Found) {
            outBox = box;
            return true;
        }
        if (step == WalkStep::Error) {
            return false;
        }
        // NotFound：继续下一轮滑动
    }
    return false;
}

// 自定义识别：走到目标点位并返回位置名框（只找不点，点击交给外层 task 的 action:Click）。
// custom_recognition_param: { "point":[19,73], "target":"避世树屋", "offset":[-47,29,-46,-2] }
MaaBool findTargetPoint(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_recognition_name,
    const char* custom_recognition_param,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* trans_arg,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    auto param = json::parse(custom_recognition_param ? custom_recognition_param : "{}").value_or(json::value { });
    int tx = 0, ty = 0;
    if (auto pt = param.get("point", json::value()); pt.is_array() && pt.as_array().size() >= 2) {
        tx = pt.as_array()[0].as_integer();
        ty = pt.as_array()[1].as_integer();
    }
    std::string target = param.get("target", json::value("")).as_string();
    int offX = 0, offY = 0, offW = 0, offH = 0;
    if (auto off = param.get("offset", json::value()); off.is_array()) {
        auto& arr = off.as_array();
        if (arr.size() >= 2) {
            offX = arr[0].as_integer();
            offY = arr[1].as_integer();
        }
        if (arr.size() >= 4) {
            offW = arr[2].as_integer();
            offH = arr[3].as_integer();
        }
    }

    MaaRect box;
    if (!walkToTargetPoint(context, tx, ty, target, box, 5)) {
        return false;
    }
    out_box->x = box.x + offX;
    out_box->y = box.y + offY;
    out_box->width = box.width + offW;
    out_box->height = box.height + offH;
    // 外层会在框内随机点击，需保证整个框都落在可操作区域内
    adjustRectToClickable(context, out_box->x, out_box->y, out_box->width, out_box->height);
    return true;
}

// 自定义识别：判断人物是否到达目标点。
// 识别 target 名字 + offset 得到目标框，读取人物世界坐标，判断人物坐标是否在目标框内。
// custom_recognition_param: { "target":"避世树屋", "offset":[-47,29,-46,-2] }
MaaBool arrivedAtTargetPoint(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_recognition_name,
    const char* custom_recognition_param,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* trans_arg,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    auto param = json::parse(custom_recognition_param ? custom_recognition_param : "{}").value_or(json::value { });
    std::string target = param.get("target", json::value("")).as_string();
    if (target.empty()) return false;

    int offX = 0, offY = 0, offW = 0, offH = 0;
    if (auto off = param.get("offset", json::value()); off.is_array()) {
        auto& arr = off.as_array();
        if (arr.size() >= 2) {
            offX = arr[0].as_integer();
            offY = arr[1].as_integer();
        }
        if (arr.size() >= 4) {
            offW = arr[2].as_integer();
            offH = arr[3].as_integer();
        }
    }

    // 读取人物世界坐标
    int cx = 0, cy = 0;
    if (!readCharacterCoord(context, cx, cy)) {
        return false;
    }

    // 识别目标名字 box
    ScreenCap cap(context);
    MaaRect targetBox;
    if (!findTextOnScreen(context, cap.img, target, targetBox)) {
        return false;
    }

    // 算上 offset 后判断人物坐标是否在目标框内
    int boxX = targetBox.x + offX;
    int boxY = targetBox.y + offY;
    int boxW = targetBox.width + offW;
    int boxH = targetBox.height + offH;
    return cx >= boxX && cx <= boxX + boxW && cy >= boxY && cy <= boxY + boxH;
}

// 服务器名归一化：去掉冒号（中英文）和空格/制表符，统一 OCR 结果避免符号差异
std::string normalizeServerName(const std::string& raw)
{
    std::string s = raw;
    // 去掉中文冒号（UTF-8 三字节）
    size_t pos;
    while ((pos = s.find("\xEF\xBC\x9A")) != std::string::npos) {
        s.erase(pos, 3);
    }
    // 去掉英文冒号和空白
    std::string result;
    for (char c : s) {
        if (c == ':' || c == ' ' || c == '\t') {
            continue;
        }
        result += c;
    }
    return result;
}

bool waitArriveMap(MaaContext* context)
{
    return waitUntilRecognitionSuccess(context, "InMap", "{}", nullptr, nullptr);
}

std::string getMapOfRole(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    MaaRect roleBox;
    auto id = MaaContextRunRecognition(context, "RolePos", "{}", cap.img);
    if (id == MaaInvalidId) {
        return { };
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &roleBox, nullptr, nullptr, nullptr);
    if (!hit) {
        return { };
    }
    if (outBox) {
        *outBox = roleBox;
    }
    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"mapName":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})",
        roleBox.x - 250,
        roleBox.y - 10,
        250,
        80);
    auto buffer = MaaStringBufferCreate();
    MaaRect ocrBox;
    id = MaaContextRunRecognition(context, "mapName", ocrJson, cap.img);
    if (id == MaaInvalidId) {
        MaaStringBufferDestroy(buffer);
        return { };
    }
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &ocrBox, buffer, nullptr, nullptr);
    if (!hit) {
        MaaStringBufferDestroy(buffer);
        return { };
    }
    std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
    MaaStringBufferDestroy(buffer);
    auto j = json::parse(detail).value_or(json::value { });
    if (auto& best = j["best"]; best.is_object()) {
        return best["text"].as_string();
    }
    return { };
}

// 读当前所在地图名（通过 CurrentSupportMap 的 InXXX 节点判断）。
std::string getCurrentMap(MaaContext* context)
{
    static const std::vector<std::pair<const char*, const char*>> kMapInTable = {
        { "InIsleOfTimesEnd", "时之尽头的孤岛" },
        { "InCrescentCounty", "新月郡" },
        { "InGremid", "格雷米德" },
        { "InFurnaceCity", "熔炉之城" },
        { "InWildBullValley", "蛮牛谷" },
        { "InSilverMoonValley", "银月谷" },
        { "InIserel", "伊瑟瑞尔" },
    };
    ScreenCap cap(context);
    for (const auto& [inNode, mapName] : kMapInTable) {
        if (doRecognition(context, cap.img, inNode, "{}", nullptr, nullptr)) {
            return mapName;
        }
    }
    return { };
}

std::string getMapOfFerdinand(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    MaaRect box;
    std::string result;
    if (doRecognition(context, cap.img, "MapOfFerdinand", "{}", outBox ? outBox : &box, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        // MapOfFerdinand 是 And 识别，detail 是子结果数组，取第二个（OCR）的文本
        if (j.is_array() && j.as_array().size() >= 2) {
            auto& ocrDetail = j.as_array()[1]["detail"];
            if (auto& best = ocrDetail["best"]; best.is_object()) {
                result = best["text"].as_string();
            }
            else if (auto& all = ocrDetail["all"]; all.is_array() && !all.empty()) {
                result = all[0]["text"].as_string();
            }
        }
    }
    MaaStringBufferDestroy(buf);
    return result;
}

json::value getCurMapList(MaaContext* context)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    auto detailBuf = MaaStringBufferCreate();
    MaaRect outBox;
    auto id = MaaContextRunRecognition(context, "CurMapList", "{}", cap.img);
    if (id == MaaInvalidId) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &outBox, detailBuf, nullptr, nullptr);
    if (!hit) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detailStr(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto parsed = json::parse(detailStr);
    if (!parsed || !parsed->exists("all")) {
        return { };
    }
    return (*parsed)["all"];
}

void exitToCampfire(MaaContext* context)
{
    MaaContextRunTask(context, "SceneEnterMainPage", "{}");
}

bool enterMapByName(MaaContext* context, const std::string& mapName)
{
    auto entries = getCurMapList(context);
    if (!entries.is_array()) {
        return false;
    }
    for (auto& item : entries.as_array()) {
        std::string text = item["text"].as_string();
        if (text.find(mapName) != std::string::npos) {
            auto& box = item.as_object().at("box");
            int bx = box[0].as_integer(), by = box[1].as_integer();
            clickRandomTarget(context, bx, by, box[2].as_integer(), box[3].as_integer());
            waitArriveMap(context);
            return true;
        }
    }
    return false;
}


void setNextNode(MaaContext* context, const char* nodeName, const char* nextNode)
{
    auto nextList = MaaStringListBufferCreate();
    auto nextBuf = MaaStringBufferCreate();
    MaaStringBufferSet(nextBuf, nextNode);
    MaaStringListBufferAppend(nextList, nextBuf);
    MaaStringBufferDestroy(nextBuf);
    MaaContextOverrideNext(context, nodeName, nextList);
    MaaStringListBufferDestroy(nextList);
}

// ──── 食谱配置（硬编码） ────

static std::map<std::string, std::vector<std::string>> s_recipeConfig;
static std::unordered_set<std::string> s_recipeNames;

void loadRecipeConfig()
{
    if (!s_recipeConfig.empty()) {
        return;
    }
    s_recipeConfig = {
        { "黏糊糊的食物", { "肉", "水果" } },
        { "面包", { "麦粉", "发酵粉" } },
        { "混合蔬菜浓汤", { "野菜", "干净的水", "盐" } },
        { "果汁", { "水果", "干净的水" } },
        { "腌菜", { "野菜", "盐" } },
        { "炸鱼", { "鱼", "橄榄油" } },
        { "蘑菇浓汤", { "野菜", "干净的水" } },
        { "炖肉", { "肉", "干净的水" } },
        { "虾饼", { "麦粉", "虾" } },
        { "腌制鱼肉香肠", { "鱼", "盐" } },
        { "鱼汤", { "鱼", "干净的水" } },
        { "野菜沙拉", { "野菜", "乳酪" } },
        { "鸟肉串", { "肉", "野菜" } },
        { "肉丸子", { "麦粉", "肉" } },
        { "风干香肠", { "肉", "鱼", "盐" } },
        { "饼干", { "麦粉", "牛奶" } },
        { "蒸虾", { "虾", "盐" } },
        { "肉酱", { "肉", "盐" } },
        { "鱼肉沙拉", { "鱼", "乳酪" } },
        { "水果布丁", { "水果", "干净的水", "牛奶" } },
        { "发酵饮品", { "麦粉", "发酵粉", "干净的水" } },
        { "炒野菜", { "肉", "野菜", "盐" } },
        { "水果酿", { "水果", "发酵粉" } },
        { "荧果肉酱", { "肉", "盐", "荧果" } },
        { "炸鸡", { "肉", "发酵粉", "盐" } },
        { "胡萝卜面包", { "麦粉", "野菜" } },
        { "胡萝卜汁", { "砂糖", "野菜" } },
        { "水果馅饼", { "麦粉", "水果", "盐" } },
        { "鸟肉馅饼", { "麦粉", "肉", "盐" } },
        { "牛奶面包", { "麦粉", "发酵粉", "牛奶" } },
        { "煎肉排", { "肉", "橄榄油" } },
        { "乳酪馅饼", { "麦粉", "乳酪" } },
        { "鲜虾野菜汤", { "野菜", "虾", "干净的水" } },
        { "烤鸟肉沙拉", { "肉", "水果", "乳酪" } },
        { "海鲜浓汤", { "鱼", "干净的水", "盐" } },
        { "鱼肉馅饼", { "麦粉", "鱼", "发酵粉" } },
        { "海陆烤肉拼盘", { "肉", "鱼" } },
        { "乱炖", { "肉", "乳酪" } },
    };
    for (auto& [name, _] : s_recipeConfig) {
        s_recipeNames.insert(name);
    }
}

const std::unordered_set<std::string>& getRecipeNames()
{
    loadRecipeConfig();
    return s_recipeNames;
}

const std::map<std::string, std::vector<std::string>>& getRecipeConfig()
{
    loadRecipeConfig();
    return s_recipeConfig;
}

void switchMapSort(MaaContext* context, const std::string& targetOrder)
{
    auto* tasker = MaaContextGetTasker(context);
    ScreenCap cap(context);
    auto buffer = MaaStringBufferCreate();
    MaaRect sortBox;
    auto id = MaaContextRunRecognition(context, "MapListSortCheck", "{}", cap.img);
    if (id != MaaInvalidId) {
        MaaBool hit = false;
        MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &sortBox, buffer, nullptr, nullptr);
        if (hit) {
            std::string detail(MaaStringBufferGet(buffer), MaaStringBufferSize(buffer));
            auto j = json::parse(detail).value_or(json::value { });
            std::string text = j["best"]["text"].as_string();
            if (text.find(targetOrder) == std::string::npos) {
                clickRandomTarget(context, sortBox.x, sortBox.y, sortBox.width, sortBox.height);
                postWaitFreezes(context, ScreenArea_MidCenter, 300);
            }
        }
    }
    MaaStringBufferDestroy(buffer);
}

void postWaitFreezes(MaaContext* context, uint32_t areaMask, int waitTime, int timeout)
{
    char screen[64];
    snprintf(screen, sizeof(screen), R"({"timeout": %d})", timeout);
    for (int i = 0; i < 9; ++i) {
        if (areaMask & (1u << i)) {
            MaaContextWaitFreezes(context, waitTime, &ScreenAreaBox[i], screen);
        }
    }
}

void postWaitFreezes(MaaContext* context, MaaRect roi, int waitTime, int timeout)
{
    char screen[64];
    snprintf(screen, sizeof(screen), R"({"timeout": %d})", timeout);
    MaaContextWaitFreezes(context, waitTime, &roi, screen);
}

std::unordered_map<std::string, MapIndex> mapTable = {
    { "时之尽头的孤岛", MapIndex::IsleOfTimesEnd }, { "新月郡", MapIndex::CrescentCounty }, { "圣城修道院", MapIndex::HolyCityMonastery },
    { "格雷米德", MapIndex::Map_Gremid },           { "熔炉之城", MapIndex::FurnaceCity },
};

std::string getMapName(MapIndex idx)
{
    switch (idx) {
    case MapIndex::IsleOfTimesEnd:
        return "时之尽头的孤岛";
    case MapIndex::CrescentCounty:
        return "新月郡";
    case MapIndex::HolyCityMonastery:
        return "圣城修道院";
    case MapIndex::Map_Gremid:
        return "格雷米德";
    case MapIndex::FurnaceCity:
        return "熔炉之城";
    default:
        return "";
    }
}

json::value getDefaultSoulUpgradeConfig()
{
    static json::value s_cached;
    if (!s_cached.is_null()) {
        return s_cached;
    }
    std::ifstream cf("./resources/config/monster_soul_config.json");
    if (!cf.is_open()) {
        return json::object { };
    }
    std::string content((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
    auto root = json::parse(content).value_or(json::object { });
    s_cached = root["upgrade_configs"]["默认配置"];
    return s_cached;
}

// ──── 清除节点命中计数（max_hit）────
// custom_action_param 示例：{"nodes":["NodeA","NodeB"],"strict":false}
// strict 为 true 时，任一节点清除失败则整个 action 返回失败；默认 false。
MaaBool ClearHitCountAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto param = json::parse(std::string(custom_action_param ? custom_action_param : "")).value_or(json::value { });

    auto nodes = param.get("nodes", json::value { });
    if (!nodes.is_array() || nodes.as_array().empty()) {
        LogUtils::error("清除节点命中计数错误:未指定节点");
        return false;
    }

    bool strict = param.get("strict", json::value(false)).as_boolean();
    bool hasFailure = false;

    for (const auto& item : nodes.as_array()) {
        std::string name = item.as_string();
        if (name.empty()) {
            LogUtils::warn("清除节点命中计数错误: nodes 含空节点名");
            hasFailure = true;
            continue;
        }

        if (!MaaContextClearHitCount(context, name.c_str())) {
            LogUtils::error(fmt("清除节点命中计数错误: 清除 %1 命中计数失败", name));
            hasFailure = true;
            continue;
        }
    }

    if (hasFailure && strict) {
        return false;
    }
    return true;
}

MaaBool FailTaskAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    return false;
}

// 读取指定节点 merge 后的 attach（JSON 对象），供自定义动作/识别读配置
json::value getNodeAttach(MaaContext* context, const char* nodeName)
{
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return json::object { };
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return json::object { };
    }
    const auto& attach = (*node)["attach"];
    return attach.is_object() ? attach : json::value(json::object { });
}

// 全局用户配置（SaveConfig 写入，战斗/魔魂升级读取）
UserBattleConfig g_user_battle;
SoulUpgradeConfig g_user_soul;

// 把节点 attach 解析进全局结构体
void parseUserConfig(const json::value& attach)
{
    auto parseDouble = [](const std::string& s, double def) -> double {
        try {
            size_t pos = 0;
            double v = std::stod(s, &pos);
            return pos > 0 ? v : def;
        }
        catch (...) {
            return def;
        }
    };

    // 战斗配置
    g_user_battle = UserBattleConfig { };
    g_user_battle.reserve = attach.get("reserve", json::value(0)).as_integer();
    g_user_battle.cryInterval = attach.get("cry_interval", json::value(0)).as_integer();
    g_user_battle.burstSeconds = attach.get("burst_seconds", json::value(0)).as_integer();
    g_user_battle.firstSkillTime = attach.get("first_skill_time", json::value(0)).as_integer();
    std::string skill = attach.get("skill", json::value("None")).as_string();
    g_user_battle.skill = (skill == "Xiaoqing") ? Skill_Xiaoqing : Skill_None;
    std::string cryColumn = attach.get("cry_column", json::value("0")).as_string();
    g_user_battle.cryColumn = (cryColumn == "Column1") ? 1 : (cryColumn == "Column3") ? 3 : (cryColumn == "Column2") ? 2 : 0;

    // 魔魂升级配置
    g_user_soul = SoulUpgradeConfig { };
    g_user_soul.countMode = (attach.get("countMode", json::value("0")).as_string() == "1") ? 1 : 0;
    g_user_soul.count = attach.get("count", json::value(0)).as_integer();

    g_user_soul.soul_yexing = attach.get("soul_yexing", json::value(false)).as_boolean();
    g_user_soul.soul_jielue = attach.get("soul_jielue", json::value(false)).as_boolean();
    g_user_soul.soul_wugu = attach.get("soul_wugu", json::value(false)).as_boolean();
    g_user_soul.soul_baolue = attach.get("soul_baolue", json::value(false)).as_boolean();
    g_user_soul.soul_xiongsha = attach.get("soul_xiongsha", json::value(false)).as_boolean();
    g_user_soul.soul_xiedian = attach.get("soul_xiedian", json::value(false)).as_boolean();
    g_user_soul.soul_jiaozhi = attach.get("soul_jiaozhi", json::value(false)).as_boolean();
    g_user_soul.soul_manhuang = attach.get("soul_manhuang", json::value(false)).as_boolean();
    g_user_soul.soul_xiongshi = attach.get("soul_xiongshi", json::value(false)).as_boolean();
    g_user_soul.soul_kuangnu = attach.get("soul_kuangnu", json::value(false)).as_boolean();
    g_user_soul.soul_shuangxue = attach.get("soul_shuangxue", json::value(false)).as_boolean();
    g_user_soul.soul_bingjing = attach.get("soul_bingjing", json::value(false)).as_boolean();
    g_user_soul.soul_linye = attach.get("soul_linye", json::value(false)).as_boolean();
    g_user_soul.soul_lingdong = attach.get("soul_lingdong", json::value(false)).as_boolean();
    g_user_soul.soul_jihan = attach.get("soul_jihan", json::value(false)).as_boolean();
    g_user_soul.soul_baopo = attach.get("soul_baopo", json::value(false)).as_boolean();
    g_user_soul.soul_chenyuan = attach.get("soul_chenyuan", json::value(false)).as_boolean();
    g_user_soul.soul_kuangre = attach.get("soul_kuangre", json::value(false)).as_boolean();
    g_user_soul.soul_tianyun = attach.get("soul_tianyun", json::value(false)).as_boolean();
    g_user_soul.soul_xianzu = attach.get("soul_xianzu", json::value(false)).as_boolean();

    g_user_soul.retention_group1_enabled = attach.get("retention_group1_enabled", json::value(false)).as_boolean();
    g_user_soul.retention_group2_enabled = attach.get("retention_group2_enabled", json::value(false)).as_boolean();
    g_user_soul.retention_group3_enabled = attach.get("retention_group3_enabled", json::value(false)).as_boolean();

    g_user_soul.g1_interval = attach.get("g1_interval", json::value(false)).as_boolean();
    g_user_soul.g1_damage = attach.get("g1_damage", json::value(false)).as_boolean();
    g_user_soul.g1_resist = attach.get("g1_resist", json::value(false)).as_boolean();
    g_user_soul.group1_interval = parseDouble(attach.get("group1_interval", json::value("0")).as_string(), 0.0);
    g_user_soul.group1_damage = parseDouble(attach.get("group1_damage", json::value("0")).as_string(), 0.0);
    g_user_soul.group1_resist = parseDouble(attach.get("group1_resist", json::value("0")).as_string(), 0.0);

    g_user_soul.g2_interval = attach.get("g2_interval", json::value(false)).as_boolean();
    g_user_soul.g2_crit = attach.get("g2_crit", json::value(false)).as_boolean();
    g_user_soul.group2_interval = parseDouble(attach.get("group2_interval", json::value("0")).as_string(), 0.0);
    g_user_soul.group2_crit = parseDouble(attach.get("group2_crit", json::value("0")).as_string(), 0.0);

    g_user_soul.g3_interval = attach.get("g3_interval", json::value(false)).as_boolean();
    g_user_soul.g3_heal = attach.get("g3_heal", json::value(false)).as_boolean();
    g_user_soul.group3_interval = parseDouble(attach.get("group3_interval", json::value("0")).as_string(), 0.0);
    g_user_soul.group3_heal = parseDouble(attach.get("group3_heal", json::value("0")).as_string(), 0.0);
}

// SaveConfig 动作：读取当前节点 attach 并解析进全局结构体
static MaaBool SaveConfigAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    parseUserConfig(getNodeAttach(context, node_name));
    return true;
}

// 判断节点是否 enabled（拿不到数据或字段时默认 enabled）
static bool isNodeEnabled(MaaContext* context, const char* nodeName)
{
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return true;
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return true;
    }
    auto enabled = (*node)["enabled"];
    return enabled.is_boolean() ? enabled.as_boolean() : true;
}

// 获取节点 focus 里的中文名称（去掉 "开始任务:" 前缀），拿不到返回空串
static std::string getNodeFocusDesc(MaaContext* context, const char* nodeName)
{
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return { };
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return { };
    }
    auto focus = (*node)["focus"];
    if (!focus.is_object()) {
        return { };
    }
    for (auto& [event, msg] : focus.as_object()) {
        std::string text = msg.as_string();
        const std::string prefix = "开始任务:";
        if (text.rfind(prefix, 0) == 0) {
            text = text.substr(prefix.size());
        }
        return text;
    }
    return { };
}

// 运行若干子任务（entry），子任务列表由 custom_action_param.sub 传入。
// continue: 某子任务失败后是否继续跑剩余子任务（默认 false，立即停止）
// strict:   有子任务失败时是否判定整个动作失败（默认 true）
MaaBool SubTaskAct(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    auto param = json::parse(custom_action_param ? custom_action_param : "{}");
    if (!param || !param->contains("sub")) {
        LogUtils::error("SubTask: custom_action_param.sub 缺失");
        return false;
    }
    auto sub = (*param)["sub"].as_array();
    if (sub.empty()) {
        LogUtils::error("SubTask: custom_action_param.sub 为空");
        return false;
    }

    bool continue_on_failure = (*param).get("continue", json::value(false)).as_boolean();
    bool strict = (*param).get("strict", json::value(true)).as_boolean();

    auto* tasker = MaaContextGetTasker(context);
    bool has_failure = false;

    for (auto& item : sub) {
        std::string name = item.as_string();
        if (name.empty()) {
            has_failure = true;
            if (!continue_on_failure) {
                break;
            }
            continue;
        }

        // 跳过已禁用的子任务
        if (!isNodeEnabled(context, name.c_str())) {
            continue;
        }

        MaaTaskId sub_id = MaaContextRunTask(context, name.c_str(), "{}");
        if (sub_id == MaaInvalidId) {
            std::string desc = getNodeFocusDesc(context, name.c_str());
            LogUtils::warn(fmt("任务失败:%1", desc.empty() ? name : desc), false);
            has_failure = true;
            if (!continue_on_failure) {
                break;
            }
            continue;
        }

        MaaStatus status = MaaStatus_Invalid;
        MaaSize node_id_list_size = 0;
        MaaTaskerGetTaskDetail(tasker, sub_id, nullptr, nullptr, &node_id_list_size, &status);
        if (status != MaaStatus_Succeeded) {
            std::string desc = getNodeFocusDesc(context, name.c_str());
            LogUtils::warn(fmt("任务失败:%1", desc.empty() ? name : desc), false);
            has_failure = true;
            if (!continue_on_failure) {
                break;
            }
        }
    }

    return !(has_failure && strict);
}

// 判断角色是否在目标地图（map_name 通过 custom_recognition_param 或 attach 传入）。
// 逻辑：getMapOfRole 拿角色地图 + box，比对 map_name；同一地图则运行 MapListSwipePos（改 roi）识别两次。
MaaBool checkRoleInTargetMap(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_recognition_name,
    const char* custom_recognition_param,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* trans_arg,
    MaaRect* out_box,
    MaaStringBuffer* out_detail)
{
    std::string targetMap;
    bool resetFlag = true;
    auto param = json::parse(custom_recognition_param ? custom_recognition_param : "{}").value_or(json::value { });
    targetMap = param.get("map_name", json::value("")).as_string();

    // custom_recognition_param 未指定时，从节点 attach 读取 map_name，便于公共节点参数化
    if (targetMap.empty()) {
        resetFlag = getNodeAttach(context, node_name).get("reset_flag", json::value(true)).as_boolean();
        targetMap = getNodeAttach(context, node_name).get("map_name", json::value("")).as_string();
    }

    if (targetMap.empty()) {
        LogUtils::error("重置地图: 未指定地图名称");
        return false;
    }

    MaaRect roleBox { 0, 0, 0, 0 };
    std::string mapOfRole = getMapOfRole(context, &roleBox);
    if (mapOfRole.empty()) {
        return false;
    }

    // map_name 支持 "a|b|c" 多选，任一命中即通过
    bool matched = false;
    for (auto& alt : split(targetMap, '|')) {
        if (mapOfRole.find(alt) != std::string::npos) {
            matched = true;
            break;
        }
    }
    if (!matched) {
        return false;
    }

    if (!resetFlag) {
        return true;
    }

    // 注意：MapListItemOcr / MapListItemTemplate 是叶子识别节点，roi 覆盖才对它们生效
    // （MapListSwipePos 是 Or 节点，覆盖 roi 会被忽略）。
    char detailJson[256];
    int boxY = roleBox.y;

    // 角色上方区域
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemOcr":{"roi":[125,225,80,%d]}})", boxY - 280);
    if (doRecognition(context, image, "MapListItemOcr", detailJson, out_box, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemTemplate":{"roi":[125,225,80,%d]}})", boxY - 280);
    if (doRecognition(context, image, "MapListItemTemplate", detailJson, out_box, nullptr)) {
        return true;
    }

    // 角色下方区域
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemOcr":{"roi":[125,%d,80,%d]}})", boxY + 100, 880 - boxY);
    if (doRecognition(context, image, "MapListItemOcr", detailJson, out_box, nullptr)) {
        return true;
    }
    snprintf(detailJson, sizeof(detailJson), R"({"MapListItemTemplate":{"roi":[125,%d,80,%d]}})", boxY + 100, 880 - boxY);
    if (doRecognition(context, image, "MapListItemTemplate", detailJson, out_box, nullptr)) {
        return true;
    }

    return false;
}

static uint32_t g_shopMask = 0;        // 已找到（购买完成）的商品位掩码
static uint32_t g_shopEnabledMask = 0; // 启用购买的商品位掩码（由配置开关决定）
MaaBool shopAssistant(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = getNodeAttach(context, nodeName);
    bool initFlag = attach.get("initFlag", json::value(false)).as_boolean();

    if (initFlag) {
        // 初始化：清空状态，读取各商品开关
        g_shopMask = 0;
        g_shopEnabledMask = 0;
        for (auto& item : shopItemList) {
            if (attach.get(item.enabledKey, json::value(false)).as_boolean()) {
                g_shopEnabledMask |= (1U << item.bit);
            }
        }
    }
    // 每次调用都累加已找到的商品
    for (auto& item : shopItemList) {
        if (attach.get(item.foundKey, json::value(false)).as_boolean()) {
            g_shopMask |= (1U << item.bit);
        }
    }
    return true;
}

MaaBool shopFinishCheck(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    const uint32_t targetMask = g_shopEnabledMask;
    if (targetMask == 0) {
        return true; // 未启用任何商品，视为完成
    }
    return (g_shopMask & targetMask) == targetMask;
}

MaaBool itemNotBought(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    auto param = json::parse(customRecognitionParam ? customRecognitionParam : "{}").value_or(json::value { });
    std::string foundKey = param.get("foundKey", json::value("")).as_string();
    if (foundKey.empty()) {
        LogUtils::error("ItemNotBought: 缺少 custom_recognition_param.foundKey");
        return false;
    }

    for (auto& item : shopItemList) {
        if (foundKey == item.foundKey) {
            return (g_shopMask & (1U << item.bit)) == 0;
        }
    }

    LogUtils::error(fmt("ItemNotBought: 未找到商品 %1", foundKey));
    return false;
}

// ──── 马车商店滑动购买 ────
// 读货币数量与商品单价，滑到最大实测滑轨两端并读最大数量，算出购买数量后线性插值点出目标，再用 +/- 模板按钮微调。
// 注意：商品点击由外部 pipeline 节点完成，进入此 action 时购买弹窗必须已打开。
// custom_action_param：
//   currencyCount   货币数量 OCR 节点名（可为普通 OCR 或 And 节点）—— 必填
//   unitPrice       商品单价 OCR 节点名（默认 CarriageShopSlideBuyUnitPrice）
//   slider          滑块(thumb)识别节点名（默认 SliderFound）
//   increaseButton  + 按钮 TemplateMatch 节点名（默认 CarriageShopIncreaseButton）
//   decreaseButton  - 按钮 TemplateMatch 节点名（默认 CarriageShopDecreaseButton）
//   quantityOffset  数量框相对 thumb box 的偏移 [dx,dy,dw,dh]（默认 [-15,35,25,0]）
//   canBuyCheck     可购买校验 task 名（默认 CarriageShopCanBuyAfterSlide）

// 读取定长 int 数组，缺省用 def 补齐
static std::vector<int> readIntArray(const json::value& v, std::vector<int> def)
{
    if (!v.is_array()) {
        return def;
    }
    auto& arr = v.as_array();
    std::vector<int> out = def;
    for (size_t i = 0; i < arr.size() && i < out.size(); ++i) {
        out[i] = arr[i].as_integer();
    }
    return out;
}

// 递归收集 OCR detail 中的所有文本和框（含 And 节点子结果的递归）
static void collectOcrTexts(const json::value& v, std::vector<OcrText>& out)
{
    auto append = [&](const json::value& obj, const std::string& t) {
        if (t.empty()) return;
        OcrText r;
        r.text = t;
        r.box = { 0, 0, 0, 0 };
        if (obj.contains("box")) {
            const auto& b = obj.at("box");
            if (b.is_array() && b.as_array().size() >= 4) {
                const auto& arr = b.as_array();
                r.box = { arr[0].as_integer(), arr[1].as_integer(), arr[2].as_integer(), arr[3].as_integer() };
            }
        }
        out.push_back(r);
    };

    if (v.is_object()) {
        const auto& obj = v.as_object();
        size_t before = out.size();

        // all / filtered：结果数组，收集每个的 text + box（优先，含框）
        for (const char* key : { "all", "filtered" }) {
            if (obj.contains(key)) {
                const auto& arr = obj.at(key);
                if (arr.is_array()) {
                    for (const auto& item : arr.as_array()) {
                        if (item.is_object() && item.as_object().contains("text")) {
                            append(item, item.as_object().at("text").as_string());
                        }
                    }
                }
            }
        }

        // best：all/filtered 都没取到文本时才用（可能无 box）
        if (out.size() == before && obj.contains("best")) {
            const auto& best = obj.at("best");
            if (best.is_object() && best.as_object().contains("text")) {
                append(best, best.as_object().at("text").as_string());
            }
        }

        // 递归子对象（And 节点 detail 是子结果数组）
        for (const auto& [k, child] : obj) {
            collectOcrTexts(child, out);
        }
    }
    else if (v.is_array()) {
        for (const auto& child : v.as_array()) {
            collectOcrTexts(child, out);
        }
    }
}

// 运行指定识别节点（OCR/And），返回识别到的所有文本和框。
// subName 非空时只取 And 节点中 name==subName 的子结果。
static std::vector<OcrText> readTextFromNode(MaaContext* context, const MaaImageBuffer* img, const char* nodeName, const char* subName)
{
    auto detailBuf = MaaStringBufferCreate();
    MaaRect box;
    if (!doRecognition(context, img, nodeName, "{}", &box, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return { };
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);

    auto j = json::parse(detail).value_or(json::value { });
    const json::value* target = &j;
    if (subName && subName[0] != '\0') {
        if (!j.is_array()) {
            return { };
        }
        target = nullptr;
        for (const auto& sub : j.as_array()) {
            if (sub.is_object() && sub.as_object().contains("name")) {
                if (sub.as_object().at("name").as_string() == subName) {
                    target = &sub;
                    break;
                }
            }
        }
        if (!target) {
            return { };
        }
    }

    std::vector<OcrText> out;
    collectOcrTexts(*target, out);
    return out;
}

// 从文本列表里取第一个数字
static bool extractNumberFromTexts(const std::vector<OcrText>& texts, int& outValue)
{
    for (const auto& r : texts) {
        std::string digits;
        for (char c : r.text) {
            if (c >= '0' && c <= '9') {
                digits += c;
            }
            else if (!digits.empty()) {
                break;
            }
        }
        if (!digits.empty()) {
            outValue = std::atoi(digits.c_str());
            return true;
        }
    }
    return false;
}

// 从 OCR 识别 detail(json) 中提取数字。subName 非空时定位 And 节点中 name==subName 的子结果
static bool extractNumberFromDetail(const std::string& detail, const char* subName, int& outValue)
{
    auto j = json::parse(detail);
    if (!j) {
        return false;
    }

    const json::value* target = &(*j);
    if (subName && subName[0] != '\0') {
        if (!j->is_array()) {
            return false;
        }
        target = nullptr;
        for (const auto& sub : j->as_array()) {
            if (sub.is_object() && sub.as_object().contains("name")) {
                if (sub.as_object().at("name").as_string() == subName) {
                    target = &sub;
                    break;
                }
            }
        }
        if (!target) {
            LogUtils::error(fmt("extractNumberFromDetail: 未找到子结果 %1", subName));
            return false;
        }
    }

    std::vector<OcrText> texts;
    collectOcrTexts(*target, texts);
    if (!extractNumberFromTexts(texts, outValue)) {
        outValue = 0;
    }
    return true;
}

// 运行指定识别节点(OCR/And)，提取其中的数字。subName 非空时从 And 节点指定子结果读取
static bool readNumberFromNode(MaaContext* context, const char* nodeName, const char* subName, int& outValue)
{
    ScreenCap cap(context);
    auto texts = readTextFromNode(context, cap.img, nodeName, subName);
    if (texts.empty()) {
        return false;
    }
    extractNumberFromTexts(texts, outValue);
    return true;
}

// 直接 OCR 指定 roi 中的数字
static bool readNumberFromRoi(MaaContext* context, const MaaRect& roi, int& outValue)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    std::string param = fmt(R"({"roi":[%1,%2,%3,%4],"only_rec":true})", roi.x, roi.y, roi.width, roi.height);
    auto id = MaaContextRunRecognitionDirect(context, "OCR", param.c_str(), cap.img);
    if (id == MaaInvalidId) {
        return false;
    }
    auto detailBuf = MaaStringBufferCreate();
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &box, detailBuf, nullptr, nullptr);
    if (!hit) {
        MaaStringBufferDestroy(detailBuf);
        return false;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    return extractNumberFromDetail(detail, nullptr, outValue);
}

// 精确点击指定坐标
static void clickPoint(MaaContext* context, int x, int y)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, x, y);
    MaaControllerWait(ctrl, id);
}

// 运行指定 task 并判断是否成功
static bool runTaskSuccess(MaaContext* context, const char* taskName)
{
    auto* tasker = MaaContextGetTasker(context);
    MaaTaskId id = MaaContextRunTask(context, taskName, "{}");
    if (id == MaaInvalidId) {
        return false;
    }
    MaaStatus status = MaaStatus_Invalid;
    MaaSize nodeCount = 0;
    MaaTaskerGetTaskDetail(tasker, id, nullptr, nullptr, &nodeCount, &status);
    return status == MaaStatus_Succeeded;
}

// 从一点滑动到另一点
static void swipeTo(MaaContext* context, int x1, int y1, int x2, int y2, int duration)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostSwipe(ctrl, x1, y1, x2, y2, duration);
    MaaControllerWait(ctrl, id);
}

// 运行识别节点，取识别框
static bool findNodeBox(MaaContext* context, const char* nodeName, MaaRect& outBox)
{
    ScreenCap cap(context);
    return doRecognition(context, cap.img, nodeName, "{}", &outBox, nullptr);
}

// 读取 handle 相对位置的数量（数量框 = handle box + qOffset）
static bool readQuantityAtHandle(MaaContext* context, const MaaRect& handleBox, const std::vector<int>& qOffset, int& out)
{
    MaaRect qb;
    qb.x = handleBox.x + qOffset[0];
    qb.y = handleBox.y + qOffset[1];
    qb.width = handleBox.width + qOffset[2];
    qb.height = handleBox.height + qOffset[3];
    return readNumberFromRoi(context, qb, out);
}

// 识别按钮(模板)并点击中心
static bool findAndClickButton(MaaContext* context, const char* buttonNode)
{
    MaaRect btnBox;
    if (!findNodeBox(context, buttonNode, btnBox)) {
        return false;
    }
    clickPoint(context, btnBox.x + btnBox.width / 2, btnBox.y + btnBox.height / 2);
    return true;
}

static MaaBool CarriageShopSlideBuyAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto param = json::parse(customActionParam ? customActionParam : "{}").value_or(json::value { });

    std::string sliderNode = param.get("slider", json::value("SliderFound")).as_string();
    std::string currencyNode = param.get("currencyCount", json::value("")).as_string();
    std::string currencyNodeSub = param.get("currencyCountSub", json::value("")).as_string();
    std::string unitPriceNode = param.get("unitPrice", json::value("CarriageShopSlideBuyUnitPrice")).as_string();
    std::string increaseBtn = param.get("increaseButton", json::value("CarriageShopIncreaseButton")).as_string();
    std::string decreaseBtn = param.get("decreaseButton", json::value("CarriageShopDecreaseButton")).as_string();
    std::string canBuyCheck = param.get("canBuyCheck", json::value("CarriageShopCanBuyAfterSlide")).as_string();
    auto qOffset = readIntArray(param.get("quantityOffset", json::value()), { -15, 35, 25, 0 });

    // 商品已由外部节点点击打开购买弹窗，这里直接识别数量滑块,未识别到滑块由外部重新点击
    MaaRect thumbBox;
    MaaRect PurchaseButtonBox;
    ScreenCap cap(context);
    bool slideFound = findNodeBox(context, sliderNode.c_str(), thumbBox);
    bool PurchaseButtonFound = findNodeBox(context, "PurchaseButton", PurchaseButtonBox);
    
    if (!slideFound && !PurchaseButtonFound) {
        return false;
    }

    if (!slideFound) {
        return true;
    }

    if (currencyNode.empty()) {
        LogUtils::error("CarriageShopSlideBuy: 缺少 currencyCount 配置");
        return false;
    }

    // 读货币数量（currencyCount 节点可为普通 OCR 或 And 节点，readNumberFromNode 均能解析）
    int currency = 0;
    if (!readNumberFromNode(context, currencyNode.c_str(), currencyNodeSub.c_str(), currency)) {
        return true;
    }

    // 读商品单价（在弹窗中识别）
    int unitPrice = 0;
    if (!readNumberFromNode(context, unitPriceNode.c_str(), "UnitPrice", unitPrice) || unitPrice <= 0) {
        LogUtils::error("CarriageShopSlideBuy: 读取单价失败");
        return false;
    }

    // 记录滑轨起点，滑到最大
    MaaRect startBox = thumbBox;
    int startX = startBox.x + startBox.width / 2;
    int startY = startBox.y + startBox.height / 2;
    swipeTo(context, startX, startY, startX + 600, startY, 200); // 向右滑 600px 到最大
    Sleep(500);

    // 识别滑轨终点，读最大数量
    MaaRect endBox;
    if (!findNodeBox(context, sliderNode.c_str(), endBox)) {
        endBox = startBox;
    }
    int maxQuantity = 0;
    if (!readQuantityAtHandle(context, endBox, qOffset, maxQuantity) || maxQuantity < 1) {
        LogUtils::error("CarriageShopSlideBuy: 读取最大数量失败");
        return false;
    }

    // 计算购买数量（受货币数量限制，最大数量已从滑轨实际测得）
    int buyCount = (std::min)(maxQuantity, currency / unitPrice);
    if (buyCount <= 0) {
        return true;
    }

    // 目标 >= 最大，已滑到最大，直接返回（pipeline 后续点购买）
    if (buyCount >= maxQuantity) {
        return true;
    }

    // 线性插值（滑块最小是 1，整体 -1）
    int endX = endBox.x + endBox.width / 2;
    int endY = endBox.y + endBox.height / 2;
    int clickX = startX + (endX - startX) * (buyCount - 1) / (maxQuantity - 1);
    int clickY = startY + (endY - startY) * (buyCount - 1) / (maxQuantity - 1);
    clickPoint(context, clickX, clickY);
    Sleep(500);

    // 读实际数量并用 +/- 模板按钮微调
    const int maxRetry = 10;
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return true;
        }
        MaaRect curBox;
        if (!findNodeBox(context, sliderNode.c_str(), curBox)) {
            curBox = endBox;
        }
        int actual = 0;
        if (!readQuantityAtHandle(context, curBox, qOffset, actual)) {
            clickPoint(context, clickX, clickY); // 读不到数量，重新点滑块
            Sleep(500);
            continue;
        }

        int diff = buyCount - actual;
        if (diff == 0) {
            break; // 命中目标数量
        }

        const char* btnNode = diff > 0 ? increaseBtn.c_str() : decreaseBtn.c_str();
        int steps = diff > 0 ? diff : -diff;
        if (steps > 30) {
            steps = 30; // 防误识别点太多
        }
        for (int i = 0; i < steps; ++i) {
            findAndClickButton(context, btnNode);
            Sleep(200);
        }
        Sleep(300); // 等界面更新，下一轮重新校验
    }

    // 校验当前数量是否可购买，不够就点 - 递减
    if (!canBuyCheck.empty()) {
        for (int attempt = 0; attempt < maxRetry; ++attempt) {
            if (MaaTaskerStopping(MaaContextGetTasker(context))) {
                return true;
            }
            if (runTaskSuccess(context, canBuyCheck.c_str())) {
                return true; // 可购买
            }
            MaaRect curBox;
            if (!findNodeBox(context, sliderNode.c_str(), curBox)) {
                curBox = endBox;
            }
            int cur = 0;
            if (readQuantityAtHandle(context, curBox, qOffset, cur) && cur <= 1) {
                return true; // 已减到 1，不能再减
            }
            findAndClickButton(context, decreaseBtn.c_str());
            Sleep(300);
        }
    }

    return true;
}

void registerCustomComm(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "ClearHitCount", ClearHitCountAct, user_data);
    registerCustomAction(res, "FailTask", FailTaskAct, user_data);
    registerCustomAction(res, "SaveConfig", SaveConfigAct, user_data);
    registerCustomAction(res, "SubTask", SubTaskAct, user_data);
    registerCustomAction(res, "ShopAssistant", shopAssistant, user_data);
    registerCustomAction(res, "CarriageShopSlideBuy", CarriageShopSlideBuyAct, user_data);
    registerCustomAction(res, "DefineMapWalls", defineMapWalls, user_data);
    registerCustomAction(res, "AdjustPosition", adjustPositionAct, user_data);
    registerCustomRecognition(res, "ShopFinishCheck", shopFinishCheck, user_data);
    registerCustomRecognition(res, "ItemNotBought", itemNotBought, user_data);
    registerCustomRecognition(res, "CheckRoleInTargetMap", checkRoleInTargetMap, user_data);
    registerCustomRecognition(res, "findTargetPoint", findTargetPoint, user_data);
    registerCustomRecognition(res, "ArrivedAtTargetPoint", arrivedAtTargetPoint, user_data);
}
