#include "Recognition.h"

#include "JsonUtil.h"
#include "MaaBuffer.h"
#include "StringUtil.h"

#include <windows.h>

ScreenCap::ScreenCap(MaaContext* context)
    : img(MaaImageBufferCreate())
{
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostScreencap(ctrl);
    MaaControllerWait(ctrl, id);
    MaaControllerCachedImage(ctrl, img);
}

ScreenCap::~ScreenCap()
{
    MaaImageBufferDestroy(img);
}

namespace recog {

bool run(MaaContext* context, const MaaImageBuffer* img, const char* entry,
         const char* detailJson, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto* tasker = MaaContextGetTasker(context);
    auto id = MaaContextRunRecognition(context, entry, detailJson ? detailJson : "{}", img);
    if (id == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, outBox, outDetail, nullptr, nullptr);
    return hit;
}

bool waitUntil(MaaContext* context, const char* entry, const char* detailJson,
               MaaRect* outBox, MaaStringBuffer* outDetail, int timeoutMs)
{
    const int g_stepMs = 1000;
    int elapsed = 0;
    while (elapsed < timeoutMs) {
        ScreenCap cap(context);
        if (run(context, cap.img, entry, detailJson, outBox, outDetail)) {
            return true;
        }
        Sleep(g_stepMs);
        elapsed += g_stepMs;
    }
    return false;
}

json::value getAndSubDetail(MaaContext* context, const MaaImageBuffer* img,
                            const char* entry, const char* subName)
{
    MaaBuffer detailBuf;
    MaaRect box;
    if (!run(context, img, entry, "{}", &box, detailBuf.get())) {
        return {};
    }
    auto j = jsonutil::parse(detailBuf.str());
    if (!j.is_array()) {
        return {};
    }
    for (const auto& item : j.as_array()) {
        if (!item.is_object()) {
            continue;
        }
        const auto& obj = item.as_object();
        if (!obj.contains("name") || !obj.at("name").is_string() || obj.at("name").as_string() != subName) {
            continue;
        }
        if (obj.contains("detail") && obj.at("detail").is_object()) {
            return obj.at("detail");
        }
        return {};
    }
    return {};
}

std::vector<ocr::Text> readTexts(MaaContext* context, const MaaImageBuffer* img,
                                 const char* nodeName, const char* subName)
{
    MaaBuffer detailBuf;
    MaaRect box;
    if (!run(context, img, nodeName, "{}", &box, detailBuf.get())) {
        return {};
    }

    auto j = jsonutil::parse(detailBuf.str());
    const json::value* target = &j;
    if (subName && subName[0] != '\0') {
        if (!j.is_array()) {
            return {};
        }
        target = nullptr;
        for (const auto& sub : j.as_array()) {
            if (!sub.is_object()) {
                continue;
            }
            const auto& obj = sub.as_object();
            if (obj.contains("name") && obj.at("name").is_string() && obj.at("name").as_string() == subName) {
                target = &sub;
                break;
            }
        }
        if (!target) {
            return {};
        }
    }
    return ocr::collectTexts(*target);
}

std::string readText(MaaContext* context, const MaaImageBuffer* img,
                     const char* nodeName, const char* subName)
{
    std::string result;
    for (const auto& item : readTexts(context, img, nodeName, subName)) {
        result += item.text;
    }
    return result;
}

std::string readTextViaEntry(MaaContext* context, const MaaImageBuffer* img,
                             const char* entry, const char* detailJson, MaaRect* box)
{
    MaaBuffer detailBuf;
    if (!run(context, img, entry, detailJson, box, detailBuf.get())) {
        return {};
    }
    std::string result;
    for (const auto& item : ocr::collectTexts(jsonutil::parse(detailBuf.str()))) {
        result += item.text;
    }
    return result;
}

bool readNumber(MaaContext* context, const char* nodeName, const char* subName, int& outValue)
{
    ScreenCap cap(context);
    return readNumberImage(context, cap.img, nodeName, subName, outValue);
}

bool readNumberImage(MaaContext* context, const MaaImageBuffer* img,
                     const char* nodeName, const char* subName, int& outValue)
{
    auto texts = readTexts(context, img, nodeName, subName);
    if (texts.empty()) {
        return false;
    }
    return ocr::extractNumber(texts, outValue);
}

bool readNumberFromRoi(MaaContext* context, const MaaRect& roi, int& outValue)
{
    ScreenCap cap(context);
    auto* tasker = MaaContextGetTasker(context);
    const std::string param =
        fmt(R"({"roi":[%1,%2,%3,%4],"only_rec":true})", roi.x, roi.y, roi.width, roi.height);
    auto id = MaaContextRunRecognitionDirect(context, "OCR", param.c_str(), cap.img);
    if (id == MaaInvalidId) {
        return false;
    }

    MaaBuffer detailBuf;
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, id, nullptr, nullptr, &hit, &box, detailBuf.get(), nullptr, nullptr);
    if (!hit) {
        return false;
    }
    auto texts = ocr::collectTexts(jsonutil::parse(detailBuf.str()));
    return ocr::extractNumber(texts, outValue);
}

} // namespace recog
