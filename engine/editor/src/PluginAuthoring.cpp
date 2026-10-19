// SPDX-License-Identifier: MIT
#include <interi/editor/PluginAuthoring.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>

namespace interi::editor
{

namespace
{

using Json = nlohmann::json;

std::string Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return std::string(text);
}

/// Removes MSVC's calling convention keywords and collapses the doubled spaces that leaves.
std::string StripCallingConvention(std::string text)
{
    for (const std::string_view keyword : {"__cdecl", "__stdcall", "__fastcall", "__vectorcall"})
    {
        for (std::size_t at = text.find(keyword); at != std::string::npos; at = text.find(keyword, at))
        {
            text.erase(at, keyword.size());
        }
    }
    std::string result;
    for (const char c : text)
    {
        if (c == ' ' && !result.empty() && result.back() == ' ')
        {
            continue;
        }
        result += c;
    }
    return Trim(result);
}

bool Opens(char c) noexcept
{
    return c == '(' || c == '<' || c == '[' || c == '{';
}

bool Closes(char c) noexcept
{
    return c == ')' || c == '>' || c == ']' || c == '}';
}

/// "R (A, B)" -> {"R", {"A", "B"}}; nullopt when it isn't a function type.
struct SplitSignature
{
    std::string result;
    std::vector<std::string> arguments;
};

std::optional<SplitSignature> Split(std::string_view signature)
{
    const std::string text = Trim(signature);
    if (text.empty() || text.back() != ')')
    {
        return std::nullopt;
    }
    int depth = 0;
    std::size_t open = std::string::npos;
    for (std::size_t i = text.size(); i-- > 0;)
    {
        if (text[i] == ')')
        {
            ++depth;
        }
        else if (text[i] == '(' && --depth == 0)
        {
            open = i;
            break;
        }
    }
    if (open == std::string::npos || open == 0)
    {
        return std::nullopt;
    }
    SplitSignature split;
    split.result = Trim(std::string_view(text).substr(0, open));
    const std::string inner = text.substr(open + 1, text.size() - open - 2);
    depth = 0;
    std::string current;
    for (const char c : inner)
    {
        if (Opens(c))
        {
            ++depth;
        }
        else if (Closes(c))
        {
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            split.arguments.push_back(Trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (const std::string last = Trim(current); !last.empty())
    {
        split.arguments.push_back(last);
    }
    if (split.arguments.size() == 1 && split.arguments[0] == "void")
    {
        split.arguments.clear();
    }
    if (split.result.empty() || std::ranges::any_of(split.arguments, [](const std::string& a) { return a.empty(); }))
    {
        return std::nullopt;
    }
    return split;
}

std::string ToType(const SplitSignature& split)
{
    std::string text = split.result + "(";
    for (std::size_t i = 0; i < split.arguments.size(); ++i)
    {
        text += (i == 0 ? "" : ", ") + split.arguments[i];
    }
    return text + ")";
}

std::string Pascal(std::string_view text)
{
    std::string name;
    bool upper = true;
    for (const char c : text)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0)
        {
            upper = true;
            continue;
        }
        name += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        upper = false;
    }
    if (name.empty() || std::isalpha(static_cast<unsigned char>(name.front())) == 0)
    {
        name = "Plugin" + name;
    }
    return name;
}

std::string LibraryName(std::string_view id)
{
    std::string name(id);
    for (char& c : name)
    {
        if (std::islower(static_cast<unsigned char>(c)) == 0 && std::isdigit(static_cast<unsigned char>(c)) == 0)
        {
            c = '_';
        }
    }
    return name;
}

/// The range of the owner's versions the override is declared for: the owner's major (minor while 0.x).
std::string OverrideRange(std::string_view ownerVersion)
{
    auto version = sdk::ParseSemVer(ownerVersion);
    if (!version)
    {
        return "*";
    }
    return version->major > 0 ? std::format("^{}", version->major) : std::format("^0.{}", version->minor);
}

std::string Quote(std::string_view text)
{
    return Json(std::string(text)).dump();
}

std::string HookSource(const PluginStubDesc& desc, const SplitSignature& split)
{
    std::string parameters;
    std::string forwarded;
    for (std::size_t i = 0; i < split.arguments.size(); ++i)
    {
        parameters += std::format(", {} a{}", split.arguments[i], i);
        forwarded += std::format("{}a{}", i == 0 ? "" : ", ", i);
    }
    const std::string call =
        split.result == "void" ? std::format("next({});", forwarded) : std::format("return next({});", forwarded);
    const bool local = sdk::IsLocalId(desc.target.id);
    const std::string installer =
        local ? std::format("INTERI_OVERRIDE(scope, {}, implementation)", Quote(desc.target.id))
              : std::format("scope.DeclareHook<Target>({}, implementation)", Quote(desc.target.id));
    return std::format(R"(bool Register(interi::hotreload::ModuleRegistrar& registrar)
{{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    // `next` is the implementation this one replaces; call it to keep the original behaviour, or don't.
    const auto implementation = [](const HookNext<Target>& next{}) {{ {} }};
    return {}.has_value();
}}
)",
                       parameters, call, installer);
}

std::string EventSource(const PluginStubDesc& desc)
{
    const PluginTarget& target = desc.target;
    const auto names = [](const std::vector<std::string>& pins)
    {
        std::string text;
        for (std::size_t i = 0; i < pins.size(); ++i)
        {
            text += std::format("{}{} {}", i == 0 ? "" : ", ", i, pins[i]);
        }
        return text.empty() ? std::string("none") : text;
    };
    const bool local = sdk::IsLocalId(target.id);
    const std::string install = local ? "events.Override(scope.Provider(), id, std::move(definition))"
                                      : "events.Provide(scope.Provider(), id, std::move(definition))";
    return std::format(R"(bool Register(interi::hotreload::ModuleRegistrar& registrar)
{{
    using namespace interi::chain;
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    EventRegistry& events = registrar.Chains().Events();
    constexpr std::string_view id = {};

    // Start from the event as it is (same pins: an override must keep the signature).
    const EventDefinition* existing = events.Resolve(id);
    if (existing == nullptr)
    {{
        return false;
    }}
    EventDefinition definition = *existing;
    if (definition.kind == EventKind::Action)
    {{
        // Replace the implementation: the original's code belongs to its module and must not outlive it.
        // Inputs: {}. Outputs: {}.
        definition.implementation = std::make_shared<const EventImpl>([](EventCall& call) {{ {} }});
    }}
    return {}.has_value();
}}
)",
                       Quote(target.id), names(target.eventInputs), names(target.eventOutputs),
                       target.eventOutputs.empty() ? "(void)call;" : "call.Fire(0);", install);
}

