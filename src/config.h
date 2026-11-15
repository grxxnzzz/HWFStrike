#pragma once

// Minimal ini reader/writer-free config for HWFStrike.
// Format: key=value, ';' or '#' starts a comment. Keys are matched case-insensitively.

#include <string>

struct Config
{
    // [Key]
    int         key          = 0x42;   // 0x42 = 'B'. Win32 virtual-key code; 0x01-0x06 are mouse buttons.
    std::string mode         = "smart"; // smart | direct | probe

    // [Strike]
    // The game shows a prompt for the contextual action it is offering. For the Silent Strike that
    // prompt text is "<localised name> <icon name=Stealth_Kill>". The icon name is not localised,
    // so matching it works in every language. Comma-separated alternatives are allowed, and the
    // localised wording (e.g. "Бесшумная") works too.
    std::string strikeMarkers = "Stealth_Kill";
    bool        widenPrompt  = true;   // hold the window open briefly after the prompt drops
    int         graceMs      = 120;    // ... for this long. Keep it small: it is for prompt flicker.
    int         leadMs       = 100;    // a press this far before the prompt appears still strikes
    int         holdMs       = 30;     // how long the injected light-attack press is held

    // [Debug]
    bool        debugHooks   = false;  // add the read-only investigation hooks (log noise, no need in play)

    // [General]
    bool        logKeys      = true;   // log every key press the mod sees while the game is focused

    // [Types]
    bool        dumpTypes    = true;   // write <dll dir>\types.txt with the game's RTTI type names
    std::string typeFilter   = "Contextual,Silent,Stealth,Kill,Player"; // names to echo into the log

    // [Hook]
    uint32_t    hookOffset   = 0x0F0B3E0; // MsgGetPotentialContextualActionsOnVictim handler, build 1.5.80.0
};

// Loads <dll dir>\HWFStrike.ini. Missing file or missing keys keep the defaults above.
Config LoadConfig(const std::wstring& dllDirectory);
