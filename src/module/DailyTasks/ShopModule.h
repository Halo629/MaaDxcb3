#pragma once

#include "MaaFramework/MaaAPI.h"

#include <array>
#include <cstdint>

struct ShopItemConfig
{
    const char* foundKey;
    const char* enabledKey;
    uint32_t bit;
};

extern const std::array<ShopItemConfig, 6> g_shopItems;

class ShopModule
{
public:
    static MaaBool assistant(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                             const char* customActionName, const char* customActionParam,
                             MaaRecoId recoId, const MaaRect* box, void* transArg);

    static MaaBool finishCheck(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                               const char* customRecognitionName, const char* customRecognitionParam,
                               const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                               MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool itemNotBought(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                 const char* customRecognitionName, const char* customRecognitionParam,
                                 const MaaImageBuffer* image, const MaaRect* roi, void* transArg,
                                 MaaRect* outBox, MaaStringBuffer* outDetail);

    static MaaBool carriageShopSlideBuy(MaaContext* context, MaaTaskId taskId, const char* nodeName,
                                        const char* customActionName, const char* customActionParam,
                                        MaaRecoId recoId, const MaaRect* box, void* transArg);

    static void registerAll(MaaResource* res, void* userData);

private:
    static uint32_t m_shopMask;
    static uint32_t m_shopEnabledMask;
};