std::string SourceText(const PluginStubDesc& desc, const std::optional<SplitSignature>& split)
{
    const bool hook = desc.target.kind == PluginTargetKind::Hook;
    std::string text = std::format("// SPDX-License-Identifier: {}\n", desc.license);
    text += std::format("// {} ({}): an Interi Plugin.\n// It overrides the {} \"{}\".\n"
                        "// Register runs when the engine loads (or reloads) the module; see\n"
                        "// interi/hotreload/ModuleApi.h for the rules.\n",
                        desc.name, desc.id, hook ? "hook" : "event", desc.target.id);
    if (hook)
    {
        text += "#include <interi/hotreload/ModuleApi.h>\n#include <interi/sdk/Hooks.h>\n\nnamespace\n{\n\n"
                "using interi::sdk::HookNext;\n";
        text += std::format("using Target = {};\n\n", ToType(*split));
        text += HookSource(desc, *split);
    }
    else
    {
        text += "#include <interi/chain/Event.h>\n#include <interi/hotreload/ModuleApi.h>\n\n#include <memory>\n"
                "#include <string_view>\n\nnamespace\n{\n\n";
        text += EventSource(desc);
    }
    text += "\n} // namespace\n\nINTERI_GAMEPLAY_MODULE(Register)\n";
    return text;
}

std::vector<std::filesystem::path> LibraryFileNames(std::string_view base)
{
#if defined(_WIN32)
    return {std::format("{}.dll", base), std::format("lib{}.dll", base)};
#elif defined(__APPLE__)
    return {std::format("lib{}.dylib", base), std::format("{}.dylib", base), std::format("lib{}.so", base),
            std::format("{}.so", base)};
#else
    return {std::format("lib{}.so", base), std::format("{}.so", base)};
#endif
}

} // namespace

