#include "types.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace
{
    std::vector<rtti::Entry> g_entries;
    std::mutex               g_mutex;

    struct ModuleRange
    {
        uintptr_t Begin = 0;
        uintptr_t End   = 0;

        ModuleRange()
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            Begin = base;
            End   = base + nt->OptionalHeader.SizeOfImage;
        }
    };

    const ModuleRange& MainModule()
    {
        static const ModuleRange range;
        return range;
    }

    bool SafeRead(uintptr_t address, void* out, size_t size)
    {
        __try
        {
            std::memcpy(out, reinterpret_cast<const void*>(address), size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // A type name must look like a C++ identifier or an MSVC mangled name, and live in the
    // game's own image. That combination rejects almost all the noise on the heap.
    bool IsNameChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == '?' || c == '@' || c == '$' || c == ':' || c == '<' ||
               c == '>' || c == '-' || c == '/';
    }

    bool ReadNameAt(uintptr_t address, std::string& out)
    {
        char buf[160]{};
        if (!SafeRead(address, buf, sizeof(buf) - 1))
            return false;

        size_t len = 0;
        while (len < sizeof(buf) - 1 && buf[len] != '\0')
        {
            if (!IsNameChar(buf[len]))
                return false;
            len++;
        }

        if (len < 3 || len > 120)
            return false;

        // Must start with something a type name can start with, not a digit.
        const char c0 = buf[0];
        if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '.' || c0 == '_'))
            return false;

        out.assign(buf, len);
        return true;
    }
}

void rtti::Capture(void* type)
{
    if (!type)
        return;

    std::lock_guard lock(g_mutex);
    if (g_entries.size() < 200000)
    {
        Entry e;
        e.Type = type;
        g_entries.push_back(e);
    }
}

size_t rtti::ResolveAll(const std::wstring& dir)
{
    std::lock_guard lock(g_mutex);

    const auto& mod = MainModule();
    size_t      resolved = 0;

    for (auto& entry : g_entries)
    {
        const auto typeAddress = reinterpret_cast<uintptr_t>(entry.Type);

        // The name pointer can sit anywhere in the first part of the type object; the exact
        // offset is part of what we are discovering, so try every pointer-sized slot and keep
        // the first one that lands in the image and reads as a name.
        for (uint32_t offset = 0; offset < 0x100; offset += 8)
        {
            uint64_t candidate = 0;
            if (!SafeRead(typeAddress + offset, &candidate, sizeof(candidate)))
                break;

            if (candidate < mod.Begin || candidate >= mod.End)
                continue;

            std::string name;
            if (!ReadNameAt((uintptr_t)candidate, name))
                continue;

            entry.Offset = offset;
            entry.Name   = name;
            resolved++;
            break;
        }
    }

    const std::wstring file = dir + L"\\types.txt";
    FILE*              f    = nullptr;
    _wfopen_s(&f, file.c_str(), L"w");
    if (f)
    {
        std::fprintf(f, "# %zu captured, %zu named\n", g_entries.size(), resolved);
        for (const auto& e : g_entries)
        {
            if (!e.Name.empty())
                std::fprintf(f, "%p  +0x%02X  %s\n", e.Type, e.Offset, e.Name.c_str());
        }
        std::fclose(f);
    }

    return resolved;
}

std::string rtti::NameOf(void* type)
{
    std::lock_guard lock(g_mutex);
    for (const auto& e : g_entries)
    {
        if (e.Type == type)
            return e.Name;
    }
    return {};
}

const std::vector<rtti::Entry>& rtti::Entries()
{
    return g_entries;
}
