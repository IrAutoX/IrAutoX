# IrAutoX / iRx

A lightweight Windows SA-MP launcher with a custom WebView2 interface.

## Features

- Rounded desktop window and animated startup screen.
- Dark violet server browser with Lucide icons.
- Live server list from the open.mp public masterlist API.
- Search, player sorting, favorites and recently played filters.
- Persistent GTA San Andreas, sampcmd.exe and nickname settings.
- Connects through the selected `sampcmd.exe` using `-c -h HOST -p PORT -n NICKNAME`.
- Native 32-bit Windows host with WebView2; no Qt, Electron or Node runtime shipped.

## Requirements

- Windows 10/11 x86 or x64.
- 32-bit GTA San Andreas installation.
- SA-MP installed and `sampcmd.exe` copied into the same folder as `gta_sa.exe`.
- Microsoft Edge WebView2 Evergreen Runtime.
- Internet connection for the live server list and remote font/icon assets.

## Build

Install Visual Studio 2022 with Desktop development with C++, CMake and NuGet.

```powershell
nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40 -OutputDirectory packages
cmake -S . -B build -A Win32 -DWEBVIEW2_ROOT="$((Get-ChildItem packages/Microsoft.Web.WebView2.* -Directory | Select-Object -First 1).FullName)"
cmake --build build --config Release
```

The executable is `build/Release/IrAutoX.exe`. Keep the `ui` folder beside it. The GitHub Actions workflow also creates a compressed `IrAutoX-win32.zip` artifact.

## sampcmd

This launcher uses the external sampcmd utility. Its documented command format is:

```
sampcmd.exe -c -h 127.0.0.1 -p 7777 -n Username
```

The launcher does not include SA-MP, GTA San Andreas, sampcmd, or game assets. Install these separately and select the executable paths in iRx.

## Credits

The layout takes inspiration from the interaction patterns of the open.mp launcher. This project is an independent implementation and does not copy its source code. Lucide icons are provided by Lucide.
