#include "Comm.h"

using namespace std::string_literals;

static int readRemainingTime(MaaContext* context)
{
    ScreenCap cap(context);

    auto detailBuf = MaaStringBufferCreate();
    if (!doRecognition(context, cap.img, "BattleRemainingTime", { }, nullptr, detailBuf)) {
        MaaStringBufferDestroy(detailBuf);
        return 60;
    }
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);
    auto j = json::parse(detail).value_or(json::value { });

    // BattleRemainingTime 是 And 节点，detail 是子识别结果数组，取 OCR 文本
    std::string text;
    if (j.is_array()) {
        for (auto& item : j.as_array()) {
            if (!item.is_object()) {
                continue;
            }
            auto& sub = item["detail"];
            if (!sub.is_object()) {
                continue;
            }
            auto& all = sub["all"];
            if (all.is_array() && !all.empty() && all[0].is_object()) {
                auto& t = all[0]["text"];
                if (t.is_string()) {
                    text = t.as_string();
                    break;
                }
            }
        }
    }
    else if (j.is_object()) {
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
    }

    auto colon = text.find(':');
    if (colon != std::string::npos) {
        return std::atoi(text.substr(0, colon).c_str()) * 60 + std::atoi(text.substr(colon + 1).c_str());
    }
    std::string digits;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
    }
    return digits.empty() ? -1 : std::atoi(digits.c_str());
}

// 豆子 ROI（从左到右），恢复方向为右→左，左边满则右边全满
static const MaaRect kBeanRois[6] = {
    { 530, 1225, 25, 40 }, { 560, 1225, 25, 40 }, { 590, 1225, 25, 40 },
    { 620, 1225, 25, 40 }, { 650, 1225, 25, 40 }, { 680, 1225, 25, 40 },
};

static int getBeanCount(MaaContext* context)
{
    ScreenCap cap(context);
    // 从左到右逐个检测，找到第一个满豆，右边全满
    for (int i = 0; i < 6; ++i) {
        char json[512];
        snprintf(
            json,
            sizeof(json),
            R"({"beanCount":{"recognition":"ColorMatch","method":40,"roi":[%d,%d,%d,%d],)"
            R"("lower":[%d,%d,%d],"upper":[%d,%d,%d],"count":%d,"connected":true}})",
            kBeanRois[i].x,
            kBeanRois[i].y,
            kBeanRois[i].width,
            kBeanRois[i].height,
            g_user_battle.configParam.beanColorLower[0],
            g_user_battle.configParam.beanColorLower[1],
            g_user_battle.configParam.beanColorLower[2],
            g_user_battle.configParam.beanColorUpper[0],
            g_user_battle.configParam.beanColorUpper[1],
            g_user_battle.configParam.beanColorUpper[2],
            g_user_battle.configParam.beanColorCount);

        auto detailBuf = MaaStringBufferCreate();
        if (!doRecognition(context, cap.img, "beanCount", json, nullptr, detailBuf)) {
            MaaStringBufferDestroy(detailBuf);
            continue;
        }

        std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
        MaaStringBufferDestroy(detailBuf);
        if (!json::parse(detail).value_or(json::value { })["filtered"].as_array().empty()) {
            return 6 - i; // 右边全满
        }
    }
    return 0;
}

static bool isSkillReady(MaaContext* context, MaaRect* outBox)
{
    if (g_user_battle.skill == Skill_Xiaoqing) {
        ScreenCap cap(context);
        auto id = MaaContextRunRecognition(context, "XiaoqingSkillReady", "{}", cap.img);
        if (id == MaaInvalidId) {
            return false;
        }
        MaaBool hit = false;
        MaaRect box;
        MaaTaskerGetRecognitionDetail(MaaContextGetTasker(context), id, nullptr, nullptr, &hit, &box, nullptr, nullptr, nullptr);
        if (hit && outBox) {
            outBox->x = box.x + box.width / 2;
            outBox->y = box.y + box.height / 2;
        }
        return hit;
    }
    return false;
}

