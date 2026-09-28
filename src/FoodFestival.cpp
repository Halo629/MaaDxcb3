#include "Comm.h"

#include <string>
#include <vector>

namespace
{

struct FoodInfo
{
    std::string name;
    MaaRect box;
    int quantity = 0;
    int perClick = 0; // 每次点击贡献值
    int donated = 0;  // 本次已捐赠次数
};

// OCR [334,196,71,40] 识别 "+x" 获取 per-click 值
static int getCurrentContribution(MaaContext* context)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    MaaRect dummy;
    int val = 0;
    if (doRecognition(
            context,
            cap.img,
            "perclick",
            R"({"perclick":{"recognition":"OCR","roi":[334,196,71,40],"expected":["\\+\\d+"],"only_rec":true}})",
            &dummy,
            buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
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
    MaaStringBufferDestroy(buf);
    return val;
}

// 识别饱腹度: 使用 CurrentSatiety 任务 (SatietyColor ColorMatch + OCR "x/y") 取 x
static int getCurSatiety(MaaContext* context)
{
    ScreenCap cap(context);
    auto buf = MaaStringBufferCreate();
    int val = 0;
    if (doRecognition(context, cap.img, "CurrentSatiety", "{}", nullptr, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });

        // CurrentSatiety 是 And 识别，detail 是子识别结果数组；取 OCR 子结果里的 "x/y"
        std::string text;
        if (j.is_array()) {
            for (auto& sub : j.as_array()) {
                if (!sub.is_object() || sub.get("algorithm", json::value("")).as_string() != "OCR") {
                    continue;
                }
                auto d = sub.get("detail", json::value { });
                if (auto& best = d["best"]; best.is_object()) {
                    text = best["text"].as_string();
                }
                else if (auto& all = d["all"]; all.is_array() && !all.empty()) {
                    text = all[0]["text"].as_string();
                }
                break;
            }
        }

        auto slash = text.find('/');
        if (slash != std::string::npos) {
            val = std::atoi(text.substr(0, slash).c_str());
        }
    }
    MaaStringBufferDestroy(buf);
    return val;
}

// ColorMatch 白色 + OCR 数字获取数量
static int getFoodQuantity(MaaContext* context, MaaRect box)
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
    if (!doRecognition(context, cap.img, "cc", colorJson, &colorBox, nullptr)) {
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
    auto buf = MaaStringBufferCreate();
    MaaRect ocrBox;
    int count = 0;
    if (doRecognition(context, cap.img, "oc", ocrJson, &ocrBox, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        std::string text;
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            text = all[0]["text"].as_string();
        }
        count = std::atoi(text.c_str());
    }
    MaaStringBufferDestroy(buf);
    return count;
}

// ColorMatch 黄色 + OCR 二次识别食物名，识别不到返回空串
static std::string refineFoodName(MaaContext* context, const MaaImageBuffer* img, MaaRect box)
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
    if (!doRecognition(context, img, "cc", colorJson, &colorBox, nullptr)) {
        return { };
    }

    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"refine":{"recognition":"OCR","roi":[%d,%d,%d,%d],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    auto buf = MaaStringBufferCreate();
    MaaRect dummy;
    std::string result;
    if (doRecognition(context, img, "refine", ocrJson, &dummy, buf)) {
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        auto j = json::parse(detail).value_or(json::value { });
        for (auto& ri : j["all"].as_array()) {
            std::string rt = ri["text"].as_string();
            if (!rt.empty()) {
                result = rt;
            }
        }
    }
    MaaStringBufferDestroy(buf);
    return result;
}

static std::vector<FoodInfo> getCanContributeFoodList(MaaContext* context)
{
    std::vector<FoodInfo> result;

    static const MaaRect kCellRois[] = {
        { 50, 360, 150, 200 },
        { 200, 360, 150, 200 },
        { 350, 360, 150, 200 },
        { 500, 360, 150, 200 },
    };

    ScreenCap cap(context);
    for (auto& roi : kCellRois) {
        char json[256];
        snprintf(json, sizeof(json), R"({"FoodCell":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})", roi.x, roi.y, roi.width, roi.height);

        auto buf = MaaStringBufferCreate();
        MaaRect dummy;
        if (!doRecognition(context, cap.img, "FoodCell", json, &dummy, buf)) {
            MaaStringBufferDestroy(buf);
            continue;
        }
        std::string detail(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
        MaaStringBufferDestroy(buf);
        auto j = json::parse(detail).value_or(json::value { });

        for (auto& item : j["all"].as_array()) {
            std::string text = item["text"].as_string();
            if (text.empty()) {
                continue;
            }

            // 识别到的食物名字不在食谱中，进行二次识别(颜色+ocr更准确)
            if (!getRecipeNames().count(text)) {
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

            if (!getRecipeNames().count(text)) {
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

MaaBool foodFestivalDonate(
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
    int curSatiety = getCurSatiety(context);
    if (curSatiety >= targetSatiety) {
        return true;
    }

    // 1. 获取可捐赠食物列表
    auto foodList = getCanContributeFoodList(context);

    if (foodList.empty()) {
        return false;
    }

    // 2. 逐个食物捐赠
    for (auto& foodInfo : foodList) {
        if (curSatiety >= targetSatiety) {
            break;
        }
        if (foodInfo.quantity <= 0) {
            continue;
        }

        // 点击食物
        auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
        auto cid = MaaControllerPostClick(ctrl, foodInfo.box.x + foodInfo.box.width / 2, foodInfo.box.y + foodInfo.box.height / 2 - 50);
        MaaControllerWait(ctrl, cid);
        Sleep(500);

        // 第一次点击获取单次贡献值，getCurrentContribution 显示当前食物累加后的值
        foodInfo.perClick = getCurrentContribution(context);
        int beforeFoodSatiety = curSatiety;
        curSatiety += foodInfo.perClick;
        int clicks = 1;

        // 持续点击直到上限或达标
        while (curSatiety < targetSatiety && clicks < foodInfo.quantity) {
            ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
            cid = MaaControllerPostClick(ctrl, foodInfo.box.x + foodInfo.box.width / 2, foodInfo.box.y + foodInfo.box.height / 2 - 50);
            MaaControllerWait(ctrl, cid);
            Sleep(300);
            ++clicks;

            int curVal = getCurrentContribution(context);
            curSatiety = beforeFoodSatiety + curVal;
        }
    }
    return true;
}

// 判断是否有可捐赠食物
MaaBool foodFestivalCanDonateFood(
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
MaaBool specifyCookingFood(
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

void registerCustomFoodFestival(MaaResource* resource, void* userData)
{
    registerCustomAction(resource, "FoodFestivalDonate", foodFestivalDonate, userData);
    registerCustomAction(resource, "SpecifyCookingFood", specifyCookingFood, userData);
    registerCustomRecognition(resource, "FoodFestivalCanDonateFood", foodFestivalCanDonateFood, userData);
}
