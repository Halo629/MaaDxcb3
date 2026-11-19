#pragma once

#include "MaaFramework/MaaAPI.h"
#include "OcrUtil.h"

#include <meojson/json.hpp>

#include <string>
#include <vector>

// 截图 RAII：构造时截图，析构销毁。
class ScreenCap
{
public:
    explicit ScreenCap(MaaContext* context);
    ~ScreenCap();

    ScreenCap(const ScreenCap&) = delete;
    ScreenCap& operator=(const ScreenCap&) = delete;

    MaaImageBuffer* img = nullptr;
};

namespace recog {

// 运行识别节点，命中返回 true；outBox/outDetail 可选。
bool run(MaaContext* context, const MaaImageBuffer* img, const char* entry,
         const char* detailJson, MaaRect* outBox, MaaStringBuffer* outDetail);

// 循环截图识别，直到命中或超时。
bool waitUntil(MaaContext* context, const char* entry, const char* detailJson,
               MaaRect* outBox, MaaStringBuffer* outDetail, int timeoutMs = 8000);

// 运行识别节点，返回识别到的文本与框；subName 非空时只取 And 节点的指定子结果。
std::vector<ocr::Text> readTexts(MaaContext* context, const MaaImageBuffer* img,
                                 const char* nodeName, const char* subName);

// 同上，返回拼接后的字符串。
std::string readText(MaaContext* context, const MaaImageBuffer* img,
                     const char* nodeName, const char* subName);

// 运行指定识别节点并拼接所有文本（直接 OCR entry），可返回框。
std::string readTextViaEntry(MaaContext* context, const MaaImageBuffer* img,
                             const char* entry, const char* detailJson, MaaRect* box);

// 从 And 识别结果中取指定子节点的 detail。
json::value getAndSubDetail(MaaContext* context, const MaaImageBuffer* img,
                            const char* entry, const char* subName);

// 运行指定识别节点，提取其中的数字；subName 非空时从 And 节点指定子结果读取。
bool readNumber(MaaContext* context, const char* nodeName, const char* subName, int& outValue);

// 同上，但使用传入的 image，不重新截图。
bool readNumberImage(MaaContext* context, const MaaImageBuffer* img,
                     const char* nodeName, const char* subName, int& outValue);

// 直接 OCR 指定 roi，提取其中的数字。
bool readNumberFromRoi(MaaContext* context, const MaaRect& roi, int& outValue);

} // namespace recog
