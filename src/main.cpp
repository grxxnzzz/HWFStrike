// HWFStrike - a separate button for the contextual Silent Strike in Horizon Forbidden West.
//
// Loaded by Native Loader from <game>\nativemods\HWFStrike\HWFStrike.dll.
// Settings come from HWFStrike.ini next to this DLL.
//
// Modes:
//   probe   - log only. Never sends input.
//   direct  - press the key -> always a light attack (exactly like LMB). Tests the injection path.
//   types   - dump the game's own RTTI type names to types.txt while it starts up. Used to find
//             the contextual-action system without guessing signatures.
//   smart   - press the key -> light attack ONLY while a contextual strike is being offered.
//             Not calibrated yet, so it currently does nothing.

#include <windows.h>
#include <share.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <vector>

#include "config.h"
#include "nativeloader.h"
#include "types.h"
#include "MinHook.h"

#ifndef HWFSTRIKE_VERSION
#define HWFSTRIKE_VERSION "0.2.0-dev"
#endif

static const char* MOD = "HWFStrike";

namespace
{
    HMODULE            g_self = nullptr;
    std::wstring       g_dir;
    std::mutex         g_logMutex;
    FILE*              g_log = nullptr;
    NativeLoaderLog_t  g_console = nullptr;
    std::atomic<bool>  g_running{true};
    std::atomic<bool>  g_started{false};
    std::atomic<long>  g_typeCount{0};
    Config             g_cfg;
    std::vector<std::string> g_markers;   // [Strike] StrikeMarkers, split up
    bool               g_smartReady = false; // the prompt hook is in place, so `smart` may act

    const ULONG_PTR    kInjectedMark = 0x48574653; // 'HWFS'

    std::wstring DirectoryOf(HMODULE module)
    {
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(module, buf, (DWORD)std::size(buf));
        std::wstring path = buf;
        const auto   pos  = path.find_last_of(L"\\/");
        return pos == std::wstring::npos ? path : path.substr(0, pos);
    }

    void LogOpen()
    {
        std::lock_guard lock(g_logMutex);
        if (g_log)
            return;

        const std::wstring file = g_dir + L"\\HWFStrike.log";
        // Append, and share for reading so the log can be inspected while the game runs.
        g_log = _wfsopen(file.c_str(), L"a", _SH_DENYWR);
        if (g_log)
            std::fprintf(g_log, "\n=== session %llu ===\n", (unsigned long long)GetTickCount64());
    }

    void Log(const char* fmt, ...)
    {
        char msg[2048]{};
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        {
            std::lock_guard lock(g_logMutex);
            if (!g_log)
                return;
            SYSTEMTIME st{};
            GetLocalTime(&st);
            std::fprintf(g_log, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond,
                         st.wMilliseconds, msg);
            std::fflush(g_log);
        }

        if (g_console)
            g_console(MOD, msg);
    }

    void LogClose()
    {
        std::lock_guard lock(g_logMutex);
        if (g_log)
        {
            std::fclose(g_log);
            g_log = nullptr;
        }
    }

