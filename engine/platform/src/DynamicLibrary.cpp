// SPDX-License-Identifier: MIT
#include <interi/platform/DynamicLibrary.h>

#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace interi
{

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr))
{
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary()
{
    Close();
}

#if defined(_WIN32)

Result<DynamicLibrary> DynamicLibrary::Open(const std::string& path)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring widePath(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), widePath.data(), length);
    HMODULE module = LoadLibraryW(widePath.c_str());
    if (module == nullptr)
    {
        return MakeError(ErrorCode::NotFound,
                         "LoadLibraryW failed for '" + path + "' (error " + std::to_string(GetLastError()) + ")");
    }
    return DynamicLibrary(static_cast<void*>(module));
}

void* DynamicLibrary::FindSymbol(const char* name) const noexcept
{
    if (m_handle == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name)); // NOLINT: FARPROC
}

void DynamicLibrary::Close() noexcept
{
    if (m_handle != nullptr)
    {
        FreeLibrary(static_cast<HMODULE>(std::exchange(m_handle, nullptr)));
    }
}

#else

Result<DynamicLibrary> DynamicLibrary::Open(const std::string& path)
{
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char* reason = dlerror(); // NOLINT(concurrency-mt-unsafe): message is copied immediately
        return MakeError(ErrorCode::NotFound,
                         "dlopen failed for '" + path + "': " + (reason != nullptr ? reason : "unknown error"));
    }
    return DynamicLibrary(handle);
}

void* DynamicLibrary::FindSymbol(const char* name) const noexcept
{
    if (m_handle == nullptr)
    {
        return nullptr;
    }
    return dlsym(m_handle, name);
}

void DynamicLibrary::Close() noexcept
{
    if (m_handle != nullptr)
    {
        dlclose(std::exchange(m_handle, nullptr));
    }
}

#endif

} // namespace interi
