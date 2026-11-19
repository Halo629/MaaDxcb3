#include "RuneWorkshopModule.h"

#include "Registry.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/Recognition.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>

namespace
{

const std::unordered_set<std::string> kTargetRunes = {
    "刃甲", "迅击", "狂热", "伺机", "雷霆", "颂歌", "圣痕", "救赎", "复苏", "舞动", "破势",
    "裁决", "宁静", "追猎", "战意", "驭魔", "巫咒", "激励", "牺牲", "赋能", "晶能", "双向",
};

// OCR 格子文字并返回文字框
std::string ocrCell(MaaContext* context, const MaaImageBuffer* img, MaaRect roi, MaaRect* outTxtBox)
{
    char json[256];
    snprintf(json, sizeof(json), R"({"cell":{"recognition":"OCR","roi":[%d,%d,%d,%d]}})", roi.x, roi.y, roi.width, roi.height);
    MaaBuffer buf;
    MaaRect dummy;
    std::string text;
    if (recog::run(context, img, "cell", json, &dummy, buf.get())) {
        std::string detail = buf.str();
        auto j = jsonutil::parse(detail);
        if (auto& best = j["best"]; best.is_object()) {
            text = best["text"].as_string();
            auto& b = best["box"];
            if (b.is_array() && b.as_array().size() >= 4) {
                outTxtBox->x = b[0].as_integer();
                outTxtBox->y = b[1].as_integer();
                outTxtBox->width = b[2].as_integer();
                outTxtBox->height = b[3].as_integer();
            }
        }
        else if (auto& all = j["all"]; all.is_array() && !all.empty()) {
            auto& first = all[0];
            text = first["text"].as_string();
            auto& b = first["box"];
            if (b.is_array() && b.as_array().size() >= 4) {
                outTxtBox->x = b[0].as_integer();
                outTxtBox->y = b[1].as_integer();
                outTxtBox->width = b[2].as_integer();
                outTxtBox->height = b[3].as_integer();
            }
        }
    }
    return text;
}

// 检查颜色是否已解锁: txtBox 的 y 往上偏移 120
bool isRuneLocked(MaaContext* context, const MaaImageBuffer* img, MaaRect txtBox)
{
    char json[256];
    snprintf(
        json,
        sizeof(json),
        R"({"cc":{"recognition":"ColorMatch","roi":[%d,%d,%d,%d],"method":40,)"
        R"("lower":[10,150,0],"upper":[30,255,255],"count":50}})",
        txtBox.x,
        txtBox.y - 120,
        txtBox.width,
        100);
    MaaRect dummy;
    return recog::run(context, img, "cc", json, &dummy, nullptr);
}

// 解析数字
int parseDigits(const std::string& text)
{
    std::string digits;
    for (char ch : text) {
        if (ch >= '0' && ch <= '9') {
            digits += ch;
        }
    }
    return digits.empty() ? -1 : std::atoi(digits.c_str());
}

// 识别符文等级：先 ColorMatch 找白色数字，再 OCR 数字
int ocrRuneLevel(MaaContext* context, const MaaImageBuffer* img, const MaaRect& roi)
{
    char colorJson[256];
    snprintf(
        colorJson,
        sizeof(colorJson),
        R"({"lv_color":{"recognition":"ColorMatch","roi":[%d,%d,%d,%d],"method":40,)"
        R"("lower":[0,0,200],"upper":[255,30,255],"count":3}})",
        roi.x,
        roi.y,
        roi.width,
        roi.height);
    MaaRect colorBox;
    if (!recog::run(context, img, "lv_color", colorJson, &colorBox, nullptr)) {
        return -1; // 没识别到白色
    }

    char ocrJson[256];
    snprintf(
        ocrJson,
        sizeof(ocrJson),
        R"({"lv":{"recognition":"OCR","roi":[%d,%d,%d,%d],"expected":["\\d+"],"only_rec":true}})",
        colorBox.x - 2,
        colorBox.y - 2,
        colorBox.width + 4,
        colorBox.height + 4);
    MaaBuffer buf;
    MaaRect resultBox;
    if (!recog::run(context, img, "lv", ocrJson, &resultBox, buf.get())) {
        return -1; // 没识别到数字
    }
    std::string detail = buf.str();
    auto parsed = jsonutil::parse(detail);
    std::string text;
    if (auto& best = parsed["best"]; best.is_object()) {
        text = best["text"].as_string();
    }
    else if (auto& all = parsed["all"]; all.is_array() && !all.empty()) {
        text = all[0]["text"].as_string();
    }
    return parseDigits(text);
}

} // namespace

MaaBool RuneWorkshopModule::hasRuneUnlock(
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
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    constexpr int kCellW = 160, kCellH = 180;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            MaaRect cell = { 50 + col * kCellW, 450 + row * kCellH, kCellW, kCellH };
            ScreenCap cap(context);
            MaaRect txtBox = cell;

            std::string text = ocrCell(context, cap.img, cell, &txtBox);
            if (!kTargetRunes.count(text)) {
                continue;
            }

            // 匹配到目标符文，文字框 y-120 检查颜色
            if (isRuneLocked(context, cap.img, txtBox)) {
                continue;
            }
            outBox->x = txtBox.x;
            outBox->y = txtBox.y - 50;
            outBox->height = txtBox.height;
            outBox->width = txtBox.width;
            return true;
        }
    }

    return false;
}

// 选择符文：检测是否有目标符文需要升级（等级 != 50）
MaaBool RuneWorkshopModule::selectRune(
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
    constexpr int kCellW = 150, kCellH = 210; // 600/4, 630/3
    constexpr int kGridX = 60, kGridY = 260;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            MaaRect cell = { kGridX + col * kCellW, kGridY + row * kCellH, kCellW, kCellH };
            ScreenCap cap(context);
            MaaRect txtBox = cell;

            std::string text = ocrCell(context, cap.img, cell, &txtBox);
            if (!kTargetRunes.count(text)) {
                continue;
            }

            // 文字框 x+40, y-35，宽高不变，识别白色数字
            MaaRect lvlRoi = { txtBox.x + 40, txtBox.y - 30, txtBox.width + 10, txtBox.height };
            int level = ocrRuneLevel(context, cap.img, lvlRoi);
            if (level != 50) {
                *outBox = cell;
                return true; // 需要升级
            }
        }
    }
    return false;
}

namespace
{

MaaBool hasRuneUnlockThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                           const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                           void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return RuneWorkshopModule::hasRuneUnlock(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool selectRuneThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return RuneWorkshopModule::selectRune(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void RuneWorkshopModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "HasRuneUnlock", hasRuneUnlockThunk, userData);
    registerCustomRecognition(res, "SelectRune", selectRuneThunk, userData);
}
