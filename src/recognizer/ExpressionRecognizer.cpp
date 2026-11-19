#include "ExpressionRecognizer.h"

#include "LogUtils.h"
#include "Registry.h"
#include "util/ExprParser.h"
#include "util/JsonUtil.h"
#include "util/MaaBuffer.h"
#include "util/OcrUtil.h"
#include "util/StringUtil.h"

#include <cctype>
#include <cstring>

namespace {


bool extractNumberFromOcrDetail(json::value& detail, long long& outValue)
{
    if (!detail.is_object()) {
        return false;
    }
    if (detail.contains("best") && detail["best"].is_object()) {
        auto& best = detail["best"];
        if (best.contains("text") && best["text"].is_string()) {
            return ocr::parseNumeric(best["text"].as_string(), outValue);
        }
    }
    if (detail.contains("all") && detail["all"].is_array()) {
        for (auto& item : detail["all"].as_array()) {
            if (item.is_object() && item.contains("text") && item["text"].is_string()) {
                return ocr::parseNumeric(item["text"].as_string(), outValue);
            }
        }
    }
    return false;
}

bool resolveAndBoxIndex(MaaContext* context, const char* nodeName, bool& isAnd, int& boxIndex)
{
    isAnd = false;
    boxIndex = 0;

    MaaBuffer buf;
    if (!MaaContextGetNodeData(context, nodeName, buf.get())) {
        return false;
    }
    auto j = jsonutil::parse(buf.str());
    if (!j.is_object()) {
        return false;
    }
    auto& reco = j["recognition"];
    if (!reco.is_object()) {
        return false;
    }
    auto& type = reco["type"];
    if (!type.is_string() || type.as_string() != "And") {
        return true;
    }

    isAnd = true;
    auto& param = reco["param"];
    if (param.is_object() && param.contains("box_index") && param["box_index"].is_number()) {
        boxIndex = (int)param["box_index"].as_integer();
    }
    return true;
}

bool runNumericRecognition(MaaContext* context, const MaaImageBuffer* image, const char* nodeName, long long& outValue)
{
    auto* tasker = MaaContextGetTasker(context);
    auto recoId = MaaContextRunRecognition(context, nodeName, "{}", image);
    if (recoId == MaaInvalidId) {
        return false;
    }

    MaaBuffer detailBuf;
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, recoId, nullptr, nullptr, &hit, &box, detailBuf.get(), nullptr, nullptr);
    if (!hit) {
        return false;
    }

    bool isAnd = false;
    int boxIndex = 0;
    resolveAndBoxIndex(context, nodeName, isAnd, boxIndex);

    auto j = jsonutil::parse(detailBuf.str());
    if (isAnd) {
        if (!j.is_array()) {
            return false;
        }
        auto& arr = j.as_array();
        if (boxIndex < 0 || (size_t)boxIndex >= arr.size() || !arr[boxIndex].is_object()) {
            return false;
        }
        auto& subDetail = arr[boxIndex]["detail"];
        return extractNumberFromOcrDetail(subDetail, outValue);
    }
    return extractNumberFromOcrDetail(j, outValue);
}

bool resolveExpressionValues(MaaContext* context, const MaaImageBuffer* image, const std::string& expression,
                             std::string& outResolved)
{
    std::string result;
    size_t i = 0;
    while (i < expression.size()) {
        if (expression[i] != '{') {
            result += expression[i];
            ++i;
            continue;
        }
        size_t close = expression.find('}', i + 1);
        if (close == std::string::npos) {
            return false;
        }
        std::string nodeName = trim(expression.substr(i + 1, close - i - 1));
        if (nodeName.empty()) {
            return false;
        }
        long long value = 0;
        if (!runNumericRecognition(context, image, nodeName.c_str(), value)) {
            return false;
        }
        result += std::to_string(value);
        i = close + 1;
    }
    outResolved = std::move(result);
    return true;
}

bool resolveResultBox(MaaContext* context, const MaaImageBuffer* image, const char* boxNode, MaaRect& outBox)
{
    auto* tasker = MaaContextGetTasker(context);
    auto recoId = MaaContextRunRecognition(context, boxNode, "{}", image);
    if (recoId == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, recoId, nullptr, nullptr, &hit, &box, nullptr, nullptr, nullptr);
    if (!hit) {
        return false;
    }
    outBox = box;
    return true;
}

} // namespace

MaaBool ExpressionRecognizer::recognize(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customRecognitionName, const char* customRecognitionParam,
                                        const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                        MaaRect* outBox, MaaStringBuffer* outDetail)
{
    auto param = jsonutil::parse(customRecognitionParam ? customRecognitionParam : "{}");
    std::string expression = param.is_object() ? param.get("expression", json::value("")).as_string() : "";
    std::string boxNode = param.is_object() ? param.get("box_node", json::value("")).as_string() : "";
    if (expression.empty()) {
        LogUtils::error("ExpressionRecognition: expression 缺失");
        return false;
    }

    std::string resolved;
    if (!resolveExpressionValues(context, image, expression, resolved)) {
        LogUtils::debug(fmt("ExpressionRecognition: 解析表达式值失败: %1", expression));
        return false;
    }

    ExprValue result;
    ExprParser parser(resolved);
    if (!parser.parse(result)) {
        LogUtils::error(fmt("ExpressionRecognition: 表达式求值失败: %1", resolved));
        return false;
    }
    if (!result.isBool) {
        LogUtils::error(fmt("ExpressionRecognition: 表达式结果必须是布尔值: %1", resolved));
        return false;
    }

    bool matched = result.boolVal;
    if (!matched) {
        return false;
    }

    MaaRect resultBox;
    if (boxNode.empty()) {
        resultBox = *roi;
    }
    else if (!resolveResultBox(context, image, boxNode.c_str(), resultBox)) {
        LogUtils::error(fmt("ExpressionRecognition: 解析 box_node %1 结果框失败", boxNode));
        return false;
    }

    if (outBox) {
        *outBox = resultBox;
    }
    if (outDetail) {
        MaaStringBufferSet(outDetail, resolved.c_str());
    }
    return true;
}

namespace {

MaaBool recognizeThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                       const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                       void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return ExpressionRecognizer::recognize(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

} // namespace

void ExpressionRecognizer::registerAll(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "ExpressionRecognition", recognizeThunk, userData);
}
