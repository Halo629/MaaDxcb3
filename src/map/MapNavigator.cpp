#include "MapNavigator.h"

#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <cstdio>

std::string MapNavigator::normalizeServerName(const std::string& raw)
{
    std::string s = raw;
    size_t pos;
    while ((pos = s.find("\xEF\xBC\x9A")) != std::string::npos) {
        s.erase(pos, 3);
    }
    std::string result;
    for (char c : s) {
        if (c == ':' || c == ' ' || c == '\t') {
            continue;
        }
        result += c;
    }
    return result;
}

bool MapNavigator::waitArriveMap(MaaContext* context)
{
    return recog::waitUntil(context, "InMap", "{}", nullptr, nullptr);
}

std::string MapNavigator::mapOfRole(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    MaaRect roleBox;
    auto id = MaaContextRunRecognition(context, "RolePos", "{}", cap.img);
    if (id == MaaInvalidId) {
        return {};
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &roleBox, nullptr, nullptr, nullptr);
    if (!hit) {
        return {};
    }
    if (outBox) {
        *outBox = roleBox;
    }

    char ocrJson[256];
    snprintf(ocrJson, sizeof(ocrJson), R"({"mapName":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})",
             roleBox.x - 250, roleBox.y - 10, 250, 80);
    MaaBuffer buffer;
    MaaRect ocrBox;
    id = MaaContextRunRecognition(context, "mapName", ocrJson, cap.img);
    if (id == MaaInvalidId) {
        return {};
    }
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &ocrBox, buffer.get(), nullptr, nullptr);
    if (!hit) {
        return {};
    }
    auto j = jsonutil::parse(buffer.str());
    if (auto& best = j["best"]; best.is_object()) {
        return best["text"].as_string();
    }
    return {};
}

std::string MapNavigator::currentMap(MaaContext* context)
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
    for (const auto& [inNode, name] : kMapInTable) {
        if (recog::run(context, cap.img, inNode, "{}", nullptr, nullptr)) {
            return name;
        }
    }
    return {};
}

std::string MapNavigator::mapOfFerdinand(MaaContext* context, MaaRect* outBox)
{
    ScreenCap cap(context);
    MaaBuffer buf;
    MaaRect box;
    std::string result;
    if (recog::run(context, cap.img, "MapOfFerdinand", "{}", outBox ? outBox : &box, buf.get())) {
        auto j = jsonutil::parse(buf.str());
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
    return result;
}

json::value MapNavigator::curMapList(MaaContext* context)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    MaaBuffer detailBuf;
    MaaRect outBox;
    auto id = MaaContextRunRecognition(context, "CurMapList", "{}", cap.img);
    if (id == MaaInvalidId) {
        return {};
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &outBox, detailBuf.get(), nullptr, nullptr);
    if (!hit) {
        return {};
    }
    auto parsed = json::parse(detailBuf.str());
    if (!parsed || !parsed->exists("all")) {
        return {};
    }
    return (*parsed)["all"];
}

void MapNavigator::exitToCampfire(MaaContext* context)
{
    MaaContextRunTask(context, "SceneEnterMainPage", "{}");
}

bool MapNavigator::enterMapByName(MaaContext* context, const std::string& mapName)
{
    auto entries = curMapList(context);
    if (!entries.is_array()) {
        return false;
    }
    for (auto& item : entries.as_array()) {
        std::string text = item["text"].as_string();
        if (text.find(mapName) != std::string::npos) {
            auto& box = item.as_object().at("box");
            int bx = box[0].as_integer(), by = box[1].as_integer();
            screen::clickRandomTarget(context, bx, by, box[2].as_integer(), box[3].as_integer());
            waitArriveMap(context);
            return true;
        }
    }
    return false;
}

void MapNavigator::setNextNode(MaaContext* context, const char* nodeName, const char* nextNode)
{
    auto nextList = MaaStringListBufferCreate();
    MaaBuffer nextBuf;
    MaaStringBufferSet(nextBuf.get(), nextNode);
    MaaStringListBufferAppend(nextList, nextBuf.get());
    MaaContextOverrideNext(context, nodeName, nextList);
    MaaStringListBufferDestroy(nextList);
}

void MapNavigator::switchMapSort(MaaContext* context, const std::string& targetOrder)
{
    auto* tasker = MaaContextGetTasker(context);
    ScreenCap cap(context);
    MaaBuffer buffer;
    MaaRect sortBox;
    auto id = MaaContextRunRecognition(context, "MapListSortCheck", "{}", cap.img);
    if (id != MaaInvalidId) {
        MaaBool hit = false;
        MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &sortBox, buffer.get(), nullptr, nullptr);
        if (hit) {
            auto j = jsonutil::parse(buffer.str());
            std::string text = j["best"]["text"].as_string();
            if (text.find(targetOrder) == std::string::npos) {
                screen::clickRandomTarget(context, sortBox.x, sortBox.y, sortBox.width, sortBox.height);
                screen::waitFreezes(context, ScreenArea_MidCenter, 300);
            }
        }
    }
}

std::string MapNavigator::mapName(MapIndex idx)
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

const std::unordered_map<std::string, MapIndex>& MapNavigator::mapTable()
{
    static const std::unordered_map<std::string, MapIndex> table = {
        { "时之尽头的孤岛", MapIndex::IsleOfTimesEnd },
        { "新月郡", MapIndex::CrescentCounty },
        { "圣城修道院", MapIndex::HolyCityMonastery },
        { "格雷米德", MapIndex::Map_Gremid },
        { "熔炉之城", MapIndex::FurnaceCity },
    };
    return table;
}
