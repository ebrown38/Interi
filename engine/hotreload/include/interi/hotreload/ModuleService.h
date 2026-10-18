// SPDX-License-Identifier: MIT
#pragma once

// Services the host application offers its modules (the editor's panels and tools, a game's console...): named
// objects in ModuleHostDesc::services that modules reach with ModuleRegistrar::Service. A service keeps what a module
// registers with it, so it must give it up before the module's library unloads: the host calls Extract for the module
// (a reload's outgoing instance, an unload) and keeps the token until the new instance is registered; a failed load
// hands it to Restore. Dropping the token destroys the entries (their code is still loaded at that point).

#include <memory>
#include <string_view>

namespace interi::hotreload
{

class IModuleService
{
public:
    IModuleService() = default;
    IModuleService(const IModuleService&) = delete;
    IModuleService& operator=(const IModuleService&) = delete;
    IModuleService(IModuleService&&) = delete;
    IModuleService& operator=(IModuleService&&) = delete;
    virtual ~IModuleService() = default;

    /// The service's type, `T::kServiceType` of the class that implements it. ServiceAs<T> compares it instead of
    /// using RTTI, which does not match types across shared libraries reliably.
    [[nodiscard]] virtual std::string_view ServiceType() const noexcept = 0;

    /// Removes everything `provider` (a module id) registered and returns it. A token for nothing may be null.
    [[nodiscard]] virtual std::shared_ptr<void> Extract(std::string_view provider) = 0;
    /// Puts back what a token from Extract held (rollback of a failed load).
    virtual void Restore(std::shared_ptr<void> token) = 0;
};

} // namespace interi::hotreload
