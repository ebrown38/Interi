// SPDX-License-Identifier: MIT
#pragma once

// AI behaviours and computer players (M24.3). Modules declare them in module.json (`behaviours`, `computerPlayers`);
// the engine gathers them in an AiCatalog and offers one hook, `ai.SpawnComputerPlayer`, that a game module (the
// Management game, say) provides to say what spawning a computer player means in its game: an entity, a team slot,
// a faction. The engine never spawns by itself, so a game decides how its AI is represented.
//
// A behaviour is a chain asset (`chain`) or native code the player's module registers as chain events; the catalog
// only names them, validates the references and lists them for the editor and docs.

#include <interi/core/Result.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/ModuleGraph.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

inline constexpr std::string_view kSpawnComputerPlayerHook = "ai.SpawnComputerPlayer";

/// What the host asks the game to spawn.
struct ComputerPlayerRequest
{
    std::string player;     ///< The computer player's id, `<module id>:<name>`.
    std::string behaviour;  ///< Its behaviour's id.
    std::string chain;      ///< The behaviour's chain asset, project-relative to its module folder (may be empty).
    std::uint32_t slot = 0; ///< 0 for the first computer player spawned in this session, 1 for the next...
};

/// The signature of the hook: returns the stable id of what the game created (0: nothing was spawned).
using SpawnComputerPlayerSignature = std::uint64_t(const ComputerPlayerRequest&);

/// Engine contract ids of the AI section (declare them in ModuleHostDesc::engineContracts).
[[nodiscard]] std::vector<std::string> AiContracts();

struct AiBehaviour
{
    std::string id;     ///< `<module id>:<name>`.
    std::string module; ///< Owner.
    std::string description;
    std::string chain; ///< Path inside the owner's folder; empty for native behaviours.
};

struct ComputerPlayer
{
    std::string id;
    std::string module;
    std::string behaviour; ///< Full behaviour id.
    std::string description;
};

class AiCatalog
{
public:
    /// Collects the behaviours and computer players of every module in the graph, in load order. InvalidArgument for a
    /// duplicate id or a computer player whose behaviour no loaded module declares.
    static Result<AiCatalog> Build(const ModuleGraph& graph);

    [[nodiscard]] const std::vector<AiBehaviour>& Behaviours() const noexcept { return m_behaviours; }
    [[nodiscard]] const std::vector<ComputerPlayer>& Players() const noexcept { return m_players; }
    [[nodiscard]] const AiBehaviour* FindBehaviour(std::string_view id) const noexcept;
    [[nodiscard]] const ComputerPlayer* FindPlayer(std::string_view id) const noexcept;

    /// The request for `playerId` as slot `slot`; NotFound for an unknown player.
    [[nodiscard]] Result<ComputerPlayerRequest> MakeRequest(std::string_view playerId, std::uint32_t slot) const;

private:
    std::vector<AiBehaviour> m_behaviours;
    std::vector<ComputerPlayer> m_players;
};

/// Calls the `ai.SpawnComputerPlayer` hook for each player id, as slots 0, 1, 2... Returns what the game created
/// (0 where it spawned nothing). NotFound for an unknown player, and for a hook nobody provides.
[[nodiscard]] Result<std::vector<std::uint64_t>>
SpawnComputerPlayers(const AiCatalog& catalog, const HookRegistry& hooks, const std::vector<std::string>& playerIds);

} // namespace interi::sdk
