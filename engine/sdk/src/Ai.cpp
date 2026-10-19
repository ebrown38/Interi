// SPDX-License-Identifier: MIT
#include <interi/sdk/Ai.h>

#include <algorithm>
#include <format>

namespace interi::sdk
{

std::vector<std::string> AiContracts()
{
    return {std::string(kSpawnComputerPlayerHook)};
}

Result<AiCatalog> AiCatalog::Build(const ModuleGraph& graph)
{
    AiCatalog catalog;
    for (const std::size_t index : graph.loadOrder)
    {
        const ModuleManifest& manifest = graph.modules[index].manifest;
        for (const AiBehaviourDecl& decl : manifest.behaviours)
        {
            AiBehaviour behaviour{.id = std::format("{}:{}", manifest.id, decl.name),
                                  .module = manifest.id,
                                  .description = decl.description,
                                  .chain = decl.chain};
            if (catalog.FindBehaviour(behaviour.id) != nullptr)
            {
                return MakeError(ErrorCode::InvalidArgument, std::format("duplicate behaviour '{}'", behaviour.id));
            }
            catalog.m_behaviours.push_back(std::move(behaviour));
        }
    }
    for (const std::size_t index : graph.loadOrder)
    {
        const ModuleManifest& manifest = graph.modules[index].manifest;
        for (const ComputerPlayerDecl& decl : manifest.computerPlayers)
        {
            ComputerPlayer player{.id = std::format("{}:{}", manifest.id, decl.name),
                                  .module = manifest.id,
                                  .behaviour = IsLocalId(decl.behaviour)
                                                   ? decl.behaviour
                                                   : std::format("{}:{}", manifest.id, decl.behaviour),
                                  .description = decl.description};
            if (catalog.FindPlayer(player.id) != nullptr)
            {
                return MakeError(ErrorCode::InvalidArgument, std::format("duplicate computer player '{}'", player.id));
            }
            if (catalog.FindBehaviour(player.behaviour) == nullptr)
            {
                return MakeError(ErrorCode::InvalidArgument,
                                 std::format("computer player '{}': no loaded module declares the behaviour '{}'",
                                             player.id, player.behaviour));
            }
            catalog.m_players.push_back(std::move(player));
        }
    }
    return catalog;
}

const AiBehaviour* AiCatalog::FindBehaviour(std::string_view id) const noexcept
{
    const auto it = std::ranges::find(m_behaviours, id, &AiBehaviour::id);
    return it == m_behaviours.end() ? nullptr : &*it;
}

const ComputerPlayer* AiCatalog::FindPlayer(std::string_view id) const noexcept
{
    const auto it = std::ranges::find(m_players, id, &ComputerPlayer::id);
    return it == m_players.end() ? nullptr : &*it;
}

Result<ComputerPlayerRequest> AiCatalog::MakeRequest(std::string_view playerId, std::uint32_t slot) const
{
    const ComputerPlayer* player = FindPlayer(playerId);
    if (player == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no computer player '{}'", playerId));
    }
    const AiBehaviour* behaviour = FindBehaviour(player->behaviour);
    return ComputerPlayerRequest{.player = player->id,
                                 .behaviour = player->behaviour,
                                 .chain = behaviour != nullptr ? behaviour->chain : std::string(),
                                 .slot = slot};
}

Result<std::vector<std::uint64_t>> SpawnComputerPlayers(const AiCatalog& catalog, const HookRegistry& hooks,
                                                        const std::vector<std::string>& playerIds)
{
    const HookPoint<SpawnComputerPlayerSignature> spawn =
        hooks.Find<SpawnComputerPlayerSignature>(kSpawnComputerPlayerHook);
    if (!spawn.IsValid())
    {
        return MakeError(ErrorCode::NotFound, "no module provides the ai.SpawnComputerPlayer hook");
    }
    std::vector<ComputerPlayerRequest> requests;
    for (std::size_t i = 0; i < playerIds.size(); ++i)
    {
        INTERI_TRY_ASSIGN(request, catalog.MakeRequest(playerIds[i], static_cast<std::uint32_t>(i)));
        requests.push_back(std::move(request));
    }
    std::vector<std::uint64_t> created;
    created.reserve(requests.size());
    for (const ComputerPlayerRequest& request : requests)
    {
        created.push_back(spawn(request));
    }
    return created;
}

} // namespace interi::sdk
