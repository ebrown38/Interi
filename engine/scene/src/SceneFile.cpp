// SPDX-License-Identifier: MIT
#include <interi/ecs/Query.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneFile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <set>

namespace interi::scene
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

Result<SceneComponent> ParseComponent(const std::string& name, const Json& json, std::string_view where)
{
    if (name.empty())
    {
        return Invalid(std::format("{}: empty component name", where));
    }
    if (!json.is_object())
    {
        return Invalid(std::format("{}: component '{}' must be an object of fields", where, name));
    }
    SceneComponent component{.name = name, .fields = {}};
    for (const auto& [field, value] : json.items()) // nlohmann objects iterate in key order
    {
        component.fields.emplace_back(field, value.dump());
    }
    return component;
}

Result<SceneObject> ParseObject(const Json& json, std::string_view where, std::vector<std::string>& warnings)
{
    if (!json.is_object())
    {
        return Invalid(std::format("{}: must be an object", where));
    }
    const auto id = json.find("id");
    if (id == json.end() || !id->is_number_unsigned() || id->get<std::uint64_t>() == 0)
    {
        return Invalid(std::format("{}: \"id\" must be a positive integer", where));
    }
    SceneObject object;
    object.id = id->get<std::uint64_t>();
    const std::string at = std::format("object {}", object.id);
    for (const auto& [key, value] : json.items())
    {
        if (key != "id" && key != "components")
        {
            warnings.push_back(std::format("{}: unknown key '{}'", at, key));
        }
    }
    if (const auto components = json.find("components"); components != json.end())
    {
        if (!components->is_object())
        {
            return Invalid(std::format("{}: \"components\" must be an object", at));
        }
        for (const auto& [name, value] : components->items())
        {
            INTERI_TRY_ASSIGN(component, ParseComponent(name, value, at));
            object.components.push_back(std::move(component));
        }
    }
    return object;
}

/// A reflected type's default field values, read from a value-initialised component.
class DefaultProbe
{
public:
    const std::vector<values::Value>& Defaults(const values::TypeReflection& type)
    {
        const auto found = m_defaults.find(type.name);
        if (found != m_defaults.end())
        {
            return found->second;
        }
        const Entity entity = m_world.CreateEntity();
        type.addDefault(m_world, entity);
        const void* data = m_world.GetErased(entity, type.type);
        std::vector<values::Value> defaults;
        defaults.reserve(type.fields.size());
        for (const values::FieldInfo& field : type.fields)
        {
            defaults.push_back(values::ReadField(field, data));
        }
        return m_defaults.emplace(type.name, std::move(defaults)).first->second;
    }

private:
    World m_world;
    std::map<std::string, std::vector<values::Value>, std::less<>> m_defaults;
};

struct ResolvedComponent
{
    const values::TypeReflection* type = nullptr;
    std::vector<std::optional<values::Value>> values; ///< By field; nullopt keeps the default.
};

} // namespace

const SceneComponent* SceneObject::Find(std::string_view component) const noexcept
{
    const auto found = std::ranges::find(components, component, &SceneComponent::name);
    return found == components.end() ? nullptr : &*found;
}

const SceneObject* SceneDocument::Find(ObjectId id) const noexcept
{
    const auto found = std::ranges::lower_bound(objects, id, {}, &SceneObject::id);
    return found == objects.end() || found->id != id ? nullptr : &*found;
}

