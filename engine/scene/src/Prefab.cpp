// SPDX-License-Identifier: MIT
#include <interi/scene/Prefab.h>
#include <interi/scene/SceneComponents.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <set>

namespace interi::scene
{

namespace
{

using Json = nlohmann::json;

constexpr std::string_view kParent = ParentComponent::kComponentName;
constexpr std::string_view kName = NameComponent::kComponentName;
constexpr std::string_view kLocal = LocalTransformComponent::kComponentName;
constexpr std::string_view kInstance = PrefabInstanceComponent::kComponentName;
constexpr std::string_view kLink = PrefabLinkComponent::kComponentName;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

SceneComponent* FindComponent(SceneObject& object, std::string_view name)
{
    const auto found = std::ranges::find(object.components, name, &SceneComponent::name);
    return found == object.components.end() ? nullptr : &*found;
}

const std::string* FindField(const SceneComponent& component, std::string_view field)
{
    const auto found = std::ranges::find(component.fields, field, &std::pair<std::string, std::string>::first);
    return found == component.fields.end() ? nullptr : &found->second;
}

/// Sets a field's JSON text, adding the component (sorted) and the field (sorted) as needed.
void SetField(SceneObject& object, std::string_view component, std::string_view field, std::string json)
{
    SceneComponent* target = FindComponent(object, component);
    if (target == nullptr)
    {
        object.components.push_back({.name = std::string(component), .fields = {}});
        std::ranges::sort(object.components, {}, &SceneComponent::name);
        target = FindComponent(object, component);
    }
    const auto found = std::ranges::find(target->fields, field, &std::pair<std::string, std::string>::first);
    if (found != target->fields.end())
    {
        found->second = std::move(json);
        return;
    }
    target->fields.emplace_back(std::string(field), std::move(json));
    std::ranges::sort(target->fields, {}, &std::pair<std::string, std::string>::first);
}

void RemoveComponent(SceneObject& object, std::string_view component)
{
    std::erase_if(object.components, [&](const SceneComponent& c) { return c.name == component; });
}

/// The parent id an object's Parent component names (0: none or unreadable).
ObjectId ParentOf(const SceneObject& object)
{
    const SceneComponent* parent = object.Find(kParent);
    const std::string* text = parent == nullptr ? nullptr : FindField(*parent, "parent");
    if (text == nullptr)
    {
        return 0;
    }
    const Json json = Json::parse(*text, nullptr, false);
    return json.is_number_unsigned() ? json.get<ObjectId>() : 0;
}

std::string IdJson(ObjectId id)
{
    return Json(id).dump();
}

/// The instance root's prefab path, or empty.
std::string PrefabPathOf(const SceneObject& root)
{
    const SceneComponent* instance = root.Find(kInstance);
    const std::string* text = instance == nullptr ? nullptr : FindField(*instance, "prefab");
    if (text == nullptr)
    {
        return {};
    }
    const Json json = Json::parse(*text, nullptr, false);
    return json.is_string() ? json.get<std::string>() : std::string{};
}

std::vector<std::string> OverridesOf(const SceneObject& root)
{
    std::vector<std::string> overrides;
    const SceneComponent* instance = root.Find(kInstance);
    const std::string* text = instance == nullptr ? nullptr : FindField(*instance, "overrides");
    if (text == nullptr)
    {
        return overrides;
    }
    const Json json = Json::parse(*text, nullptr, false);
    if (json.is_array())
    {
        for (const Json& entry : json)
        {
            if (entry.is_string())
            {
                overrides.push_back(entry.get<std::string>());
            }
        }
    }
    return overrides;
}

[[nodiscard]] bool IsInstanceRoot(const SceneObject& object)
{
    return !IsPrefabObjectId(object.id) && object.Find(kInstance) != nullptr;
}

/// Whether `id` is an object instance `root` expanded into (not the root itself).
[[nodiscard]] bool ExpandedFrom(ObjectId id, ObjectId root)
{
    return IsPrefabObjectId(id) && ((id & ~(ObjectId{1} << 63)) >> 16) == root;
}

} // namespace

void RegisterPrefabReflection(values::ReflectionRegistry& registry)
{
    values::ValueMeta readOnly;
    readOnly.readOnly = true;
    registry.Register<PrefabInstanceComponent>({
        values::Field("prefab", &PrefabInstanceComponent::prefab, readOnly),
        values::Field("overrides", &PrefabInstanceComponent::overrides, readOnly),
    });
    registry.Register<PrefabLinkComponent>({
        values::Field("instance", &PrefabLinkComponent::instance, readOnly),
        values::Field("source", &PrefabLinkComponent::source, readOnly),
    });
}

std::string FormatOverride(const PrefabOverride& entry)
{
    return std::format("{}/{}/{}={}", entry.source, entry.component, entry.field, entry.json);
}

Result<PrefabOverride> ParseOverride(std::string_view text)
{
    const std::size_t equals = text.find('=');
    const std::size_t first = text.find('/');
    const std::size_t second = first == std::string_view::npos ? first : text.find('/', first + 1);
    if (equals == std::string_view::npos || second == std::string_view::npos || second > equals)
    {
        return Invalid(std::format("prefab override '{}': expected <source>/<component>/<field>=<json>", text));
    }
    PrefabOverride entry;
    const std::string_view source = text.substr(0, first);
    const auto parsed = std::from_chars(source.data(), source.data() + source.size(), entry.source);
    if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size())
    {
        return Invalid(std::format("prefab override '{}': bad source id", text));
    }
    entry.component = std::string(text.substr(first + 1, second - first - 1));
    entry.field = std::string(text.substr(second + 1, equals - second - 1));
    entry.json = std::string(text.substr(equals + 1));
    if (entry.component.empty() || entry.field.empty() || Json::parse(entry.json, nullptr, false).is_discarded())
    {
        return Invalid(std::format("prefab override '{}': empty name or bad JSON", text));
    }
    return entry;
}

