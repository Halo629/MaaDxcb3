#include "Comm.h"

namespace
{

// 全局记录：总共获得银币、进入迷宫次数
int g_totalSilver = 0;
int g_enterMazeCount = 0;

// 从 OCR 识别 detail 里提取第一个数字
int extractNumberFromOcrDetail(const std::string& detail)
{
    auto j = json::parse(detail).value_or(json::value { });
    std::string text;
    if (auto& best = j["best"]; best.is_object()) {
        text = best["text"].as_string();
    }
    else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
        text = all[0]["text"].as_string();
    }
    std::string digits;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
        else if (!digits.empty()) {
            break;
        }
    }
    return digits.empty() ? 0 : std::atoi(digits.c_str());
}

} // namespace

// 自定义动作：记录银币数量（遍历 RewardSilver 识别到的所有银币图标，逐个累加）
MaaBool supremeCourtSilverFarmRecordAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    ScreenCap cap(context);

    // 1. 识别所有银币图标
    MaaRect dummy;
    auto rewardBuf = MaaStringBufferCreate();
    if (!doRecognition(context, cap.img, "RewardSilver", "{}", &dummy, rewardBuf)) {
        MaaStringBufferDestroy(rewardBuf);
        return false;
    }
    std::string rewardDetail(MaaStringBufferGet(rewardBuf), MaaStringBufferSize(rewardBuf));
    MaaStringBufferDestroy(rewardBuf);

    auto j = json::parse(rewardDetail).value_or(json::value { });
    std::vector<MaaRect> boxes;
    if (j.contains("all") && j["all"].is_array()) {
        for (auto& item : j["all"].as_array()) {
            if (item.is_object() && item.contains("box") && item["box"].is_array()) {
                auto& b = item["box"].as_array();
                if (b.size() >= 4) {
                    boxes.push_back({ b[0].as_integer(), b[1].as_integer(), b[2].as_integer(), b[3].as_integer() });
                }
            }
        }
    }

    // 2. 对每个银币图标：先 ColorMatch 定位数字颜色，再 OCR 读数字
    int silver = 0;
    for (auto& b : boxes) {
        std::string colorOverride =
            fmt(R"({"SilverCountColor":{"roi":[%1,%2,%3,%4]}})", b.x, b.y, b.width, b.height);
        MaaRect colorBox;
        if (!doRecognition(context, cap.img, "SilverCountColor", colorOverride.c_str(), &colorBox, nullptr)) {
            continue;
        }

        std::string ocrOverride =
            fmt(R"({"SilverCountOcr":{"roi":[%1,%2,%3,%4]}})", colorBox.x, colorBox.y, colorBox.width, colorBox.height);
        MaaRect ocrBox;
        auto ocrBuf = MaaStringBufferCreate();
        if (doRecognition(context, cap.img, "SilverCountOcr", ocrOverride.c_str(), &ocrBox, ocrBuf)) {
            std::string ocrDetail(MaaStringBufferGet(ocrBuf), MaaStringBufferSize(ocrBuf));
            silver += extractNumberFromOcrDetail(ocrDetail);
        }
        MaaStringBufferDestroy(ocrBuf);
    }

    g_totalSilver += silver;
    LogUtils::info(fmt("获得银币%1，总共获得银币%2", silver, g_totalSilver));
    return true;
}

// 自定义动作：清空银币记录（任务开始前调用）
MaaBool clearSilverFarmRecordAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    g_totalSilver = 0;
    g_enterMazeCount = 0;
    return true;
}

// 自定义动作：记录进入迷宫次数
MaaBool recordEnterMazeAct(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    ++g_enterMazeCount;
    LogUtils::info(fmt("第%1次", g_enterMazeCount));
    return true;
}

void registerCustomSilverFarm(MaaResource* res, void* userData)
{
    registerCustomAction(res, "SupremeCourtSilverFarmRecord", supremeCourtSilverFarmRecordAct, userData);
    registerCustomAction(res, "ClearSilverFarmRecord", clearSilverFarmRecordAct, userData);
    registerCustomAction(res, "RecordEnterMaze", recordEnterMazeAct, userData);
}
