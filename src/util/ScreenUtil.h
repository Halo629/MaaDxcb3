#pragma once

#include "DirectionUtil.h"
#include "MaaFramework/MaaAPI.h"

#include <cstdint>
#include <string>
#include <vector>

// 屏幕区域划分 (720×1280 九宫格)
enum ScreenArea : uint32_t {
    ScreenArea_TopLeft = 1 << 0,
    ScreenArea_TopCenter = 1 << 1,
    ScreenArea_TopRight = 1 << 2,
    ScreenArea_MidLeft = 1 << 3,
    ScreenArea_MidCenter = 1 << 4,
    ScreenArea_MidRight = 1 << 5,
    ScreenArea_BotLeft = 1 << 6,
    ScreenArea_BotCenter = 1 << 7,
    ScreenArea_BotRight = 1 << 8,

    ScreenArea_TopRow = ScreenArea_TopLeft | ScreenArea_TopCenter | ScreenArea_TopRight,
    ScreenArea_MidRow = ScreenArea_MidLeft | ScreenArea_MidCenter | ScreenArea_MidRight,
    ScreenArea_BotRow = ScreenArea_BotLeft | ScreenArea_BotCenter | ScreenArea_BotRight,
    ScreenArea_LeftCol = ScreenArea_TopLeft | ScreenArea_MidLeft | ScreenArea_BotLeft,
    ScreenArea_CenterCol = ScreenArea_TopCenter | ScreenArea_MidCenter | ScreenArea_BotCenter,
    ScreenArea_RightCol = ScreenArea_TopRight | ScreenArea_MidRight | ScreenArea_BotRight,
    ScreenArea_Full = ScreenArea_TopRow | ScreenArea_MidRow | ScreenArea_BotRow,
};

namespace screen {

// 获取可操作区域（屏幕减去不可操作区域后的剩余矩形列表）
std::vector<MaaRect> clickableAreas(MaaContext* context);

// 判断一个点是否在可操作区域内
bool isClickable(MaaContext* context, int x, int y);

// 朝目标方向 (dx,dy) 连续滑动屏幕（dx=东分量、dy=北分量）
void swipeToward(MaaContext* context, int dx, int dy, int dist = 400, int duration = 300);

// 重置记住的滑动起点（开始一次新的移动时调用）
void resetSwipeStart();

// 朝某方位滑动屏幕（12 方位量化）
void swipeAvoidOutpost(MaaContext* context, CompassDir dir, int dist = 400, int duration = 300);

// 点击指定坐标
void clickPoint(MaaContext* context, int x, int y);

// 在矩形内随机点击
void clickRandomTarget(MaaContext* context, int x, int y, int w, int h);

// 等待画面静止
void waitFreezes(MaaContext* context, uint32_t areaMask = ScreenArea_MidCenter, int waitTime = 300, int timeout = 3000);
void waitFreezes(MaaContext* context, MaaRect roi, int waitTime = 300, int timeout = 3000);

// 把识别框（+offset）调整到可操作区域（滑动视角并重新识别），返回可直接点击的区域
MaaRect adjustRectToClickable(MaaContext* context, const std::string& task,
                              const std::vector<int>& offset, const MaaRect& initialBox);

// 找离 (px,py) 最近的可点击区域
MaaRect findClickableAreaNear(MaaContext* context, int px, int py);

} // namespace screen
