#include "RedeemCodeModule.h"

#include "LogUtils.h"
#include "Registry.h"
#include "config/Config.h"
#include "util/JsonUtil.h"
#include "util/StringUtil.h"

std::vector<std::string> RedeemCodeModule::m_codes;
size_t RedeemCodeModule::m_index = 0;

MaaBool RedeemCodeModule::inputCodes(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customActionName, const char* customActionParam,
                                     MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    auto attach = Config::instance().nodeAttach(context, nodeName);
    std::string code = attach.get("code", json::value("")).as_string();

    m_codes.clear();
    m_index = 0;

    size_t start = 0;
    while (start <= code.size()) {
        size_t pos = code.find('/', start);
        std::string token = (pos == std::string::npos) ? code.substr(start) : code.substr(start, pos - start);
        if (!token.empty()) {
            m_codes.push_back(token);
        }
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }

    LogUtils::debug(fmt("兑换码解析: 共 %1 个", m_codes.size()));
    return true;
}

MaaBool RedeemCodeModule::isComplete(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customRecognitionName, const char* customRecognitionParam,
                                     const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                     MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return m_index >= m_codes.size();
}

MaaBool RedeemCodeModule::inputSingle(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customActionName, const char* customActionParam,
                                      MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    if (m_index >= m_codes.size()) {
        return false;
    }

    std::string code = m_codes[m_index];
    auto* ctrl = MaaTaskerGetController(MaaContextGetTasker(context));
    auto id = MaaControllerPostInputText(ctrl, code.c_str());
    MaaControllerWait(ctrl, id);

    m_index++;
    LogUtils::debug(fmt("粘贴兑换码: %1（剩余 %2 个）", code, m_codes.size() - m_index));
    return true;
}

namespace {

MaaBool inputCodesThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return RedeemCodeModule::inputCodes(ctx, tid, node, name, param, recoId, box, transArg);
}

MaaBool isCompleteThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                        const char* param, const MaaImageBuffer* img, const MaaRect* roi,
                        void* transArg, MaaRect* outBox, MaaStringBuffer* outDetail)
{
    return RedeemCodeModule::isComplete(ctx, tid, node, name, param, img, roi, transArg, outBox, outDetail);
}

MaaBool inputSingleThunk(MaaContext* ctx, MaaTaskId tid, const char* node, const char* name,
                         const char* param, MaaRecoId recoId, const MaaRect* box, void* transArg)
{
    return RedeemCodeModule::inputSingle(ctx, tid, node, name, param, recoId, box, transArg);
}

} // namespace

void RedeemCodeModule::registerAll(MaaResource* res, void* userData)
{
    registerCustomAction(res, "InputRedeemCodes", inputCodesThunk, userData);
    registerCustomRecognition(res, "RedeemComplete", isCompleteThunk, userData);
    registerCustomAction(res, "InputSingleRedeemCode", inputSingleThunk, userData);
}
