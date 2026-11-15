# Building and configuring HWFStrike

This document is for source users and maintainers. Normal players do not need to compile the mod;
they should install the release archive and edit the installed INI if desired.

## Build requirements

- Windows x64
- Visual Studio 2022 or Visual Studio 2022 Build Tools
- The **Desktop development with C++** workload
- A Windows SDK selected in the Visual Studio Installer

The build script searches common Visual Studio 2022 installation locations and does not depend on
any particular user's directory.

## Build

Close the game, open Command Prompt or PowerShell in this repository, and run:

```text
build.cmd
```

The output is:

```text
build\HWFStrike.dll
```

The script compiles the bundled MinHook C sources and the HWFStrike C++ sources with MSVC x64.

## Configuration

Copy:

```text
HWFStrike.ini.example -> HWFStrike.ini
```

The main settings are:

```ini
[Key]
Key = 0x42

[Key]
Mode = smart

[Strike]
StrikeMarkers = Stealth_Kill
WidenPrompt = true
GraceMs = 120
LeadMs = 100
HoldMs = 30
```

Available modes are `smart`, `direct`, `watch`, `probe`, and `types`. `smart` is the normal mode.
`direct` always sends a light attack and is for input testing only. `watch`, `probe`, and `types`
are diagnostic modes and do not send gameplay input.

`Key` accepts Windows virtual-key codes. Examples:

```ini
Key = 0x42   # B
Key = 0x56   # V
Key = 0x01   # left mouse button
Key = 0x02   # right mouse button
Key = 0x05   # X1 mouse button
```

Configuration changes do not require recompiling. Restart the game after changing the INI.

## Local test installation

With the game closed, replace `<game>` with the user's game installation directory:

```powershell
$game = "<game>"
New-Item -ItemType Directory -Force "$game\nativemods\HWFStrike" | Out-Null
Copy-Item ".\build\HWFStrike.dll" "$game\nativemods\HWFStrike\HWFStrike.dll" -Force
Copy-Item ".\HWFStrike.ini" "$game\nativemods\HWFStrike\HWFStrike.ini" -Force
```

Do not copy the Native Loader `winmm.dll` from this repository; install Native Loader separately.
Do not overwrite the DLL while the game is running.

## Release checklist

1. Update the version string in `build.cmd`.
2. Build the DLL.
3. Test on the supported game build.
4. Confirm the hook installs and the Silent Strike marker is detected.
5. Package only the compiled DLL and INI in the release archive.
6. Do not package logs, memory snapshots, RTTI dumps, game files, `winmm.dll`, or private local
   tooling.

## Privacy checklist

Before pushing a commit, check that it contains none of the following:

- Absolute paths containing a username or local game installation path.
- Save files, profile files, crash dumps, or extracted game assets.
- Runtime logs, memory snapshots, RTTI dumps, or process captures.
- API keys, access tokens, passwords, or `.env` files.
- Native Loader's `winmm.dll`.

This repository intentionally excludes the development recon scripts and runtime snapshots used to
calibrate the supported build.
