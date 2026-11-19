# MaaDxcb3 架构说明

## 概览

`cpp-service` 作为 MaaAgentServer 子进程运行，向 MaaFramework 注册自定义识别（Recognition）与自定义动作（Action）。业务为游戏自动化，识别与动作通过 pipeline 的 JSON 配置驱动。

重构目标：把原先散落在 20 个文件、以自由函数为主的代码，整理成**分层、职责清晰、无重复**的结构。

## 目录结构

```
src/
├── main.cpp                    # 入口：初始化日志 → 注册 → 启动 MaaAgentServer
├── LogUtils.h/.cpp             # 日志（双写 stdout + 文件）
├── ServerConfig.h              # 配置结构体定义（UserBattleConfig 等）
├── Registry.h                  # registerCustomRecognition/Action → MaaAgentServer 重定向
│
├── util/                       # 工具层：无状态，RAII + 命名空间
│   ├── MaaBuffer.h             # MaaStringBuffer 的 RAII 包装
│   ├── JsonUtil.h              # JSON 解析辅助（jsonutil::parse / readIntArray）
│   ├── StringUtil.h            # fmt / split / join / trim
│   ├── OcrUtil.h/.cpp          # OCR 文本与数字解析（ocr 命名空间）
│   ├── Recognition.h/.cpp      # 识别工具 + ScreenCap 截图 RAII（recog 命名空间）
│   └── ScreenUtil.h/.cpp       # 屏幕交互：可点击区域 / 罗盘 / 滑动 / 点击（screen 命名空间）
│
├── config/                     # 配置中心
│   └── Config.h/.cpp           # 节点 attach 读取 + 用户配置 + 食谱（替代 g_user_battle 等全局变量）
│
├── map/                        # 地图导航
│   └── MapNavigator.h/.cpp     # 地图识别 / 进入 / 服务器名归一化
│
├── movement/                   # 走位
│   └── Movement.h/.cpp         # 走位 + 穿墙 + findTargetPoint/arrivedAtTargetPoint
│
├── core/                       # 通用与注册
│   ├── CommonActions.h/.cpp    # 通用动作：SaveConfig / SubTask / AdjustPosition 等
│   └── Registrar.h/.cpp        # 注册中心：统一调用所有模块的 registerAll
│
├── recognizer/                 # 通用识别器
│   └── ExpressionRecognizer.h/.cpp  # 表达式识别（OCR 数值组合成布尔表达式）
│
└── module/                     # 业务模块（20 个，每个一个类）
    ├── BattleModule.h/.cpp
    ├── ServerModule.h/.cpp
    ├── ShopModule.h/.cpp
    └── ...
```

## 分层与依赖

```
注册中心 Registrar  ──►  业务模块 module/*、core/CommonActions、recognizer/*
                              │
                              ▼
                        基础设施 config/ map/ movement/
                              │
                              ▼
                        工具层 util/（被所有层依赖）
```

依赖方向自上而下，工具层不依赖任何业务代码。

## 模块类范式

每个业务模块是一个类，统一遵循同一结构：

```cpp
class XxxModule {
public:
    static MaaBool xxxRecognition(...);   // 自定义识别回调
    static MaaBool xxxAction(...);        // 自定义动作回调
    static void registerAll(MaaResource* res, void* userData);
private:
    static int state_;                    // 原 static 全局状态 → 静态成员
};
```

- 原 `.cpp` 里的 `static` 辅助函数 → 移到 `.cpp` 匿名命名空间。
- 回调函数 → 类的 `static` 方法，配一个匿名命名空间的 thunk 桥接到 MaaFramework 的 C 回调。
- 注册 → `registerAll`，由 `Registrar` 统一调用。

## 关键设计决策

1. **工具函数用命名空间而非类**：`recog`/`screen`/`ocr`/`jsonutil` 都是无状态的纯函数，命名空间比"全静态方法类"更直接，避免无意义的对象。
2. **不引入识别/动作接口继承**：MaaFramework 的 `MaaCustomRecognitionCallback`/`MaaCustomActionCallback` 已经是识别/动作的契约，再包一层接口是冗余。模块类以"一个游戏功能"为聚合单元，同时包含多个识别与动作。
3. **全局变量收敛**：`g_user_battle`/`g_user_soul` 等裸全局变量收敛进 `Config` 的静态成员；各模块的 `g_*` 状态收敛进模块类静态成员。
4. **RAII 消除手动资源管理**：`MaaBuffer` 包装 `MaaStringBuffer`，`ScreenCap` 包装截图，消灭了大量手写 create/destroy 及其泄漏风险。
5. **消除重复代码**：`readIntArray`（原定义两次）、OCR 文本收集逻辑、`json::parse(...).value_or(...)` 模式，统一收进工具层。

## 模块清单

| 模块类 | 原文件 | 注册的回调数 |
|---|---|---|
| BattleModule | Battle.cpp | 1 动作 |
| ServerModule | Server.cpp | 2 识别 + 2 动作 |
| ArcaneCellModule | ArcaneCell.cpp | 1 识别 + 2 动作 |
| FerdinandModule | Ferdinand.cpp | 1 动作 |
| GuildModule | Guild.cpp | 1 动作 |
| WeeklyScheduleModule | WeeklySchedule.cpp | 1 识别 |
| BattleTimeTestModule | BattleTimeTest.cpp | 1 动作 |
| CelestialIsleModule | CelestialIsle.cpp | 6 识别 + 7 动作 |
| FoodFestivalModule | FoodFestival.cpp | 1 识别 + 2 动作 |
| RecipeModule | Recipe.cpp | 3 动作 |
| MonsterSoulUpgradeModule | MonsterSoulUpgrade.cpp | 2 识别 + 3 动作 |
| HuntingGroundFeastModule | HuntingGroundFeast.cpp | 4 识别 + 5 动作 |
| GreatMarshExpeditionModule | GreatMarshExpedition.cpp | 12 识别 + 7 动作 |
| EndlessWealthIsleModule | EndlessWealthIsle.cpp | 2 动作 |
| DarkSecretTreasureModule | DarkSecretTreasure.cpp | 2 动作 |
| RuneWorkshopModule | RuneWorkshop.cpp | 2 识别 |
| ShadowMirrorModule | ShadowMirror.cpp | 1 识别 + 1 动作 |
| SoulTombModule | SoulTomb.cpp | 1 识别 + 3 动作 |
| StewardModule | Steward.cpp | 2 动作 |
| TreasureSageModule | TreasureSage.cpp | 1 动作 |
| ShopModule | Comm.cpp（拆出） | 2 识别 + 2 动作 |

## 构建

CMakeLists 使用 `file(GLOB_RECURSE "src/*.cpp")` 收集源文件。构建命令：

```
cmake -S . -B build
cmake --build build --config Release
```

产物 `cpp-service.exe` 会尝试拷贝到 `agent/`（若 `agent/cpp-service.exe` 正被运行占用会失败，需先关闭运行中的程序）。
