// SPDX-License-Identifier: MIT
// Sandbox: an RFG-style playground. A third-person character (Jolt CharacterVirtual) with a sledgehammer, remote
// charges and rockets in a level of pre-fractured concrete walls (data/levels/sandbox.json), on the ECS, Jolt
// physics, destruction, debris and the Vulkan forward renderer (interior materials, GPU particles, crack and scorch
// decals). Gameplay rules live in a hot-reloadable module (game/sandbox_module).
//
//   interi_sandbox [--video-driver <name>] [--frames <count>] [--validation 1] [--auto-spawn 1] [--hot-reload 1]
//
// Controls (data/gameplay/input.json): WASD / left stick walk, mouse / right stick look, Space / South jump,
// 1 2 3 / d-pad pick the sledgehammer, remote charges or rockets, left mouse / right shoulder uses the weapon
// (swing, place a charge, fire), right mouse / left shoulder detonates the charges, G / North spawns a burst of
// debris cubes, F5 saves the destruction state, F9 reloads the level and restores it, F6 reloads the gameplay
// module, Escape quits.
//
// --frames exits after that many frames (smoke runs; --video-driver dummy has no Vulkan so nothing is drawn,
// offscreen renders without a display). --auto-spawn plays by itself: debris bursts, a rocket at a wall every second
// and charges set off in between. --hot-reload watches game/sandbox_module, rebuilds the module when it changes
// and swaps it in; the module library itself is always watched.
#include <interi/core/Log.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction_render/BreakEffects.h>
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/gameplay/Character.h>
#include <interi/gameplay/DestructionState.h>
#include <interi/gameplay/FollowCamera.h>
#include <interi/gameplay/InputConfig.h>
#include <interi/gameplay/Level.h>
#include <interi/gameplay/WeaponDef.h>
#include <interi/gameplay/Weapons.h>
#include <interi/hotreload/FileWatcher.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltRuntime.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/platform/FileSystem.h>
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/render/Decals.h>
#include <interi/runtime/FixedStepClock.h>

#include "SandboxComponents.h"
#include "SandboxRenderer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using interi::Vec3;
namespace gameplay = interi::gameplay;

struct Options
{
    std::string videoDriver;
    std::uint64_t maxFrames = 0; ///< 0 = run until quit.
    bool validation = false;
    bool autoSpawn = false;
    bool hotReload = false;
};

Options ParseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string_view flag = argv[i];
        if (flag == "--video-driver")
        {
            options.videoDriver = argv[i + 1];
        }
        else if (flag == "--frames")
        {
            options.maxFrames = std::strtoull(argv[i + 1], nullptr, 10);
        }
        else if (flag == "--validation")
        {
            options.validation = std::string_view(argv[i + 1]) == "1";
        }
        else if (flag == "--auto-spawn")
        {
            options.autoSpawn = std::string_view(argv[i + 1]) == "1";
        }
        else if (flag == "--hot-reload")
        {
            options.hotReload = std::string_view(argv[i + 1]) == "1";
        }
    }
    return options;
}

/// A 4 x 3 x 0.3 m concrete wall standing on y = 0, centred on x = z = 0, pre-fractured into 4 regions of up to 10
/// chunks each (as fracture_cooker would, without the glTF).
interi::Result<std::shared_ptr<const interi::destruction::DestructibleAsset>> BuildWall(interi::JobSystem& jobs)
{
    using namespace interi::destruction;
    HierarchySettings hierarchy;
    for (const auto& [count, seed] : {std::pair{4u, 11u}, std::pair{10u, 12u}})
    {
        SeedSettings level;
        level.count = count;
        level.randomSeed = seed;
        hierarchy.levels.push_back(level);
    }
    const ChunkHierarchy chunks =
        BuildHierarchy(MakeBoxMesh({-2.0f, 0.0f, -0.15f}, {2.0f, 3.0f, 0.15f}), hierarchy, jobs);
    std::vector<DestructionMaterial> materials(1);
    materials[0].name = "concrete";
    BondSettings bonds;
    bonds.groundHeight = 0.0f;
    BondGraph graph = BuildBondGraph(chunks.chunks, materials, bonds);
    INTERI_TRY_ASSIGN(asset, AssembleAsset(chunks.chunks, std::move(graph), materials, {}, jobs));
    return std::make_shared<const DestructibleAsset>(std::move(asset));
}

