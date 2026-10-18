// SPDX-License-Identifier: MIT
#pragma once

// The data sections of a module.json: what a module declares besides code (Values, character attributes, data
// tables). Parsed in the style of ParseMaterials: anything malformed is an error, unknown keys are warnings.
//
//     "values": [
//       { "id": "JumpHeight", "type": "float", "default": 1.2, "min": 0, "max": 10, "step": 0.1, "unit": "m",
//         "category": "Movement", "tooltip": "...", "readOnly": false, "version": 1 },
//       { "id": "value.Gravity", "shared": true, "type": "float", "default": -9.81 },
//       { "overrides": "com.author.base:JumpHeight", "type": "float", "default": 2.0, "max": 20 },
//       { "id": "FireMode", "type": "enum", "options": ["Single", "Auto"], "default": "Single" },
//       { "id": "Waypoints", "type": "list", "element": "vec3", "default": [[0, 0, 0]] }
//     ]
//
// An "id" without "shared" is a local name: the item's id is "<module id>:<name>". With "shared": true the id is a
// shared contract id (declared by the engine or an Extension's "contracts"). "overrides" names a lower module's local
// item instead (declared in the manifest's "overrides"); it keeps the item's type and replaces its default, range
// and metadata. "type" is required; a missing "default" is the type's zero value.
//
// Characters: attributes are Values of type bool, int or float (shared ids "attr.*" where modules should agree),
// grouped into attribute sets; an archetype combines sets and sets per-archetype defaults. References name items by
// their full id.
//
//     "attributes":    [ { "id": "attr.Health", "shared": true, "type": "float", "default": 100, "min": 0 },
//                        { "id": "Stamina", "type": "float", "default": 50 } ],
//     "attributeSets": [ { "id": "Vitals", "attributes": ["attr.Health", "com.author.fps:Stamina"] } ],
//     "archetypes":    [ { "id": "Soldier", "sets": ["com.author.fps:Vitals"], "defaults": { "attr.Health": 150 } } ]
//
// Sets and archetypes take "overrides" like Values (an override replaces the whole item) and a "version".
//
// Data tables ("dataTables") are described in DataTables.h.

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>
#include <interi/values/DataTables.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::values
{

/// One module's definition of a Value.
struct ValueDefinition
{
    std::string id; ///< Full id: "<module>:<name>", a shared id, or the overridden item's id.
    ValueSchema schema;
    std::uint32_t version = 1; ///< Bumped when the schema changes incompatibly (project data migrates).
    bool isOverride = false;   ///< Declared with "overrides".
};

/// A character attribute: a bool, int or float Value.
using AttributeDefinition = ValueDefinition;

/// A named group of attributes.
struct AttributeSetDefinition
{
    std::string id;
    std::vector<std::string> attributes; ///< Full attribute ids, in display order.
    std::uint32_t version = 1;
    bool isOverride = false;
};

/// A character archetype: attribute sets plus per-archetype defaults.
struct ArchetypeDefinition
{
    std::string id;
    std::vector<std::string> sets; ///< Full attribute set ids; attributes appear in set order, first wins.
    /// Attribute id -> default, as JSON text (typed once the attribute's winning definition is known).
    std::vector<std::pair<std::string, std::string>> defaults;
    std::uint32_t version = 1;
    bool isOverride = false;
};

/// Everything a module.json declares in its data sections.
struct ModuleContent
{
    std::string module; ///< The module's id.
    std::vector<ValueDefinition> values;
    std::vector<AttributeDefinition> attributes;
    std::vector<AttributeSetDefinition> attributeSets;
    std::vector<ArchetypeDefinition> archetypes;
    std::vector<DataTableDefinition> dataTables;
    std::vector<std::string> warnings;
};

/// Parses the data sections of module.json text for module `moduleId`. InvalidArgument for malformed sections, with
/// the section and index in the message.
[[nodiscard]] Result<ModuleContent> ParseModuleContent(std::string_view moduleJson, std::string_view moduleId);

/// Reads `<manifest.directory>/module.json`'s data sections. A manifest without a directory (parsed from text) has no
/// content. IoError if the file can't be read.
[[nodiscard]] Result<ModuleContent> LoadModuleContent(const sdk::ModuleManifest& manifest);

} // namespace interi::values