static bool useSkill(MaaContext* context, const MaaRect& skillPoint)
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, skillPoint.x, skillPoint.y);
    MaaControllerWait(ctrl, id);
    Sleep(200);

    MaaRect releaseBox;
    if (!waitUntilRecognitionSuccess(context, "UseSkill", "{}", &releaseBox, nullptr, 1000)) {
        return false;
    }
    // waitUntilRecognitionSuccess 内部会重新获取 controller，之前的 ctrl 已失效，需重新取
    ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto rid = MaaControllerPostClick(ctrl, releaseBox.x + releaseBox.width / 2, releaseBox.y + releaseBox.height / 2);
    MaaControllerWait(ctrl, rid);
    Sleep(two_second_delay);
    return false;
}

bool isInBattle(MaaContext* context)
{
    for (int i = 0; i < 20; ++i) {
        ScreenCap cap(context);
        if (doRecognition(context, cap.img, "InBattle", { }, nullptr, nullptr)) {
            return true;
        }
        Sleep(500);
    }
    return false;
}

// 判断战斗是否结束，返回结算界面的继续按钮位置
static bool isBattleEnd(MaaContext* context)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    auto id = MaaContextRunRecognition(context, "InBattleResult", "{}", cap.img);
    if (id == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, nullptr, nullptr, nullptr, nullptr);
    if (hit) {
        Sleep(1000);
    }
    return hit;
}

static BattleCryDecision decideCry(int beanCount, int remaining, DWORD lastCryTick, DWORD now)
{
    if (beanCount <= 0) {
        return BATTLE_CRY_NONE;
    }

    // burst：最后 burstSeconds 秒，无论策略全部吼完
    if (g_user_battle.burstSeconds > 0 && remaining >= 0 && remaining <= g_user_battle.burstSeconds) {
        return BATTLE_CRY_BURST;
    }

    // 满豆无条件吼
    if (g_user_battle.cryAnywayThreshold > 0 && beanCount >= g_user_battle.cryAnywayThreshold) {
        return BATTLE_CRY_NORMAL;
    }

    // 超过保留数才吼，受 CD 限制
    if (beanCount > g_user_battle.reserve) {
        if (lastCryTick == 0) {
            return BATTLE_CRY_NORMAL;
        }
        int sinceLast = (int)((now - lastCryTick) / 1000);
        if (sinceLast >= g_user_battle.cryInterval) {
            return BATTLE_CRY_NORMAL;
        }
    }

    return BATTLE_CRY_NONE;
}

static void adjustBattleSpeed(MaaContext* context)
{
    ScreenCap cap(context);
    MaaRect box;
    if (doRecognition(context, cap.img, "Is2XSpeed", "{}", &box, nullptr)) {
        return; // 已经是 2 倍速，不点
    }
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, 40, 39); // target [23,23,33,32] 中心
    MaaControllerWait(ctrl, id);
}

// 战吼：按配置的列坐标滑动一次
static void battleCry(MaaContext* context, int cryColumn)
{
    // cryColumn 是 1 起始（1/2/3=第几列），cryCols 是 0 起始数组，需 -1
    if (cryColumn <= 0 || cryColumn > 3) {
        return;
    }
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto& col = g_user_battle.configParam.cryCols[cryColumn - 1];
    if (col.beginX != 0 || col.beginY != 0) {
        auto s = MaaControllerPostSwipe(ctrl, col.beginX, col.beginY, col.endX, col.endY, col.duration);
        MaaControllerWait(ctrl, s);
        Sleep(1500);
    }
}

