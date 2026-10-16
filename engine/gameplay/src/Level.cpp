// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/gameplay/Level.h>
#include <interi/physics/Components.h>
#include <interi/platform/FileSystem.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <numbers>

namespace interi::gameplay
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

Result<std::vector<float>> Numbers(const Json& value, std::size_t count, std::string_view what)
{
    if (!value.is_array() || value.size() != count ||
        !std::ranges::all_of(value, [](const Json& element) { return element.is_number(); }))
    {
        return Invalid(std::format("{} must be an array of {} numbers", what, count));
    }
    std::vector<float> numbers;
    for (const Json& element : value)
    {
        numbers.push_back(element.get<float>());
    }
    return numbers;
}

Result<Vec3> ReadVec3(const Json& object, std::string_view key, std::string_view what, bool required)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        if (required)
        {
            return Invalid(std::format("{} has no \"{}\"", what, key));
        }
        return Vec3();
    }
    INTERI_TRY_ASSIGN(numbers, Numbers(*it, 3, std::format("{}.{}", what, key)));
    return Vec3(numbers[0], numbers[1], numbers[2]);
}

Result<Quat> ReadRotation(const Json& object, std::string_view what)
{
    const auto it = object.find("rotation");
    if (it == object.end())
    {
        return Quat();
    }
    INTERI_TRY_ASSIGN(numbers, Numbers(*it, 4, std::format("{}.rotation", what)));
    const float length = std::sqrt(numbers[0] * numbers[0] + numbers[1] * numbers[1] + numbers[2] * numbers[2] +
                                   numbers[3] * numbers[3]);
    if (!(length > 1e-6f) || !std::isfinite(length))
    {
        return Invalid(std::format("{}.rotation is not a rotation", what));
    }
    return Quat{numbers[0] / length, numbers[1] / length, numbers[2] / length, numbers[3] / length};
}

void WarnUnknown(const Json& object, std::initializer_list<std::string_view> known, std::string_view what,
                 std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : object.items())
    {
        if (std::ranges::find(known, key) == known.end())
        {
            warnings.push_back(std::format("{}: unknown field \"{}\" ignored", what, key));
        }
    }
}

/// The array under `key`, or an empty one. Error if present but not an array of objects.
Result<const Json*> ObjectList(const Json& root, std::string_view key)
{
    static const Json kEmpty = Json::array();
    const auto it = root.find(key);
    if (it == root.end())
    {
        return &kEmpty;
    }
    if (!it->is_array() || !std::ranges::all_of(*it, [](const Json& entry) { return entry.is_object(); }))
    {
        return Invalid(std::format("\"{}\" must be an array of objects", key));
    }
    return &*it;
}

} // namespace

