// SPDX-License-Identifier: MIT
#pragma once

// Plugin authoring (no UI): a Plugin stub that overrides a chosen hook or Event, generated from the conflict or the
// palette view, and the hot-reload loop that follows (stop play, build, reload, play again).

#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/hotreload/FileWatcher.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

enum class PluginTargetKind : std::uint8_t
{
    Hook,
    Event,
};

/// What a Plugin overrides.
struct PluginTarget
{
    PluginTargetKind kind = PluginTargetKind::Hook;
    std::string id;
    /// The owning module's version for a local id ("com.author.base:Fire"); empty for a shared id.
    std::string ownerVersion;
    /// Hooks: the C++ function type of the hook ("int(int)").
    std::string hookSignature;
    /// Events: the input and output names, listed in the stub's comments.
    std::vector<std::string> eventInputs;
    std::vector<std::string> eventOutputs;
};

/// Describes `id` from what the project loaded: a hook's signature or an event's pins, and its owner's version.
/// NotFound when nothing provides it or the owner isn't in the graph; InvalidArgument for a hook signature the
/// compiler spells in a way the stub can't reuse.
[[nodiscard]] Result<PluginTarget> DescribePluginTarget(PluginTargetKind kind, std::string_view id,
                                                        const sdk::ModuleGraph& graph,
                                                        const chain::ChainRegistry* chains,
                                                        const sdk::HookRegistry* hooks);

struct PluginStubDesc
{
    std::string id; ///< Reverse-DNS module id of the new Plugin.
    std::string name;
    std::string author;
    std::string license = "MIT";
    std::string engineRange = "^0.1";
    PluginTarget target;
};

struct GeneratedFile
{
    std::string path; ///< Relative, '/'-separated.
    std::string text;
};

/// module.json (with the `overrides` entry for a local id), a standalone CMakeLists.txt and src/<Name>Module.cpp.
/// InvalidArgument for a bad id or name, or a hook signature that can't be split.
[[nodiscard]] Result<std::vector<GeneratedFile>> GeneratePluginStub(const PluginStubDesc& desc);

/// Writes the stub into `directory` (created; it must not exist or be empty). Returns the files' paths.
[[nodiscard]] Result<std::vector<std::filesystem::path>> WritePluginStub(const std::filesystem::path& directory,
                                                                         const PluginStubDesc& desc);

/// What the hot-reload loop does at each stage, supplied by the editor (so a shell never runs inside the engine
/// library). Empty functions are skipped.
struct DevLoopHooks
{
    std::function<void()> stopPlay;
    /// Runs the build (e.g. `cmake --build`); the error's message, or the Ok text, is the build output.
    std::function<Result<std::string>()> build;
    /// Reloads the module and its dependents.
    std::function<Result<std::string>()> reload;
    /// Starts playing again.
    std::function<Result<void>()> replay;
};

enum class DevLoopStage : std::uint8_t
{
    Build,
    Reload,
    Replay,
    Done,
};

struct DevLoopReport
{
    bool success = false;
    DevLoopStage failedAt = DevLoopStage::Done; ///< Done when successful.
    std::string output;                         ///< Build and reload output, in order.
    std::string message;                        ///< The failure's reason.
};

/// Stop play, build, reload, play again; stops at the first failure (the previous modules stay loaded when the
/// build or reload fails, and play is not restarted).
[[nodiscard]] DevLoopReport RunDevLoop(const DevLoopHooks& hooks);

/// The library files of `manifests` (found in their folder, then `searchPaths`, by the platform's naming), for the
/// loop to watch.
[[nodiscard]] std::vector<std::filesystem::path>
FindModuleLibraries(std::span<const sdk::ModuleManifest> manifests, std::span<const std::filesystem::path> searchPaths);

/// Polls module libraries; true when one was rebuilt (size or modification time changed, or it appeared).
class LibraryWatcher
{
public:
    void Watch(std::span<const std::filesystem::path> libraries);
    [[nodiscard]] bool Poll() { return m_watcher.Poll(); }

private:
    hotreload::FileWatcher m_watcher;
};

} // namespace interi::editor
