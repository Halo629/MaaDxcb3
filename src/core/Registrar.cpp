#include "Registrar.h"

#include "CommonActions.h"
#include "module/ArcaneCellModule.h"
#include "module/BattleModule.h"
#include "module/BattleTimeTestModule.h"
#include "module/CelestialIsleModule.h"
#include "module/DarkSecretTreasureModule.h"
#include "module/EndlessWealthIsleModule.h"
#include "module/FerdinandModule.h"
#include "module/FoodFestivalModule.h"
#include "module/GreatMarshExpeditionModule.h"
#include "module/GuildModule.h"
#include "module/HuntingGroundFeastModule.h"
#include "module/MonsterSoulUpgradeModule.h"
#include "module/RecipeModule.h"
#include "module/RuneWorkshopModule.h"
#include "module/ServerModule.h"
#include "module/ShadowMirrorModule.h"
#include "module/ShopModule.h"
#include "module/SoulTombModule.h"
#include "module/StewardModule.h"
#include "module/TreasureSageModule.h"
#include "module/WeeklyScheduleModule.h"
#include "movement/Movement.h"
#include "recognizer/ExpressionRecognizer.h"

void registerAllCustomRecognition(MaaResource* res, void* userData)
{
    CommonActions::registerAll(res, userData);
    Movement::registerAll(res, userData);
    ShopModule::registerAll(res, userData);
    ExpressionRecognizer::registerAll(res, userData);

    ArcaneCellModule::registerAll(res, userData);
    BattleModule::registerAll(res, userData);
    BattleTimeTestModule::registerAll(res, userData);
    CelestialIsleModule::registerAll(res, userData);
    DarkSecretTreasureModule::registerAll(res, userData);
    EndlessWealthIsleModule::registerAll(res, userData);
    FerdinandModule::registerAll(res, userData);
    FoodFestivalModule::registerAll(res, userData);
    GreatMarshExpeditionModule::registerAll(res, userData);
    GuildModule::registerAll(res, userData);
    HuntingGroundFeastModule::registerAll(res, userData);
    MonsterSoulUpgradeModule::registerAll(res, userData);
    RecipeModule::registerAll(res, userData);
    RuneWorkshopModule::registerAll(res, userData);
    ServerModule::registerAll(res, userData);
    ShadowMirrorModule::registerAll(res, userData);
    SoulTombModule::registerAll(res, userData);
    StewardModule::registerAll(res, userData);
    TreasureSageModule::registerAll(res, userData);
    WeeklyScheduleModule::registerAll(res, userData);
}
