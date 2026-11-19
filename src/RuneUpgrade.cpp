#include "Comm.h"

namespace
{

// 中文符文名 → 模板节点名（与 RuneUnLock.json 中的 RuneXxx 节点对应）。
const std::map<std::string, std::string>& runeNodeTable()
{
    static const std::map<std::string, std::string> kTable = {
        { "刃甲", "RuneRenjia" },   { "迅击", "RuneXunji" },       { "狂热", "RuneKuangre" },  { "伺机", "RuneSiji" },
        { "雷霆", "RuneLeiting" },  { "颂歌", "RuneSongge" },      { "圣痕", "RuneShenghen" }, { "救赎", "RuneJiushu" },
        { "复苏", "RuneFusu" },     { "舞动", "RuneWudong" },      { "破势", "RunePoshi" },    { "裁决", "RuneCaijue" },
        { "宁静", "RuneNingjing" }, { "追猎", "RuneZhuilie" },     { "战意", "RuneZhanyi" },   { "驭魔", "RuneYumo" },
        { "巫咒", "RuneWuzhou" },   { "激励", "RuneJili" },        { "牺牲", "RuneXisheng" },  { "赋能", "RuneFuneng" },
        { "晶能", "RuneJingneng" }, { "双向", "RuneShuangxiang" },
    };
    return kTable;
}

// 解析符文参数 → 模板节点名。支持中文名（刃甲）、英文名（Renjia）以及 RuneXxx 全名。
std::string resolveRuneNode(const std::string& rune)
{
    if (rune.empty()) {
        return { };
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

// 符文等级读取结果。
enum class RuneLevelStatus
{
    NotFound,  // 符文模板未识别到
    NoNumber,  // 识别到符文，但等级数字不存在
    HasNumber, // 识别到符文和等级数字，outLevel 有效
};

// 识别符文并读取等级数字。outRuneBox 非空时返回符文所在框。
RuneLevelStatus readRuneLevel(
    MaaContext* context,
    const MaaImageBuffer* image,
    const std::string& rune,
    int& outLevel,
    MaaRect* outRuneBox)
{
    std::string runeNode = resolveRuneNode(rune);
    if (runeNode.empty()) {
        return RuneLevelStatus::NotFound;
    }

    MaaRect runeBox;
    if (!doRecognition(context, image, runeNode.c_str(), "{}", &runeBox, nullptr)) {
        return RuneLevelStatus::NotFound;
    }
    if (outRuneBox) {
        *outRuneBox = runeBox;
    }

    std::string colorOverride =
        fmt(R"({"RuneLevelColor":{"roi":[%1,%2,%3,%4]}})", runeBox.x, runeBox.y, runeBox.width, runeBox.height);
    MaaRect colorBox;
    if (!doRecognition(context, image, "RuneLevelColor", colorOverride.c_str(), &colorBox, nullptr)) {
        return RuneLevelStatus::NoNumber;
    }

    std::string levelOverride =
        fmt(R"({"RuneLevel":{"roi":[%1,%2,%3,%4]}})", colorBox.x, colorBox.y, colorBox.width, colorBox.height);
    MaaRect levelBox;
    std::string levelText = ocrText(context, image, "RuneLevel", levelOverride.c_str(), &levelBox);

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

// 自定义识别：判断符文能否升级（未满级）。
MaaBool runeCanUpgradeCheck(
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

// 自定义识别：判断符文是否满级（等级 >= 50）。
MaaBool runeMaxLevel(
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

void registerCustomRuneUpgrade(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "RuneCanUpgradeCheck", runeCanUpgradeCheck, userData);
    registerCustomRecognition(res, "RuneMaxLevel", runeMaxLevel, userData);
}