std::vector<ObjectId> SpawnPrefabs(std::span<const chain::SpawnRequest> requests, World& world,
                                   const values::ReflectionRegistry& reflection, const PrefabLoader& load,
                                   std::vector<std::string>& warnings)
{
    std::vector<ObjectId> spawned;
    for (const chain::SpawnRequest& request : requests)
    {
        SceneObject root{.id = request.id, .components = {}};
        SetField(root, kParent, "parent", "0");
        SetField(root, kLocal, "position", values::ValueToJsonText(request.transform.position));
        SetField(root, kLocal, "rotation", values::ValueToJsonText(request.transform.rotation));
        SetField(root, kLocal, "scale", values::ValueToJsonText(request.transform.scale));
        SetField(root, kInstance, "prefab", Json(request.prefab).dump());
        auto objects = ExpandInstance(root, load, warnings);
        if (!objects)
        {
            warnings.push_back(std::format("spawn {}: {}", request.prefab, objects.error().message));
            continue;
        }
        SceneDocument document;
        document.objects = std::move(*objects);
        auto instantiated = InstantiateScene(document, world, reflection);
        if (!instantiated)
        {
            warnings.push_back(std::format("spawn {}: {}", request.prefab, instantiated.error().message));
            continue;
        }
        spawned.push_back(request.id);
    }
    return spawned;
}

std::string CanonicalJson(std::string_view json)
{
    const Json parsed = Json::parse(json, nullptr, false);
    return parsed.is_discarded() ? std::string(json) : parsed.dump();
}

Result<ObjectId> PrefabRoot(const SceneDocument& prefab)
{
    std::vector<ObjectId> roots;
    for (const SceneObject& object : prefab.objects)
    {
        if (object.id >= kMaxPrefabObjects)
        {
            return Invalid(std::format("prefab: object id {} is too large", object.id));
        }
        const ObjectId parent = ParentOf(object);
        if (parent == 0)
        {
            roots.push_back(object.id);
        }
        else if (prefab.Find(parent) == nullptr)
        {
            return Invalid(std::format("prefab: object {} names a missing parent {}", object.id, parent));
        }
    }
    if (roots.size() != 1)
    {
        return Invalid(std::format("prefab: needs exactly one root object, has {}", roots.size()));
    }
    return roots[0];
}

Result<SceneDocument> PrefabLibrary::Load(std::string_view path)
{
    if (const auto found = m_cache.find(path); found != m_cache.end())
    {
        return found->second;
    }
    INTERI_TRY_ASSIGN(document, LoadScene(*m_files, path));
    if (auto root = PrefabRoot(document); !root)
    {
        return MakeError(root.error().code, std::format("{}: {}", path, root.error().message));
    }
    return m_cache.emplace(std::string(path), std::move(document)).first->second;
}

