#pragma once

// Reads the game's own RTTI type objects, which Native Loader hands us one by one while the
// engine registers its types. Nothing here needs a hardcoded signature or offset: we look for
// pointers into the main module's image, because that is where the type name strings live.

#include <string>
#include <vector>

namespace rtti
{
    // Called from OnTypeRegistering / OnTypeRegistered. Cheap: only stores the pointer.
    void Capture(void* type);

    // Called from OnTypesRegistered. Resolves every captured type's name and writes
    // <dir>\types.txt. Returns how many names were resolved.
    size_t ResolveAll(const std::wstring& dir);

    // Name of a captured type, or "" if it never resolved.
    std::string NameOf(void* type);

    struct Entry
    {
        void*       Type    = nullptr;
        uint32_t    Offset  = 0;      // where the name pointer was found inside the type object
        std::string Name;
    };

    const std::vector<Entry>& Entries();
}