std::optional<std::string> ExtractHookSignature(std::string_view spelled)
{
    std::string type;
    if (const std::size_t at = spelled.find("SignatureName<"); at != std::string_view::npos)
    {
        // MSVC: ...SignatureName<int __cdecl(int)>(void) noexcept
        int depth = 1;
        const std::size_t start = at + std::string_view("SignatureName<").size();
        for (std::size_t i = start; i < spelled.size(); ++i)
        {
            if (spelled[i] == '<')
            {
                ++depth;
            }
            else if (spelled[i] == '>' && --depth == 0)
            {
                type = std::string(spelled.substr(start, i - start));
                break;
            }
        }
    }
    else if (const std::size_t eq = spelled.find("T = "); eq != std::string_view::npos)
    {
        // Clang: [T = int (int)]; GCC: [with T = int(int); std::string_view = ...]
        int depth = 0;
        const std::size_t start = eq + 4;
        std::size_t end = spelled.size();
        for (std::size_t i = start; i < spelled.size(); ++i)
        {
            const char c = spelled[i];
            if (Opens(c))
            {
                ++depth;
            }
            else if (depth == 0 && (c == ']' || c == ';'))
            {
                end = i;
                break;
            }
            else if (Closes(c))
            {
                --depth;
            }
        }
        type = std::string(spelled.substr(start, end - start));
    }
    type = StripCallingConvention(std::move(type));
    if (const std::optional<SplitSignature> split = Split(type))
    {
        return ToType(*split);
    }
    return std::nullopt;
}

Result<PluginTarget> DescribePluginTarget(PluginTargetKind kind, std::string_view id, const sdk::ModuleGraph& graph,
                                          const chain::ChainRegistry* chains, const sdk::HookRegistry* hooks)
{
    PluginTarget target;
    target.kind = kind;
    target.id = std::string(id);
    if (sdk::IsLocalId(id))
    {
        const sdk::ResolvedModule* owner = graph.Get(sdk::OwnerOf(id));
        if (owner == nullptr)
        {
            return MakeError(ErrorCode::NotFound,
                             std::format("'{}': its module '{}' isn't in the project", id, sdk::OwnerOf(id)));
        }
        target.ownerVersion = owner->manifest.version.ToString();
    }
    if (kind == PluginTargetKind::Hook)
    {
        const std::span stack = hooks != nullptr ? hooks->Registry().Stack(id) : std::span<const sdk::HookEntry>{};
        if (stack.empty())
        {
            return MakeError(ErrorCode::NotFound, std::format("no hook '{}' is declared", id));
        }
        const std::optional<std::string> signature = ExtractHookSignature(stack.back().value.signature);
        if (!signature)
        {
            return MakeError(ErrorCode::InvalidArgument, std::format("can't read the signature of hook '{}': {}", id,
                                                                     stack.back().value.signature));
        }
        target.hookSignature = *signature;
        return target;
    }
    const chain::EventDefinition* event = chains != nullptr ? chains->Events().Resolve(id) : nullptr;
    if (event == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no event '{}' is provided", id));
    }
    for (const chain::EventParam& input : event->inputs)
    {
        target.eventInputs.push_back(input.name);
    }
    for (const chain::EventOutput& output : event->outputs)
    {
        target.eventOutputs.push_back(output.name);
    }
    return target;
}