Result<LevelDef> ParseLevel(std::string_view json)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("level is not a JSON object");
    }
    LevelDef level;
    WarnUnknown(root, {"staticBoxes", "destructibles", "spawnPoints"}, "level", level.warnings);

    INTERI_TRY_ASSIGN(boxes, ObjectList(root, "staticBoxes"));
    for (std::size_t i = 0; i < boxes->size(); ++i)
    {
        const Json& entry = (*boxes)[i];
        const std::string what = std::format("staticBoxes[{}]", i);
        WarnUnknown(entry, {"position", "rotation", "halfExtents"}, what, level.warnings);
        StaticBoxDef box;
        INTERI_TRY_ASSIGN(position, ReadVec3(entry, "position", what, false));
        INTERI_TRY_ASSIGN(rotation, ReadRotation(entry, what));
        INTERI_TRY_ASSIGN(halfExtents, ReadVec3(entry, "halfExtents", what, true));
        if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f || !std::isfinite(halfExtents.x) ||
            !std::isfinite(halfExtents.y) || !std::isfinite(halfExtents.z))
        {
            return Invalid(std::format("{}.halfExtents must be positive", what));
        }
        box.transform.position = position;
        box.transform.rotation = rotation;
        box.halfExtents = halfExtents;
        level.staticBoxes.push_back(box);
    }

    INTERI_TRY_ASSIGN(destructibles, ObjectList(root, "destructibles"));
    for (std::size_t i = 0; i < destructibles->size(); ++i)
    {
        const Json& entry = (*destructibles)[i];
        const std::string what = std::format("destructibles[{}]", i);
        WarnUnknown(entry, {"asset", "position", "rotation"}, what, level.warnings);
        const auto asset = entry.find("asset");
        if (asset == entry.end() || !asset->is_string() || asset->get<std::string>().empty())
        {
            return Invalid(std::format("{} has no \"asset\" path", what));
        }
        DestructibleDef destructible;
        destructible.asset = asset->get<std::string>();
        INTERI_TRY_ASSIGN(position, ReadVec3(entry, "position", what, true));
        INTERI_TRY_ASSIGN(rotation, ReadRotation(entry, what));
        destructible.transform.position = position;
        destructible.transform.rotation = rotation;
        level.destructibles.push_back(std::move(destructible));
    }

    INTERI_TRY_ASSIGN(spawns, ObjectList(root, "spawnPoints"));
    for (std::size_t i = 0; i < spawns->size(); ++i)
    {
        const Json& entry = (*spawns)[i];
        const std::string what = std::format("spawnPoints[{}]", i);
        WarnUnknown(entry, {"name", "position", "yawDegrees"}, what, level.warnings);
        const auto name = entry.find("name");
        if (name == entry.end() || !name->is_string())
        {
            return Invalid(std::format("{} has no \"name\"", what));
        }
        SpawnPoint spawn;
        spawn.name = name->get<std::string>();
        INTERI_TRY_ASSIGN(position, ReadVec3(entry, "position", what, true));
        spawn.position = position;
        if (const auto yaw = entry.find("yawDegrees"); yaw != entry.end())
        {
            if (!yaw->is_number())
            {
                return Invalid(std::format("{}.yawDegrees is not a number", what));
            }
            spawn.yaw = yaw->get<float>() * std::numbers::pi_v<float> / 180.0f;
        }
        level.spawnPoints.push_back(std::move(spawn));
    }
    for (const std::string& warning : level.warnings)
    {
        INTERI_LOG_WARN("Gameplay", "{}", warning);
    }
    return level;
}

Result<LevelDef> LoadLevel(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    return ParseLevel(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

const SpawnPoint* LoadedLevel::FindSpawnPoint(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(spawnPoints, name, &SpawnPoint::name);
    if (found != spawnPoints.end())
    {
        return &*found;
    }
    return spawnPoints.empty() ? nullptr : &spawnPoints.front();
}

Result<LoadedLevel> SpawnLevel(const LevelDef& level, const IFileSystem& files, World& world, JobSystem& jobs,
                               destruction::DestructionWorld& destruction)
{
    // Read every asset first, so a missing one creates nothing.
    std::map<std::string, std::shared_ptr<const destruction::DestructibleAsset>, std::less<>> assets;
    for (const DestructibleDef& destructible : level.destructibles)
    {
        if (!assets.contains(destructible.asset))
        {
            INTERI_TRY_ASSIGN(asset, destruction::LoadDestructibleAsset(files, destructible.asset));
            assets.emplace(destructible.asset, std::move(asset));
        }
    }

    LoadedLevel loaded;
    loaded.spawnPoints = level.spawnPoints;
    for (const StaticBoxDef& box : level.staticBoxes)
    {
        physics::RigidBodyComponent body;
        body.motion = physics::MotionType::Static;
        body.layer = physics::ObjectLayer::Static;
        loaded.staticEntities.push_back(
            world.CreateEntity(box.transform, body, physics::ColliderComponent::Box(box.halfExtents)));
    }
    for (const DestructibleDef& destructible : level.destructibles)
    {
        loaded.destructibles.push_back(
            destruction.Spawn(world, jobs, assets.find(destructible.asset)->second, destructible.transform));
    }
    return loaded;
}

void DespawnLevel(LoadedLevel& level, World& world, destruction::DestructionWorld& destruction)
{
    for (const destruction::DestructibleId id : level.destructibles)
    {
        (void)destruction.Despawn(world, id);
    }
    for (const Entity entity : level.staticEntities)
    {
        (void)world.DestroyEntity(entity);
    }
    level = {};
}

} // namespace interi::gameplay
