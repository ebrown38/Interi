// SPDX-License-Identifier: MIT
#include <interi/editor_ui/FracturePanel.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>

namespace interi::editor
{

namespace
{

using destruction::MaterialVolume;
using destruction::MaterialVolumeSettings;
using destruction::ObjectCookSettings;
using destruction::SeedPattern;
using destruction::SeedSettings;

constexpr std::array kPatternNames{"uniform", "clustered", "grid", "painted"};

/// A combo over the entry's material names; true when `name` changed.
bool MaterialCombo(const char* label, const FracturePreview::Entry& entry, std::string& name)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, name.c_str()))
    {
        for (const destruction::DestructionMaterial& material : entry.materials)
        {
            if (ImGui::Selectable(material.name.c_str(), material.name == name))
            {
                changed = material.name != name;
                name = material.name;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool LevelWidgets(SeedSettings& level)
{
    bool changed = false;
    int pattern = static_cast<int>(level.pattern);
    if (ImGui::Combo("pattern", &pattern, kPatternNames.data(), static_cast<int>(kPatternNames.size())))
    {
        level.pattern = static_cast<SeedPattern>(pattern);
        changed = true;
    }
    int count = static_cast<int>(level.count);
    if (level.pattern != SeedPattern::Grid && ImGui::DragInt("count", &count, 0.25f, 1, 4096))
    {
        level.count = static_cast<std::uint32_t>(std::max(count, 1));
        changed = true;
    }
    int seed = static_cast<int>(level.randomSeed & 0x7FFFFFFF);
    if (ImGui::DragInt("seed", &seed, 0.25f, 0, 1 << 30))
    {
        level.randomSeed = static_cast<std::uint64_t>(seed);
        changed = true;
    }
    if (level.pattern == SeedPattern::Clustered)
    {
        changed |= ImGui::DragFloat3("impact point", &level.impactPoint.x, 0.05f);
        changed |= ImGui::DragFloat("cluster radius", &level.clusterRadius, 0.02f, 0.01f, 100.0f);
    }
    if (level.pattern == SeedPattern::Grid)
    {
        changed |= ImGui::DragFloat3("cell size", &level.gridCellSize.x, 0.01f, 0.01f, 100.0f);
        changed |= ImGui::SliderFloat("jitter", &level.jitter, 0.0f, 1.0f);
    }
    if (level.pattern == SeedPattern::Painted)
    {
        const std::string strokes = std::format("{} painted strokes", level.strokes.size());
        ImGui::TextUnformatted(strokes.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("clear"))
        {
            level.strokes.clear();
            changed = true;
        }
    }
    return changed;
}

bool VolumeWidgets(const FracturePreview::Entry& entry, MaterialVolumeSettings& volume)
{
    bool changed = MaterialCombo("material", entry, volume.material);
    int shape = volume.volume.shape == MaterialVolume::Shape::Box ? 0 : 1;
    if (ImGui::Combo("shape", &shape, "box\0sphere\0"))
    {
        volume.volume.shape = shape == 0 ? MaterialVolume::Shape::Box : MaterialVolume::Shape::Sphere;
        changed = true;
    }
    changed |= ImGui::DragFloat3("centre", &volume.volume.centre.x, 0.05f);
    if (volume.volume.shape == MaterialVolume::Shape::Box)
    {
        changed |= ImGui::DragFloat3("half extents", &volume.volume.halfExtents.x, 0.02f, 0.01f, 1000.0f);
    }
    else
    {
        changed |= ImGui::DragFloat("radius", &volume.volume.radius, 0.02f, 0.01f, 1000.0f);
    }
    return changed;
}

} // namespace

ObjectCookSettings EditedSettings(const FracturePreview::Entry& entry, const std::string& object)
{
    if (object.empty())
    {
        return entry.config.defaults;
    }
    const auto found = std::ranges::find(entry.scene.objects, object, &destruction::SourceObject::name);
    const std::string_view material =
        found == entry.scene.objects.end() ? std::string_view() : std::string_view(found->firstMaterialName);
    auto settings = entry.config.SettingsFor(object, material);
    if (!settings)
    {
        return entry.config.defaults;
    }
    return std::move(*settings);
}

void StoreSettings(FracturePreview::Entry& entry, const std::string& object, const ObjectCookSettings& settings)
{
    if (object.empty())
    {
        entry.config.defaults = settings;
    }
    else
    {
        entry.config.SetObjectSettings(object, settings);
    }
}

FractureRequests DrawFracturePanel(FractureTool& tool, FracturePreview& preview, std::span<const AssetEntry> sources)
{
    FractureRequests requests;
    if (!ImGui::Begin(kFractureWindow))
    {
        ImGui::End();
        return requests;
    }
    if (ImGui::BeginCombo("source", tool.active.empty() ? "(open a glTF)" : tool.active.c_str()))
    {
        for (const AssetEntry& source : sources)
        {
            if (source.kind == AssetKind::Source && ImGui::Selectable(source.path.c_str(), source.path == tool.active))
            {
                requests.open = source.path;
            }
        }
        ImGui::EndCombo();
    }
    FracturePreview::Entry* entry = tool.active.empty() ? nullptr : preview.Find(tool.active);
    if (entry == nullptr)
    {
        ImGui::End();
        return requests;
    }

    ImGui::Checkbox("show preview", &tool.showPreview);
    ImGui::SameLine();
    requests.saveAndCook = ImGui::Button("Save sidecar && cook");
    if (!entry->error.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "cook failed: %s", entry->error.c_str());
    }
    else if (entry->asset != nullptr)
    {
        const std::string stats =
            std::format("{} chunks, {} bonds, {} anchors (revision {})", entry->asset->chunks.size(),
                        entry->asset->bonds.size(), entry->asset->anchors.size(), entry->revision);
        ImGui::TextUnformatted(stats.c_str());
    }
    ImGui::SliderInt("level shown", &tool.levelShown, 0, 4);
    ImGui::SliderFloat("explode", &tool.explode, 0.0f, 1.0f);

    ImGui::SeparatorText("Object");
    if (ImGui::BeginCombo("object", tool.object.empty() ? "(defaults)" : tool.object.c_str()))
    {
        if (ImGui::Selectable("(defaults)", tool.object.empty()))
        {
            tool.object.clear();
        }
        for (const destruction::SourceObject& object : entry->scene.objects)
        {
            if (ImGui::Selectable(object.name.c_str(), object.name == tool.object))
            {
                tool.object = object.name;
            }
        }
        ImGui::EndCombo();
    }

    ObjectCookSettings settings = EditedSettings(*entry, tool.object);
    bool changed = MaterialCombo("material", *entry, settings.material);
    changed |= ImGui::Checkbox("anchor", &settings.anchor);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("skip", &settings.skip);

    ImGui::SeparatorText("Fracture levels");
    for (std::size_t i = 0; i < settings.levels.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const std::string header =
            std::format("level {} ({})", i + 1, kPatternNames[static_cast<std::size_t>(settings.levels[i].pattern)]);
        if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= LevelWidgets(settings.levels[i]);
            if (ImGui::SmallButton("remove level"))
            {
                settings.levels.erase(settings.levels.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("add level") && settings.levels.size() < 4)
    {
        settings.levels.emplace_back();
        changed = true;
    }

    ImGui::SeparatorText("Seed paint");
    ImGui::Checkbox("paint (left mouse in the viewport)", &tool.paint);
    const int maxLevel = std::max(static_cast<int>(settings.levels.size()) - 1, 0);
    tool.levelEdited = std::clamp(tool.levelEdited, 0, maxLevel);
    ImGui::SliderInt("into level", &tool.levelEdited, 0, maxLevel);
    ImGui::DragFloat("brush radius", &tool.brushRadius, 0.01f, 0.05f, 10.0f, "%.2f m");
    ImGui::DragFloat("brush density", &tool.brushDensity, 1.0f, 1.0f, 5000.0f, "%.0f / m^3");

    ImGui::SeparatorText("Material volumes");
    for (std::size_t i = 0; i < settings.materialVolumes.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(1000 + i));
        const std::string header = std::format("volume {} ({})", i + 1, settings.materialVolumes[i].material);
        if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= VolumeWidgets(*entry, settings.materialVolumes[i]);
            if (ImGui::SmallButton("remove volume"))
            {
                settings.materialVolumes.erase(settings.materialVolumes.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ImGui::Button("add volume"))
    {
        MaterialVolumeSettings volume;
        volume.material = entry->materials.empty() ? settings.material : entry->materials.back().name;
        if (entry->asset != nullptr && !entry->asset->chunks.empty())
        {
            volume.volume.centre = entry->asset->chunks[0].centroid;
        }
        settings.materialVolumes.push_back(std::move(volume));
        changed = true;
    }

    if (changed)
    {
        StoreSettings(*entry, tool.object, settings);
    }
    ImGui::End();
    return requests;
}

} // namespace interi::editor
