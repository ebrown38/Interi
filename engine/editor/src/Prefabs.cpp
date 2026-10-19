// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelManifest.h>
#include <interi/assets/Material.h>
#include <interi/editor/Commands.h>
#include <interi/editor/Prefabs.h>
#include <interi/platform/FileSystem.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <ranges>
#include <utility>

namespace interi::editor
{

namespace
{

using Commands = std::vector<std::unique_ptr<ICommand>>;

/// Snapshots of scene objects (field JSON resolved through the scene's reflection).
Result<std::vector<EntitySnapshot>> SnapshotsOf(const EditorScene& scene, std::vector<scene::SceneObject> objects)
{
    scene::SceneDocument document;
    document.objects = std::move(objects);
    EditorScene scratch(scene.Reflection());
    INTERI_TRY(scratch.Load(document));
    std::vector<EntitySnapshot> snapshots;
    for (const scene::SceneObject& object : document.objects)
    {
        if (auto snapshot = scratch.Snapshot(object.id))
        {
            snapshots.push_back(std::move(*snapshot));
        }
    }
    return snapshots;
}

/// The instance root `id` as a scene object with only its own components (name, parent, local transform, instance).
std::optional<scene::SceneObject> OwnComponents(const EditorScene& scene, EditorEntityId id, bool keepOverrides)
{
    const scene::SceneDocument document = scene.ToDocument();
    const scene::SceneObject* found = document.Find(id);
    if (found == nullptr || found->Find(scene::PrefabInstanceComponent::kComponentName) == nullptr)
    {
        return std::nullopt;
    }
    scene::SceneObject root{.id = id, .components = {}};
    for (const scene::SceneComponent& component : found->components)
    {
        if (component.name == scene::NameComponent::kComponentName ||
            component.name == scene::ParentComponent::kComponentName ||
            component.name == scene::LocalTransformComponent::kComponentName)
        {
            root.components.push_back(component);
        }
        else if (component.name == scene::PrefabInstanceComponent::kComponentName)
        {
            scene::SceneComponent instance = component;
            if (!keepOverrides)
            {
                for (auto& [field, json] : instance.fields)
                {
                    if (field == "overrides")
                    {
                        json = "[]";
                    }
                }
            }
            root.components.push_back(std::move(instance));
        }
    }
    return root;
}

/// Commands that create objects from snapshots, parents first.
void CreateAll(EditorScene& scene, Commands& commands, std::vector<EntitySnapshot> snapshots)
{
    for (EntitySnapshot& snapshot : snapshots)
    {
        commands.push_back(std::make_unique<CreateEntityCommand>(scene, std::move(snapshot)));
    }
}

/// The objects an instance expanded into that still exist (the root first).
std::vector<EditorEntityId> InstanceObjects(const EditorScene& scene, EditorEntityId root)
{
    std::vector<EditorEntityId> ids;
    for (const EditorEntityId id : scene.Ids())
    {
        if (InstanceRootOf(scene, id) == root)
        {
            ids.push_back(id);
        }
    }
    return ids; // ascending: the root's ordinary id sorts before the derived ones
}

} // namespace

scene::PrefabLoader PrefabLoaderFor(const IFileSystem& files)
{
    return [&files](std::string_view path) -> Result<scene::SceneDocument>
    {
        INTERI_TRY_ASSIGN(document, scene::LoadScene(files, path));
        if (auto root = scene::PrefabRoot(document); !root)
        {
            return MakeError(root.error().code, std::format("{}: {}", path, root.error().message));
        }
        return document;
    };
}

EditorEntityId InstanceRootOf(const EditorScene& scene, EditorEntityId id)
{
    const std::optional<Entity> entity = scene.Find(id);
    const auto* link = entity ? scene.GetWorld().Get<scene::PrefabLinkComponent>(*entity) : nullptr;
    return link == nullptr ? 0 : link->instance.id;
}

Result<void> CreatePrefabFromSelection(EditorDocument& document, IFileSystem& files, const std::string& path)
{
    EditorScene& scene = document.Scene();
    const std::vector<EditorEntityId> roots = document.SelectedRoots();
    if (roots.size() != 1)
    {
        return MakeError(ErrorCode::InvalidArgument, "create prefab: select one object (its children come along)");
    }
    const EditorEntityId root = roots[0];
    const scene::SceneDocument current = scene.ToDocument();
    INTERI_TRY_ASSIGN(prefab, scene::MakePrefab(current, root));
    const std::string text = scene::WriteScene(prefab);
    INTERI_TRY(files.WriteFile(path, std::as_bytes(std::span(text))));

    // The root becomes an instance (keeping its id, name, parent and placement) of the file just written.
    scene::SceneObject instance{.id = root, .components = {}};
    for (const scene::SceneComponent& component : current.Find(root)->components)
    {
        if (component.name == scene::NameComponent::kComponentName ||
            component.name == scene::ParentComponent::kComponentName ||
            component.name == scene::LocalTransformComponent::kComponentName)
        {
            instance.components.push_back(component);
        }
    }
    instance.components.push_back(
        {.name = scene::PrefabInstanceComponent::kComponentName,
         .fields = {{"overrides", "[]"}, {"prefab", values::ValueToJsonText(values::AssetRef{path})}}});
    std::ranges::sort(instance.components, {}, &scene::SceneComponent::name);
    std::vector<std::string> warnings;
    INTERI_TRY_ASSIGN(objects, scene::ExpandInstance(instance, PrefabLoaderFor(files), warnings));
    INTERI_TRY_ASSIGN(snapshots, SnapshotsOf(scene, std::move(objects)));

    Commands commands;
    for (const EditorEntityId id : std::views::reverse(scene.Subtree(root)))
    {
        commands.push_back(std::make_unique<DestroyEntityCommand>(scene, id));
    }
    CreateAll(scene, commands, std::move(snapshots));
    document.Undo().Execute(std::make_unique<MacroCommand>("Create prefab", std::move(commands)));
    document.Select(root);
    return {};
}

Result<EditorEntityId> InstantiatePrefab(EditorDocument& document, const IFileSystem& files, std::string path,
                                         const Transform& world)
{
    EditorScene& scene = document.Scene();
    const EditorEntityId id = scene.NextId();
    scene::SceneObject root{.id = id, .components = {}};
    root.components.push_back({.name = scene::LocalTransformComponent::kComponentName,
                               .fields = {{"position", values::ValueToJsonText(world.position)},
                                          {"rotation", values::ValueToJsonText(world.rotation)},
                                          {"scale", values::ValueToJsonText(world.scale)}}});
    root.components.push_back(
        {.name = scene::PrefabInstanceComponent::kComponentName,
         .fields = {{"overrides", "[]"}, {"prefab", values::ValueToJsonText(values::AssetRef{std::move(path)})}}});
    std::vector<std::string> warnings;
    INTERI_TRY_ASSIGN(objects, scene::ExpandInstance(root, PrefabLoaderFor(files), warnings));
    INTERI_TRY_ASSIGN(snapshots, SnapshotsOf(scene, std::move(objects)));
    Commands commands;
    CreateAll(scene, commands, std::move(snapshots));
    document.Undo().Execute(std::make_unique<MacroCommand>("Instance prefab", std::move(commands)));
    document.Select(id);
    return id;
}

Result<EditorEntityId> InstantiateModel(EditorDocument& document, const IFileSystem& files, std::string manifestPath,
                                        const Transform& world)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(manifestPath));
    auto manifest =
        asset_import::ParseModelManifest(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!manifest)
    {
        return MakeError(manifest.error().code, std::format("{}: {}", manifestPath, manifest.error().message));
    }
    EditorScene& scene = document.Scene();
    const EditorEntityId rootId = scene.NextId();
    const auto transformFields = [](const Transform& t) -> std::vector<std::pair<std::string, std::string>>
    {
        return {{"position", values::ValueToJsonText(t.position)},
                {"rotation", values::ValueToJsonText(t.rotation)},
                {"scale", values::ValueToJsonText(t.scale)}};
    };
    const auto object = [&](EditorEntityId id, std::string name, EditorEntityId parent, const Transform& local)
    {
        scene::SceneObject result{.id = id, .components = {}};
        result.components.push_back({.name = scene::NameComponent::kComponentName,
                                     .fields = {{"name", values::ValueToJsonText(std::move(name))}}});
        result.components.push_back({.name = scene::ParentComponent::kComponentName,
                                     .fields = {{"parent", values::ValueToJsonText(values::EntityRef{parent})}}});
        result.components.push_back(
            {.name = scene::LocalTransformComponent::kComponentName, .fields = transformFields(local)});
        return result;
    };
    std::vector<scene::SceneObject> objects;
    objects.push_back(object(rootId, std::filesystem::path(manifestPath).parent_path().filename().string(), 0, world));
    EditorEntityId next = rootId;
    for (const asset_import::ModelManifestMesh& mesh : manifest->meshes)
    {
        scene::SceneObject child = object(++next, mesh.name, rootId, Transform{});
        values::ValueList colors{.element = values::ValueType::Color, .items = {}};
        for (const std::string& materialPath : mesh.materials)
        {
            values::Color color{.r = 0.6f, .g = 0.6f, .b = 0.6f, .a = 1.0f};
            if (!materialPath.empty())
            {
                if (const auto material = assets::LoadMaterialAsset(files, materialPath))
                {
                    color = {.r = material->baseColor.x,
                             .g = material->baseColor.y,
                             .b = material->baseColor.z,
                             .a = material->baseColor.w};
                }
            }
            colors.items.emplace_back(color);
        }
        child.components.push_back({.name = scene::MeshComponent::kComponentName,
                                    .fields = {{"asset", values::ValueToJsonText(values::AssetRef{mesh.mesh})},
                                               {"colors", values::ValueToJsonText(std::move(colors))}}});
        std::ranges::sort(child.components, {}, &scene::SceneComponent::name);
        objects.push_back(std::move(child));
    }
    std::ranges::sort(objects.front().components, {}, &scene::SceneComponent::name);
    INTERI_TRY_ASSIGN(snapshots, SnapshotsOf(scene, std::move(objects)));
    Commands commands;
    CreateAll(scene, commands, std::move(snapshots));
    document.Undo().Execute(std::make_unique<MacroCommand>("Instance model", std::move(commands)));
    document.Select(rootId);
    return rootId;
}

