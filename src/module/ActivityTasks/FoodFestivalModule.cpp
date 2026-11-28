#include "FoodFestivalModule.h"

#include "Registry.h"
#include "config/Config.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <windows.h>

namespace {

struct FoodInfo
{
    std::string name;
    MaaRect box;
    int quantity = 0;
};

// OCR [334,196,71,40] 识别 "+x" 获取 per-click 值
int getCurrentContribution(MaaContext* context)
{
    ScreenCap cap(context);
    MaaBuffer buf;
    MaaRect dummy;
    int val = 0;
    if (recog::run(
            context,
            cap.img,
            "perclick",
            R"({"perclick":{"recognition":"OCR","roi":[334,196,71,40],"expected":["\\+\\d+"],"only_rec":true}})",
            &dummy,
            buf.get())) {
        auto j = jsonutil::parse(buf.str());
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
        if (!text.empty() && text[0] == '+') {
            text = text.substr(1);
        }
        val = std::atoi(text.c_str());
    }
    return val;
}

// 识别饱腹度
int getCurSatiety(MaaContext* context)
{
    ScreenCap cap(context);
    MaaBuffer buf;
    int val = 0;
    if (recog::run(context, cap.img, "CurrentSatiety", "{}", nullptr, buf.get())) {
        auto j = jsonutil::parse(buf.str());
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }

        val = std::atoi(text.c_str());
    }
    return val;
}

