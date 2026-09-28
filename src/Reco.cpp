#include "Reco.h"
#include "Comm.h"

void registerCustomScene(MaaResource* res, void* user_data);

namespace custom_map
{
void registerCustomMap(MaaResource* res, void* user_data);
}

namespace custom_battle
{
void registerCustomBattle(MaaResource* res, void* user_data);
}

void registerCustomFerdinand(MaaResource* res, void* user_data);
void registerCustomGuild(MaaResource* res, void* user_data);
void registerCustomSoulTomb(MaaResource* res, void* user_data);
void registerCustomServer(MaaResource* res, void* user_data);
void registerCustomMonsterSoulUpgrade(MaaResource* res, void* user_data);
void registerCustomShadowMirror(MaaResource* res, void* user_data);
void registerCustomCelestialIsle(MaaResource* res, void* user_data);
void registerCustomRecipe(MaaResource* res, void* user_data);
void registerCustomFoodFestival(MaaResource* res, void* user_data);
void registerCustomMonthlyCard(MaaResource* res, void* user_data);
void registerCustomEndlessWealthIsle(MaaResource* res, void* user_data);
void registerCustomRuneWorkshop(MaaResource* res, void* user_data);
void registerCustomHuntingGroundFeast(MaaResource* res, void* user_data);
void registerCustomArcaneCell(MaaResource* res, void* user_data);
void registerCustomTreasureSage(MaaResource* res, void* user_data);
void registerCustomWeeklySchedule(MaaResource* res, void* user_data);
void registerCustomSteward(MaaResource* res, void* user_data);

void registerAllCustomRecognition(MaaResource* res, void* user_data)
{
    registerCustomScene(res, user_data);
    custom_map::registerCustomMap(res, user_data);
    custom_battle::registerCustomBattle(res, user_data);
    registerCustomFerdinand(res, user_data);
    registerCustomGuild(res, user_data);
    registerCustomSoulTomb(res, user_data);
    registerCustomServer(res, user_data);
    registerCustomMonsterSoulUpgrade(res, user_data);
    registerCustomShadowMirror(res, user_data);
    registerCustomCelestialIsle(res, user_data);
    registerCustomRecipe(res, user_data);
    registerCustomFoodFestival(res, user_data);
    registerCustomMonthlyCard(res, user_data);
    registerCustomEndlessWealthIsle(res, user_data);
    registerCustomRuneWorkshop(res, user_data);
    registerCustomHuntingGroundFeast(res, user_data);
    registerCustomArcaneCell(res, user_data);
    registerCustomComm(res, user_data);
    registerCustomTreasureSage(res, user_data);
    registerCustomWeeklySchedule(res, user_data);
    registerCustomSteward(res, user_data);
}
