#pragma once

#include "MaaFramework/MaaAPI.h"

#include <meojson/json.hpp>

#include <string>
#include <unordered_map>

enum class MapIndex
{
    Map_None = 0,
    IsleOfTimesEnd,
    CrescentCounty,
    HolyCityMonastery,
    Map_Gremid,
    FurnaceCity,
};

class MapNavigator
{
public:
    // 服务器名归一化：去掉冒号（中英文）和空白，统一 OCR 结果
    static std::string normalizeServerName(const std::string& raw);

    static bool waitArriveMap(MaaContext* context);
    static std::string mapOfRole(MaaContext* context, MaaRect* outBox = nullptr);
    static std::string currentMap(MaaContext* context);
    static std::string mapOfFerdinand(MaaContext* context, MaaRect* outBox);
    static json::value curMapList(MaaContext* context);
    static void exitToCampfire(MaaContext* context);
    static bool enterMapByName(MaaContext* context, const std::string& mapName);
    static void setNextNode(MaaContext* context, const char* nodeName, const char* nextNode);
    static void switchMapSort(MaaContext* context, const std::string& targetOrder);

    static std::string mapName(MapIndex idx);
    static const std::unordered_map<std::string, MapIndex>& mapTable();
};
