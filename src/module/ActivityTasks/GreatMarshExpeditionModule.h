#pragma once

#include "MaaFramework/MaaAPI.h"

#include <utility>
#include <vector>

// 祝福类型数 / 颜色数（顺序固定，与 pipeline 节点命名 {Color}{Type}{Pos|Count} 保持一致）。
inline constexpr int g_typeCount = 6;
inline constexpr int g_colorCount = 4; // 灰/蓝/紫/金，从低到高

// 装备决策结果，供后续 task 读取。
struct EquipPlan
{
    int unload[g_typeCount][g_colorCount] = {}; // 需要从祈愿页卸下的数量
    int equip[g_typeCount][g_colorCount] = {};  // 需要装备的数量
};

class GreatMarshExpeditionModule
{
public:
    // ──── 自定义动作 ────
    static MaaBool blessingStatistic(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customActionName, const char* customActionParam,
                                     MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool targetBlessing(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                  const char* customActionName, const char* customActionParam,
                                  MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool unloadAllLowBlessings(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                         const char* customActionName, const char* customActionParam,
                                         MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool blessingEquip(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customActionName, const char* customActionParam,
                                 MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool prayResult(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                              const char* customActionName, const char* customActionParam,
                              MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool blessingCraftOnPage(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customActionName, const char* customActionParam,
                                       MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool blessingEquipAdvance(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customActionName, const char* customActionParam,
                                        MaaRecoId recoId, const MaaRect* box, void* transArg);

    // ──── 自定义识别 ────
    static MaaBool blessingNeedReshapeOnPage(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                             const char* customRecognitionName, const char* customRecognitionParam,
                                             const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                             MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingCraftCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                      const char* customRecognitionName, const char* customRecognitionParam,
                                      const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                      MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingCanCraftHigher(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customRecognitionName, const char* customRecognitionParam,
                                          const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                          MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingCanCraftOnPage(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customRecognitionName, const char* customRecognitionParam,
                                          const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                          MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingNeedReshape(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                       const char* customRecognitionName, const char* customRecognitionParam,
                                       const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                       MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingCanCraftGold(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customRecognitionName, const char* customRecognitionParam,
                                        const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                        MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingNeedPray(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                    const char* customRecognitionName, const char* customRecognitionParam,
                                    const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                    MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingTargetGoldEnough(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                            const char* customRecognitionName, const char* customRecognitionParam,
                                            const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                            MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool isTargetBlessing(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                    const char* customRecognitionName, const char* customRecognitionParam,
                                    const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                    MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingPageCountEnough(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                           const char* customRecognitionName, const char* customRecognitionParam,
                                           const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                           MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool targetBlessingSelected(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                          const char* customRecognitionName, const char* customRecognitionParam,
                                          const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                          MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool blessingEquipDone(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                     const char* customRecognitionName, const char* customRecognitionParam,
                                     const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                     MaaRect* outBox, MaaStringBuffer* outDetail);

    static void registerAll(MaaResource* res, void* userData);

    // 供 .cpp 匿名命名空间辅助函数访问共享状态。
    static std::vector<std::vector<int>>& blessingPray() { return m_blessingPray; }
    static std::vector<std::vector<int>>& blessingBag() { return m_blessingBag; }
    static EquipPlan& equipPlan() { return m_equipPlan; }

private:
    static std::vector<std::vector<int>> m_blessingPray;
    static std::vector<std::vector<int>> m_blessingBag;
    static EquipPlan m_equipPlan;
    static int m_targetType;
    static int m_equipTargetType;
    static int m_equipTargetColor;
    static std::vector<std::pair<int, int>> m_equipOrder;
};
