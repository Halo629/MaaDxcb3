#pragma once

#include "MaaFramework/MaaAPI.h"

#include <meojson/json.hpp>

#include <map>
#include <string>

enum class WalkStep
{
    Found,
    NotFound,
    Error,
};

class Movement
{
public:
    // 走到目标点位（含穿墙），按 pipeline 任务找目标框，找到返回 true
    static bool walkToTargetPoint(MaaContext* context, int tx, int ty, const std::string& task,
                                  MaaRect& outBox, int maxAttempts = 10, int dist = 200);

    // 屏幕查找文字（迷宫名字），命中返回 box
    static bool findTextOnScreen(MaaContext* context, const MaaImageBuffer* img,
                                 const std::string& text, MaaRect& outBox);

    // 自定义识别：走到目标点位并返回位置名框（只找不点，点击交给外层 Click）
    static MaaBool findTargetPoint(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                   const char* customRecognitionName, const char* customRecognitionParam,
                                   const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                   MaaRect* outBox, MaaStringBuffer* outDetail);

    // 自定义识别：判断人物是否到达目标坐标点
    static MaaBool arrivedAtTargetPoint(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customRecognitionName, const char* customRecognitionParam,
                                        const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                        MaaRect* outBox, MaaStringBuffer* outDetail);

    // 自定义动作：定义地图的墙/孤岛配置
    static MaaBool defineMapWalls(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customActionName, const char* customActionParam,
                                  MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

    // 供走位辅助函数（.cpp 匿名命名空间）访问
    static std::map<std::string, json::value>& mapConfig() { return m_mapConfig; }
    static double walkCoef() { return m_walkCoef; }

private:
    static std::map<std::string, json::value> m_mapConfig;
    static std::string m_lastMoveNode;
    static int m_lastMoveAttempts;
    static double m_walkCoef;
};
