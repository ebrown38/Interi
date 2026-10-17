// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/DestructibleAsset.h>
#include <interi/gameplay/Level.h>
#include <interi/platform/FileSystem.h>

#include "../destruction_runtime/ScenarioUtil.h"

#include <string>
#include <vector>

namespace interi::editor::testing
{

/// A ground box and two concrete towers (2 x 5 x 1 unit cubes each, anchored at the bottom) whose asset lives in a
/// MemoryFileSystem at "assets/tower.idest".
struct PlayLevel
{
    PlayLevel()
    {
        const auto asset = destruction::testing::WithConcreteLimits(destruction::testing::MakeGridAsset(
            destruction::testing::BoxCells(2, 5, 1), destruction::testing::Concrete()));
        (void)files.WriteFile("assets/tower.idest", destruction::WriteDestructibleAsset(asset, 1));
        gameplay::StaticBoxDef ground;
        ground.transform.position = {0.0f, -1.0f, 0.0f};
        ground.halfExtents = {30.0f, 1.0f, 30.0f};
        level.staticBoxes.push_back(ground);
        for (const float x : {-3.0f, 3.0f})
        {
            gameplay::DestructibleDef tower;
            tower.asset = "assets/tower.idest";
            tower.transform.position = {x, 0.0f, 0.0f};
            level.destructibles.push_back(tower);
        }
    }

    /// A blast at the foot of the first tower that breaks its lowest bonds.
    [[nodiscard]] static destruction::RadialDamage Blast()
    {
        destruction::RadialDamage damage;
        damage.center = {-2.0f, 0.5f, 1.2f};
        damage.energy = 5000.0f;
        damage.innerRadius = 1.0f;
        damage.radius = 3.0f;
        return damage;
    }

    MemoryFileSystem files;
    gameplay::LevelDef level;
};

} // namespace interi::editor::testing
