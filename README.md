# IrAutoX

A small 32-bit Windows launcher and runtime-hook prototype for the original PC release of Grand Theft Auto: San Andreas.

## Prototype features

- Native Win32 launcher with a saved GTA executable path.
- Starts `gta_sa.exe` suspended, injects `IrAutoXHook.dll`, then resumes the game.
- Uses MinHook to hook GTA 1.0 US `CGame::Process` at `0x53BEE0` and Direct3D 9 creation / `EndScene`.
- Select any 32-bit executable file; the launcher no longer requires the filename to be exactly `gta_sa.exe`.
- Starts with a custom full-screen IrAutoX front-end styled with a dark, cinematic GTA-inspired palette; the stock front-end drawing is suppressed on the supported GTA SA 1.0 US executable.
- The custom front-end has mouse-hover and click support, with PLAY, SETTINGS, and EXIT entries; the Windows cursor is shown while the custom menu is active.
- Press **F7** in-game to show or hide the IrAutoX overlay menu.
- Press **F8** in GTA SA 1.0 US to add $10,000 to the player's in-game money (offline test feature).
- No multiplayer connection or server code is included yet.

## Requirements

- Windows 10/11, 32-bit GTA SA process.
- Original GTA San Andreas PC executable (GTA SA 1.0 US recommended).
- Visual Studio 2022 with the Desktop development with C++ workload.
- CMake 3.21+ and an internet connection for the MinHook dependency at configure time.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

The output files are:

- `build/Release/IrAutoX.exe`
- `build/Release/IrAutoXHook.dll`

Put both files in the same directory as `gta_sa.exe`, run the launcher, select the game executable, and press **Launch GTA + Inject**.

## Notes

This is an early prototype. The custom front-end and menu hooks target the original GTA SA 1.0 US executable at fixed addresses and are not compatible with other game builds without address updates. PLAY hands control back to the game's front-end input path; verify the new-game transition in-game. The Direct3D 9 overlay and mouse interaction are experimental. The F8 money-value write is an offline test feature; networking, anti-cheat, and a full UI framework are not implemented. Test with a clean copy of GTA SA 1.0. The launcher does not include or distribute GTA game files.