// ColorMatch 白色 + OCR 数字获取数量
int getFoodQuantity(MaaContext* context, MaaRect box)
{
    // 扩大 box: y-50, w+80
    MaaRect expanded = { box.x, box.y - 50, box.width + 80, box.height + 50 };
    ScreenCap cap(context);

    // ColorMatch 白色
    char colorJson[256];
    snprintf(
        colorJson,
        sizeof(colorJson),
        R"({"cc":{"recognition":"ColorMatch","roi":[%d,%d,%d,%d],"method":40,)"
        R"("lower":[0,0,200],"upper":[255,55,255],"count":3}})",
        expanded.x,
        expanded.y,
        expanded.width,
        expanded.height);
    MaaRect colorBox;
    if (!recog::run(context, cap.img, "cc", colorJson, &colorBox, nullptr)) {
        return 0;
    }

    // OCR 数字
    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"oc":{"recognition":"OCR","roi":[%d,%d,%d,%d],"expected":["\\d+"],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    MaaBuffer buf;
    MaaRect ocrBox;
    int count = 0;
    if (recog::run(context, cap.img, "oc", ocrJson, &ocrBox, buf.get())) {
        auto j = jsonutil::parse(buf.str());
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
        count = std::atoi(text.c_str());
    }
    return count;
}

// ColorMatch 黄色 + OCR 二次识别食物名，识别不到返回空串
std::string refineFoodName(MaaContext* context, const MaaImageBuffer* img, MaaRect box)
{
    char colorJson[256];
    snprintf(
        colorJson,
        sizeof(colorJson),
        R"({"cc":{"recognition":"ColorMatch","roi":[%d,%d,%d,%d],"method":40,)"
        R"("lower":[15,90,100],"upper":[30,100,200],"count":30}})",
        box.x,
        box.y,
        box.width,
        box.height);
    MaaRect colorBox;
    if (!recog::run(context, img, "cc", colorJson, &colorBox, nullptr)) {
        return { };
    }

    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"refine":{"recognition":"OCR","roi":[%d,%d,%d,%d],"replace":[["燉","炖"]],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    MaaBuffer buf;
    MaaRect dummy;
    std::string result;
    if (recog::run(context, img, "refine", ocrJson, &dummy, buf.get())) {
        auto j = jsonutil::parse(buf.str());
        for (auto& ri : j["all"].as_array()) {
            std::string rt = ri["text"].as_string();
            if (!rt.empty()) {
                result = rt;
            }
        }
    }
    return result;
}

std::vector<FoodInfo> getCanContributeFoodList(MaaContext* context)
{
    std::vector<FoodInfo> result;

    static const MaaRect g_cellRois[] = {
        { 50, 360, 150, 200 },
        { 200, 360, 150, 200 },
        { 350, 360, 150, 200 },
        { 500, 360, 150, 200 },
    };

    ScreenCap cap(context);
    for (auto& roi : g_cellRois) {
        char json[256];
        snprintf(json, sizeof(json), R"({"FoodCell":{"recognition":"OCR","roi":[%d,%d,%d,%d],"replace":[["燉","炖"]]}})", roi.x, roi.y, roi.width, roi.height);

        MaaBuffer buf;
        MaaRect dummy;
        if (!recog::run(context, cap.img, "FoodCell", json, &dummy, buf.get())) {
            continue;
        }
        auto j = jsonutil::parse(buf.str());

        for (auto& item : j["all"].as_array()) {
            std::string text = item["text"].as_string();
            if (text.empty()) {
                continue;
            }

            // 识别到的食物名字不在食谱中，进行二次识别(颜色+ocr更准确)
            if (!Config::instance().recipeConfig().count(text)) {
                MaaRect itemBox = {
                    item["box"][0].as_integer(),
                    item["box"][1].as_integer(),
                    item["box"][2].as_integer(),
                    item["box"][3].as_integer(),
                };
                std::string refined = refineFoodName(context, cap.img, itemBox);
                if (!refined.empty()) {
                    text = refined;
                }
            }

            if (!Config::instance().recipeConfig().count(text)) {
                continue;
            }

            FoodInfo fi;
            fi.name = text;
            fi.box.x = item["box"][0].as_integer();
            fi.box.y = item["box"][1].as_integer();
            fi.box.width = item["box"][2].as_integer();
            fi.box.height = item["box"][3].as_integer();
            fi.quantity = getFoodQuantity(context, fi.box);
            result.emplace_back(fi);
        }
    }
    return result;
}

} // namespace

// ──── FoodFestivalDonate: 美食祭捐赠 ────

MaaBool FoodFestivalModule::donate(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    const int targetSatiety = 50;

    // 读取当前饱腹度
    int initialSatiety = getCurSatiety(context);
    if (initialSatiety >= targetSatiety) {
        return true;
    }

    // 1. 获取可捐赠食物列表
    auto foodList = getCanContributeFoodList(context);

    if (foodList.empty()) {
        return false;
    }

    // 2. 逐个食物捐赠
    int curSatiety = initialSatiety;
    for (auto& foodInfo : foodList) {
        if (curSatiety >= targetSatiety) {
            break;
        }
        if (foodInfo.quantity <= 0) {
            continue;
        }

        // 逐个点击该食物，数量上限内达标即停
        for (int i = 0; i < foodInfo.quantity; ++i) {
            auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            auto cid = MaaControllerPostClick(ctrl, foodInfo.box.x + foodInfo.box.width / 2, foodInfo.box.y + foodInfo.box.height / 2 - 50);
            MaaControllerWait(ctrl, cid);
            Sleep(300);

            // 当前饱腹度 = 进入时饱腹度 + 累计值
            curSatiety = initialSatiety + getCurrentContribution(context);
            if (curSatiety >= targetSatiety) {
                break;
            }
        }
    }
    return true;
}

// 判断是否有可捐赠食物
MaaBool FoodFestivalModule::canDonateFood(
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
    auto foodList = getCanContributeFoodList(context);
    for (const auto& foodInfo : foodList) {
        if (foodInfo.quantity > 0) {
            return true;
        }
    }
    bool res = false;
    return res;
}

// 生产指定食物
MaaBool FoodFestivalModule::specifyCookingFood(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customActionName,
    const char* customActionParam,
    MaaRecoId recoId,
    const MaaRect* box,
    void* transArg)
{
    const int targetSatiety = 50;
    int curSatiety = getCurSatiety(context);
    int needCnt = (targetSatiety - curSatiety) / 2 + 1;
    auto foodList = getCanContributeFoodList(context);
    for (auto foodInfo : foodList) {
        json::value param = json::object {
            { "name", foodInfo.name },
            { "count", needCnt },
        };
        json::value overrideVal = json::object {
            { "CookSpecifiedFood", json::object { { "custom_action_param", param } } },
        };
        MaaContextOverridePipeline(context, overrideVal.to_string().c_str());
        break;
    }
    return true;
}

namespace {

MaaBool donateThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                    const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return FoodFestivalModule::donate(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool canDonateFoodThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                           void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return FoodFestivalModule::canDonateFood(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool specifyCookingFoodThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                                const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return FoodFestivalModule::specifyCookingFood(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void FoodFestivalModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "FoodFestivalDonate", donateThunk, userData);
    registerCustomAction(res, "SpecifyCookingFood", specifyCookingFoodThunk, userData);
    registerCustomRecognition(res, "FoodFestivalCanDonateFood", canDonateFoodThunk, userData);
}