Result<SceneDocument> ParseScene(std::string_view text)
{
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        return Invalid("scene: not a JSON object");
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_unsigned())
    {
        return Invalid("scene: \"version\" must be a positive integer");
    }
    if (version->get<std::uint64_t>() > kSceneFormatVersion)
    {
        return MakeError(ErrorCode::VersionMismatch, std::format("scene: version {} is newer than {}",
                                                                 version->get<std::uint64_t>(), kSceneFormatVersion));
    }
    if (version->get<std::uint64_t>() < kSceneFormatVersion)
    {
        return Invalid(std::format("scene: version {} is not a scene v2 file (open old levels through the level "
                                   "importer)",
                                   version->get<std::uint64_t>()));
    }
    SceneDocument document;
    for (const auto& [key, value] : json.items())
    {
        if (key != "version" && key != "objects")
        {
            document.warnings.push_back(std::format("scene: unknown key '{}'", key));
        }
    }
    if (const auto objects = json.find("objects"); objects != json.end())
    {
        if (!objects->is_array())
        {
            return Invalid("scene: \"objects\" must be an array");
        }
        for (std::size_t i = 0; i < objects->size(); ++i)
        {
            INTERI_TRY_ASSIGN(object, ParseObject((*objects)[i], std::format("object #{}", i), document.warnings));
            document.objects.push_back(std::move(object));
        }
    }
    std::ranges::sort(document.objects, {}, &SceneObject::id);
    const auto duplicate = std::ranges::adjacent_find(document.objects, {}, &SceneObject::id);
    if (duplicate != document.objects.end())
    {
        return Invalid(std::format("scene: duplicate object id {}", duplicate->id));
    }
    return document;
}

Result<SceneDocument> LoadScene(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto document = ParseScene(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!document)
    {
        return MakeError(document.error().code, std::format("{}: {}", path, document.error().message));
    }
    return document;
}

std::string WriteScene(const SceneDocument& document)
{
    Json objects = Json::array();
    for (const SceneObject& object : document.objects)
    {
        Json components = Json::object();
        for (const SceneComponent& component : object.components)
        {
            Json fields = Json::object();
            for (const auto& [field, text] : component.fields)
            {
                fields[field] = Json::parse(text, nullptr, false);
            }
            components[component.name] = std::move(fields);
        }
        objects.push_back(Json{{"id", object.id}, {"components", std::move(components)}});
    }
    const Json json = {{"version", kSceneFormatVersion}, {"objects", std::move(objects)}};
    return json.dump(2) + "\n";
}

Result<InstantiatedScene> InstantiateScene(const SceneDocument& document, World& world,
                                           const values::ReflectionRegistry& reflection)
{
    InstantiatedScene result;
    std::set<ObjectId> used;
    Query().With<StableIdComponent>().ForEachChunk(world,
                                                   [&](ChunkView& view)
                                                   {
                                                       for (const StableIdComponent& id :
                                                            view.Read<StableIdComponent>())
                                                       {
                                                           used.insert(id.id);
                                                       }
                                                   });

    // Resolve everything first, so a bad field creates nothing.
    DefaultProbe probe;
    std::vector<std::vector<ResolvedComponent>> resolved(document.objects.size());
    for (std::size_t o = 0; o < document.objects.size(); ++o)
    {
        const SceneObject& object = document.objects[o];
        if (used.contains(object.id))
        {
            return Invalid(std::format("object {}: id already in use", object.id));
        }
        for (const SceneComponent& component : object.components)
        {
            const values::TypeReflection* type = reflection.Find(component.name);
            if (type == nullptr)
            {
                result.kept[object.id].push_back(component);
                result.warnings.push_back(
                    std::format("object {}: unknown component '{}' (kept)", object.id, component.name));
                continue;
            }
            const std::vector<values::Value>& defaults = probe.Defaults(*type);
            ResolvedComponent entry{.type = type,
                                    .values = std::vector<std::optional<values::Value>>(type->fields.size())};
            for (const auto& [name, text] : component.fields)
            {
                const auto field = std::ranges::find(type->fields, name, &values::FieldInfo::name);
                if (field == type->fields.end())
                {
                    result.warnings.push_back(
                        std::format("object {}: {} has no field '{}' (dropped)", object.id, component.name, name));
                    continue;
                }
                const auto index = static_cast<std::size_t>(field - type->fields.begin());
                values::ValueSchema schema;
                schema.type = field->type;
                if (const auto* list = std::get_if<values::ValueList>(&defaults[index].Variant()))
                {
                    schema.element = list->element;
                }
                auto value = values::ParseValueJson(text, schema);
                if (!value)
                {
                    return Invalid(
                        std::format("object {}: {}.{}: {}", object.id, component.name, name, value.error().message));
                }
                entry.values[index] = std::move(*value);
            }
            resolved[o].push_back(std::move(entry));
        }
    }

    for (std::size_t o = 0; o < document.objects.size(); ++o)
    {
        const ObjectId id = document.objects[o].id;
        const Entity entity = world.CreateEntity(StableIdComponent{id}, NameComponent{}, ParentComponent{},
                                                 LocalTransformComponent{}, Transform{});
        result.entities.emplace(id, entity);
        for (const ResolvedComponent& component : resolved[o])
        {
            if (world.GetErased(entity, component.type->type) == nullptr)
            {
                component.type->addDefault(world, entity);
            }
            void* data = world.GetErased(entity, component.type->type);
            for (std::size_t f = 0; f < component.values.size(); ++f)
            {
                if (const std::optional<values::Value>& value = component.values[f]; value.has_value())
                {
                    (void)values::WriteField(component.type->fields[f], data, value.value());
                }
            }
        }
    }
    const HierarchyIndex index = PropagateTransforms(world);
    for (const ObjectId broken : index.broken)
    {
        if (result.entities.contains(broken))
        {
            result.warnings.push_back(std::format("object {}: parent missing or in a loop (placed as a root)", broken));
        }
    }
    return result;
}

