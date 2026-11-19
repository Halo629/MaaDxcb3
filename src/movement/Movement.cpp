#include "Movement.h"

#include "LogUtils.h"
#include "Registry.h"
#include "map/MapNavigator.h"
#include "util/JsonUtil.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <cmath>

std::map<std::string, json::value> Movement::mapConfig_;
std::string Movement::lastMoveNode_;
int Movement::lastMoveAttempts_ = 0;
double Movement::walkCoef_ = 5.0;

namespace {

bool isStopping(MaaContext* context)
{
    return MaaTaskerStopping(MaaContextGetTasker(context));
}

struct OcrText
{
    std::string text;
    MaaRect box;
};

struct DoorInfo
{
    std::string task;
    int x = 0;
    int y = 0;
    bool hasPos = false;
};

bool readCharacterCoord(MaaContext* context, int& cx, int& cy, int dx = 0, int dy = 0)
{
    auto read = [&]() {
        ScreenCap cap(context);
        return direction::parseCoord(recog::readTextViaEntry(context, cap.img, "SceneMapCharacterPosition", "{}", nullptr), cx, cy);
    };
    if (read()) {
        return true;
    }

    if (dx != 0 || dy != 0) {
        for (int i = 0; i < 3; ++i) {
            if (isStopping(context)) {
                return false;
            }
            screen::swipeToward(context, dx, dy, 20);
            if (read()) {
                return true;
            }
        }
        return false;
    }

    static const int kDirs[4][2] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };
    for (const auto& d : kDirs) {
        for (int i = 0; i < 3; ++i) {
            if (isStopping(context)) {
                return false;
            }
            screen::swipeToward(context, d[0], d[1], 20);
            if (read()) {
                return true;
            }
        }
    }
    return false;
}

bool recognizeTask(MaaContext* context, const char* task, MaaRect* outBox)
{
    ScreenCap cap(context);
    return recog::run(context, cap.img, task, "{}", outBox, nullptr);
}

std::vector<OcrText> collectMazeNames(MaaContext* context, const MaaImageBuffer* img)
{
    std::vector<OcrText> out;
    for (auto& t : recog::readTexts(context, img, "CompleteMazeNameRecg", "CompleteMazeName")) {
        out.push_back({ t.text, t.box });
    }
    for (auto& t : recog::readTexts(context, img, "IncompleteMazeNameRecg", "IncompleteMazeName")) {
        out.push_back({ t.text, t.box });
    }
    return out;
}

WalkStep walkStep(MaaContext* context, int tx, int ty, const std::string& task, MaaRect& outBox,
                  int& lastDx, int& lastDy)
{
    if (isStopping(context)) {
        return WalkStep::Error;
    }

    int cx = 0, cy = 0;
    if (!readCharacterCoord(context, cx, cy, lastDx, lastDy)) {
        LogUtils::warn("walkStep: 读取人物坐标失败");
        return WalkStep::Error;
    }

    int dx = tx - cx;
    int dy = ty - cy;
    lastDx = dx;
    lastDy = dy;

    double len = std::sqrt((double)dx * dx + (double)dy * dy);
    int slideDist = (int)std::round(len * Movement::walkCoef());
    if (slideDist < 80) {
        slideDist = 80;
    }

    if (recognizeTask(context, task.c_str(), &outBox)) {
        return WalkStep::Found;
    }
    if (len <= 3.0) {
        return WalkStep::Error;
    }
    screen::swipeToward(context, dx, dy, slideDist);

    if (recognizeTask(context, task.c_str(), &outBox)) {
        return WalkStep::Found;
    }
    return WalkStep::NotFound;
}

bool crossHorizontalWall(int cx, int cy, int tx, int ty, int value, const json::value& extent)
{
    if ((long long)(cy - value) * (ty - value) >= 0) {
        return false;
    }
    if (!extent.is_array() || extent.as_array().size() < 2) {
        return true;
    }
    int a = extent.as_array()[0].as_integer();
    int b = extent.as_array()[1].as_integer();
    return (cx >= a && cx <= b) || (tx >= a && tx <= b);
}

bool crossVerticalWall(int cx, int cy, int tx, int ty, int value, const json::value& extent)
{
    if ((long long)(cx - value) * (tx - value) >= 0) {
        return false;
    }
    if (!extent.is_array() || extent.as_array().size() < 2) {
        return true;
    }
    int a = extent.as_array()[0].as_integer();
    int b = extent.as_array()[1].as_integer();
    return (cy >= a && cy <= b) || (ty >= a && ty <= b);
}

void runTaskAndWait(MaaContext* context, const std::string& task)
{
    auto* tasker = MaaContextGetTasker(context);
    auto taskId = MaaContextRunTask(context, task.c_str(), "{}");
    if (taskId != MaaInvalidId) {
        MaaTaskerWait(tasker, taskId);
    }
}

DoorInfo parseDoor(const json::value& d)
{
    DoorInfo di;
    di.task = d.get("task", json::value("")).as_string();
    if (di.task.empty()) {
        return di;
    }
    auto ox = d.get("x", json::value());
    auto oy = d.get("y", json::value());
    if (ox.is_number() && oy.is_number()) {
        di.x = ox.as_integer();
        di.y = oy.as_integer();
        di.hasPos = true;
    }
    return di;
}

