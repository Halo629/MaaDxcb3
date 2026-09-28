#include "Comm.h"

// 每周魂师指南：识别所有未完成的任务（"完成" 文本），
// 逐个往左偏移 360px 识别任务名，enable/disable 对应的挑战节点。
MaaBool soulMasterGuideWeeklyTasks(
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

    auto attach = getNodeAttach(context, nodeName);
    bool gateShattered = attach.get("ShatteredRealmEnable", json::value(false)).as_boolean();
    bool gateSoulTomb = attach.get("SoulTombEnable", json::value(false)).as_boolean();
    bool gateEngrave = attach.get("EngraveEquipmentEnable", json::value(false)).as_boolean();
    bool gateReforge = attach.get("ReforgeEquipmentEnable", json::value(false)).as_boolean();

    bool soulTombEnable = false;
    bool shatteredRealmEnable = false;
    bool engraveEnable = false;
    bool reforgeEnable = false;

    // 1. 识别所有未完成任务（"完成" 文本，竖向排列）
    auto detailBuf = MaaStringBufferCreate();
    MaaRect dummy;
    if (doRecognition(context, cap.img, "SoulMasterGuideWeeklyNotCompleteTask", "{}", &dummy, detailBuf)) {
        std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
        auto j = json::parse(detail).value_or(json::value { });
        auto all = j["filtered"];

        // 2. 对每个 "完成" box，往左偏移 360px，识别任务名
        for (auto& item : all.as_array()) {
            int x = item["box"][0].as_integer();
            int y = item["box"][1].as_integer();
            int h = item["box"][3].as_integer();

            char nameJson[256];
            snprintf(nameJson, sizeof(nameJson), R"({"name":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})", x - 480, y, 480, h);
            std::string name = ocrText(context, cap.img, "name", nameJson, nullptr);

            if (gateSoulTomb && name.find("灵魂") != std::string::npos) {
                soulTombEnable = true;
            }
            else if (gateShattered && name.find("破碎") != std::string::npos) {
                shatteredRealmEnable = true;
            }
            else if (gateEngrave && name.find("铭刻") != std::string::npos) {
                engraveEnable = true;
            }
            else if (gateReforge && name.find("重铸") != std::string::npos) {
                reforgeEnable = true;
            }
        }
    }
    MaaStringBufferDestroy(detailBuf);

    // 3. enable/disable 对应的挑战节点
    json::value overrideVal = json::object {
        { "ShatteredRealmStart", json::object { { "enabled", shatteredRealmEnable } } },
        { "ActivityCommonSoulTombSub", json::object { { "enabled", soulTombEnable } } },
        { "ActivityCommonEngraveSub", json::object { { "enabled", engraveEnable } } },
        { "ActivityCommonReforgeSub", json::object { { "enabled", reforgeEnable } } },
    };
    MaaContextOverridePipeline(context, overrideVal.to_string().c_str());

    return true;
}

// 从任务文本中解析 "x/y"，返回剩余次数 y - x；解析失败（截断等）返回 0
static int parseRemainCount(const std::string& text)
{
    size_t slash = text.rfind('/');
    if (slash == std::string::npos) {
        slash = text.rfind("\xEF\xBC\x8F"); // 全角斜杠 ／
    }
    if (slash == std::string::npos || slash == 0 || slash + 1 >= text.size()) {
        return 0;
    }

    // 斜杠前的连续数字（当前进度 x）
    int x = 0;
    int place = 1;
    size_t i = slash;
    while (i > 0) {
        char c = text[i - 1];
        if (c < '0' || c > '9') {
            break;
        }
        x += (c - '0') * place;
        place *= 10;
        --i;
    }

    // 斜杠后的连续数字（目标次数 y）
    int y = 0;
    size_t j = slash + 1;
    while (j < text.size()) {
        char c = text[j];
        if (c < '0' || c > '9') {
            break;
        }
        y = y * 10 + (c - '0');
        ++j;
    }

    if (y <= 0) {
        return 0;
    }
    int remain = y - x;
    return remain > 0 ? remain : 0;
}

