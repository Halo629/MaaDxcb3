#include "Comm.h"

#include <ctime>

// 通用周任务日期检查：只在该节点 attach 里勾选的星期几执行。
// 供多个周任务复用：pipeline 节点 recognition=Custom, custom_recognition=WeeklyScheduleEnabled，
// 节点 attach 由任务的「执行周期」checkbox 通过 pipeline_override 写入（monday~sunday bool）。
MaaBool weeklyScheduleEnabled(
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
    // 读当前节点 merge 后的完整 JSON（含 pipeline_override 写入的 attach）
    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return true; // 读不到就默认放行
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto node = json::parse(raw);
    if (!node) {
        return true;
    }
    const auto& attach = (*node)["attach"];
    if (!attach.is_object()) {
        return true;
    }

    // 今天星期几：tm_wday 0=周日 1=周一 ... 6=周六
    std::time_t tt = std::time(nullptr);
    std::tm t { };
    localtime_s(&t, &tt);
    const char* key = "sunday";
    switch (t.tm_wday) {
    case 1:
        key = "monday";
        break;
    case 2:
        key = "tuesday";
        break;
    case 3:
        key = "wednesday";
        break;
    case 4:
        key = "thursday";
        break;
    case 5:
        key = "friday";
        break;
    case 6:
        key = "saturday";
        break;
    default:
        key = "sunday";
        break;
    }
    return attach.get(key, json::value(false)).as_boolean();
}

void registerCustomWeeklySchedule(MaaResource* res, void* user_data)
{
    registerCustomRecognition(res, "WeeklyScheduleEnabled", weeklyScheduleEnabled, user_data);
}
