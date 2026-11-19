#include "CelestialIsleModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "map/MapNavigator.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"
#include "util/ScreenUtil.h"
#include "util/StringUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

int CelestialIsleModule::enemyCardCount_ = 0;

namespace {

struct CardInfo
{
    MaaRect box;
    std::vector<int> points;
};

struct CardEntry
{
    int points = 0;
    int count = 0;
    MaaRect box;
};

struct Candidate
{
    int optIdx;
    int points;
};

// 识别卡牌区域，返回所有竞技场石牌的 {点数, 数量, 框}
// 文本格式: 竞技场石牌x: y/z   x=点数, y=数量, z=1
// 注意冒号后面可能有空格: "竞技场石牌1: 3/1"
std::vector<CardEntry> getCardPoints(MaaContext* context)
{
    recog::waitUntil(
        context,
        "back",
        R"({"back":{"recognition":"OCR","roi":[267,523,197,662],"expected":["返回"]}})",
        nullptr,
        nullptr);
    ScreenCap cap(context);
    std::vector<CardEntry> result;
    MaaBuffer buf;
    MaaRect dummy;
    if (!recog::run(context, cap.img, "card", R"({"card":{"recognition":"OCR","roi":[267,523,197,662]}})", &dummy, buf.get())) {
        return result;
    }
    auto j = jsonutil::parse(buf.str());

    for (auto& item : j["all"].as_array()) {
        std::string text = item["text"].as_string();
        auto ptPos = text.find("竞技场石牌");
        if (ptPos == std::string::npos) {
            continue;
        }
        // 跳过 "竞技场石牌" 取点数
        const char* afterTag = text.c_str() + ptPos + strlen("竞技场石牌");
        int pts = std::atoi(afterTag);

        // 找冒号（中文或英文），冒号后可能有空格
        auto colon1 = text.find(':', ptPos);
        auto colon2 = text.find("：", ptPos);
        auto colon = (colon1 != std::string::npos && (colon2 == std::string::npos || colon1 < colon2)) ? colon1 : colon2;
        if (colon == std::string::npos) {
            continue;
        }
        int colonLen = (colon == colon1) ? 1 : 3; // 中文冒号 UTF-8 占 3 字节
        int afterColon = (int)colon + colonLen;
        while (afterColon < (int)text.size() && text[afterColon] == ' ') {
            ++afterColon;
        }
        // 取 / 前的数字作为数量
        auto slash = text.find('/', afterColon);
        int count = (slash != std::string::npos) ? std::atoi(text.substr(afterColon, slash - afterColon).c_str())
                                                 : std::atoi(text.c_str() + afterColon);

        int bx = item["box"][0].as_integer(), by = item["box"][1].as_integer();
        int bw = item["box"][2].as_integer(), bh = item["box"][3].as_integer();

        result.push_back({ pts, count, { bx, by, bw, bh } });
    }
    return result;
}

// 从所有候选牌中选择最佳组合：返回选中的 {optIdx, points} 集合
// 优先用大点数牌，张数少。enemyCardCount 按引用传入以便读取/回填。
std::vector<Candidate> selectBestCards(const CardInfo* cards, int optionCount, int& enemyCardCount)
{
    std::vector<Candidate> cands;
    for (int i = 0; i < optionCount; ++i) {
        for (size_t k = 0; k < cards[i].points.size(); ++k) {
            cands.push_back({ i, cards[i].points[k] });
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.points > b.points; });

    if (enemyCardCount == 0) {
        enemyCardCount = 18;
    }
    int bestSum = 22;
    unsigned bestMask = 0;
    for (unsigned mask = 1; mask < (1u << cands.size()); ++mask) {
        int sum = 0;
        for (size_t j = 0; j < cands.size(); ++j) {
            if (mask & (1u << j)) {
                sum += cands[j].points;
            }
        }
        if (sum >= enemyCardCount && sum < bestSum) {
            bestSum = sum;
            bestMask = mask;
        }
    }

    std::vector<Candidate> result;
    for (size_t j = 0; j < cands.size(); ++j) {
        if (bestMask & (1u << j)) {
            result.push_back(cands[j]);
        }
    }
    return result;
}

// 按选择出牌：选项→重OCR→按点数匹配点击
void playSelectedCards(MaaContext* context, CardInfo* cards, const std::vector<Candidate>& selected)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    for (int i = 0; i < 3; ++i) {
        std::vector<int> needPoints;
        for (auto& s : selected) {
            if (s.optIdx == i) {
                needPoints.push_back(s.points);
            }
        }
        if (needPoints.empty()) {
            continue;
        }

        for (auto targetPts : needPoints) {
            screen::clickRandomTarget(context, cards[i].box.x, cards[i].box.y, cards[i].box.width, cards[i].box.height);
            screen::waitFreezes(context, ScreenArea_BotRight, 500, 3000);

            auto cardInfos = getCardPoints(context);
            MaaRect targetBox;
            bool found = false;
            for (auto& ce : cardInfos) {
                if (ce.points == targetPts) {
                    targetBox = ce.box;
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }
            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto cid = MaaControllerPostClick(ctrl, targetBox.x + targetBox.width / 2, targetBox.y + targetBox.height / 2);
            MaaControllerWait(ctrl, cid);
            Sleep(2000);
        }
    }
}

} // namespace

