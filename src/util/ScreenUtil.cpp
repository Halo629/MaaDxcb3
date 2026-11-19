#include "ScreenUtil.h"

#include "LogUtils.h"
#include "MaaBuffer.h"
#include "Recognition.h"
#include "StringUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include <windows.h>

namespace {

constexpr int kScreenW = 720;
constexpr int kScreenH = 1280;

// 720×1280 九宫格区域坐标
constexpr MaaRect kScreenAreaBox[] = {
    { 0, 0, 240, 427 },      // ScreenArea_TopLeft
    { 240, 0, 240, 427 },    // ScreenArea_TopCenter
    { 480, 0, 240, 427 },    // ScreenArea_TopRight
    { 0, 427, 240, 427 },    // ScreenArea_MidLeft
    { 240, 427, 240, 427 },  // ScreenArea_MidCenter
    { 480, 427, 240, 427 },  // ScreenArea_MidRight
    { 0, 854, 240, 426 },    // ScreenArea_BotLeft
    { 240, 854, 240, 426 },  // ScreenArea_BotCenter
    { 480, 854, 240, 426 },  // ScreenArea_BotRight
};

// 记住的滑动起点：本次移动内有效，避免每次都从屏幕中心滑又弹据点框
bool g_hasSwipeStart = false;
int g_swipeStartX = 0;
int g_swipeStartY = 0;

bool isStopping(MaaContext* context)
{
    return MaaTaskerStopping(MaaContextGetTasker(context));
}

void addUniqueDir(std::vector<std::pair<int, int>>& dirs, int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        return;
    }
    for (const auto& d : dirs) {
        if (d.first == dx && d.second == dy) {
            return;
        }
    }
    dirs.push_back({ dx, dy });
}

MaaRect applyRectOffset(MaaRect r, const std::vector<int>& offset)
{
    if (offset.size() >= 2) {
        r.x += offset[0];
        r.y += offset[1];
    }
    if (offset.size() >= 4) {
        r.width += offset[2];
        r.height += offset[3];
    }
    return r;
}