// 复活角色：单帧检测到角色死亡且还有免费复活次数时，点击复活按钮（非阻塞）
static void reviveDeadRole(MaaContext* context)
{
    ScreenCap cap(context);

    MaaRect deadBox;
    if (!doRecognition(context, cap.img, "RoleDead", "{}", &deadBox, nullptr)) {
        return;
    }
    // 还有免费复活次数才复活
    if (!doRecognition(context, cap.img, "BattleFreeReviveCount", "{}", nullptr, nullptr)) {
        return;
    }

    MaaContextRunTask(context, "PauseBattle", "{}");

    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostClick(ctrl, deadBox.x, deadBox.y);
    MaaControllerWait(ctrl, id);
    Sleep(200);

    // 复活后若停留在角色信息页，说明复活误匹配了，按 Esc 关闭并恢复战斗
    ScreenCap pauseCap(context);
    if (doRecognition(context, pauseCap.img, "InBattleRoleInfoPage", "{}", nullptr, nullptr)) {
        MaaContextRunTask(context, "PressEsc", "{}");
        MaaContextRunTask(context, "ResumeBattle", "{}");
    }
}

MaaBool battleAI(
    MaaContext* context,
    MaaTaskId task_id,
    const char* node_name,
    const char* custom_action_name,
    const char* custom_action_param,
    MaaRecoId reco_id,
    const MaaRect* box,
    void* trans_arg)
{
    // attach 里的 quit_immediately 为 true 时，立即退出战斗（不走战吼/技能逻辑）
    auto attach = getNodeAttach(context, node_name);
    bool quitImmediately = attach.get("quit_immediately", json::value(false)).as_boolean();
    if (quitImmediately) {
        MaaContextRunTask(context, "QuitBattle", "{}");
        return true;
    }

    if (g_user_battle.cryColumn == 0) {
        LogUtils::log("当前服务器未配置战斗策略,在启动游戏任务中可进行配置", "#ef4444");
        return true;
    }
    if (!isInBattle(context)) {
        return false;
    }

    adjustBattleSpeed(context);

    DWORD lastCryTick = 0;
    MaaRect skillBox { };
    DWORD startTick = GetTickCount();

    while (true) {
        if (MaaTaskerStopping(MaaContextGetTasker(context))) {
            return true;
        }
        ScreenCap cap(context);
        if (isBattleEnd(context)) {
            return true;
        }
        // 复活角色
        reviveDeadRole(context);
        // 获取战斗剩余时间
        int remaining = readRemainingTime(context);
        if (remaining == -1) {
            continue;
        }
        // 获取当前豆子
        int beanCount = getBeanCount(context);
        DWORD now = GetTickCount();

        auto cryDecision = decideCry(beanCount, remaining, lastCryTick, now);

        // 技能使用时间判断
        int battleElapsed = (int)((now - startTick) / 1000);
        if (isSkillReady(context, &skillBox) && g_user_battle.firstSkillTime > 0 && battleElapsed >= g_user_battle.firstSkillTime) {
            if (beanCount >= 5) {
                // 豆子少于5不使用技能, 使用技能后连续吼两次
                if (useSkill(context, skillBox)) {
                    for (int i = 0; i < 2; ++i) {
                        battleCry(context, g_user_battle.cryColumn);
                    }
                    lastCryTick = GetTickCount();
                    continue;
                }
            }
            else if (cryDecision == BATTLE_CRY_BURST && beanCount >= 2) {
                useSkill(context, skillBox); // burst 阶段有2个及以上豆子就使用技能
            }
            continue;
        }

        if (cryDecision == BATTLE_CRY_BURST) {
            // 最后几秒吼完所有豆子
            for (int i = 0; i < beanCount; ++i) {
                if (isBattleEnd(context)) {
                    return true;
                }
                battleCry(context, g_user_battle.cryColumn);
                lastCryTick = GetTickCount();
            }
            continue;
        }

        if (cryDecision == BATTLE_CRY_NORMAL) {
            battleCry(context, g_user_battle.cryColumn);
            lastCryTick = GetTickCount();
        }
    }
    return true;
}

void registerCustomBattle(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "BattleAI", battleAI, user_data);
}