// ──── CelestialIsleGotoMap: 进入天界岛 ────

MaaBool CelestialIsleModule::gotoMap(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    std::string mapOfRole = MapNavigator::mapOfRole(context);
    bool sameMap = !mapOfRole.empty() && mapOfRole.find("天界") != std::string::npos;
    if (!sameMap) {
        MapNavigator::setNextNode(context, nodeName, "CelestialIsleEnterMap");
        return true;
    }

    // 角色与目标在同一张图 → 进时之尽头的孤岛重置
    // 1. 切换为正序
    MapNavigator::switchMapSort(context, "正序");

    // 2. 进时之尽头的孤岛
    MapNavigator::enterMapByName(context, "孤岛");

    // 3. 退出返回营火，重定向重新导航
    MapNavigator::exitToCampfire(context);
    MapNavigator::setNextNode(context, nodeName, "CelestialIsleMapEntry");
    return true;
}

// ──── CelestialIsleGotoMinerCamp: 进入矿工营地 ────

MaaBool CelestialIsleModule::gotoMinerCamp(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    std::string mapOfRole = MapNavigator::mapOfRole(context);
    bool sameMap = !mapOfRole.empty() && mapOfRole.find("矿工") != std::string::npos;
    if (!sameMap) {
        return false; // 不在同一张图，fall through 到 JSON click
    }

    LogUtils::warn(fmt("天界岛: 角色已在 %1，需要重置", mapOfRole));
    MapNavigator::switchMapSort(context, "正序");
    MapNavigator::enterMapByName(context, "孤岛");
    MapNavigator::exitToCampfire(context);

    MapNavigator::setNextNode(context, nodeName, "CelestialIsleEnterMap");
    return true;
}

// ──── RecordEnemyCard: 记录敌方卡牌数量 ────

MaaBool CelestialIsleModule::recordEnemyCard(
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
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);

    // 1. ColorMatch 找白色数字区域
    MaaRect colorBox;
    char colorJson[256];
    snprintf(
        colorJson,
        sizeof(colorJson),
        R"({"cc":{"recognition":"ColorMatch","roi":[465,1004,68,83],"method":40,)"
        R"("lower":[0,0,200],"upper":[255,55,255],"count":3}})");
    if (!recog::run(context, cap.img, "cc", colorJson, &colorBox, nullptr)) {
        enemyCardCount_ = 0;
        return false;
    }

    // 2. OCR 识别数字
    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"oc":{"recognition":"OCR","roi":[%d,%d,%d,%d],"expected":["\\d+"],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    MaaBuffer buffer;
    MaaRect ocrBox;
    if (!recog::run(context, cap.img, "oc", ocrJson, &ocrBox, buffer.get())) {
        return false;
    }
    auto j = jsonutil::parse(buffer.str());
    std::string text;
    if (auto& best = j["best"]; best.is_object()) {
        text = best["text"].as_string();
    }
    else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
        text = all[0]["text"].as_string();
    }
    enemyCardCount_ = std::atoi(text.c_str());
    LogUtils::info(fmt("天界岛: 敌方卡牌点数: %1", enemyCardCount_));
    return true;
}

// ──── FreeTeleportRecord: 记录免费传送已开启 ────

MaaBool CelestialIsleModule::freeTeleportRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    return true;
}

// ──── FreeTeleportEnabled: 检查免费传送是否已开启 ────

MaaBool CelestialIsleModule::freeTeleportEnabled(
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
    return true;
}

// ──── TrialGroundDoorCanPassRecord: 记录试炼场大门可以通过 ────

MaaBool CelestialIsleModule::trialGroundDoorCanPassRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    return true;
}

// ──── TrialGroundDoorCanPass: 检查试炼场大门是否可通过 ────

MaaBool CelestialIsleModule::trialGroundDoorCanPass(
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
    return true;
}

// ──── EnlightenmentConfidantRecord: 记录启迪之书关系已到知己 ────

MaaBool CelestialIsleModule::enlightenmentConfidantRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    return true;
}

// ──── EnlightenmentConfidant: 检查启迪之书关系是否已到知己 ────

MaaBool CelestialIsleModule::enlightenmentConfidant(
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
    return true;
}