Result<std::vector<SceneObject>> ExpandInstance(const SceneObject& root, const PrefabLoader& load,
                                                std::vector<std::string>& warnings)
{
    const std::string path = PrefabPathOf(root);
    if (path.empty())
    {
        return Invalid(std::format("object {}: no prefab path", root.id));
    }
    INTERI_TRY_ASSIGN(prefab, load(path));
    INTERI_TRY_ASSIGN(rootSource, PrefabRoot(prefab));
    const auto idOf = [&](ObjectId source) { return source == rootSource ? root.id : PrefabObjectId(root.id, source); };

    std::vector<SceneObject> objects;
    for (const SceneObject& source : prefab.objects)
    {
        SceneObject object{.id = idOf(source.id), .components = {}};
        for (const SceneComponent& component : source.components)
        {
            if (component.name != kInstance && component.name != kLink)
            {
                object.components.push_back(component);
            }
        }
        if (source.id == rootSource)
        {
            for (const std::string_view own : {kName, kParent, kLocal, kInstance})
            {
                if (const SceneComponent* component = root.Find(own)) // else the prefab root's (a spawn's name)
                {
                    RemoveComponent(object, own);
                    object.components.push_back(*component);
                }
            }
            std::ranges::sort(object.components, {}, &SceneComponent::name);
        }
        else
        {
            SetField(object, kParent, "parent", IdJson(idOf(ParentOf(source))));
        }
        SetField(object, kLink, "instance", IdJson(root.id));
        SetField(object, kLink, "source", IdJson(source.id));
        objects.push_back(std::move(object));
    }
    for (const std::string& text : OverridesOf(root))
    {
        auto entry = ParseOverride(text);
        if (!entry)
        {
            warnings.push_back(std::format("object {}: {}", root.id, entry.error().message));
            continue;
        }
        const ObjectId target = idOf(entry->source);
        const auto found = std::ranges::find(objects, target, &SceneObject::id);
        if (prefab.Find(entry->source) == nullptr || found == objects.end())
        {
            warnings.push_back(
                std::format("object {}: override of {}.{} names prefab object {}, which {} no longer has", root.id,
                            entry->component, entry->field, entry->source, path));
            continue;
        }
        SetField(*found, entry->component, entry->field, std::move(entry->json));
    }
    std::ranges::sort(objects, {}, &SceneObject::id);
    return objects;
}

void ExpandPrefabs(SceneDocument& document, const PrefabLoader& load, std::vector<std::string>& warnings)
{
    std::vector<SceneObject> roots;
    for (const SceneObject& object : document.objects)
    {
        if (IsInstanceRoot(object))
        {
            roots.push_back(object);
        }
    }
    for (const SceneObject& root : roots)
    {
        auto expanded = ExpandInstance(root, load, warnings);
        if (!expanded)
        {
            warnings.push_back(std::format("object {}: {} (kept unexpanded)", root.id, expanded.error().message));
            continue;
        }
        std::erase_if(document.objects, [&](const SceneObject& object)
                      { return object.id == root.id || ExpandedFrom(object.id, root.id); });
        document.objects.insert(document.objects.end(), std::make_move_iterator(expanded->begin()),
                                std::make_move_iterator(expanded->end()));
    }
    std::ranges::sort(document.objects, {}, &SceneObject::id);
}