std::optional<SceneObject> CaptureObject(World& world, Entity entity, const values::ReflectionRegistry& reflection)
{
    const auto* id = world.Get<StableIdComponent>(entity);
    if (id == nullptr)
    {
        return std::nullopt;
    }
    SceneObject object{.id = id->id, .components = {}};
    const ComponentTypeId skipped[] = {ComponentId<StableIdComponent>(), ComponentId<Transform>()};
    const interi::detail::Archetype& archetype = *world.Archetypes()[world.ArchetypeOf(entity)];
    for (const interi::detail::Column& column : archetype.columns)
    {
        if (std::ranges::contains(skipped, column.type))
        {
            continue;
        }
        const values::TypeReflection* type = reflection.Find(column.type);
        if (type == nullptr)
        {
            continue;
        }
        const void* data = world.GetErased(entity, type->type);
        SceneComponent component{.name = type->name, .fields = {}};
        for (const values::FieldInfo& field : type->fields)
        {
            component.fields.emplace_back(field.name, values::ValueToJsonText(values::ReadField(field, data)));
        }
        std::ranges::sort(component.fields, {}, &std::pair<std::string, std::string>::first);
        object.components.push_back(std::move(component));
    }
    std::ranges::sort(object.components, {}, &SceneComponent::name);
    return object;
}

SceneDocument CaptureScene(World& world, const values::ReflectionRegistry& reflection, const KeptComponents* kept)
{
    std::map<ObjectId, Entity> entities;
    Query().With<StableIdComponent>().ForEachChunk(world,
                                                   [&](ChunkView& view)
                                                   {
                                                       const auto ids = view.Read<StableIdComponent>();
                                                       const auto handles = view.Entities();
                                                       for (std::size_t i = 0; i < ids.size(); ++i)
                                                       {
                                                           entities.emplace(ids[i].id, handles[i]);
                                                       }
                                                   });
    SceneDocument document;
    for (const auto& [id, entity] : entities)
    {
        std::optional<SceneObject> captured = CaptureObject(world, entity, reflection); // has an id: found by it
        if (!captured)
        {
            continue;
        }
        SceneObject object = std::move(*captured);
        if (kept != nullptr)
        {
            if (const auto found = kept->find(id); found != kept->end())
            {
                for (const SceneComponent& component : found->second)
                {
                    if (object.Find(component.name) == nullptr)
                    {
                        object.components.push_back(component);
                    }
                }
                std::ranges::sort(object.components, {}, &SceneComponent::name);
            }
        }
        document.objects.push_back(std::move(object));
    }
    return document;
}

} // namespace interi::scene