std::string ReadText(const interi::IFileSystem& files, std::string_view path)
{
    auto bytes = files.ReadFile(path);
    return bytes ? std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()) : std::string();
}

/// Rebuilds the gameplay module on a background thread when its sources change (--hot-reload).
class ModuleBuilder
{
public:
    ModuleBuilder()
    {
        m_sources.Watch(INTERI_SANDBOX_MODULE_SOURCE_DIR);
        (void)m_sources.Poll();
    }
    ModuleBuilder(const ModuleBuilder&) = delete;
    ModuleBuilder& operator=(const ModuleBuilder&) = delete;
    ModuleBuilder(ModuleBuilder&&) = delete;
    ModuleBuilder& operator=(ModuleBuilder&&) = delete;
    ~ModuleBuilder() = default;

    void Poll()
    {
        if (m_building.load() || !m_sources.Poll())
        {
            return;
        }
        m_building = true;
        m_thread = std::jthread(
            [this]
            {
                const std::string command =
                    std::format("cmake --build \"{}\" --target interi_sandbox_module", INTERI_SANDBOX_BUILD_DIR);
                INTERI_LOG_INFO("Sandbox", "module sources changed: {}", command);
                const int status = std::system(command.c_str());
                INTERI_LOG_INFO("Sandbox", "module build finished ({})", status == 0 ? "ok" : "failed");
                m_building = false;
            });
    }

    [[nodiscard]] bool Building() const noexcept { return m_building.load(); }

private:
    interi::hotreload::FileWatcher m_sources;
    std::atomic<bool> m_building{false};
    std::jthread m_thread;
};

/// The live level: what SpawnLevel made, and the assets (cached across reloads).
struct LevelState
{
    gameplay::LevelDef def;
    gameplay::AssetCache assets;
    gameplay::LoadedLevel loaded;
};

} // namespace