    uint32_t ImageTimestamp(uintptr_t base)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return nt->FileHeader.TimeDateStamp;
    }

    bool GameIsForeground()
    {
        const HWND fg = GetForegroundWindow();
        if (!fg)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId();
    }

    void SendLightAttack(int holdMs)
    {
        INPUT down{};
        down.type           = INPUT_MOUSE;
        down.mi.dwFlags     = MOUSEEVENTF_LEFTDOWN;
        down.mi.dwExtraInfo = kInjectedMark;
        SendInput(1, &down, sizeof(INPUT));

        Sleep((DWORD)holdMs);

        INPUT up{};
        up.type           = INPUT_MOUSE;
        up.mi.dwFlags     = MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
        up.mi.dx          = 0;
        up.mi.dy          = 0;
        up.mi.dwExtraInfo = kInjectedMark;
        SendInput(1, &up, sizeof(INPUT));
    }

    // -----------------------------------------------------------------------------------
    // Probes.
    //
    // Every detour below calls the original first and then copies a little state out. None of
    // them reads or writes the caller's data, so the game behaves exactly as it normally does.
    // -----------------------------------------------------------------------------------

    bool SafeCopy(void* dst, const void* src, size_t size)
    {
        __try
        {
            std::memcpy(dst, src, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const void* Add(const void* p, size_t offset)
    {
        return static_cast<const char*>(p) + offset;
    }

    void LogMemory(const char* tag, const void* address, size_t size)
    {
        unsigned char buf[0x40]{};
        const size_t  n = size > sizeof(buf) ? sizeof(buf) : size;
        if (!SafeCopy(buf, address, n))
        {
            Log("%s %p: unreadable", tag, address);
            return;
        }

        char line[256]{};
        int  used = std::snprintf(line, sizeof(line), "%s %p:", tag, address);
        for (size_t i = 0; i < n && used < (int)sizeof(line) - 20; i += 8)
        {
            uint64_t value = 0;
            std::memcpy(&value, buf + i, 8);
            used += std::snprintf(line + used, sizeof(line) - used, " %016llx",
                                  (unsigned long long)value);
        }
        Log("%s", line);
    }

    // --- MsgGetActionPrompts: the prompts the HUD is showing right now -------------------
    //
    // The handler receives a vector at msg+0x10:
    //   +0x08  count (int)
    //   +0x0C  capacity (int)
    //   +0x10  entries (pointer)
    // and appends 0x18-byte entries {resource ref, extra, float, byte}.
    //
    // entry.resource points at the prompt text itself ("Бесшумная атака <icon name=Stealth_Kill>"
    // in the Russian build). That buffer is rebuilt every frame and the pointer moves with it, so
    // the text is copied here, inside the game's own frame, where the pointer is guaranteed alive.

    constexpr int kMaxPromptText = 0xA0;

    struct PromptEntry
    {
        void*         resource;
        void*         extra;
        float         amount;
        unsigned char flag;
        unsigned char pad[3];
    };
    static_assert(sizeof(PromptEntry) == 0x18, "unexpected prompt entry layout");

    constexpr int kMaxPrompts = 8;

    struct PromptSnapshot
    {
        int         count = 0;
        PromptEntry entry[kMaxPrompts]{};
        char        text[kMaxPrompts][kMaxPromptText]{};
        bool        textOk[kMaxPrompts]{};
        uint64_t    serial = 0;
    };

    PromptSnapshot g_prompts;
    std::mutex     g_promptMutex;

    typedef void* (__cdecl* ActionPromptsFn)(void* self, void* msg);
    ActionPromptsFn   g_origActionPrompts = nullptr;
    std::atomic<long> g_promptCalls{0};
    std::atomic<bool> g_promptHookReady{false};

    // Printable form of a captured prompt: control bytes become '.', everything else is kept so
    // the log carries the real UTF-8 text.
    const char* SafeText(const char* in, size_t size, char* out)
    {
        size_t i = 0;
        for (; i + 1 < size && i + 1 < kMaxPromptText && in[i]; i++)
        {
            const unsigned char c = (unsigned char)in[i];
            out[i] = (c < 0x20 || c == 0x7F) ? '.' : (char)c;
        }
        out[i] = '\0';
        return out;
    }

    void* __cdecl DetourActionPrompts(void* self, void* msg)
    {
        void* result = g_origActionPrompts ? g_origActionPrompts(self, msg) : nullptr;
        ++g_promptCalls;

        void* vector = nullptr;
        int   count  = 0;
        void* data   = nullptr;

        if (!msg || !SafeCopy(&vector, Add(msg, 0x10), sizeof vector) || !vector ||
            !SafeCopy(&count, Add(vector, 0x08), sizeof count) ||
            !SafeCopy(&data, Add(vector, 0x10), sizeof data) || !data || count < 0 || count > 64)
        {
            return result;
        }

        const int n = count > kMaxPrompts ? kMaxPrompts : count;

        PromptEntry entries[kMaxPrompts]{};
        if (n > 0 && !SafeCopy(entries, data, sizeof(PromptEntry) * n))
            return result;

        PromptSnapshot next;
        next.count = n;
        std::memcpy(next.entry, entries, sizeof(PromptEntry) * n);

        for (int i = 0; i < n; i++)
        {
            next.textOk[i] = entries[i].resource &&
                             SafeCopy(next.text[i], entries[i].resource, kMaxPromptText);
            next.text[i][kMaxPromptText - 1] = '\0';
        }

        bool changed = false;
        {
            std::lock_guard lock(g_promptMutex);
            changed = g_prompts.count != n;
            for (int i = 0; i < n && !changed; i++)
            {
                if (!g_prompts.textOk[i] || !next.textOk[i] ||
                    std::strcmp(g_prompts.text[i], next.text[i]) != 0)
                {
                    changed = true;
                }
            }
            next.serial = g_prompts.serial + 1;
            g_prompts   = next;
        }

        if (changed)
        {
            char line[512]{};
            int  used = std::snprintf(line, sizeof(line), "PROMPTS n=%d", n);
            char shown[kMaxPromptText]{};
            for (int i = 0; i < n && used < (int)sizeof(line) - kMaxPromptText - 8; i++)
            {
                used += std::snprintf(line + used, sizeof(line) - used, " [%d] \"%s\"", i,
                                      next.textOk[i] ? SafeText(next.text[i], kMaxPromptText, shown)
                                                     : "<no text>");
            }
            Log("%s", line);
        }

        return result;
    }

    bool StrikeInSnapshot(const PromptSnapshot& snap, int* which)
    {
        for (int i = 0; i < snap.count; i++)
        {
            if (!snap.textOk[i])
                continue;

            for (const auto& marker : g_markers)
            {
                if (marker.empty())
                    continue;

                // Case-insensitive ASCII search: the marker is an icon/resource name, never
                // localised text, so this stays true in every language.
                const size_t m = marker.size();
                for (const char* p = snap.text[i]; *p; p++)
                {
                    size_t j = 0;
                    while (j < m && p[j] &&
                           std::tolower((unsigned char)p[j]) ==
                               std::tolower((unsigned char)marker[j]))
                    {
                        j++;
                    }
                    if (j == m)
                    {
                        if (which)
                            *which = i;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // --- MsgGetPotentialContextualActionsOnVictim (kept from the earlier probe) ----------

    typedef void* (__cdecl* GetPotentialFn)(void* self, void* message);
    GetPotentialFn    g_origGetPotential = nullptr;
    std::atomic<long> g_hookCalls{0};

    void* __cdecl DetourGetPotential(void* self, void* message)
    {
        void* result = g_origGetPotential ? g_origGetPotential(self, message) : nullptr;

        const long n = ++g_hookCalls;

        static void* lastResult = (void*)~(uintptr_t)0;
        static void* lastSelf   = nullptr;
        const bool   changed    = result != lastResult || self != lastSelf;
        const bool   sample     = n <= 10 || changed || (n % 2000) == 0;

        lastResult = result;
        lastSelf   = self;

        if (sample)
            Log("HOOK GetPotential #%ld self=%p msg=%p -> %p", n, self, message, result);

        return result;
    }

    // --- MsgIsContextualActionAllowed: the per-action "may I do this now?" answer ---------
    //
    // Handlers write the answer into the message at +0x08 and mark it answered at +0x10, so
    // reading the message after the original has run gives us the game's own verdict.

    struct AllowedSnapshot
    {
        int           answer   = -1;
        int           answered = -1;
        long          calls    = 0;
        unsigned char msg[0x40]{};
    };

    AllowedSnapshot g_allowed[2];
    std::mutex      g_allowedMutex;

    // Declared as returning void* purely so the original's rax is carried through untouched,
    // whatever the game does with it.
    typedef void* (__cdecl* AllowedFn)(void* self, void* msg);
    AllowedFn g_origAllowed[2] = {nullptr, nullptr};

    void* HandleAllowed(int index, void* self, void* msg, AllowedFn original)
    {
        void* result = original ? original(self, msg) : nullptr;

        if (!msg)
            return result;

        unsigned char answer   = 0;
        unsigned char answered = 0;
        SafeCopy(&answer, Add(msg, 0x08), 1);
        SafeCopy(&answered, Add(msg, 0x10), 1);

        int previous = -1;
        {
            std::lock_guard lock(g_allowedMutex);
            previous                   = g_allowed[index].answer;
            g_allowed[index].answer    = answer;
            g_allowed[index].answered  = answered;
            g_allowed[index].calls++;
            SafeCopy(g_allowed[index].msg, msg, sizeof g_allowed[index].msg);
        }

        if (answer != previous)
            Log("ALLOWED[%d] answer=%d answered=%d", index, answer, answered);

        return result;
    }

    void* __cdecl DetourAllowedPlayer(void* self, void* msg)
    {
        return HandleAllowed(0, self, msg, g_origAllowed[0]);
    }

    void* __cdecl DetourAllowedCac(void* self, void* msg)
    {
        return HandleAllowed(1, self, msg, g_origAllowed[1]);
    }

    // --- installation --------------------------------------------------------------------

    bool InstallHook(uintptr_t base, uint32_t offset, const char* name, const uint8_t* expected,
                     size_t expectedSize, void* detour, void** original)
    {
        auto target = reinterpret_cast<void*>(base + offset);

        if (std::memcmp(target, expected, expectedSize) != 0)
        {
            Log("hook %s REFUSED: bytes at %p do not match build 1.5.80.0", name, target);
            return false;
        }

        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status != MH_OK)
        {
            Log("hook %s: MH_CreateHook failed (%d)", name, (int)status);
            return false;
        }

        status = MH_EnableHook(target);
        if (status != MH_OK)
        {
            Log("hook %s: MH_EnableHook failed (%d)", name, (int)status);
            return false;
        }

        Log("hook %s installed at %p (module+0x%X)", name, target, offset);
        return true;
    }

    bool InstallWatchHooks(uintptr_t base)
    {
        if (MH_Initialize() != MH_OK)
        {
            Log("hook: MH_Initialize failed");
            return false;
        }

        static const uint8_t getPotential[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
            0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x99, 0x80, 0x02, 0x00, 0x00};

        static const uint8_t actionPrompts[] = {
            0x40, 0x55, 0x53, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C,
            0x24, 0xC9, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x33, 0xFF};

        static const uint8_t allowedPlayer[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79,
                                                0x50, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x39};

        static const uint8_t allowedCac[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x79,
                                             0x78, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x15};

        bool any = false;

        // The one hook `smart` mode needs: the prompt list the game is currently showing.
        any = InstallHook(base, 0xEFF990, "ActionPrompts", actionPrompts, sizeof(actionPrompts),
                          (void*)&DetourActionPrompts, (void**)&g_origActionPrompts);
        g_promptHookReady = any && g_origActionPrompts != nullptr;

        if (!g_promptHookReady)
            Log("WARNING: the prompt hook is not installed, so smart mode cannot tell whether a "
                "strike is offered and will never send anything");

        if (!g_cfg.debugHooks)
            return any;

        // Investigation hooks from the calibration runs. Read-only, but noisy, so off by default.
        any |= InstallHook(base, g_cfg.hookOffset, "GetPotential", getPotential,
                           sizeof(getPotential), (void*)&DetourGetPotential,
                           (void**)&g_origGetPotential);
        any |= InstallHook(base, 0xF624F0, "IsAllowed(player)", allowedPlayer,
                           sizeof(allowedPlayer), (void*)&DetourAllowedPlayer,
                           (void**)&g_origAllowed[0]);
        any |= InstallHook(base, 0xF62EC0, "IsAllowed(contextual)", allowedCac, sizeof(allowedCac),
                           (void*)&DetourAllowedCac, (void**)&g_origAllowed[1]);

        return any;
    }

    void LogProbeState()
    {
        PromptSnapshot prompts;
        AllowedSnapshot allowed[2];
        {
            std::lock_guard lock(g_promptMutex);
            prompts = g_prompts;
        }
        {
            std::lock_guard lock(g_allowedMutex);
            allowed[0] = g_allowed[0];
            allowed[1] = g_allowed[1];
        }

        Log("state prompts=%d (calls=%ld)", prompts.count, g_promptCalls.load());
        for (int i = 0; i < prompts.count; i++)
        {
            char shown[kMaxPromptText]{};
            Log("  prompt[%d] res=%p extra=%p f=%.3f flag=%u text=\"%s\"", i,
                prompts.entry[i].resource, prompts.entry[i].extra, prompts.entry[i].amount,
                prompts.entry[i].flag,
                prompts.textOk[i] ? SafeText(prompts.text[i], kMaxPromptText, shown) : "<none>");
        }

        for (int i = 0; i < 2; i++)
        {
            Log("state allowed[%d] answer=%d answered=%d calls=%ld", i, allowed[i].answer,
                allowed[i].answered, allowed[i].calls);
            LogMemory("  allowed.msg", allowed[i].msg, 0x40);
        }
    }

    // The answer the whole mod exists for: is the game offering a Silent Strike right now?
    // It is read from the prompt list the HUD is showing, matched on the icon name embedded in
    // the prompt text, which is the same in every language.
    bool StrikeIsAvailable(int* whichIndex = nullptr, char* matched = nullptr, size_t matchedSize = 0)
    {
        if (!g_smartReady)
            return false;

        PromptSnapshot snap;
        {
            std::lock_guard lock(g_promptMutex);
            snap = g_prompts;
        }

        int which = -1;
        const bool found = StrikeInSnapshot(snap, &which);
        if (found)
        {
            if (whichIndex)
                *whichIndex = which;
            if (matched && matchedSize)
            {
                char shown[kMaxPromptText]{};
                std::snprintf(matched, matchedSize, "%s",
                              snap.textOk[which] ? SafeText(snap.text[which], kMaxPromptText, shown)
                                                 : "<no text>");
            }
        }
        return found;
    }

    // -----------------------------------------------------------------------------------
    // Native Loader callbacks
    // -----------------------------------------------------------------------------------

    void __cdecl OnGameStart()
    {
        Log("OnGameStart: %ld types captured so far", g_typeCount.load());

        if (g_cfg.mode == "watch" || g_cfg.mode == "probe" || g_cfg.mode == "probe2" ||
            g_cfg.mode == "smart")
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            InstallWatchHooks(base);
            g_smartReady = g_promptHookReady.load();
            Log("smart mode %s", g_smartReady ? "ready: prompt detection active" : "NOT ready");
        }
    }

    void __cdecl OnTypesRegistering()
    {
        Log("OnTypesRegistering: the engine is registering its RTTI types");
    }

    void __cdecl OnTypeRegistering(void*)
    {
        // Intentionally empty: thousands of calls, must stay cheap.
    }

    void __cdecl OnTypeRegistered(void* type)
    {
        rtti::Capture(type);
        g_typeCount++;
    }

    void __cdecl OnTypesRegistered()
    {
        Log("OnTypesRegistered: %ld types", g_typeCount.load());

        if (g_cfg.mode != "types" && !g_cfg.dumpTypes)
            return;

        const size_t resolved = rtti::ResolveAll(g_dir);
        Log("types.txt written: %zu of %ld types resolved to a name", resolved, g_typeCount.load());

        if (g_cfg.typeFilter.empty())
            return;

        std::vector<std::string> needles;
        {
            std::string current;
            for (char c : g_cfg.typeFilter)
            {
                if (c == ',' || c == ' ')
                {
                    if (!current.empty())
                        needles.push_back(current);
                    current.clear();
                }
                else
                {
                    current.push_back((char)std::tolower((unsigned char)c));
                }
            }
            if (!current.empty())
                needles.push_back(current);
        }

        for (const auto& e : rtti::Entries())
        {
            if (e.Name.empty())
                continue;

            std::string lower = e.Name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });

            for (const auto& needle : needles)
            {
                if (lower.find(needle) != std::string::npos)
                {
                    Log("TYPE %p  name@+0x%02X  %s", e.Type, e.Offset, e.Name.c_str());
                    break;
                }
            }
        }
    }
}

extern "C" __declspec(dllexport) void __cdecl NativeLoaderInit(NativeLoaderCallbacks* callbacks)
{
    LogOpen();
    g_console = NativeLoaderGetLog();

    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Log("HWFStrike %s loaded (game build 0x%08X)", HWFSTRIKE_VERSION, ImageTimestamp(base));
    Log("key=0x%02X mode=%s widen=%s grace=%dms lead=%dms hold=%dms",
        g_cfg.key, g_cfg.mode.c_str(), g_cfg.widenPrompt ? "on" : "off", g_cfg.graceMs,
        g_cfg.leadMs, g_cfg.holdMs);

    // [Strike] StrikeMarkers -> the list matched against prompt text.
    {
        const std::string& list = g_cfg.strikeMarkers;
        std::string        current;
        auto               flush = [&]() {
            const size_t a = current.find_first_not_of(" \t");
            const size_t b = current.find_last_not_of(" \t");
            if (a != std::string::npos)
                g_markers.push_back(current.substr(a, b - a + 1));
            current.clear();
        };

        for (char c : list)
        {
            if (c == ',' || c == ';')
                flush();
            else
                current.push_back(c);
        }
        flush();

        if (g_markers.empty())
            Log("WARNING: StrikeMarkers is empty, so smart mode cannot recognise the strike prompt");
        for (const auto& m : g_markers)
            Log("strike marker \"%s\"", m.c_str());
    }
    Log("console=%s dumpTypes=%s filter=%s", g_console ? "yes" : "no", g_cfg.dumpTypes ? "yes" : "no",
        g_cfg.typeFilter.c_str());

    if (callbacks)
    {
        callbacks->OnGameStart        = OnGameStart;
        callbacks->OnTypesRegistering = OnTypesRegistering;
        callbacks->OnTypesRegistered  = OnTypesRegistered;
        callbacks->OnTypeRegistering  = OnTypeRegistering;
        callbacks->OnTypeRegistered   = OnTypeRegistered;
        Log("callbacks registered");
    }
    else
    {
        Log("WARNING: no callbacks pointer; this build only works under Native Loader");
    }

    g_started = true;
}

namespace
{
    DWORD WINAPI WorkerThread(LPVOID)
    {
        bool      wasDown   = false;
        ULONGLONG lastAvail = 0;
        bool      loggedRun = false;
        bool      wasAvail  = false;

        while (g_running.load())
        {
            Sleep(8);

            if (!g_started.load())
                continue;

            if (!loggedRun)
            {
                loggedRun = true;
                Log("worker running (game focused: %s)", GameIsForeground() ? "yes" : "no");
            }

            const bool down    = (GetAsyncKeyState(g_cfg.key) & 0x8000) != 0;
            const bool pressed = down && !wasDown;
            wasDown            = down;

            char       matched[kMaxPromptText]{};
            int        which          = -1;
            bool       available      = StrikeIsAvailable(&which, matched, sizeof matched);
            const ULONGLONG now       = GetTickCount64();
            if (available)
                lastAvail = now;

            if (available != wasAvail)
            {
                wasAvail = available;
                if (available)
                    Log("STRIKE offered (prompt %d) \"%s\"", which, matched);
                else
                    Log("STRIKE no longer offered");
            }

            if (!pressed)
                continue;

            if (!GameIsForeground())
            {
                if (g_cfg.logKeys)
                    Log("key pressed but the game is not the foreground window - ignored");
                continue;
            }

            const int  graceMs = g_cfg.widenPrompt ? g_cfg.graceMs : 0;
            const int  leadMs  = g_cfg.widenPrompt ? g_cfg.leadMs : 0;
            if (g_cfg.mode == "probe" || g_cfg.mode == "types" || g_cfg.mode == "watch" ||
                g_cfg.mode == "probe2")
            {
                Log("MARK key pressed (probe mode, nothing sent) available=%d",
                    available ? 1 : 0);
                if (g_cfg.mode == "watch" || g_cfg.mode == "probe2")
                    LogProbeState();
                Log("MARK end");
            }
            else if (g_cfg.mode == "direct")
            {
                Log("key pressed -> sending light attack (direct mode)");
                SendLightAttack(g_cfg.holdMs);
            }
            else if (g_cfg.mode == "smart")
            {
                auto inGraceNow = [&]() {
                    if (graceMs <= 0 || lastAvail == 0)
                        return false;
                    return (GetTickCount64() - lastAvail) <= (ULONGLONG)graceMs;
                };

                // Pressed a little early: wait for the prompt instead of turning into a light
                // attack. Nothing is sent if it never shows.
                ULONGLONG waited = 0;
                if (!available && leadMs > 0)
                {
                    const ULONGLONG start = GetTickCount64();
                    while (GetTickCount64() - start < (ULONGLONG)leadMs)
                    {
                        Sleep(4);
                        if (StrikeIsAvailable(&which, matched, sizeof matched))
                        {
                            available = true;
                            wasAvail  = true;
                            waited    = GetTickCount64() - start;
                            lastAvail = GetTickCount64();
                            break;
                        }
                    }
                }

                if (available)
                {
                    if (waited)
                        Log("key pressed -> \"%s\" appeared after %llums -> sending light attack",
                            matched, (unsigned long long)waited);
                    else
                        Log("key pressed -> \"%s\" is offered -> sending light attack", matched);
                    SendLightAttack(g_cfg.holdMs);
                }
                else if (inGraceNow())
                {
                    Log("key pressed -> window closed %llums ago, inside the %dms grace -> sending "
                        "light attack",
                        (unsigned long long)(GetTickCount64() - lastAvail), graceMs);
                    SendLightAttack(g_cfg.holdMs);
                }
                else
                {
                    Log("key pressed -> no silent strike offered -> nothing sent");
                }
            }
            else
            {
                Log("unknown mode '%s' - nothing sent", g_cfg.mode.c_str());
            }
        }

        return 0;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        g_dir  = DirectoryOf(instance);
        g_cfg  = LoadConfig(g_dir);
        CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        g_running = false;
        Log("unloaded");
        LogClose();
        break;
    }
    return TRUE;
}