Result<std::vector<GeneratedFile>> GeneratePluginStub(const PluginStubDesc& desc)
{
    if (!sdk::IsValidModuleId(desc.id))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is not a module id (reverse-DNS, lowercase: com.author.name)", desc.id));
    }
    if (desc.name.empty() || desc.target.id.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "a Plugin needs a name and a target");
    }
    std::optional<SplitSignature> split;
    if (desc.target.kind == PluginTargetKind::Hook)
    {
        split = Split(desc.target.hookSignature);
        if (!split)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("'{}' is not a function type", desc.target.hookSignature));
        }
    }
    const std::string pascal = Pascal(desc.name);
    const std::string library = LibraryName(desc.id);

    Json manifest = {{"id", desc.id},
                     {"name", desc.name},
                     {"version", "0.1.0"},
                     {"kind", "Plugin"},
                     {"author", desc.author},
                     {"license", desc.license},
                     {"copyright", "Copyright (c) " + desc.author},
                     {"description", std::format("{}: overrides {}.", desc.name, desc.target.id)},
                     {"engine", desc.engineRange},
                     {"library", library},
                     {"dependencies", Json::array()},
                     {"overrides", Json::array()}};
    if (sdk::IsLocalId(desc.target.id))
    {
        manifest["overrides"].push_back(
            {{"target", desc.target.id}, {"version", OverrideRange(desc.target.ownerVersion)}});
    }

    const std::string cmake = std::format(R"(# SPDX-License-Identifier: {}
# {} ({}): an Interi Plugin, built against an installed engine.
cmake_minimum_required(VERSION 3.29)
project({} LANGUAGES CXX)

find_package(Interi REQUIRED)

interi_add_gameplay_module({} SOURCES src/{}Module.cpp)

# ctest: loads this folder's module.json and the library through the engine's module host.
enable_testing()
interi_add_module_load_test({})
)",
                                          desc.license, desc.name, desc.id, library, library, pascal, library);

    std::vector<GeneratedFile> files;
    files.push_back({.path = "module.json", .text = manifest.dump(2) + "\n"});
    files.push_back({.path = "CMakeLists.txt", .text = cmake});
    files.push_back({.path = std::format("src/{}Module.cpp", pascal), .text = SourceText(desc, split)});
    return files;
}

Result<std::vector<std::filesystem::path>> WritePluginStub(const std::filesystem::path& directory,
                                                           const PluginStubDesc& desc)
{
    INTERI_TRY_ASSIGN(files, GeneratePluginStub(desc));
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("{} is not empty", directory.string()));
    }
    std::vector<std::filesystem::path> written;
    for (const GeneratedFile& file : files)
    {
        const std::filesystem::path path = directory / file.path;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(file.text.data(), static_cast<std::streamsize>(file.text.size()));
        if (!out)
        {
            return MakeError(ErrorCode::IoError, "can't write " + path.string());
        }
        written.push_back(path);
    }
    return written;
}

DevLoopReport RunDevLoop(const DevLoopHooks& hooks)
{
    DevLoopReport report;
    const auto fail = [&](DevLoopStage stage, const Error& error)
    {
        report.failedAt = stage;
        report.message = error.message;
        return report;
    };
    if (hooks.stopPlay)
    {
        hooks.stopPlay();
    }
    if (hooks.build)
    {
        auto built = hooks.build();
        if (!built)
        {
            report.output += built.error().message;
            return fail(DevLoopStage::Build, built.error());
        }
        report.output += *built;
    }
    if (hooks.reload)
    {
        auto reloaded = hooks.reload();
        if (!reloaded)
        {
            return fail(DevLoopStage::Reload, reloaded.error());
        }
        report.output += *reloaded;
    }
    if (hooks.replay)
    {
        if (auto replayed = hooks.replay(); !replayed)
        {
            return fail(DevLoopStage::Replay, replayed.error());
        }
    }
    report.success = true;
    return report;
}

std::vector<std::filesystem::path> FindModuleLibraries(std::span<const sdk::ModuleManifest> manifests,
                                                       std::span<const std::filesystem::path> searchPaths)
{
    std::vector<std::filesystem::path> libraries;
    for (const sdk::ModuleManifest& manifest : manifests)
    {
        if (manifest.library.empty())
        {
            continue;
        }
        std::vector<std::filesystem::path> folders{manifest.directory};
        folders.insert(folders.end(), searchPaths.begin(), searchPaths.end());
        [&]
        {
            for (const std::filesystem::path& folder : folders)
            {
                for (const std::filesystem::path& name : LibraryFileNames(manifest.library))
                {
                    std::error_code error;
                    if (std::filesystem::is_regular_file(folder / name, error))
                    {
                        libraries.push_back(folder / name);
                        return;
                    }
                }
            }
        }();
    }
    return libraries;
}

void LibraryWatcher::Watch(std::span<const std::filesystem::path> libraries)
{
    for (const std::filesystem::path& library : libraries)
    {
        m_watcher.Watch(library);
    }
}

} // namespace interi::editor
