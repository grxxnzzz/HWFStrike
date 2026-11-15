#include "config.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>

static std::string Trim(std::string s)
{
    const char* ws = " \t\r\n\"";
    const auto  a  = s.find_first_not_of(ws);
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(ws);
    return s.substr(a, b - a + 1);
}

static std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static int ParseInt(const std::string& v, int fallback)
{
    try
    {
        return (int)std::stol(v, nullptr, 0); // accepts 0x42 and 66
    }
    catch (...)
    {
        return fallback;
    }
}

static bool ParseBool(const std::string& v, bool fallback)
{
    const auto s = Lower(v);
    if (s == "1" || s == "true" || s == "yes" || s == "on")
        return true;
    if (s == "0" || s == "false" || s == "no" || s == "off")
        return false;
    return fallback;
}

Config LoadConfig(const std::wstring& dllDirectory)
{
    Config cfg;

    std::wstring path = dllDirectory + L"\\HWFStrike.ini";
    std::ifstream file(path);
    if (!file)
        return cfg;

    std::string line;
    while (std::getline(file, line))
    {
        const auto comment = line.find_first_of(";#");
        if (comment != std::string::npos)
            line.erase(comment);

        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        const auto key = Lower(Trim(line.substr(0, eq)));
        const auto val = Trim(line.substr(eq + 1));
        if (key.empty() || val.empty())
            continue;

        if (key == "key")
            cfg.key = ParseInt(val, cfg.key);
        else if (key == "mode")
            cfg.mode = Lower(val);
        else if (key == "gracems")
            cfg.graceMs = ParseInt(val, cfg.graceMs);
        else if (key == "leadms")
            cfg.leadMs = ParseInt(val, cfg.leadMs);
        else if (key == "widenprompt")
            cfg.widenPrompt = ParseBool(val, cfg.widenPrompt);
        else if (key == "holdms")
            cfg.holdMs = ParseInt(val, cfg.holdMs);
        else if (key == "strikemarkers" || key == "strikemarker")
            cfg.strikeMarkers = val;
        else if (key == "debughooks")
            cfg.debugHooks = ParseBool(val, cfg.debugHooks);
        else if (key == "logkeys")
            cfg.logKeys = ParseBool(val, cfg.logKeys);
        else if (key == "dumptypes")
            cfg.dumpTypes = ParseBool(val, cfg.dumpTypes);
        else if (key == "typefilter")
            cfg.typeFilter = val;
        else if (key == "hookoffset")
            cfg.hookOffset = (uint32_t)ParseInt(val, (int)cfg.hookOffset);
    }

    if (cfg.holdMs < 1)
        cfg.holdMs = 1;
    if (cfg.graceMs < 0)
        cfg.graceMs = 0;
    if (cfg.leadMs < 0)
        cfg.leadMs = 0;

    return cfg;
}
