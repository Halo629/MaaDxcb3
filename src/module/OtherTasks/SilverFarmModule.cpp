#include "SilverFarmModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/StringUtil.h"

#include <cstdlib>

int SilverFarmModule::m_totalSilver = 0;
int SilverFarmModule::m_enterMazeCount = 0;

namespace {

int extractNumberFromOcrDetail(const std::string& detail)
{
    auto j = jsonutil::parse(detail);
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

MaaBool SilverFarmModule::recordSilver(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customActionName, const char* customActionParam,
                                       MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    ScreenCap cap(context);

    MaaRect dummy;
    MaaBuffer rewardBuf;
    if (!recog::run(context, cap.img, "RewardSilver", "{}", &dummy, rewardBuf.get())) {
        return false;
    }

    auto j = jsonutil::parse(rewardBuf.str());
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

    int silver = 0;
    for (auto& b : boxes) {
        std::string colorOverride =
            fmt(R"({"SilverCountColor":{"roi":[%1,%2,%3,%4]}})", b.x, b.y, b.width, b.height);
        MaaRect colorBox;
        if (!recog::run(context, cap.img, "SilverCountColor", colorOverride.c_str(), &colorBox, nullptr)) {
            continue;
        }

        std::string ocrOverride =
            fmt(R"({"SilverCountOcr":{"roi":[%1,%2,%3,%4]}})", colorBox.x, colorBox.y, colorBox.width, colorBox.height);
        MaaRect ocrBox;
        MaaBuffer ocrBuf;
        if (recog::run(context, cap.img, "SilverCountOcr", ocrOverride.c_str(), &ocrBox, ocrBuf.get())) {
            silver += extractNumberFromOcrDetail(ocrBuf.str());
        }
    }

    m_totalSilver += silver;
    LogUtils::info(fmt("获得银币%1，总共获得银币%2", silver, m_totalSilver));
    return true;
}

MaaBool SilverFarmModule::clearRecord(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    m_totalSilver = 0;
    m_enterMazeCount = 0;
    return true;
}

MaaBool SilverFarmModule::recordEnterMaze(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customActionName, const char* customActionParam,
                                          MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    ++m_enterMazeCount;
    LogUtils::info(fmt("第%1次", m_enterMazeCount));
    return true;
}

namespace {

MaaBool recordSilverThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                          const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return SilverFarmModule::recordSilver(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool clearRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                         const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return SilverFarmModule::clearRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool recordEnterMazeThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                             const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return SilverFarmModule::recordEnterMaze(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void SilverFarmModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "SilverFarmRecord", recordSilverThunk, userData);
    registerCustomAction(res, "ClearSilverFarmRecord", clearRecordThunk, userData);
    registerCustomAction(res, "RecordEnterMaze", recordEnterMazeThunk, userData);
}
