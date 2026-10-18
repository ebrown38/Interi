// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/editor/Commands.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/LevelBridge.h>
#include <interi/editor/Modeling.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Primitives.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <ranges>

namespace interi::editor
{

namespace
{

using Commands = std::vector<std::unique_ptr<ICommand>>;

[[nodiscard]] ComponentSnapshot LocalSnapshot(const Transform& local)
{
    return {.component = scene::LocalTransformComponent::kComponentName,
            .values = {local.position, local.rotation, local.scale}};
}

[[nodiscard]] Transform ToLocal(const EditorScene& scene, EditorEntityId parent, const Transform& world)
{
    return parent == 0 ? world : scene.WorldTransform(parent).Inverse() * world;
}

/// Sets an object's local transform (nothing if unchanged).
void SetLocal(EditorScene& scene, Commands& commands, EditorEntityId id, const Transform& local)
{
    std::optional<ComponentSnapshot> before = scene.ReadComponent(id, scene::LocalTransformComponent::kComponentName);
    ComponentSnapshot after = LocalSnapshot(local);
    if (before && before->values != after.values)
    {
        commands.push_back(std::make_unique<SetComponentCommand>(scene, id, std::move(*before), std::move(after)));
    }
}

/// Moves an object to a world transform (its parent is unchanged).
void SetWorld(EditorScene& scene, Commands& commands, EditorEntityId id, const Transform& world)
{
    SetLocal(scene, commands, id, ToLocal(scene, scene.ParentOf(id), world));
}

void SetParent(EditorScene& scene, Commands& commands, EditorEntityId id, EditorEntityId parent)
{
    std::optional<ComponentSnapshot> before = scene.ReadComponent(id, scene::ParentComponent::kComponentName);
    if (before)
    {
        commands.push_back(
            std::make_unique<SetComponentCommand>(scene, id, std::move(*before),
                                                  ComponentSnapshot{.component = scene::ParentComponent::kComponentName,
                                                                    .values = {values::EntityRef{parent}}}));
    }
}

/// Creates an object (made with `create`) through a command, so undo removes it.
template <typename Create>
EditorEntityId CreateThroughCommand(EditorScene& scene, Commands& commands, Create&& create)
{
    const EditorEntityId id = create(scene);
    std::optional<EntitySnapshot> snapshot = scene.Snapshot(id);
    (void)scene.Destroy(id);
    if (snapshot)
    {
        commands.push_back(std::make_unique<CreateEntityCommand>(scene, std::move(*snapshot)));
    }
    return id;
}

bool Execute(EditorDocument& document, std::string label, Commands commands)
{
    if (commands.empty())
    {
        return false;
    }
    document.Undo().Execute(std::make_unique<MacroCommand>(std::move(label), std::move(commands)));
    return true;
}

[[nodiscard]] float Axis(const Vec3& v, int axis) noexcept
{
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

[[nodiscard]] Vec3 AlongAxis(int axis, float amount) noexcept
{
    return {axis == 0 ? amount : 0.0f, axis == 1 ? amount : 0.0f, axis == 2 ? amount : 0.0f};
}

[[nodiscard]] AABB Union(std::span<const AABB> boxes)
{
    AABB all = AABB::Empty();
    for (const AABB& box : boxes)
    {
        all.Expand(box.min);
        all.Expand(box.max);
    }
    return all;
}

[[nodiscard]] Vec3 Centre(const AABB& box) noexcept
{
    return (box.min + box.max) * 0.5f;
}

/// The geometry offset field of an object's shape component, or nullopt if it has none.
[[nodiscard]] std::optional<std::string_view> OffsetComponent(const EditorScene& scene, EditorEntityId id)
{
    for (const std::string_view component : {std::string_view(scene::PrimitiveComponent::kComponentName),
                                             std::string_view(scene::MeshComponent::kComponentName)})
    {
        if (scene.ReadComponent(id, component))
        {
            return component;
        }
    }
    return std::nullopt;
}

void SetOffset(EditorScene& scene, Commands& commands, EditorEntityId id, std::string_view component,
               const Vec3& offset)
{
    std::optional<ComponentSnapshot> before = scene.ReadComponent(id, component);
    const TypeReflection* type = scene.Reflection().Find(component);
    const FieldInfo* field = type == nullptr ? nullptr : type->Find("offset");
    if (!before || field == nullptr)
    {
        return;
    }
    ComponentSnapshot after = *before;
    after.values[static_cast<std::size_t>(field - type->fields.data())] = offset;
    if (after.values != before->values)
    {
        commands.push_back(std::make_unique<SetComponentCommand>(scene, id, std::move(*before), std::move(after)));
    }
}

/// Points entity references at copies (parent links and any EntityRef field, lists included).
void Remap(values::Value& value, const std::map<EditorEntityId, EditorEntityId>& copies)
{
    if (auto* ref = std::get_if<values::EntityRef>(&value.Variant()))
    {
        if (const auto found = copies.find(ref->id); found != copies.end())
        {
            ref->id = found->second;
        }
    }
    else if (auto* list = std::get_if<values::ValueList>(&value.Variant()))
    {
        for (values::Value& item : list->items)
        {
            Remap(item, copies);
        }
    }
}

[[nodiscard]] bool HasOnlyStandardComponents(const EditorScene& scene, EditorEntityId id)
{
    const std::optional<EntitySnapshot> snapshot = scene.Snapshot(id);
    return snapshot && snapshot->kept.empty() &&
           std::ranges::all_of(snapshot->components,
                               [](const ComponentSnapshot& component)
                               {
                                   return component.component == scene::NameComponent::kComponentName ||
                                          component.component == scene::ParentComponent::kComponentName ||
                                          component.component == scene::LocalTransformComponent::kComponentName;
                               });
}

/// R' = M R M_k: the rotation part of mirroring a rotation across world axis `axis`, with local axis `local`'s scale
/// negated (a proper rotation for any choice of `local`).
[[nodiscard]] Quat MirrorRotation(const Quat& rotation, int axis, int local)
{
    Mat4 m = Mat4::Rotation(rotation);
    for (int column = 0; column < 3; ++column)
    {
        m(axis, column) = -m(axis, column);
        if (column == local)
        {
            for (int row = 0; row < 3; ++row)
            {
                m(row, column) = -m(row, column);
            }
        }
    }
    return Normalize(Quat::FromRotationMatrix(m));
}

} // namespace

AABB WorldBounds(const EditorScene& scene, EditorEntityId id, const AssetBoundsFn& bounds)
{
    const Transform world = scene.WorldTransform(id);
    const std::optional<AABB> local = LocalBounds(scene, id, bounds);
    AABB box = AABB::Empty();
    if (!local)
    {
        box.Expand(world.position);
        return box;
    }
    for (int corner = 0; corner < 8; ++corner)
    {
        box.Expand(world.TransformPoint({(corner & 1) != 0 ? local->max.x : local->min.x,
                                         (corner & 2) != 0 ? local->max.y : local->min.y,
                                         (corner & 4) != 0 ? local->max.z : local->min.z}));
    }
    return box;
}

EditorEntityId GroupSelection(EditorDocument& document, const AssetBoundsFn& bounds)
{
    EditorScene& scene = document.Scene();
    const std::vector<EditorEntityId> roots = document.SelectedRoots();
    if (roots.empty())
    {
        return 0;
    }
    std::vector<AABB> boxes;
    boxes.reserve(roots.size());
    for (const EditorEntityId id : roots)
    {
        boxes.push_back(WorldBounds(scene, id, bounds));
    }
    const EditorEntityId parent =
        std::ranges::all_of(roots, [&](EditorEntityId id) { return scene.ParentOf(id) == scene.ParentOf(roots[0]); })
            ? scene.ParentOf(roots[0])
            : 0;
    Transform groupWorld;
    groupWorld.position = Centre(Union(boxes));
    Commands commands;
    const EditorEntityId group =
        CreateThroughCommand(scene, commands, [&](EditorScene& s)
                             { return s.CreateObject("Group", ToLocal(s, parent, groupWorld), parent); });
    for (const EditorEntityId id : roots)
    {
        SetParent(scene, commands, id, group);
        SetLocal(scene, commands, id, groupWorld.Inverse() * scene.WorldTransform(id));
    }
    (void)Execute(document, "Group", std::move(commands));
    document.Select(group);
    return group;
}

bool UngroupSelection(EditorDocument& document)
{
    EditorScene& scene = document.Scene();
    Commands commands;
    std::vector<EditorEntityId> released;
    for (const EditorEntityId id : document.SelectedRoots())
    {
        const auto node = scene.Hierarchy().nodes.find(id);
        if (node == scene.Hierarchy().nodes.end() || node->second.children.empty())
        {
            continue;
        }
        const EditorEntityId parent = scene.ParentOf(id);
        for (const EditorEntityId child : node->second.children)
        {
            SetParent(scene, commands, child, parent);
            SetLocal(scene, commands, child, ToLocal(scene, parent, scene.WorldTransform(child)));
            released.push_back(child);
        }
        if (HasOnlyStandardComponents(scene, id))
        {
            commands.push_back(std::make_unique<DestroyEntityCommand>(scene, id));
        }
    }
    if (!Execute(document, "Ungroup", std::move(commands)))
    {
        return false;
    }
    document.Select(released);
    return true;
}

std::vector<EditorEntityId> DuplicateSelection(EditorDocument& document, std::uint32_t count, const Vec3& offset)
{
    EditorScene& scene = document.Scene();
    Commands commands;
    std::vector<EditorEntityId> copies;
    EditorEntityId next = scene.NextId();
    for (std::uint32_t copy = 1; copy <= count; ++copy)
    {
        for (const EditorEntityId root : document.SelectedRoots())
        {
            const std::vector<EditorEntityId> subtree = scene.Subtree(root);
            std::map<EditorEntityId, EditorEntityId> ids;
            for (const EditorEntityId id : subtree)
            {
                ids.emplace(id, next++);
            }
            for (const EditorEntityId id : subtree) // parents first
            {
                std::optional<EntitySnapshot> snapshot = scene.Snapshot(id);
                if (!snapshot)
                {
                    continue;
                }
                snapshot->id = ids.at(id);
                for (ComponentSnapshot& component : snapshot->components)
                {
                    for (values::Value& value : component.values)
                    {
                        Remap(value, ids);
                    }
                    if (id == root && component.component == scene::LocalTransformComponent::kComponentName)
                    {
                        std::get<Vec3>(component.values[0]) += offset * static_cast<float>(copy);
                    }
                }
                commands.push_back(std::make_unique<CreateEntityCommand>(scene, std::move(*snapshot)));
            }
            copies.push_back(ids.at(root));
        }
    }
    if (Execute(document, count == 1 ? "Duplicate" : "Array", std::move(commands)))
    {
        document.Select(copies);
    }
    return copies;
}

bool AlignSelection(EditorDocument& document, int axis, AlignMode mode, const AssetBoundsFn& bounds)
{
    EditorScene& scene = document.Scene();
    const std::vector<EditorEntityId> roots = document.SelectedRoots();
    if (roots.size() < 2)
    {
        return false;
    }
    std::vector<AABB> boxes;
    boxes.reserve(roots.size());
    for (const EditorEntityId id : roots)
    {
        boxes.push_back(WorldBounds(scene, id, bounds));
    }
    const auto pick = [&](const AABB& box)
    {
        return mode == AlignMode::Min   ? Axis(box.min, axis)
               : mode == AlignMode::Max ? Axis(box.max, axis)
                                        : Axis(Centre(box), axis);
    };
    const float target = pick(Union(boxes));
    Commands commands;
    for (std::size_t i = 0; i < roots.size(); ++i)
    {
        Transform world = scene.WorldTransform(roots[i]);
        world.position += AlongAxis(axis, target - pick(boxes[i]));
        SetWorld(scene, commands, roots[i], world);
    }
    return Execute(document, "Align", std::move(commands));
}

bool DistributeSelection(EditorDocument& document, int axis, const AssetBoundsFn& bounds)
{
    EditorScene& scene = document.Scene();
    std::vector<std::pair<float, EditorEntityId>> order;
    for (const EditorEntityId id : document.SelectedRoots())
    {
        order.emplace_back(Axis(Centre(WorldBounds(scene, id, bounds)), axis), id);
    }
    if (order.size() < 3)
    {
        return false;
    }
    std::ranges::sort(order);
    const float first = order.front().first;
    const float step = (order.back().first - first) / static_cast<float>(order.size() - 1);
    Commands commands;
    for (std::size_t i = 1; i + 1 < order.size(); ++i)
    {
        Transform world = scene.WorldTransform(order[i].second);
        world.position += AlongAxis(axis, first + step * static_cast<float>(i) - order[i].first);
        SetWorld(scene, commands, order[i].second, world);
    }
    return Execute(document, "Distribute", std::move(commands));
}

bool MirrorSelection(EditorDocument& document, int axis, const AssetBoundsFn& bounds)
{
    EditorScene& scene = document.Scene();
    const std::vector<EditorEntityId> roots = document.SelectedRoots();
    if (roots.empty())
    {
        return false;
    }
    std::vector<AABB> boxes;
    boxes.reserve(roots.size());
    for (const EditorEntityId id : roots)
    {
        boxes.push_back(WorldBounds(scene, id, bounds));
    }
    const float plane = Axis(Centre(Union(boxes)), axis);
    Commands commands;
    for (const EditorEntityId id : roots)
    {
        Transform world = scene.WorldTransform(id);
        // The local axis whose world direction is closest to the mirror axis takes the negative scale.
        const Mat4 rotation = Mat4::Rotation(world.rotation);
        int local = 0;
        for (int column = 1; column < 3; ++column)
        {
            if (std::abs(rotation(axis, column)) > std::abs(rotation(axis, local)))
            {
                local = column;
            }
        }
        world.position += AlongAxis(axis, 2.0f * (plane - Axis(world.position, axis)));
        world.rotation = MirrorRotation(world.rotation, axis, local);
        world.scale = {local == 0 ? -world.scale.x : world.scale.x, local == 1 ? -world.scale.y : world.scale.y,
                       local == 2 ? -world.scale.z : world.scale.z};
        SetWorld(scene, commands, id, world);
    }
    return Execute(document, "Mirror", std::move(commands));
}

bool SnapSelection(EditorDocument& document, float grid)
{
    EditorScene& scene = document.Scene();
    Commands commands;
    for (const EditorEntityId id : document.SelectedRoots())
    {
        Transform world = scene.WorldTransform(id);
        world.position = Snap(world.position, grid);
        SetWorld(scene, commands, id, world);
    }
    return Execute(document, "Snap", std::move(commands));
}

bool SetPivot(EditorDocument& document, EditorEntityId id, const Vec3& world)
{
    EditorScene& scene = document.Scene();
    if (!scene.Contains(id))
    {
        return false;
    }
    // q: the new origin in the object's own space.
    const Vec3 q = scene.WorldTransform(id).Inverse().TransformPoint(world);
    if (Length(q) <= 1e-5f) // already there (up to the round trip through the inverse)
    {
        return false;
    }
    Commands commands;
    Transform local = scene.LocalTransform(id);
    local.position = local.TransformPoint(q);
    SetLocal(scene, commands, id, local);
    if (const auto component = OffsetComponent(scene, id))
    {
        const std::optional<ComponentSnapshot> shape = scene.ReadComponent(id, *component);
        const TypeReflection* type = scene.Reflection().Find(*component);
        const FieldInfo* field = type == nullptr ? nullptr : type->Find("offset");
        if (shape && field != nullptr)
        {
            const Vec3 offset = std::get<Vec3>(shape->values[static_cast<std::size_t>(field - type->fields.data())]);
            SetOffset(scene, commands, id, *component, offset - q);
        }
    }
    for (const EditorEntityId child : scene.Hierarchy().nodes.at(id).children)
    {
        Transform childLocal = scene.LocalTransform(child);
        childLocal.position = childLocal.position - q;
        SetLocal(scene, commands, child, childLocal);
    }
    return Execute(document, "Set pivot", std::move(commands));
}

bool SetPivot(EditorDocument& document, EditorEntityId id, PivotPlacement placement, const AssetBoundsFn& bounds)
{
    const AABB box = WorldBounds(document.Scene(), id, bounds);
    Vec3 point = Centre(box);
    if (placement == PivotPlacement::BoundsBottom)
    {
        point.y = box.min.y;
    }
    return SetPivot(document, id, point);
}

Result<scene::TriangleMesh> ObjectMesh(const EditorScene& scene, EditorEntityId id, const IFileSystem& files)
{
    const auto entity = scene.Find(id);
    const World& world = scene.GetWorld();
    Result<scene::TriangleMesh> mesh = MakeError(ErrorCode::NotFound, std::format("object {} has no mesh", id));
    Vec3 offset;
    if (const auto* primitive = entity ? world.Get<scene::PrimitiveComponent>(*entity) : nullptr)
    {
        INTERI_TRY_ASSIGN(params, scene::ParamsOf(*primitive));
        mesh = scene::GeneratePrimitive(params);
        offset = primitive->offset;
    }
    else if (const auto* asset = entity ? world.Get<scene::MeshComponent>(*entity) : nullptr)
    {
        mesh = scene::LoadMeshAsset(files, asset->asset.path);
        offset = asset->offset;
    }
    if (mesh)
    {
        for (Vec3& position : mesh->positions)
        {
            position += offset;
        }
    }
    return mesh;
}

Result<EditorEntityId> CsgSelection(EditorDocument& document, IFileSystem& files, modeling::CsgOperation operation,
                                    std::string meshPath)
{
    EditorScene& scene = document.Scene();
    const std::vector<EditorEntityId> roots = document.SelectedRoots();
    if (roots.size() < 2)
    {
        return MakeError(ErrorCode::InvalidArgument, "csg: select at least two objects");
    }
    std::vector<scene::TriangleMesh> meshes;
    meshes.reserve(roots.size());
    std::vector<modeling::CsgOperand> operands;
    for (const EditorEntityId id : roots)
    {
        INTERI_TRY_ASSIGN(mesh, ObjectMesh(scene, id, files));
        meshes.push_back(std::move(mesh));
        operands.push_back({.mesh = &meshes.back(), .transform = scene.WorldTransform(id)});
    }
    INTERI_TRY_ASSIGN(result, modeling::Combine(operands, operation));

    // Centre the mesh on its bounds; the object sits there.
    const scene::MeshBounds bounds = scene::BoundsOf(result);
    const Vec3 centre = (bounds.min + bounds.max) * 0.5f;
    for (Vec3& position : result.positions)
    {
        position -= centre;
    }
    const std::vector<std::byte> bytes = scene::WriteMeshAsset(result);
    INTERI_TRY(files.WriteFile(meshPath, bytes));

    const EditorEntityId parent = scene.ParentOf(roots[0]);
    Transform world;
    world.position = centre;
    values::ValueList colors{.element = values::ValueType::Color, .items = {}};
    if (const auto entity = scene.Find(roots[0]))
    {
        if (const auto* primitive = scene.GetWorld().Get<scene::PrimitiveComponent>(*entity))
        {
            colors = primitive->colors;
        }
    }
    Commands commands;
    const EditorEntityId created = CreateThroughCommand(
        scene, commands,
        [&](EditorScene& s)
        {
            const EditorEntityId id =
                s.CreateObject(std::string(modeling::ToString(operation)), ToLocal(s, parent, world), parent);
            (void)s.GetWorld().Add(*s.Find(id),
                                   scene::MeshComponent{.asset = {meshPath}, .colors = colors, .offset = {}});
            return id;
        });
    for (const EditorEntityId root : roots)
    {
        for (const EditorEntityId id : std::views::reverse(scene.Subtree(root)))
        {
            commands.push_back(std::make_unique<DestroyEntityCommand>(scene, id));
        }
    }
    (void)Execute(document, std::format("CSG {}", modeling::ToString(operation)), std::move(commands));
    document.Select(created);
    return created;
}

} // namespace interi::editor
