# HWFStrike

`Native` mod for Horizon Forbidden West Complete Edition on PC that gives the contextual `Silent
Strike` (stealth kill action) its own button.

> also its funny how AI misspelled HFW (horizon forbidden west) as HWF (horizon west forbidden) XD

### *<!> AI disclosure*

*This project was created with assistance from an AI coding agent. The author reviewed the source,
configuration, packaging, and in-game behaviour. No AI-generated art, audio, voice, or game assets
are included.*

## Features

- Default dedicated button: **B**.
- Configurable keyboard or mouse button.
- Does nothing when no `Silent Strike` is offered, preventing accidental light attacks.
- Detects the language-independent `Stealth_Kill` prompt marker.
- Optional short grace window for prompt flicker.
- Uses `Native Loader` and `MinHook`.
- Single-player/offline use only.

## Requirements

- Horizon Forbidden West Complete Edition PC, build `1.5.80.0`.
- `Native Loader` for Horizon Forbidden West.
- Windows 10/11 x64.
- Visual Studio or Visual Studio Build Tools with the Desktop development with C++ workload, if
  building from source.

Native Loader is a separate dependency and is intentionally not included in this repository. The
game's `winmm.dll` and any game files are never part of this source tree.

## Installation for users

Use the release archive rather than building from source. Install `Native Loader` first, then extract
the release archive into the game's main directory:

```text
<game_folder>\nativemods\HWFStrike\HWFStrike.dll
<game_folder>\nativemods\HWFStrike\HWFStrike.ini
```

The optional Native Loader `console` marker is not included. The mod still writes its own log to
`<game>\nativemods\HWFStrike\HWFStrike.log`.

## Source build

1. Clone or download this repository.
2. Open a normal Command Prompt or PowerShell window.
3. Run `build.cmd` from the repository root.
4. The compiled DLL will be written to `build\HWFStrike.dll`.
5. With the game closed, copy the DLL and `HWFStrike.ini.example` (renamed to `HWFStrike.ini`)
   into `<game>\nativemods\HWFStrike\`.

See [`BUILDING.md`](BUILDING.md) for detailed build, configuration, testing, and release guidance.

## How it works

Silent Strike is a contextual action, not an ordinary remappable action. The mod hooks the game's
action-prompt handler for the supported build. After the original handler runs, it copies the
temporary prompt text and looks for:

```text
<icon name=Stealth_Kill>
```

When that marker is present, the configured button sends one normal light-attack input. The game
then performs its own contextual action. When the marker is absent, the button is ignored.

The hook is protected by a build-specific byte signature and refuses to install if the target code
does not match the supported build.

## Repository contents

```text
src/                         HWFStrike source code
third_party/minhook/         MinHook source and license
HWFStrike.ini.example        example user configuration
build.cmd                    Windows MSVC build script
BUILDING.md                  configuration and maintainer guide
LICENSE                      license for HWFStrike source
```

## Safety and scope

This project is intended for the user's own offline/single-player game. It does not bypass DRM,
ownership checks, anti-cheat, or multiplayer protections. Do not use it in online play or against
other players.

## Credits and licenses

- `Native Loader`: loading and callback interface dependency.
- `MinHook`: bundled under its own license in `third_party/minhook/LICENSE.txt`.
- Horizon Forbidden West: Guerrilla Games, Nixxes Software, and Sony Interactive Entertainment.

**HWFStrike** source is available under the MIT License. Game code, game assets, and Native Loader are
not included and remain under their respective licenses.