Result<void> RevertInstance(EditorDocument& document, const IFileSystem& files, EditorEntityId root)
{
    EditorScene& scene = document.Scene();
    const std::optional<scene::SceneObject> own = OwnComponents(scene, root, false);
    if (!own)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("object {} is not a prefab instance", root));
    }
    std::vector<std::string> warnings;
    INTERI_TRY_ASSIGN(objects, scene::ExpandInstance(*own, PrefabLoaderFor(files), warnings));
    INTERI_TRY_ASSIGN(snapshots, SnapshotsOf(scene, std::move(objects)));
    Commands commands;
    for (const EditorEntityId id : std::views::reverse(InstanceObjects(scene, root)))
    {
        commands.push_back(std::make_unique<DestroyEntityCommand>(scene, id));
    }
    CreateAll(scene, commands, std::move(snapshots));
    document.Undo().Execute(std::make_unique<MacroCommand>("Revert prefab", std::move(commands)));
    return {};
}

Result<void> ApplyInstance(EditorDocument& document, IFileSystem& files, EditorEntityId root)
{
    EditorScene& scene = document.Scene();
    const std::optional<scene::SceneObject> own = OwnComponents(scene, root, true);
    if (!own)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("object {} is not a prefab instance", root));
    }
    const std::optional<Entity> rootEntity = scene.Find(root);
    const auto* instance = rootEntity ? scene.GetWorld().Get<scene::PrefabInstanceComponent>(*rootEntity) : nullptr;
    if (instance == nullptr)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("object {} is not a prefab instance", root));
    }
    const std::string path = instance->prefab.path;
    const scene::PrefabLoader load = PrefabLoaderFor(files);
    INTERI_TRY_ASSIGN(original, load(path));
    INTERI_TRY_ASSIGN(originalRoot, scene::PrefabRoot(original));

    // The new prefab: the instance's objects under their prefab ids, so other instances' overrides still match.
    const scene::SceneDocument expanded = scene.ToDocument();
    scene::SceneDocument prefab;
    std::map<EditorEntityId, EditorEntityId> sources;
    for (const EditorEntityId id : InstanceObjects(scene, root))
    {
        const std::optional<Entity> entity = scene.Find(id);
        if (const auto* link = entity ? scene.GetWorld().Get<scene::PrefabLinkComponent>(*entity) : nullptr)
        {
            sources.emplace(id, link->source.id);
        }
    }
    for (const auto& [id, source] : sources)
    {
        scene::SceneObject object = *expanded.Find(id);
        object.id = source;
        std::erase_if(object.components,
                      [](const scene::SceneComponent& c)
                      {
                          return c.name == scene::PrefabInstanceComponent::kComponentName ||
                                 c.name == scene::PrefabLinkComponent::kComponentName;
                      });
        const EditorEntityId parent = id == root ? 0 : scene.ParentOf(id);
        const auto parentSource = sources.find(parent);
        for (scene::SceneComponent& component : object.components)
        {
            if (component.name == scene::ParentComponent::kComponentName)
            {
                component.fields = {
                    {"parent", std::to_string(parentSource == sources.end() ? 0 : parentSource->second)}};
            }
            if (id == root && component.name == scene::LocalTransformComponent::kComponentName)
            {
                // The prefab root keeps its own placement (an instance's is its own).
                if (const scene::SceneComponent* placement =
                        original.Find(originalRoot)->Find(scene::LocalTransformComponent::kComponentName))
                {
                    component = *placement;
                }
            }
        }
        prefab.objects.push_back(std::move(object));
    }
    std::ranges::sort(prefab.objects, {}, &scene::SceneObject::id);

    // Every instance's overrides, against the prefab as it was; then the new prefab; then expand again.
    scene::SceneDocument collapsed = expanded;
    scene::CollapsePrefabs(collapsed, load, scene.Reflection());
    const std::string text = scene::WriteScene(prefab);
    INTERI_TRY(files.WriteFile(path, std::as_bytes(std::span(text))));
    for (scene::SceneObject& object : collapsed.objects)
    {
        if (object.id != root)
        {
            continue;
        }
        for (scene::SceneComponent& component : object.components)
        {
            if (component.name == scene::PrefabInstanceComponent::kComponentName)
            {
                for (auto& [field, json] : component.fields)
                {
                    if (field == "overrides")
                    {
                        json = "[]";
                    }
                }
            }
        }
    }
    std::vector<std::string> warnings;
    scene::ExpandPrefabs(collapsed, load, warnings);
    INTERI_TRY(scene.Load(collapsed));
    document.Undo().Clear();
    return {};
}

} // namespace interi::editor