std::vector<DoorInfo> parseDoors(const json::value& doors)
{
    std::vector<DoorInfo> out;
    if (!doors.is_array()) {
        return out;
    }
    for (const auto& d : doors.as_array()) {
        DoorInfo di = parseDoor(d);
        if (!di.task.empty()) {
            out.push_back(di);
        }
    }
    return out;
}

DoorInfo selectClosestDoor(MaaContext* context, const std::vector<DoorInfo>& doors)
{
    if (doors.empty()) {
        return {};
    }
    if (doors.size() == 1) {
        return doors[0];
    }

    ScreenCap cap(context);
    MaaRect charBox;
    int charCx = 0, charCy = 0;
    if (!recog::run(context, cap.img, "RolePos", "{}", &charBox, nullptr)) {
        return doors[0];
    }
    charCx = charBox.x + charBox.width / 2;
    charCy = charBox.y + charBox.height / 2;

    DoorInfo best = doors[0];
    int bestDist = 0x7fffffff;
    for (const auto& door : doors) {
        if (isStopping(context)) {
            break;
        }
        MaaRect doorBox;
        if (!recog::run(context, cap.img, door.task.c_str(), "{}", &doorBox, nullptr)) {
            continue;
        }
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

void runDoor(MaaContext* context, int cx, int cy, int tx, int ty, const DoorInfo& door, int dist)
{
    if (door.task.empty()) {
        return;
    }
    if (door.hasPos) {
        for (int i = 0; i < 10; ++i) {
            if (isStopping(context)) {
                return;
            }
            ScreenCap cap(context);
            if (recog::run(context, cap.img, door.task.c_str(), "{}", nullptr, nullptr)) {
                break;
            }
            screen::swipeToward(context, door.x - cx, door.y - cy, dist);
        }
    }
    LogUtils::info(fmt("walkToTargetPoint: 穿过[%1] 人物=(%2,%3) 目标=(%4,%5)", door.task, cx, cy, tx, ty));
    runTaskAndWait(context, door.task);
}

bool crossOneWall(MaaContext* context, int cx, int cy, int tx, int ty, const json::value& walls, int dist)
{
    if (!walls.is_array()) {
        return false;
    }
    for (const auto& ob : walls.as_array()) {
        if (isStopping(context)) {
            break;
        }
        std::string axis = ob.get("axis", json::value("")).as_string();
        int value = ob.get("value", json::value(0)).as_integer();
        auto extent = ob.get("extent", json::value());
        auto doors = ob.get("doors", json::value());
        if (!doors.is_array() || doors.as_array().empty()) {
            continue;
        }

        bool cross = false;
        if (axis == "horizontal") {
            cross = crossHorizontalWall(cx, cy, tx, ty, value, extent);
        }
        else if (axis == "vertical") {
            cross = crossVerticalWall(cx, cy, tx, ty, value, extent);
        }
        if (!cross) {
            continue;
        }

        auto doorList = parseDoors(doors);
        if (doorList.empty()) {
            continue;
        }
        runDoor(context, cx, cy, tx, ty, selectClosestDoor(context, doorList), dist);
        return true;
    }
    return false;
}

bool pointInPolygon(int px, int py, const std::vector<std::pair<int, int>>& pts)
{
    int n = (int)pts.size();
    if (n < 3) {
        return false;
    }
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        int xi = pts[i].first, yi = pts[i].second;
        int xj = pts[j].first, yj = pts[j].second;
        if ((yi > py) != (yj > py)) {
            double x = (double)(xj - xi) * (py - yi) / (yj - yi) + xi;
            if (px < x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

bool crossOneIsland(MaaContext* context, int cx, int cy, int tx, int ty, const json::value& islands, int dist)
{
    if (!islands.is_array()) {
        return false;
    }
    for (const auto& island : islands.as_array()) {
        if (isStopping(context)) {
            break;
        }
        auto ptsJson = island.get("points", json::value());
        if (!ptsJson.is_array()) {
            continue;
        }
        std::vector<std::pair<int, int>> pts;
        for (const auto& p : ptsJson.as_array()) {
            if (!p.is_array() || p.as_array().size() < 2) {
                continue;
            }
            pts.push_back({ p.as_array()[0].as_integer(), p.as_array()[1].as_integer() });
        }
        if (pts.size() < 3) {
            continue;
        }

        bool charInside = pointInPolygon(cx, cy, pts);
        bool targetInside = pointInPolygon(tx, ty, pts);

        DoorInfo door;
        if (!charInside && targetInside) {
            door = parseDoor(island.get("entrance", json::value()));
        }
        else if (charInside && !targetInside) {
            door = parseDoor(island.get("exit", json::value()));
        }
        else {
            continue;
        }
        if (door.task.empty()) {
            continue;
        }
        runDoor(context, cx, cy, tx, ty, door, dist);
        return true;
    }
    return false;
}

void crossObstacle(MaaContext* context, int tx, int ty, int dist)
{
    std::string map = MapNavigator::currentMap(context);
    auto& cfg = Movement::mapConfig()[map];
    if (!cfg.is_object()) {
        return;
    }

    const json::value& walls = cfg.get("walls", json::value());
    const json::value& islands = cfg.get("islands", json::value());

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (isStopping(context)) {
            return;
        }
        int cx = 0, cy = 0;
        if (!readCharacterCoord(context, cx, cy)) {
            return;
        }
        if (crossOneWall(context, cx, cy, tx, ty, walls, dist)) {
            continue;
        }
        if (crossOneIsland(context, cx, cy, tx, ty, islands, dist)) {
            continue;
        }
        return;
    }
}

} // namespace

bool Movement::walkToTargetPoint(MaaContext* context, int tx, int ty, const std::string& task,
                                 MaaRect& outBox, int maxAttempts, int dist)
{
    screen::resetSwipeStart();
    crossObstacle(context, tx, ty, dist);

    int lastDx = 0, lastDy = 0;
    for (int i = 0; i < maxAttempts; ++i) {
        if (isStopping(context)) {
            return false;
        }
        MaaRect box;
        WalkStep step = walkStep(context, tx, ty, task, box, lastDx, lastDy);
        if (step == WalkStep::Found) {
            outBox = box;
            return true;
        }
        if (step == WalkStep::Error) {
            return false;
        }
    }
    return false;
}

bool Movement::findTextOnScreen(MaaContext* context, const MaaImageBuffer* img,
                                const std::string& text, MaaRect& outBox)
{
    for (auto& m : collectMazeNames(context, img)) {
        if (m.text.find(text) != std::string::npos) {
            outBox = m.box;
            LogUtils::info(fmt("findTextOnScreen: 期望[%1] 识别到迷宫[%2]", text, m.text));
            return true;
        }
    }
    return false;
}

MaaBool Movement::findTargetPoint(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customRecognitionName, const char* customRecognitionParam,
                                  const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                  MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    auto point = jsonutil::readIntArray(param.get("point", json::value()), { 0, 0 });
    int tx = point[0], ty = point[1];
    std::string task = param.get("task", json::value("")).as_string();
    int dist = param.get("dist", json::value(200)).as_integer();
    auto offset = jsonutil::readIntArray(param.get("offset", json::value()), { 0, 0, 0, 0 });

    const std::string moveKey = nodeName ? nodeName : "";
    if (lastMoveNode_ != moveKey) {
        lastMoveNode_ = moveKey;
        lastMoveAttempts_ = 0;
    }
    walkCoef_ = (lastMoveAttempts_++ == 0) ? 10.0 : 5.0;

    MaaRect box;
    if (!walkToTargetPoint(context, tx, ty, task, box, 5, dist)) {
        MaaRect best = screen::findClickableAreaNear(context, 360, 900);
        if (best.width <= 0 || best.height <= 0) {
            return false;
        }
        LogUtils::info(fmt("findTargetPoint: 未识别到目标，返回可点击区域(%1,%2,%3,%4)供点击重试",
                           best.x, best.y, best.width, best.height));
        *outBox = best;
        return true;
    }
    *outBox = screen::adjustRectToClickable(context, task, offset, box);
    return true;
}

MaaBool Movement::arrivedAtTargetPoint(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customRecognitionName, const char* customRecognitionParam,
                                       const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                       MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    auto point = jsonutil::readIntArray(param.get("point", json::value()), { 0, 0 });
    int px = point[0], py = point[1];
    auto offset = jsonutil::readIntArray(param.get("offset", json::value()), { 0, 0 });
    int offX = offset[0], offY = offset[1];

    int cx = 0, cy = 0;
    if (!readCharacterCoord(context, cx, cy)) {
        LogUtils::warn("arrivedAtTargetPoint: 读取人物坐标失败");
        return false;
    }

    bool arrived = cx >= px - offX && cx <= px + offX && cy >= py - offY && cy <= py + offY;
    if (arrived) {
        lastMoveAttempts_ = 0;
    }
    return arrived;
}

MaaBool Movement::defineMapWalls(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customActionName, const char* customActionParam,
                                 MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    auto param = jsonutil::parse(customActionParam ? customActionParam : "{}");
    auto store = [](const json::value& item) {
        std::string map = item.get("map", json::value("")).as_string();
        if (map.empty()) {
            return;
        }
        Movement::mapConfig()[map] = item;
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

namespace {

MaaBool findTargetPointThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                             const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                             void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return Movement::findTargetPoint(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool arrivedAtTargetPointThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                  const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                  void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return Movement::arrivedAtTargetPoint(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool defineMapWallsThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                            const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return Movement::defineMapWalls(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void Movement::registerAll(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "findTargetPoint", findTargetPointThunk, userData);
    registerCustomRecognition(res, "ArrivedAtTargetPoint", arrivedAtTargetPointThunk, userData);
    registerCustomAction(res, "DefineMapWalls", defineMapWallsThunk, userData);
}
