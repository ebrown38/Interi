// SPDX-License-Identifier: MIT
#include <interi/hotreload/ModuleDocs.h>
#include <interi/sdk/HookSignature.h>
#include <interi/values/ModuleContent.h>

#include <algorithm>
#include <format>
#include <string_view>

namespace interi::hotreload
{

namespace
{

/// Escapes what would break a Markdown table cell.
std::string Cell(std::string_view text)
{
    std::string result;
    for (const char c : text)
    {
        if (c == '|')
        {
            result += "\\|";
        }
        else if (c == '\n')
        {
            result += ' ';
        }
        else
        {
            result += c;
        }
    }
    return result.empty() ? "-" : result;
}

std::string TypeText(const values::ValueSchema& schema)
{
    std::string text(values::ToString(schema.type));
    if (schema.type == values::ValueType::List)
    {
        text += std::format("<{}>", values::ToString(schema.element));
    }
    if (schema.type == values::ValueType::Enum)
    {
        text += " (";
        for (std::size_t i = 0; i < schema.options.size(); ++i)
        {
            text += (i == 0 ? "" : ", ") + schema.options[i];
        }
        text += ")";
    }
    return text;
}

std::string RangeText(const values::ValueMeta& meta)
{
    if (!meta.min && !meta.max)
    {
        return {};
    }
    return std::format("{} .. {}{}{}", meta.min ? std::format("{}", *meta.min) : "",
                       meta.max ? std::format("{}", *meta.max) : "", meta.unit.empty() ? "" : " ", meta.unit);
}

void Section(std::string& page, std::string_view title)
{
    page += std::format("\n## {}\n\n", title);
}

template <typename Registry, typename Describe>
void ProvidedItems(const Registry& registry, std::string_view module, const Describe& describe)
{
    for (const std::string& id : registry.Ids())
    {
        for (const auto& entry : registry.Stack(id))
        {
            if (entry.provider == module)
            {
                describe(id, entry);
            }
        }
    }
}

void List(std::string& page, std::string_view label, const std::vector<std::string>& items)
{
    if (items.empty())
    {
        return;
    }
    page += std::format("- **{}:** ", label);
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        page += std::format("{}`{}`", i == 0 ? "" : ", ", items[i]);
    }
    page += "\n";
}

void Metadata(std::string& page, const sdk::ModuleManifest& manifest)
{
    page += std::format("# {}\n\n`{}` · version {} · {}\n\n", manifest.name.empty() ? manifest.id : manifest.name,
                        manifest.id, manifest.version.ToString(), sdk::ToString(manifest.kind));
    if (!manifest.description.empty())
    {
        page += manifest.description + "\n\n";
    }
    page += std::format("- **Author:** {}\n- **License:** {}\n- **Engine:** {}\n", manifest.author, manifest.license,
                        manifest.engine.Text());
    if (!manifest.homepage.empty())
    {
        page += std::format("- **Homepage:** {}\n", manifest.homepage);
    }
    page += manifest.library.empty() ? "- **Code:** none (content only)\n"
                                     : "- **Code:** native library, runs with full trust and no sandbox\n";
    std::vector<std::string> items;
    items.reserve(manifest.dependencies.size());
    for (const sdk::ModuleDependency& dependency : manifest.dependencies)
    {
        items.push_back(
            std::format("{} {}{}", dependency.id, dependency.version.Text(), dependency.optional ? " (optional)" : ""));
    }
    List(page, "Dependencies", items);
    items.clear();
    for (const sdk::ModuleConflict& conflict : manifest.conflicts)
    {
        items.push_back(std::format("{} {}", conflict.id, conflict.version.Text()));
    }
    List(page, "Conflicts", items);
    List(page, "Contracts", manifest.contracts);
    items.clear();
    for (const sdk::OverrideDeclaration& declaration : manifest.overrides)
    {
        items.push_back(std::format("{} {}", declaration.target, declaration.version.Text()));
    }
    List(page, "Overrides", items);
    items.clear();
    for (const sdk::PhysicsEngineEntry& entry : manifest.physicsEngines)
    {
        items.push_back(std::format("{} {}{}", entry.id, entry.version.Text(), entry.isDefault ? " (default)" : ""));
    }
    List(page, "Physics Engines", items);
}

void Events(std::string& page, const sdk::ModuleManifest& manifest, const chain::ChainRegistry& chains)
{
    std::string text;
    ProvidedItems(chains.Events().Registry(), manifest.id,
                  [&](const std::string& id, const auto& entry)
                  {
                      const chain::EventDefinition& event = entry.value;
                      text += std::format("### `{}`\n\n{} {}{}\n\n", id,
                                          event.kind == chain::EventKind::Action ? "Action" : "Trigger",
                                          event.category.empty() ? "" : "in " + event.category + ". ",
                                          entry.isOverride ? "Overrides the lower module's event. " : "");
                      if (!event.tooltip.empty())
                      {
                          text += event.tooltip + "\n\n";
                      }
                      if (!event.inputs.empty())
                      {
                          text += "| Input | Type | Default |\n|---|---|---|\n";
                          for (const chain::EventParam& input : event.inputs)
                          {
                              text += std::format(
                                  "| `{}` | {} | {} |\n", input.name,
                                  Cell(input.typeFrom.empty() ? TypeText(input.schema)
                                                              : "type of `" + input.typeFrom + "`'s Value"),
                                  Cell(input.typeFrom.empty() ? values::ValueToJsonText(input.schema.defaultValue)
                                                              : std::string{}));
                          }
                          text += "\n";
                      }
                      if (!event.outputs.empty())
                      {
                          text += "Outputs: ";
                          for (std::size_t i = 0; i < event.outputs.size(); ++i)
                          {
                              text += std::format("{}`{}`", i == 0 ? "" : ", ", event.outputs[i].name);
                              if (!event.outputs[i].payload.empty())
                              {
                                  text += " (";
                                  for (std::size_t p = 0; p < event.outputs[i].payload.size(); ++p)
                                  {
                                      text += std::format("{}{}", p == 0 ? "" : ", ", event.outputs[i].payload[p].name);
                                  }
                                  text += ")";
                              }
                          }
                          text += event.latent ? ". Latent: fires outputs later.\n\n" : ".\n\n";
                      }
                  });
    if (!text.empty())
    {
        Section(page, "Events");
        page += text;
    }
}

void Functions(std::string& page, const sdk::ModuleManifest& manifest, const chain::ChainRegistry& chains)
{
    std::string text;
    ProvidedItems(chains.Functions().Registry(), manifest.id,
                  [&](const std::string& id, const auto& entry)
                  {
                      std::string parameters;
                      for (std::size_t i = 0; i < entry.value.parameters.size(); ++i)
                      {
                          parameters +=
                              std::format("{}{}", i == 0 ? "" : ", ", values::ToString(entry.value.parameters[i]));
                      }
                      text +=
                          std::format("- `{}({}) -> {}`{}{}\n", id, parameters, values::ToString(entry.value.result),
                                      entry.value.tooltip.empty() ? "" : ": " + entry.value.tooltip,
                                      entry.isOverride ? " (override)" : "");
                  });
    if (!text.empty())
    {
        Section(page, "Functions");
        page += text;
    }
}

void Hooks(std::string& page, const sdk::ModuleManifest& manifest, const sdk::HookRegistry& hooks)
{
    std::string text;
    ProvidedItems(hooks.Registry(), manifest.id,
                  [&](const std::string& id, const auto& entry)
                  {
                      const std::optional<std::string> signature = sdk::ExtractHookSignature(entry.value.signature);
                      text += std::format("- `{}`: `{}`{}\n", id, signature.value_or("?"),
                                          entry.isOverride ? " (override)" : "");
                  });
    if (!text.empty())
    {
        Section(page, "Hook points");
        page += text;
    }
}

void Values(std::string& page, const values::ModuleContent& content)
{
    if (content.values.empty())
    {
        return;
    }
    std::vector<const values::ValueDefinition*> sorted;
    sorted.reserve(content.values.size());
    for (const values::ValueDefinition& value : content.values)
    {
        sorted.push_back(&value);
    }
    std::ranges::sort(sorted, {}, &values::ValueDefinition::id);
    Section(page, "Values");
    page += "| Id | Type | Default | Range | Category | Description |\n|---|---|---|---|---|---|\n";
    for (const values::ValueDefinition* value : sorted)
    {
        page += std::format("| `{}`{} | {} | {} | {} | {} | {} |\n", value->id, value->isOverride ? " (override)" : "",
                            Cell(TypeText(value->schema)), Cell(values::ValueToJsonText(value->schema.defaultValue)),
                            Cell(RangeText(value->schema.meta)), Cell(value->schema.meta.category),
                            Cell(value->schema.meta.tooltip));
    }
}

void Tables(std::string& page, const values::ModuleContent& content)
{
    if (content.dataTables.empty())
    {
        return;
    }
    std::vector<const values::DataTableDefinition*> sorted;
    sorted.reserve(content.dataTables.size());
    for (const values::DataTableDefinition& table : content.dataTables)
    {
        sorted.push_back(&table);
    }
    std::ranges::sort(sorted, {}, &values::DataTableDefinition::id);
    Section(page, "Data Tables");
    for (const values::DataTableDefinition* table : sorted)
    {
        page += std::format("### `{}` (version {})\n\n| Column | Type | Default |\n|---|---|---|\n", table->id,
                            table->version);
        for (const values::ColumnDefinition& column : table->columns)
        {
            page += std::format("| `{}` | {} | {} |\n", column.name, Cell(TypeText(column.schema)),
                                Cell(values::ValueToJsonText(column.schema.defaultValue)));
        }
        page += "\n";
    }
}

} // namespace

Result<std::string> GenerateModuleDocs(const sdk::ModuleManifest& manifest, const chain::ChainRegistry* chains,
                                       const sdk::HookRegistry* hooks)
{
    values::ModuleContent content;
    content.module = manifest.id;
    if (!manifest.directory.empty())
    {
        INTERI_TRY_ASSIGN(loaded, values::LoadModuleContent(manifest));
        content = std::move(loaded);
    }
    std::string page;
    Metadata(page, manifest);
    if (chains != nullptr)
    {
        Events(page, manifest, *chains);
        Functions(page, manifest, *chains);
    }
    if (hooks != nullptr)
    {
        Hooks(page, manifest, *hooks);
    }
    Values(page, content);
    Tables(page, content);
    return page;
}

} // namespace interi::hotreload