std::vector<MaaRect> subtractRects(const std::vector<MaaRect>& areas, const MaaRect& cut)
{
    std::vector<MaaRect> result;
    for (const auto& a : areas) {
        int ix = (std::max)(a.x, cut.x);
        int iy = (std::max)(a.y, cut.y);
        int ir = (std::min)(a.x + a.width, cut.x + cut.width);
        int ib = (std::min)(a.y + a.height, cut.y + cut.height);
        if (ix >= ir || iy >= ib) {
            result.push_back(a);
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

std::vector<MaaRect> forbiddenAreas(MaaContext* context)
{
    ScreenCap cap(context);
    MaaBuffer detailBuf;
    MaaRect outBox;
    std::vector<MaaRect> taskIcons;
    if (recog::run(context, cap.img, "TaskIcon", "{}", &outBox, detailBuf.get())) {
        auto j = json::parse(detailBuf.str()).value_or(json::value {});
        if (j.is_object()) {
            if (auto& filtered = j["filtered"]; filtered.is_array()) {
                for (auto& item : filtered.as_array()) {
                    auto& box = item["box"];
                    taskIcons.push_back(
                        { box[0].as_integer(), box[1].as_integer(), box[2].as_integer(), box[3].as_integer() });
                }
            }
        }
    }

    std::vector<MaaRect> forbidden;
    if (!taskIcons.empty()) {
        int rightX = 0;
        int bottomY = 0;
        for (auto& r : taskIcons) {
            rightX = (std::max)(rightX, r.x + r.width);
            bottomY = (std::max)(bottomY, r.y + r.height);
        }
        forbidden.push_back({ 0, 0, rightX + 30, bottomY + 30 });
    }

    forbidden.push_back({ 0, 0, 140, kScreenH });
    forbidden.push_back({ 0, kScreenH - 110, kScreenW, 110 });
    forbidden.push_back({ 0, 0, kScreenW, 160 });
    forbidden.push_back({ kScreenW - 130, 0, 130, 365 });
    return forbidden;
}

bool isRectClickable(MaaContext* context, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || x + w > kScreenW || y + h > kScreenH) {
        return false;
    }
    for (auto& r : forbiddenAreas(context)) {
        if (x < r.x + r.width && x + w > r.x && y < r.y + r.height && y + h > r.y) {
            return false;
        }
    }
    return true;
}

bool cropRectToClickable(MaaContext* context, int& x, int& y, int& w, int& h)
{
    std::vector<MaaRect> parts = { { x, y, w, h } };
    for (auto& r : forbiddenAreas(context)) {
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

bool recognizeTask(MaaContext* context, const char* task, MaaRect* outBox)
{
    ScreenCap cap(context);
    return recog::run(context, cap.img, task, "{}", outBox, nullptr);
}

bool swipeWithPopupHandling(MaaContext* context, int sx, int sy, int dx, int dy, int duration)
{
    auto ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostSwipe(ctrl, sx, sy, sx + dx, sy + dy, duration);
    MaaControllerWait(ctrl, id);
    screen::waitFreezes(context, ScreenArea_MidCenter, 300, 1000);

    if (!recognizeTask(context, "LeaveButton", nullptr)) {
        return true;
    }

    LogUtils::debug(fmt("滑动弹框: 起点=(%1,%2) 向量=(%3,%4)", sx, sy, dx, dy));
    auto quitId = MaaContextRunTask(context, "QuitMapOutpost", "{}");
    if (quitId != MaaInvalidId) {
        MaaTaskerWait(MaaContextGetTasker(context), quitId);
    }
    return false;
}

bool swipeWithOffsetRetry(MaaContext* context, int baseX, int baseY, int dx, int dy, int duration, int* outSx, int* outSy)
{
    if (swipeWithPopupHandling(context, baseX, baseY, dx, dy, duration)) {
        if (outSx) {
            *outSx = baseX;
        }
        if (outSy) {
            *outSy = baseY;
        }
        return true;
    }

    int offDX = (dx > 0) ? -1 : (dx < 0 ? 1 : 0);
    int offDY = (dy > 0) ? -1 : (dy < 0 ? 1 : 0);

    std::vector<std::pair<int, int>> offsetDirs;
    addUniqueDir(offsetDirs, offDX, offDY);
    addUniqueDir(offsetDirs, offDX, 0);
    addUniqueDir(offsetDirs, 0, offDY);
    addUniqueDir(offsetDirs, -offDX, 0);
    addUniqueDir(offsetDirs, 0, -offDY);

    std::set<std::pair<int, int>> tried;
    for (int step = 200; step <= 640; step += 200) {
        if (isStopping(context)) {
            return false;
        }
        for (const auto& d : offsetDirs) {
            if (isStopping(context)) {
                return false;
            }
            int sx = baseX + d.first * step;
            int sy = baseY + d.second * step;
            if (sx + dx < 0 || sy + dy < 0 || sx + dx >= kScreenW || sy + dy >= kScreenH) {
                continue;
            }
            if (!screen::isClickable(context, sx, sy)) {
                continue;
            }
            if (!tried.insert({ sx, sy }).second) {
                continue;
            }
            LogUtils::debug(fmt("滑动重试: 起点=(%1,%2) 偏移=(%3,%4)", sx, sy, d.first * step, d.second * step));
            if (swipeWithPopupHandling(context, sx, sy, dx, dy, duration)) {
                if (outSx) {
                    *outSx = sx;
                }
                if (outSy) {
                    *outSy = sy;
                }
                return true;
            }
        }
    }
    return false;
}

std::vector<std::pair<int, int>> computeEscapeDirs(int x, int y, int w, int h)
{
    const int cx = 360, cy = 640;
    std::vector<std::pair<int, int>> dirs;

    int tx = x + w / 2;
    int ty = y + h / 2;
    int dx = tx < cx ? 1 : (tx > cx ? -1 : 0);
    int dy = ty < cy ? 1 : (ty > cy ? -1 : 0);

    if (dx != 0 && dy != 0) {
        addUniqueDir(dirs, dx, dy);
    }
    if (dx != 0) {
        addUniqueDir(dirs, dx, 0);
    }
    if (dy != 0) {
        addUniqueDir(dirs, 0, dy);
    }
    return dirs;
}

void swipeVec(MaaContext* context, int dx, int dy, int duration)
{
    int baseX = g_hasSwipeStart ? g_swipeStartX : 360;
    int baseY = g_hasSwipeStart ? g_swipeStartY : 900;

    int sx = baseX, sy = baseY;
    if (swipeWithOffsetRetry(context, baseX, baseY, dx, dy, duration, &sx, &sy)) {
        g_hasSwipeStart = true;
        g_swipeStartX = sx;
        g_swipeStartY = sy;
    }
}

} // namespace

namespace screen {

std::vector<MaaRect> clickableAreas(MaaContext* context)
{
    std::vector<MaaRect> clickable;
    clickable.push_back({ 0, 0, kScreenW, kScreenH });
    for (auto& cut : forbiddenAreas(context)) {
        clickable = subtractRects(clickable, cut);
    }
    return clickable;
}

bool isClickable(MaaContext* context, int x, int y)
{
    for (auto& r : forbiddenAreas(context)) {
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            return false;
        }
    }
    return true;
}

MaaRect findClickableAreaNear(MaaContext* context, int px, int py)
{
    MaaRect best = { 0, 0, 0, 0 };
    int bestDist = 0x7fffffff;
    for (const auto& r : clickableAreas(context)) {
        int rcx = r.x + r.width / 2;
        int rcy = r.y + r.height / 2;
        int d = (rcx - px) * (rcx - px) + (rcy - py) * (rcy - py);
        if (d < bestDist) {
            bestDist = d;
            best = r;
        }
    }
    return best;
}

MaaRect adjustRectToClickable(MaaContext* context, const std::string& task,
                              const std::vector<int>& offset, const MaaRect& initialBox)
{
    const int cx = 360, cy = 640;
    MaaRect box = initialBox;

    for (int attempt = 0; attempt < 20; ++attempt) {
        if (isStopping(context)) {
            break;
        }
        MaaRect target = applyRectOffset(box, offset);
        if (isRectClickable(context, target.x, target.y, target.width, target.height)) {
            break;
        }

        int mx = 0, my = 0;
        if (target.x < 0) {
            mx = 100;
        }
        else if (target.x + target.width > kScreenW) {
            mx = -100;
        }
        if (target.y < 0) {
            my = 100;
        }
        else if (target.y + target.height > kScreenH) {
            my = -100;
        }

        if (mx != 0 || my != 0) {
            swipeWithOffsetRetry(context, cx, cy, mx, my, 200, nullptr, nullptr);
            MaaRect newBox;
            if (!recognizeTask(context, task.c_str(), &newBox)) {
                break;
            }
            box = newBox;
            continue;
        }

        auto dirs = computeEscapeDirs(target.x, target.y, target.width, target.height);
        if (dirs.empty()) {
            break;
        }

        bool escaped = false;
        bool failed = false;
        for (const auto& d : dirs) {
            for (int k = 0; k < 5; ++k) {
                if (isStopping(context)) {
                    failed = true;
                    break;
                }
                swipeWithOffsetRetry(context, cx, cy, d.first * 100, d.second * 100, 200, nullptr, nullptr);

                MaaRect newBox;
                if (recognizeTask(context, task.c_str(), &newBox)) {
                    box = newBox;
                    MaaRect t = applyRectOffset(box, offset);
                    if (isRectClickable(context, t.x, t.y, t.width, t.height)) {
                        escaped = true;
                        break;
                    }
                }
                else {
                    failed = true;
                    break;
                }
            }
            if (escaped || failed || isStopping(context)) {
                break;
            }
        }
        break;
    }

    MaaRect target = applyRectOffset(box, offset);
    cropRectToClickable(context, target.x, target.y, target.width, target.height);
    return target;
}

void resetSwipeStart()
{
    g_hasSwipeStart = false;
    g_swipeStartX = 0;
    g_swipeStartY = 0;
}

void swipeToward(MaaContext* context, int dx, int dy, int dist, int duration)
{
    double len = std::sqrt((double)dx * dx + (double)dy * dy);
    if (len < 1e-6) {
        return;
    }
    double ux = dx / len;
    double uy = dy / len;
    int sx = static_cast<int>(-ux * dist);
    int sy = static_cast<int>(uy * dist);
    swipeVec(context, sx, sy, duration);
}

void swipeAvoidOutpost(MaaContext* context, CompassDir dir, int dist, int duration)
{
    int idx = static_cast<int>(dir);
    if (idx < 0 || idx >= 12) {
        return;
    }
    double angle = idx * 30.0 * 3.14159265358979323846 / 180.0;
    int dx = static_cast<int>(-std::sin(angle) * dist);
    int dy = static_cast<int>(std::cos(angle) * dist);
    swipeVec(context, dx, dy, duration);
}

void clickPoint(MaaContext* context, int x, int y)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, x, y);
    MaaControllerWait(ctrl, id);
}

void clickRandomTarget(MaaContext* context, int x, int y, int w, int h)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    int rx = x + rand() % w;
    int ry = y + rand() % h;
    auto id = MaaControllerPostClick(ctrl, rx, ry);
    MaaControllerWait(ctrl, id);
}

void waitFreezes(MaaContext* context, uint32_t areaMask, int waitTime, int timeout)
{
    char param[64];
    snprintf(param, sizeof(param), R"({"timeout": %d})", timeout);
    for (int i = 0; i < 9; ++i) {
        if (areaMask & (1u << i)) {
            MaaContextWaitFreezes(context, waitTime, &kScreenAreaBox[i], param);
        }
    }
}

void waitFreezes(MaaContext* context, MaaRect roi, int waitTime, int timeout)
{
    char param[64];
    snprintf(param, sizeof(param), R"({"timeout": %d})", timeout);
    MaaContextWaitFreezes(context, waitTime, &roi, param);
}

} // namespace screen