// 活动通用任务：识别所有 "未完成" 任务，往左偏移 420px/上移 20px/高 50 识别任务名，
// 解析 "x/y" 剩余次数设置 max_hit，并按需往上滑动 100px 继续查找。
MaaBool activityCommonTasks(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    auto attach = getNodeAttach(context, nodeName);
    bool gateFood = attach.get("UseFoodEnable", json::value(false)).as_boolean();
    bool gateEngrave = attach.get("EngraveEquipmentEnable", json::value(false)).as_boolean();
    bool gateSoulTomb = attach.get("SoulTombEnable", json::value(false)).as_boolean();
    bool gateReforge = attach.get("ReforgeEquipmentEnable", json::value(false)).as_boolean();
    bool gateScroll = attach.get("UseScrollEnable", json::value(false)).as_boolean();

    bool foodEnable = false;
    bool engraveEnable = false;
    bool soulTombEnable = false;
    bool reforgeEnable = false;
    bool scrollEnable = false;
    int foodMax = 3;
    int engraveMax = 2;
    int soulTombMax = 2;
    int reforgeMax = 10;
    int scrollMax = 1;
    bool allFinished = false;

    constexpr int kMaxSwipe = 3;

    for (int swipe = 0; swipe <= kMaxSwipe; ++swipe) {
        ScreenCap cap(context);

        bool foundAny = false;
        auto detailBuf = MaaStringBufferCreate();
        MaaRect dummy;
        if (doRecognition(context, cap.img, "ActivityCommonHasNotCompleteTasks", "{}", &dummy, detailBuf)) {
            std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
            auto j = json::parse(detail).value_or(json::value { });
            auto all = j["all"];

            for (auto& item : all.as_array()) {
                std::string text = item["text"].as_string();
                auto ptPos = text.find("已领取");
                if (ptPos != std::string::npos) {
                    allFinished = true;
                    break;
                }
                foundAny = true;
                int x = item["box"][0].as_integer();
                int y = item["box"][1].as_integer();

                char nameJson[256];
                snprintf(nameJson, sizeof(nameJson), R"({"name":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})", x - 480, y - 40, 480, 50);
                std::string name = ocrText(context, cap.img, "name", nameJson, nullptr);

                int remain = parseRemainCount(name);
                if (gateFood && name.find("食物") != std::string::npos) {
                    foodEnable = true;
                    if (remain > 0) {
                        foodMax = remain;
                    }
                }
                else if (gateEngrave && name.find("铭刻") != std::string::npos) {
                    engraveEnable = true;
                    if (remain > 0) {
                        engraveMax = remain;
                    }
                }
                else if (gateSoulTomb && (name.find("灵魂之墓") != std::string::npos || name.find("灵魂") != std::string::npos)) {
                    soulTombEnable = true;
                    if (remain > 0) {
                        soulTombMax = remain;
                    }
                }
                else if (gateReforge && name.find("重铸") != std::string::npos) {
                    reforgeEnable = true;
                    if (remain > 0) {
                        reforgeMax = remain;
                    }
                }
                else if (gateScroll && (name.find("唤灵") != std::string::npos || name.find("召唤") != std::string::npos)) {
                    scrollEnable = true;
                    if (remain > 0) {
                        scrollMax = remain;
                    }
                }
            }
        }
        MaaStringBufferDestroy(detailBuf);

        // 所有启用的子任务都识别到了，无需再滑动
        bool allFound = (!gateFood || foodEnable) && (!gateEngrave || engraveEnable) && (!gateSoulTomb || soulTombEnable)
                        && (!gateReforge || reforgeEnable) && (!gateScroll || scrollEnable);
        if (allFinished || allFound) {
            break;
        }
        // 滑到列表底部（本页已无 "未完成"），停止
        if (!foundAny && swipe > 0) {
            break;
        }
        if (swipe < kMaxSwipe) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto sid = MaaControllerPostSwipe(ctrl, 400, 800, 400, 700, 200);
            MaaControllerWait(ctrl, sid);
            Sleep(1000);
        }
    }

    json::value overrideVal = json::object { };
    if (gateFood) {
        overrideVal["ActivityCommonUseFood"] = json::value(json::object { { "enabled", foodEnable } });
        overrideVal["ActivityCommonFoodPageUseFood"] = json::value(json::object { { "max_hit", foodMax } });
    }
    if (gateEngrave) {
        overrideVal["ActivityCommonEngraveSub"] = json::value(json::object { { "enabled", engraveEnable } });
        overrideVal["EngraveStart"] = json::value(json::object { { "max_hit", engraveMax } });
    }
    if (gateSoulTomb) {
        overrideVal["ActivityCommonSoulTombSub"] = json::value(json::object { { "enabled", soulTombEnable } });
        overrideVal["SoulTombStartBattle"] = json::value(json::object { { "max_hit", soulTombMax } });
    }
    if (gateReforge) {
        overrideVal["ActivityCommonReforgeSub"] = json::value(json::object { { "enabled", reforgeEnable } });
        overrideVal["ReforgeConfirm"] = json::value(json::object { { "max_hit", reforgeMax } });
    }
    if (gateScroll) {
        overrideVal["ActivityCommonUseScrollSub"] = json::value(json::object { { "enabled", scrollEnable }, { "max_hit", scrollMax } });
    }
    MaaContextOverridePipeline(context, overrideVal.to_string().c_str());

    return true;
}

void registerCustomSteward(MaaResource* res, void* user_data)
{
    registerCustomAction(res, "SoulMasterGuideWeeklyTasks", soulMasterGuideWeeklyTasks, user_data);
    registerCustomAction(res, "ActivityCommonTasks", activityCommonTasks, user_data);
}