int main(int argc, char** argv)
{
    const Options options = ParseOptions(argc, argv);
    interi::Logger logger;
    logger.AddSink(std::make_unique<interi::ConsoleSink>());
    interi::SetGlobalLogger(&logger);
    auto platform = interi::SdlPlatform::Create(interi::SdlPlatformDesc{.videoDriver = options.videoDriver});
    if (!platform)
    {
        std::fprintf(stderr, "%s\n", platform.error().message.c_str());
        return EXIT_FAILURE;
    }
    auto window =
        (*platform)->OpenWindow(interi::WindowDesc{.title = "Interi Sandbox", .size = {1280, 720}, .vulkan = true});
    if (!window)
    {
        // No Vulkan in this video driver (e.g. "dummy"): run the simulation without drawing.
        window = (*platform)->OpenWindow(interi::WindowDesc{.title = "Interi Sandbox", .size = {1280, 720}});
    }
    if (!window)
    {
        std::fprintf(stderr, "%s\n", window.error().message.c_str());
        return EXIT_FAILURE;
    }

    // Data: the repository's data directory, plus generated assets in memory.
    interi::NativeFileSystem dataFiles(INTERI_SANDBOX_DATA_DIR);
    interi::MemoryFileSystem generatedFiles;
    interi::VirtualFileSystem files;
    (void)files.Mount("", dataFiles);
    (void)files.Mount("generated", generatedFiles);

    interi::InputMapper input;
    if (auto bound = gameplay::ApplyInputConfig(ReadText(files, "gameplay/input.json"), input); !bound)
    {
        std::fprintf(stderr, "input config: %s\n", bound.error().message.c_str());
        return EXIT_FAILURE;
    }
    const auto quit = input.AddAction("quit");
    const auto jump = input.AddAction("jump");
    const auto fire = input.AddAction("fire");
    const auto detonate = input.AddAction("detonate");
    const auto spawnDebris = input.AddAction("spawnDebris");
    const auto save = input.AddAction("save");
    const auto load = input.AddAction("load");
    const auto reloadModule = input.AddAction("reloadModule");
    const std::array selectWeapon{input.AddAction("sledgehammer"), input.AddAction("remoteCharge"),
                                  input.AddAction("rocket")};
    const auto moveX = input.AddAxis("moveX");
    const auto moveY = input.AddAxis("moveY");
    const auto lookX = input.AddAxis("lookX");
    const auto lookY = input.AddAxis("lookY");

    auto weaponDefs = gameplay::ParseWeaponDefs(ReadText(files, "gameplay/weapons.json"));
    if (!weaponDefs)
    {
        std::fprintf(stderr, "weapons: %s\n", weaponDefs.error().message.c_str());
        return EXIT_FAILURE;
    }
    const std::array weapons{gameplay::FindWeapon(weaponDefs->weapons, "sledgehammer"),
                             gameplay::FindWeapon(weaponDefs->weapons, "remote_charge"),
                             gameplay::FindWeapon(weaponDefs->weapons, "rocket")};
    if (weapons[0] == nullptr || weapons[1] == nullptr || weapons[2] == nullptr)
    {
        std::fprintf(stderr, "weapons.json needs sledgehammer, remote_charge and rocket\n");
        return EXIT_FAILURE;
    }

    const interi::physics::JoltRuntime joltRuntime;
    interi::JobSystem jobs;
    std::unique_ptr<sandbox::SandboxRenderer> renderer;
    if (auto created = sandbox::SandboxRenderer::Create(**platform, **window, jobs, options.validation); created)
    {
        renderer = std::move(*created);
    }
    else
    {
        std::fprintf(stderr, "Rendering disabled: %s\n", created.error().message.c_str());
    }
    interi::CommandBuffers commands(jobs);
    interi::World world;
    interi::physics::PhysicsWorld physics(jobs);
    interi::physics::PhysicsSync physicsSync(physics);
    interi::FixedStepClock clock;
    const auto dt = static_cast<float>(clock.StepSeconds());
    interi::destruction::DestructionWorld destruction(physics);
    interi::destruction::DebrisSystem debrisSystem(physics);
    destruction.SetDebrisSystem(&debrisSystem);
    interi::destruction::BreakEffects breakEffects;
    gameplay::WeaponSystem weaponSystem(physics, destruction);

    // The level's wall is generated (as fracture_cooker would cook it) and served from memory.
    auto wall = BuildWall(jobs);
    if (!wall)
    {
        std::fprintf(stderr, "wall: %s\n", wall.error().message.c_str());
        return EXIT_FAILURE;
    }
    (void)generatedFiles.WriteFile("wall.idest", interi::destruction::WriteDestructibleAsset(**wall, 0));
    LevelState level;
    if (auto def = gameplay::LoadLevel(files, "levels/sandbox.json"); def)
    {
        level.def = std::move(*def);
    }
    else
    {
        std::fprintf(stderr, "level: %s\n", def.error().message.c_str());
        return EXIT_FAILURE;
    }
    const auto spawnLevel = [&]() -> bool
    {
        auto loaded = gameplay::SpawnLevel(level.def, files, world, jobs, destruction, &level.assets);
        if (!loaded)
        {
            INTERI_LOG_ERROR("Sandbox", "level: {}", loaded.error().message);
            return false;
        }
        level.loaded = std::move(*loaded);
        for (const auto& asset : level.loaded.assets)
        {
            if (renderer == nullptr)
            {
                break;
            }
            if (auto added = renderer->AddAsset(asset); !added)
            {
                INTERI_LOG_ERROR("Sandbox", "level meshes: {}", added.error().message);
                return false;
            }
        }
        return true;
    };
    if (!spawnLevel())
    {
        return EXIT_FAILURE;
    }
    const gameplay::SpawnPoint spawn = *level.loaded.FindSpawnPoint("player");
    gameplay::CharacterController player(physics, gameplay::CharacterSettings{}, spawn.position);
    gameplay::FollowCamera camera;
    camera.yaw = spawn.yaw;

    // Gameplay rules: the hot-reloadable module.
    (void)world.CreateEntity(sandbox::StepTime{dt});
    interi::hotreload::ModuleHost modules(world);
    if (auto loaded = modules.Load(INTERI_SANDBOX_MODULE_PATH); !loaded)
    {
        INTERI_LOG_ERROR("Sandbox", "gameplay module: {} (debris will not expire)", loaded.error().message);
    }
    interi::hotreload::FileWatcher moduleFile;
    moduleFile.Watch(INTERI_SANDBOX_MODULE_PATH);
    (void)moduleFile.Poll();
    std::unique_ptr<ModuleBuilder> builder = options.hotReload ? std::make_unique<ModuleBuilder>() : nullptr;
    bool moduleChanged = false;

    const std::filesystem::path savePath = std::filesystem::temp_directory_path() / "interi_sandbox_destruction.idss";
    const interi::physics::ColliderComponent debrisCollider =
        interi::physics::ColliderComponent::Box(interi::Vec3(0.25f));
    interi::Query debrisQuery;
    debrisQuery.With<sandbox::Lifetime, interi::Transform>();

    std::size_t weapon = 2;
    float cooldown = 0.0f;
    std::vector<interi::InputEvent> events;
    std::vector<interi::Transform> debris;
    std::vector<Vec3> markers;
    std::vector<interi::render::Particle> particles;
    std::deque<interi::render::Decal> scorches;
    std::vector<interi::render::Decal> decals;
    std::uint64_t stepCount = 0;
    std::uint32_t blasts = 0;
    std::uint64_t presented = 0;
    std::uint32_t moduleReloads = 0;
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    auto titleTime = previous;
    std::uint64_t framesSinceTitle = 0;
    for (std::uint64_t frame = 0; options.maxFrames == 0 || frame < options.maxFrames; ++frame)
    {
        events.clear();
        (*platform)->PollEvents(events);
        input.Update(events);
        if (input.QuitRequested() || input.WasPressed(quit))
        {
            break;
        }

        // Hot reload: the module library changed (a build finished) or F6.
        if (builder != nullptr)
        {
            builder->Poll();
        }
        moduleChanged = moduleFile.Poll() || moduleChanged;
        if ((moduleChanged && (builder == nullptr || !builder->Building())) || input.WasPressed(reloadModule))
        {
            moduleChanged = false;
            if (auto loaded = modules.Load(INTERI_SANDBOX_MODULE_PATH); loaded)
            {
                ++moduleReloads;
            }
            else
            {
                INTERI_LOG_ERROR("Sandbox", "module reload failed, keeping the old one: {}", loaded.error().message);
            }
        }

        if (input.WasPressed(save))
        {
            const std::vector<std::byte> bytes =
                gameplay::WriteDestructionState(gameplay::CaptureDestructionState(world, destruction, physics));
            interi::NativeFileSystem saveFiles(savePath.parent_path());
            if (auto written = saveFiles.WriteFile(savePath.filename().string(), bytes); written)
            {
                INTERI_LOG_INFO("Sandbox", "saved destruction state to {}", savePath.string());
            }
        }
        if (input.WasPressed(load))
        {
            interi::NativeFileSystem saveFiles(savePath.parent_path());
            auto bytes = saveFiles.ReadFile(savePath.filename().string());
            auto state = bytes ? gameplay::ReadDestructionState(*bytes)
                               : interi::Result<gameplay::DestructionState>(std::unexpected(bytes.error()));
            if (state)
            {
                // Restoring adds damage to an intact level: reload it first.
                gameplay::DespawnLevel(level.loaded, world, destruction);
                weaponSystem.Clear();
                if (spawnLevel())
                {
                    if (auto restored = gameplay::RestoreDestructionState(world, jobs, destruction, physics, *state);
                        !restored)
                    {
                        INTERI_LOG_ERROR("Sandbox", "load: {}", restored.error().message);
                    }
                }
            }
            else
            {
                INTERI_LOG_ERROR("Sandbox", "load: {}", state.error().message);
            }
        }

        camera.Rotate(input.MouseDeltaX() + input.AxisValue(lookX) * 12.0f,
                      input.MouseDeltaY() + input.AxisValue(lookY) * 12.0f);
        for (std::size_t i = 0; i < selectWeapon.size(); ++i)
        {
            if (input.WasPressed(selectWeapon[i]))
            {
                weapon = i;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const float frameSeconds = std::chrono::duration<float>(now - previous).count();
        const std::uint32_t steps = clock.Advance(now - previous);
        previous = now;
        const double seconds = std::chrono::duration<double>(now - start).count();
        for (std::uint32_t step = 0; step < steps; ++step)
        {
            const bool firstStep = step == 0;
            cooldown = std::max(cooldown - dt, 0.0f);
            const Vec3 aimFrom = player.EyePosition();
            Vec3 aim = camera.Forward();

            if (options.autoSpawn)
            {
                // Play by itself: aim at the walls in turn, a rocket every second, charges set off in between.
                const float x = level.def.destructibles[blasts % level.def.destructibles.size()].transform.position.x;
                aim = Normalize(Vec3(x, 1.0f + 0.4f * static_cast<float>(blasts % 4), 0.0f) - aimFrom);
                if (stepCount % 60 == 30)
                {
                    weaponSystem.FireRocket(*weapons[2], aimFrom, aim);
                    ++blasts;
                }
                else if (stepCount % 60 == 0)
                {
                    (void)weaponSystem.PlaceCharge(world, *weapons[1], aimFrom, aim);
                }
                else if (stepCount % 60 == 15)
                {
                    weaponSystem.DetonateCharges();
                }
            }
            else if (firstStep && input.IsDown(fire) && cooldown <= 0.0f)
            {
                const gameplay::WeaponDef& def = *weapons[weapon];
                cooldown = def.cooldown;
                switch (def.kind)
                {
                case gameplay::WeaponKind::Melee:
                    (void)weaponSystem.Swing(def, aimFrom, aim);
                    break;
                case gameplay::WeaponKind::RemoteCharge:
                    (void)weaponSystem.PlaceCharge(world, def, aimFrom, aim);
                    break;
                case gameplay::WeaponKind::Rocket:
                    weaponSystem.FireRocket(def, aimFrom + aim * 0.5f, aim);
                    break;
                }
            }
            if (firstStep && input.WasPressed(detonate))
            {
                weaponSystem.DetonateCharges();
            }
            if ((firstStep && input.IsDown(spawnDebris)) || (options.autoSpawn && stepCount % 4 == 0))
            {
                const Vec3 origin = player.FootPosition() + camera.FlatForward() * 4.0f + Vec3(0.0f, 6.0f, 0.0f);
                for (int i = 0; i < 16; ++i)
                {
                    // Every fourth piece is large debris that piles up; the rest is rubble that only hits the level.
                    const float angle = static_cast<float>(stepCount * 16 + static_cast<std::uint64_t>(i)) * 0.39f;
                    interi::physics::RigidBodyComponent body;
                    body.layer =
                        i % 4 == 0 ? interi::physics::ObjectLayer::Debris : interi::physics::ObjectLayer::DebrisSmall;
                    body.linearVelocity = {std::cos(angle) * 3.0f, 6.0f, std::sin(angle) * 3.0f};
                    interi::Transform transform;
                    transform.position = origin;
                    (void)world.CreateEntity(body, debrisCollider, transform, sandbox::Lifetime{6.0f});
                }
            }

            modules.RunSystems(jobs, commands);
            physicsSync.Step(world, jobs, dt);
            player.Update(dt, {.move = camera.MoveDirection(input.AxisValue(moveX), input.AxisValue(moveY)),
                               .jump = firstStep && input.WasPressed(jump)});
            weaponSystem.Update(world, dt);
            destruction.Update(world, jobs);
            interi::destruction::DebrisView debrisView;
            debrisView.position = camera.Eye(player.EyePosition());
            debrisSystem.Update(world, jobs, debrisView, dt);
            breakEffects.Spawn(destruction, particles);
            for (const gameplay::Blast& blast : weaponSystem.Blasts())
            {
                const Vec3 normal = LengthSquared(blast.normal) > 0.0f ? blast.normal : Vec3(0.0f, 1.0f, 0.0f);
                scorches.push_back({.transform = interi::render::DecalBox(blast.center, normal, 2.2f, 2.2f),
                                    .color = {0.02f, 0.02f, 0.02f},
                                    .intensity = 0.8f,
                                    .kind = interi::render::DecalKind::Scorch,
                                    .seed = static_cast<std::uint32_t>(scorches.size() + stepCount)});
                if (scorches.size() > 32)
                {
                    scorches.pop_front();
                }
            }
            ++stepCount;
        }

        if (renderer != nullptr)
        {
            debris.clear();
            debrisQuery.ForEachChunk(world,
                                     [&](interi::ChunkView& view)
                                     {
                                         for (const interi::Transform& transform : view.Read<interi::Transform>())
                                         {
                                             debris.push_back(transform);
                                         }
                                     });
            markers.clear();
            for (const gameplay::RemoteCharge& charge : weaponSystem.Charges())
            {
                markers.push_back(charge.position);
            }
            for (const gameplay::Rocket& rocket : weaponSystem.Rockets())
            {
                markers.push_back(rocket.position);
            }
            decals.assign(scorches.begin(), scorches.end());
            const Vec3 target = player.EyePosition();
            presented += renderer->Render({.cubes = debris,
                                           .world = &world,
                                           .destruction = &destruction,
                                           .particles = particles,
                                           .decals = decals,
                                           .markers = markers,
                                           .player = player.FootPosition(),
                                           .eye = camera.Eye(target),
                                           .target = target + camera.Right() * camera.shoulder,
                                           .seconds = seconds,
                                           .dt = frameSeconds})
                             ? 1
                             : 0;
        }

        particles.clear();
        ++framesSinceTitle;
        if (now - titleTime >= std::chrono::seconds(1))
        {
            const double elapsed = std::chrono::duration<double>(now - titleTime).count();
            (*window)->SetTitle(std::format("Interi Sandbox - {} - {} entities - {:.0f} fps", weapons[weapon]->name,
                                            world.EntityCount(), static_cast<double>(framesSinceTitle) / elapsed));
            titleTime = now;
            framesSinceTitle = 0;
        }
    }
    if (options.maxFrames != 0)
    {
        // Smoke runs check this: every frame should have been presented when rendering is enabled.
        std::printf("Presented %llu of %llu frames (%zu debris at exit, %u rockets, %u debris pieces, module "
                    "generation %u, %u reloads)\n",
                    static_cast<unsigned long long>(presented), static_cast<unsigned long long>(options.maxFrames),
                    debris.size(), blasts, debrisSystem.BodyCount() + debrisSystem.ParticleCount(),
                    modules.Generation(), moduleReloads);
    }
    modules.Unload();
    interi::SetGlobalLogger(nullptr);
    return EXIT_SUCCESS;
}