void CollapsePrefabs(SceneDocument& document, const PrefabLoader& load, const values::ReflectionRegistry& reflection)
{
    std::vector<ObjectId> roots;
    for (const SceneObject& object : document.objects)
    {
        if (IsInstanceRoot(object))
        {
            roots.push_back(object.id);
        }
    }
    for (const ObjectId rootId : roots)
    {
        SceneObject root = *document.Find(rootId);
        SetField(root, kInstance, "overrides", "[]");
        std::vector<std::string> ignored;
        auto expanded = ExpandInstance(root, load, ignored);
        if (!expanded)
        {
            continue;
        }
        // Resolve the prefab's fields as a world would hold them (defaults filled, canonical numbers).
        SceneDocument resolvedPrefab;
        resolvedPrefab.objects = std::move(*expanded);
        World scratch;
        auto instantiated = InstantiateScene(resolvedPrefab, scratch, reflection);
        if (!instantiated)
        {
            continue;
        }
        const SceneDocument resolved = CaptureScene(scratch, reflection, &instantiated->kept);
        const std::vector<SceneObject>* expected = &resolved.objects;
        std::set<std::string> overrides;
        for (const SceneObject& wanted : *expected)
        {
            const SceneObject* actual = document.Find(wanted.id);
            if (actual == nullptr)
            {
                continue; // deleted expanded objects are not recorded
            }
            const SceneComponent* link = wanted.Find(kLink);
            const std::string* sourceText = link == nullptr ? nullptr : FindField(*link, "source");
            const Json sourceJson = sourceText == nullptr ? Json() : Json::parse(*sourceText, nullptr, false);
            const ObjectId source = sourceJson.is_number_unsigned() ? sourceJson.get<ObjectId>() : 0;
            for (const SceneComponent& component : actual->components)
            {
                const bool own = component.name == kLink || component.name == kInstance || component.name == kParent ||
                                 (wanted.id == rootId && (component.name == kName || component.name == kLocal));
                if (own)
                {
                    continue;
                }
                const SceneComponent* prefabComponent = wanted.Find(component.name);
                for (const auto& [field, text] : component.fields)
                {
                    const std::string canonical = CanonicalJson(text);
                    const std::string* original =
                        prefabComponent == nullptr ? nullptr : FindField(*prefabComponent, field);
                    if (original == nullptr || CanonicalJson(*original) != canonical)
                    {
                        overrides.insert(FormatOverride(
                            {.source = source, .component = component.name, .field = field, .json = canonical}));
                    }
                }
            }
        }
        // The root keeps its own components and the instance (with the overrides); the rest come from the prefab.
        SceneObject collapsed{.id = rootId, .components = {}};
        for (const std::string_view own : {kInstance, kLocal, kName, kParent})
        {
            if (const SceneComponent* component = root.Find(own))
            {
                collapsed.components.push_back(*component);
            }
        }
        std::ranges::sort(collapsed.components, {}, &SceneComponent::name);
        SetField(collapsed, kInstance, "overrides",
                 Json(std::vector<std::string>(overrides.begin(), overrides.end())).dump());
        std::erase_if(document.objects, [&](const SceneObject& object) { return ExpandedFrom(object.id, rootId); });
        *std::ranges::find(document.objects, rootId, &SceneObject::id) = std::move(collapsed);
    }
}

Result<SceneDocument> MakePrefab(const SceneDocument& document, ObjectId root)
{
    if (document.Find(root) == nullptr)
    {
        return Invalid(std::format("prefab: no object {}", root));
    }
    // The subtree, by parent links.
    std::map<ObjectId, std::vector<ObjectId>> children;
    for (const SceneObject& object : document.objects)
    {
        children[ParentOf(object)].push_back(object.id);
    }
    std::vector<ObjectId> subtree{root};
    for (std::size_t i = 0; i < subtree.size(); ++i)
    {
        if (const auto found = children.find(subtree[i]); found != children.end())
        {
            subtree.insert(subtree.end(), found->second.begin(), found->second.end());
        }
    }
    if (subtree.size() >= kMaxPrefabObjects)
    {
        return Invalid(std::format("prefab: {} objects is too many", subtree.size()));
    }
    std::ranges::sort(subtree.begin() + 1, subtree.end());
    std::map<ObjectId, ObjectId> ids;
    for (std::size_t i = 0; i < subtree.size(); ++i)
    {
        ids.emplace(subtree[i], static_cast<ObjectId>(i + 1));
    }
    SceneDocument prefab;
    for (const ObjectId id : subtree)
    {
        SceneObject object = *document.Find(id);
        object.id = ids.at(id);
        RemoveComponent(object, kInstance);
        RemoveComponent(object, kLink);
        if (id == root)
        {
            SetField(object, kParent, "parent", "0");
            SetField(object, kLocal, "position", "[0.0,0.0,0.0]");
            SetField(object, kLocal, "rotation", "[0.0,0.0,0.0,1.0]");
            SetField(object, kLocal, "scale", "[1.0,1.0,1.0]");
        }
        else
        {
            SetField(object, kParent, "parent", IdJson(ids.at(ParentOf(*document.Find(id)))));
        }
        prefab.objects.push_back(std::move(object));
    }
    std::ranges::sort(prefab.objects, {}, &SceneObject::id);
    return prefab;
}

} // namespace interi::scene
