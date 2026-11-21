# IrAutoX

A small 32-bit Windows launcher and runtime-hook prototype for the original PC release of Grand Theft Auto: San Andreas.

## Prototype features

- Native Win32 launcher with a saved GTA executable path.
- Starts `gta_sa.exe` suspended, injects `IrAutoXHook.dll`, then resumes the game.
- Uses MinHook to hook GTA 1.0 US `CGame::Process` at `0x53BEE0` and Direct3D 9 creation / `EndScene`.
- Select any 32-bit executable file; the launcher no longer requires the filename to be exactly `gta_sa.exe`.
- Press **F7** in-game to show or hide the IrAutoX test menu.
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

This is an early prototype. It hooks the Direct3D 9 rendering path and draws a simple in-game diagnostic menu; it includes an experimental GTA 1.0 US money-value write on F8; networking, anti-cheat, and a full UI framework are not implemented. Test with a clean copy of GTA SA 1.0. The launcher does not include or distribute GTA game files.
