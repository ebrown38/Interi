// SPDX-License-Identifier: MIT
// Starter (com.interi.starter): a top-down "collect the coins" game. Register runs when the engine loads (or
// reloads) the module; see interi/hotreload/ModuleApi.h for the rules and interi/sdk/Hooks.h for hooks.
//
// What lives where:
//   module.json        the Values (coins, total, score, won) and the CoinKinds data table (the project fills the rows)
//   templates/coins    a project: scene, chains, HUD and menus, input bindings, camera rigs
//   this file          what chains can't do on their own: a function that reads the table, an event that counts
//                      finished levels, and the save section for that count
#include <interi/chain/Event.h>
#include <interi/core/Archive.h>
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>
#include <interi/values/Reflection.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace
{

using interi::sdk::HookNext;
using Example = int(int);

/// Module state that outlives a level: it survives a hot reload (DeclareState) and goes into saves.
struct Progress
{
    std::int32_t levelsCompleted = 0;
};
Progress g_progress;

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    g_progress = {}; // a fresh load starts from zero; DeclareState brings back a reloaded instance's values
    interi::hotreload::DeclareState(registrar, g_progress,
                                    {interi::values::Field("levelsCompleted", &Progress::levelsCompleted)});

    // A hook that Plugins and higher modules can override ("com.interi.starter:Example").
    if (!scope.DeclareHook<Example>(scope.Local("Example"), [](const HookNext<Example>&, int value) { return value; })
             .has_value())
    {
        return false;
    }

    // A pure function for chain expressions: what a kind of coin is worth, from the CoinKinds table the project
    // fills in. Unknown kinds are worth nothing.
    const interi::values::DataRegistry& data = registrar.Data();
    interi::chain::FunctionDefinition coinValue;
    coinValue.parameters = {interi::values::ValueType::String};
    coinValue.result = interi::values::ValueType::Int;
    coinValue.tooltip = "The value of a coin kind (a row of the CoinKinds table).";
    coinValue.implementation = std::make_shared<const interi::chain::FunctionImpl>(
        [&data, table = scope.Local("CoinKinds")](std::span<const interi::values::Value> arguments)
        {
            const auto& kind = std::get<std::string>(arguments[0].Variant());
            return interi::values::Value(data.Tables().Get<std::int32_t>(table, kind, "value").value_or(0));
        });
    if (!registrar.Chains().Functions().Provide(scope.Provider(), scope.Local("CoinValue"), std::move(coinValue)))
    {
        return false;
    }

    // An action: LevelComplete -> Out. Effects run serially at the end of the step, so writing module state there
    // is safe.
    interi::chain::EventDefinition complete;
    complete.outputs = {{.name = "Out", .payload = {}}};
    complete.tooltip = "Counts a finished level.";
    complete.implementation = std::make_shared<const interi::chain::EventImpl>(
        [](interi::chain::EventCall& call)
        {
            call.Defer([](interi::chain::ApplyContext&) { ++g_progress.levelsCompleted; });
            call.Fire(0);
        });
    if (!registrar.Chains().Events().Provide(scope.Provider(), scope.Local("LevelComplete"), std::move(complete)))
    {
        return false;
    }

    // The save section: levelsCompleted, versioned so later builds can add fields.
    return registrar.Saves()
        .Add(scope.Provider(), {.id = scope.Local("progress"),
                                .version = 1,
                                .capture =
                                    []
                                {
                                    std::vector<std::byte> bytes;
                                    interi::ArchiveWriter writer(bytes, interi::MakeFourCC("STRT"), 1);
                                    Progress copy = g_progress;
                                    writer(copy.levelsCompleted);
                                    return bytes;
                                },
                                .restore = [](std::span<const std::byte> data, std::uint32_t) -> interi::Result<void>
                                {
                                    INTERI_TRY_ASSIGN(reader,
                                                      interi::ArchiveReader::Open(data, interi::MakeFourCC("STRT"), 1));
                                    Progress loaded;
                                    reader(loaded.levelsCompleted);
                                    INTERI_TRY(reader.Finish());
                                    g_progress = loaded;
                                    return {};
                                }})
        .has_value();
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
