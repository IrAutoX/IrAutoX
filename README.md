# IrAutoX

A small 32-bit Windows launcher and runtime-hook prototype for the original PC release of Grand Theft Auto: San Andreas.

## Prototype features

- Native Win32 launcher with a saved GTA executable path.
- Starts `gta_sa.exe` suspended, injects `IrAutoXHook.dll`, then resumes the game.
- Uses MinHook to hook GTA 1.0 US `CGame::Process` at `0x53BEE0` and Direct3D 9 creation / `EndScene`.
- Press **F7** in-game to show or hide the IrAutoX test menu.
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

This is an early prototype. It hooks the Direct3D 9 rendering path and draws a simple in-game diagnostic menu; it does not yet implement game gameplay hooks, networking, anti-cheat, or a full UI framework. Test with a clean copy of GTA SA 1.0. The launcher does not include or distribute GTA game files.
