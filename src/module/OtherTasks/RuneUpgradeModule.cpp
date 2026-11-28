#include "RuneUpgradeModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "util/JsonUtil.h"
#include "util/Recognition.h"
#include "util/StringUtil.h"

#include <cstdlib>
#include <map>
#include <string>

namespace {

// 中文符文名 → 模板节点名（与 RuneUnLock.json 中的 RuneXxx 节点对应）。
const std::map<std::string, std::string>& runeNodeTable()
{
    static const std::map<std::string, std::string> g_table = {
        { "刃甲", "RuneRenjia" },   { "迅击", "RuneXunji" },       { "狂热", "RuneKuangre" },  { "伺机", "RuneSiji" },
        { "雷霆", "RuneLeiting" },  { "颂歌", "RuneSongge" },      { "圣痕", "RuneShenghen" }, { "救赎", "RuneJiushu" },
        { "复苏", "RuneFusu" },     { "舞动", "RuneWudong" },      { "破势", "RunePoshi" },    { "裁决", "RuneCaijue" },
        { "宁静", "RuneNingjing" }, { "追猎", "RuneZhuilie" },     { "战意", "RuneZhanyi" },   { "驭魔", "RuneYumo" },
        { "巫咒", "RuneWuzhou" },   { "激励", "RuneJili" },        { "牺牲", "RuneXisheng" },  { "赋能", "RuneFuneng" },
        { "晶能", "RuneJingneng" }, { "双向", "RuneShuangxiang" },
    };
    return g_table;
}

// 解析符文参数 → 模板节点名。支持中文名（刃甲）、英文名（Renjia）以及 RuneXxx 全名。
std::string resolveRuneNode(const std::string& rune)
{
    if (rune.empty()) {
        return {};
    }
    auto it = runeNodeTable().find(rune);
    if (it != runeNodeTable().end()) {
        return it->second;
    }
    if (rune.size() > 4 && rune.compare(0, 4, "Rune") == 0) {
        return rune;
    }
    return "Rune" + rune;
}

enum class RuneLevelStatus
{
    NotFound,
    NoNumber,
    HasNumber,
};

RuneLevelStatus readRuneLevel(MaaContext* context, const MaaImageBuffer* image, const std::string& rune,
                              int& outLevel, MaaRect* outRuneBox)
{
    std::string runeNode = resolveRuneNode(rune);
    if (runeNode.empty()) {
        return RuneLevelStatus::NotFound;
    }

    MaaRect runeBox;
    if (!recog::run(context, image, runeNode.c_str(), "{}", &runeBox, nullptr)) {
        return RuneLevelStatus::NotFound;
    }
    if (outRuneBox) {
        *outRuneBox = runeBox;
    }

    std::string colorOverride =
        fmt(R"({"RuneLevelColor":{"roi":[%1,%2,%3,%4]}})", runeBox.x, runeBox.y, runeBox.width, runeBox.height);
    MaaRect colorBox;
    if (!recog::run(context, image, "RuneLevelColor", colorOverride.c_str(), &colorBox, nullptr)) {
        return RuneLevelStatus::NoNumber;
    }

    std::string levelOverride =
        fmt(R"({"RuneLevel":{"roi":[%1,%2,%3,%4]}})", colorBox.x, colorBox.y, colorBox.width, colorBox.height);
    MaaRect levelBox;
    std::string levelText = recog::readTextViaEntry(context, image, "RuneLevel", levelOverride.c_str(), &levelBox);

    std::string digits;
    for (char c : levelText) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
        else if (!digits.empty()) {
            break;
        }
    }
    if (digits.empty()) {
        return RuneLevelStatus::NoNumber;
    }
    outLevel = std::atoi(digits.c_str());
    return RuneLevelStatus::HasNumber;
}

} // namespace

MaaBool RuneUpgradeModule::canUpgrade(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customRecognitionName, const char* customRecognitionParam,
                                      const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                      MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    std::string rune = param.is_object() ? param.get("target_rune", json::value("")).as_string() : "";
    if (rune.empty()) {
        LogUtils::error("RuneCanUpgradeCheck: target_rune 缺失");
        return false;
    }

    int level = 0;
    MaaRect runeBox;
    auto status = readRuneLevel(context, image, rune, level, &runeBox);

    if (status == RuneLevelStatus::NotFound) {
        LogUtils::debug(fmt("RuneCanUpgradeCheck: 未识别到符文 %1", rune));
        return false;
    }
    if (status == RuneLevelStatus::NoNumber) {
        LogUtils::info(fmt("[%1]等级0，开始升级...", rune));
        if (outBox) {
            *outBox = runeBox;
        }
        return true;
    }

    if (level >= 50) {
        LogUtils::info(fmt("[%1]已满级，跳过", rune));
        return false;
    }

    LogUtils::info(fmt("[%1]等级%2，开始升级...", rune, level));
    if (outBox) {
        *outBox = runeBox;
    }
    if (outDetail) {
        MaaStringBufferSet(outDetail, std::to_string(level).c_str());
    }
    return true;
}

MaaBool RuneUpgradeModule::isMaxLevel(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customRecognitionName, const char* customRecognitionParam,
                                      const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                      MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    std::string rune = param.is_object() ? param.get("target_rune", json::value("")).as_string() : "";
    if (rune.empty()) {
        LogUtils::error("RuneMaxLevel: target_rune 缺失");
        return false;
    }

    int level = 0;
    MaaRect runeBox;
    auto status = readRuneLevel(context, image, rune, level, &runeBox);

    if (status != RuneLevelStatus::HasNumber || level < 50) {
        return false;
    }

    LogUtils::info(fmt("[%1]已满级，跳过", rune));
    if (outBox) {
        *outBox = runeBox;
    }
    if (outDetail) {
        MaaStringBufferSet(outDetail, std::to_string(level).c_str());
    }
    return true;
}

namespace {

MaaBool canUpgradeThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return RuneUpgradeModule::canUpgrade(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool isMaxLevelThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return RuneUpgradeModule::isMaxLevel(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void RuneUpgradeModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "RuneCanUpgradeCheck", canUpgradeThunk, userData);
    registerCustomRecognition(res, "RuneMaxLevel", isMaxLevelThunk, userData);
}