// ──── QueenFamiliarRecord: 记录与女王关系已到熟识 ────

MaaBool CelestialIsleModule::queenFamiliarRecord(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    return true;
}

// ──── QueenFamiliar: 检查与女王关系是否已到熟识 ────

MaaBool CelestialIsleModule::queenFamiliar(
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
    return true;
}

// ──── EnemyCardNotRecord: 检查敌方卡牌是否未记录 ────

MaaBool CelestialIsleModule::enemyCardNotRecord(
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
    return enemyCardCount_ == 0;
}

// ──── GambleDecision: 赌博决策 ────

MaaBool CelestialIsleModule::gambleDecision(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    static const char* kPointOptions[] = { "小点数", "中点数", "大点数" };

    CardInfo cards[3];

    // 1. 扫描每个选项，记录所有牌的点数
    for (int i = 0; i < 3; ++i) {
        ScreenCap cap(context);
        char json[256];
        snprintf(json, sizeof(json), R"({"opt":{"recognition":"OCR","expected":"%s","roi":[271,642,172,392]}})", kPointOptions[i]);
        MaaRect optBox;
        if (!recog::run(context, cap.img, "opt", json, &optBox, nullptr)) {
            continue;
        }
        cards[i].box = optBox;

        screen::clickRandomTarget(context, optBox.x, optBox.y, optBox.width, optBox.height);
        screen::waitFreezes(context, ScreenArea_BotRight, 500, 3000);

        for (auto& ce : getCardPoints(context)) {
            for (int n = 0; n < ce.count; ++n) {
                cards[i].points.push_back(ce.points);
            }
        }

        MaaContextRunTask(context, "RightDownScreenBackOption", "{}");
    }

    // 2. 选择牌
    auto selected = selectBestCards(cards, 3, enemyCardCount_);
    if (selected.empty()) {
        LogUtils::warn("天界岛: 无法凑齐对手点数,今日放弃出牌");
        return true;
    }

    int totalPoints = 0;
    for (auto& s : selected) {
        totalPoints += s.points;
    }
    LogUtils::info(fmt("天界岛: 赌博决策完成，对手%1点，我方%2点", enemyCardCount_, totalPoints));

    // 3. 出牌
    playSelectedCards(context, cards, selected);
    return true;
}

namespace {

MaaBool gotoMapThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                     const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::gotoMap(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool gotoMinerCampThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::gotoMinerCamp(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool recordEnemyCardThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                             const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                             void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CelestialIsleModule::recordEnemyCard(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool freeTeleportRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::freeTeleportRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool freeTeleportEnabledThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                 const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                 void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CelestialIsleModule::freeTeleportEnabled(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool trialGroundDoorCanPassRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                          const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::trialGroundDoorCanPassRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool trialGroundDoorCanPassThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CelestialIsleModule::trialGroundDoorCanPass(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool enlightenmentConfidantRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                          const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::enlightenmentConfidantRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool enlightenmentConfidantThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                    const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                    void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CelestialIsleModule::enlightenmentConfidant(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool queenFamiliarRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                 const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::queenFamiliarRecord(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool queenFamiliarThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                           void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CelestialIsleModule::queenFamiliar(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool enemyCardNotRecordThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                                void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return CelestialIsleModule::enemyCardNotRecord(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool gambleDecisionThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                            const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return CelestialIsleModule::gambleDecision(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void CelestialIsleModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "CelestialIsleGotoMap", gotoMapThunk, userData);
    registerCustomAction(res, "CelestialIsleGotoMinerCamp", gotoMinerCampThunk, userData);
    registerCustomRecognition(res, "RecordEnemyCard", recordEnemyCardThunk, userData);
    registerCustomAction(res, "GambleDecision", gambleDecisionThunk, userData);
    registerCustomAction(res, "FreeTeleportRecord", freeTeleportRecordThunk, userData);
    registerCustomRecognition(res, "FreeTeleportEnabled", freeTeleportEnabledThunk, userData);
    registerCustomAction(res, "TrialGroundDoorCanPassRecord", trialGroundDoorCanPassRecordThunk, userData);
    registerCustomRecognition(res, "TrialGroundDoorCanPass", trialGroundDoorCanPassThunk, userData);
    registerCustomAction(res, "EnlightenmentConfidantRecord", enlightenmentConfidantRecordThunk, userData);
    registerCustomRecognition(res, "EnlightenmentConfidant", enlightenmentConfidantThunk, userData);
    registerCustomAction(res, "QueenFamiliarRecord", queenFamiliarRecordThunk, userData);
    registerCustomRecognition(res, "QueenFamiliar", queenFamiliarThunk, userData);
    registerCustomRecognition(res, "EnemyCardNotRecord", enemyCardNotRecordThunk, userData);
}
