# IrAutoX / iRx 1.2

Compact Win32 SA-MP launcher with an embedded web UI and native C++ host.

- HTML and minified Tailwind CSS are generated into a C++ header and embedded in IrAutoX.exe. Release ZIP contains only the EXE; no UI folder is distributed.
- Borderless custom window, startup splash, gold/black dark theme and white/gold light theme following Windows appearance.
- Live open.mp masterlist with IP, port, hostname, game mode, player counts, search and favorites.
- Automatic GTA SA, samp.exe and sampcmd.exe detection using common install folders and registry keys.
- Connects with sampcmd documented arguments; falls back to samp.exe command line when sampcmd is absent.
- Optional native Discord Rich Presence module. Configure HKEY_CURRENT_USER\\Software\\IrAutoX\\DiscordAppId (REG_SZ) with your Discord application's ID.

## Requirements

Windows 10/11, 32-bit GTA SA and SA-MP, Microsoft Edge WebView2 Evergreen Runtime. Internet is used for open.mp server data and remote icon/font assets.

## Build

Install Visual Studio 2022 Desktop C++, CMake, Python 3.12+, Node.js 22+ and NuGet.

    npm install
    nuget install Microsoft.Web.WebView2 -Version 1.0.2903.40 -OutputDirectory packages
    cmake -S . -B build -A Win32 -DWEBVIEW2_ROOT=<WebView2 NuGet directory>
    npm run build:css
    cmake --build build --config Release

Output: build/Release/IrAutoX.exe. The UI source remains modular for development but is embedded during compilation.
