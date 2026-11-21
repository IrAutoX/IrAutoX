#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using EndSceneFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using GameProcessFn = void (__cdecl*)();
using GameMemberFn = void (__thiscall*)(void*);

static Direct3DCreate9Fn originalDirect3DCreate9{};
static CreateDeviceFn originalCreateDevice{};
static EndSceneFn originalEndScene{};
static GameProcessFn originalGameProcess{};
static GameMemberFn originalRenderSplash{};
static GameMemberFn originalRenderLoadingBar{};
static IDirect3DTexture9* uiTexture{};
static HDC uiDC{};
static HBITMAP uiBitmap{};
static HGDIOBJ oldBitmap{};
static void* uiPixels{};
static int uiWidth{};
static int uiHeight{};
static volatile LONG customMainMenuVisible = 1;
static volatile LONG settingsPage = 0;
static HWND gameWindow{};
static bool cursorAcquired = false;
static bool gameHookInstalled = false;
static volatile LONG menuVisible = 0;
static bool deviceHookInstalled = false;

struct Vertex {
    float x, y, z, rhw;
    DWORD color;
};

static void Quad(IDirect3DDevice9* device, float x, float y, float w, float h, DWORD color) {
    Vertex v[] = {
        {x, y, 0, 1, color}, {x + w, y, 0, 1, color}, {x + w, y + h, 0, 1, color},
        {x, y, 0, 1, color}, {x + w, y + h, 0, 1, color}, {x, y + h, 0, 1, color}
    };
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(Vertex));
}

static void AcquireMenuCursor() {
    if (!cursorAcquired) {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
        cursorAcquired = true;
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

static void ReleaseMenuCursor() {
    if (cursorAcquired) {
        while (ShowCursor(FALSE) >= 0) {}
        cursorAcquired = false;
    }
}

static bool MousePosition(float& x, float& y) {
    POINT point{};
    if (!GetCursorPos(&point) || !gameWindow || !ScreenToClient(gameWindow, &point)) return false;
    x = static_cast<float>(point.x);
    y = static_cast<float>(point.y);
    return true;
}

static void ReleaseUIResources() {
    if (uiTexture) { uiTexture->Release(); uiTexture = nullptr; }
    if (uiDC) {
        if (oldBitmap) SelectObject(uiDC, oldBitmap);
        if (uiBitmap) DeleteObject(uiBitmap);
        DeleteDC(uiDC);
    }
    uiDC = nullptr;
    uiBitmap = nullptr;
    oldBitmap = nullptr;
    uiPixels = nullptr;
    uiWidth = uiHeight = 0;
}

static bool EnsureUIResources(IDirect3DDevice9* device, int width, int height) {
    if (uiTexture && uiWidth == width && uiHeight == height) return true;
    ReleaseUIResources();
    if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, &uiTexture, nullptr))) return false;
    uiDC = CreateCompatibleDC(nullptr);
    if (!uiDC) { ReleaseUIResources(); return false; }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    uiBitmap = CreateDIBSection(uiDC, &info, DIB_RGB_COLORS, &uiPixels, nullptr, 0);
    if (!uiBitmap || !uiPixels) { ReleaseUIResources(); return false; }
    oldBitmap = SelectObject(uiDC, uiBitmap);
    uiWidth = width;
    uiHeight = height;
    return true;
}

static void Fill(HDC dc, int x, int y, int w, int h, COLORREF color) {
    RECT r{x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void DrawLabel(HDC dc, const wchar_t* value, int x, int y, int size,
                      COLORREF color, int weight = FW_NORMAL) {
    HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Bahnschrift");
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutW(dc, x, y, value, static_cast<int>(wcslen(value)));
    SelectObject(dc, previous);
    DeleteObject(font);
}

static bool DrawMenuButton(HDC dc, int x, int y, int w, int h, int mx, int my,
                           bool mouseValid, const wchar_t* label) {
    const bool hover = mouseValid && mx >= x && mx <= x + w && my >= y && my <= y + h;
    const COLORREF fill = hover ? RGB(183, 54, 42) : RGB(27, 30, 33);
    Fill(dc, x, y, w, h, fill);
    Fill(dc, x, y, 4, h, RGB(229, 89, 59));
    HPEN pen = CreatePen(PS_SOLID, 1, hover ? RGB(244, 127, 91) : RGB(69, 73, 76));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    DrawLabel(dc, label, x + 24, y + (h - 24) / 2, 24, RGB(245, 243, 235), FW_SEMIBOLD);
    return hover && (GetAsyncKeyState(VK_LBUTTON) & 1);
}

static void RenderMenuTexture(IDirect3DDevice9* device, bool mainMenu) {
    D3DVIEWPORT9 vp{};
    if (FAILED(device->GetViewport(&vp)) || !EnsureUIResources(device, static_cast<int>(vp.Width), static_cast<int>(vp.Height)))
        return;

    const int w = static_cast<int>(vp.Width);
    const int h = static_cast<int>(vp.Height);
    Fill(uiDC, 0, 0, w, h, RGB(10, 13, 16));
    for (int y = 0; y < h; y += 4) {
        const int shade = 12 + (y * 13 / std::max(1, h));
        Fill(uiDC, 0, y, w, std::min(4, h - y), RGB(shade, shade + 2, shade + 3));
    }

    Fill(uiDC, 0, 0, w, 7, RGB(224, 76, 54));
    Fill(uiDC, static_cast<int>(w * 0.065f), static_cast<int>(h * 0.12f), 5,
         static_cast<int>(h * 0.72f), RGB(224, 76, 54));

    DrawLabel(uiDC, L"IRAutoX", static_cast<int>(w * 0.09f), static_cast<int>(h * 0.13f),
              std::max(36, h / 17), RGB(246, 244, 237), FW_BOLD);
    DrawLabel(uiDC, L"SAN ANDREAS", static_cast<int>(w * 0.092f), static_cast<int>(h * 0.235f),
              std::max(19, h / 39), RGB(231, 91, 65), FW_BOLD);
    DrawLabel(uiDC, L"COMMUNITY EDITION  /  OFFLINE", static_cast<int>(w * 0.092f),
              static_cast<int>(h * 0.285f), std::max(13, h / 57), RGB(174, 180, 181));

    POINT pt{};
    const bool mouseValid = GetCursorPos(&pt) && gameWindow && ScreenToClient(gameWindow, &pt);
    const int mx = mouseValid ? pt.x : -1;
    const int my = mouseValid ? pt.y : -1;
    const int bx = static_cast<int>(w * 0.09f);
    const int by = static_cast<int>(h * 0.42f);
    const int bw = std::min(static_cast<int>(w * 0.30f), 440);
    const int bh = std::max(52, static_cast<int>(h * 0.075f));
    bool clicked = false;

    if (mainMenu && !InterlockedCompareExchange(&settingsPage, 0, 0)) {
        clicked = DrawMenuButton(uiDC, bx, by, bw, bh, mx, my, mouseValid, L"PLAY");
        if (!clicked) clicked = DrawMenuButton(uiDC, bx, by + bh + 14, bw, bh, mx, my, mouseValid, L"SETTINGS");
        if (!clicked) clicked = DrawMenuButton(uiDC, bx, by + (bh + 14) * 2, bw, bh, mx, my, mouseValid, L"EXIT");
        if (clicked) {
            if (my >= by + (bh + 14) * 2) {
                PostMessageW(gameWindow, WM_CLOSE, 0, 0);
            } else if (my >= by + bh + 14) {
                InterlockedExchange(&settingsPage, 1);
            } else {
                InterlockedExchange(&customMainMenuVisible, 0);
                ReleaseMenuCursor();
                INPUT input{};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = VK_RETURN;
                SendInput(1, &input, sizeof(input));
                input.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &input, sizeof(input));
            }
        }
    } else if (mainMenu) {
        DrawLabel(uiDC, L"SETTINGS", bx, by, 28, RGB(245, 243, 235), FW_BOLD);
        DrawLabel(uiDC, L"DISPLAY  /  AUDIO  /  CONTROLS", bx, by + 48, 18, RGB(178, 184, 184));
        if (DrawMenuButton(uiDC, bx, by + 100, bw, bh, mx, my, mouseValid, L"BACK"))
            InterlockedExchange(&settingsPage, 0);
    } else {
        DrawLabel(uiDC, L"IRAUTOX OVERLAY", bx, by, 28, RGB(245, 243, 235), FW_BOLD);
        DrawLabel(uiDC, L"PRESS F7 TO CLOSE", bx, by + 48, 18, RGB(178, 184, 184));
    }

    DrawLabel(uiDC, L"A NEW WAY TO PLAY SAN ANDREAS", static_cast<int>(w * 0.09f),
              static_cast<int>(h * 0.91f), std::max(12, h / 65), RGB(167, 173, 173));

    D3DLOCKED_RECT locked{};
    if (SUCCEEDED(uiTexture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD))) {
        const auto* src = static_cast<const uint8_t*>(uiPixels);
        auto* dst = static_cast<uint8_t*>(locked.pBits);
        for (int y = 0; y < h; ++y)
            memcpy(dst + y * locked.Pitch, src + y * w * 4, static_cast<size_t>(w) * 4);
        uiTexture->UnlockRect(0);
    } else {
        return;
    }

    IDirect3DStateBlock9* state{};
    if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &state))) state->Capture();
    struct TVertex { float x, y, z, rhw; DWORD color; float u, v; };
    TVertex quad[] = {
        {0, 0, 0, 1, 0xFFFFFFFF, 0, 0},
        {static_cast<float>(w), 0, 0, 1, 0xFFFFFFFF, 1, 0},
        {static_cast<float>(w), static_cast<float>(h), 0, 1, 0xFFFFFFFF, 1, 1},
        {0, 0, 0, 1, 0xFFFFFFFF, 0, 0},
        {static_cast<float>(w), static_cast<float>(h), 0, 1, 0xFFFFFFFF, 1, 1},
        {0, static_cast<float>(h), 0, 1, 0xFFFFFFFF, 0, 1}
    };
    device->SetTexture(0, uiTexture);
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(TVertex));
    if (state) { state->Apply(); state->Release(); }
}

static void __fastcall HookRenderSplash(void* self, void*) {
    (void)self;
}

static void __fastcall HookRenderLoadingBar(void* self, void*) {
    (void)self;
}

static HRESULT STDMETHODCALLTYPE HookEndScene(IDirect3DDevice9* device) {
    if (GetAsyncKeyState(VK_F7) & 1) {
        const LONG next = InterlockedXor(&menuVisible, 1) ^ 1;
        if (next) AcquireMenuCursor();
        else ReleaseMenuCursor();
    }
    const bool customMain = InterlockedCompareExchange(&customMainMenuVisible, 0, 0) != 0;
    const bool overlay = InterlockedCompareExchange(&menuVisible, 0, 0) != 0;
    if (customMain || overlay) RenderMenuTexture(device, customMain);
    else ReleaseMenuCursor();
    return originalEndScene(device);
}
static HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                                                    D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** output) {
    gameWindow = focus;
    const HRESULT result = originalCreateDevice(d3d, adapter, type, focus, behavior, parameters, output);
    if (SUCCEEDED(result) && output && *output && !deviceHookInstalled) {
        void** table = *reinterpret_cast<void***>(*output);
        if (MH_CreateHook(table[42], &HookEndScene, reinterpret_cast<void**>(&originalEndScene)) == MH_OK) {
            MH_EnableHook(table[42]);
            deviceHookInstalled = true;
            OutputDebugStringW(L"[IrAutoX] Direct3D EndScene hook installed. Press F7.\n");
        }
    }
    return result;
}

static IDirect3D9* WINAPI HookDirect3DCreate9(UINT sdk) {
    IDirect3D9* d3d = originalDirect3DCreate9(sdk);
    if (d3d && !originalCreateDevice) {
        void** table = *reinterpret_cast<void***>(d3d);
        if (MH_CreateHook(table[16], &HookCreateDevice, reinterpret_cast<void**>(&originalCreateDevice)) == MH_OK)
            MH_EnableHook(table[16]);
    }
    return d3d;
}


static void AddGameMoney() {
    HMODULE game = GetModuleHandleW(nullptr);
    void* address = reinterpret_cast<void*>(0xB7CE50);
    MEMORY_BASIC_INFORMATION memory{};
    if (!game || reinterpret_cast<uintptr_t>(game) != 0x400000 ||
        !VirtualQuery(address, &memory, sizeof(memory)) ||
        memory.AllocationBase != game ||
        (memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        return;
    }
    auto* money = reinterpret_cast<volatile LONG*>(address);
    InterlockedExchangeAdd(money, 10000);
    OutputDebugStringW(L"[IrAutoX] Added $10,000 to the player money value.\\n");
}

static void __cdecl HookGameProcess() {
    originalGameProcess();
    if (GetAsyncKeyState(VK_F8) & 1) AddGameMoney();
    HMODULE game = GetModuleHandleW(nullptr);
    if (game && reinterpret_cast<uintptr_t>(game) == 0x400000) {
        auto* menuVisibleFlag = reinterpret_cast<volatile BYTE*>(0xBA67A4);
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(const_cast<BYTE*>(menuVisibleFlag), &info, sizeof(info)) &&
            info.AllocationBase == game && (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)))
            *menuVisibleFlag = 0;
    }
}

static void InstallLoadingHooks() {
    HMODULE game = GetModuleHandleW(nullptr);
    if (!game || reinterpret_cast<uintptr_t>(game) != 0x400000) return;
    const uintptr_t addresses[] = {0x58FF60, 0x590370};
    void* hooks[] = {reinterpret_cast<void*>(&HookRenderSplash), reinterpret_cast<void*>(&HookRenderLoadingBar)};
    void** originals[] = {reinterpret_cast<void**>(&originalRenderSplash), reinterpret_cast<void**>(&originalRenderLoadingBar)};
    for (int i = 0; i < 2; ++i) {
        MEMORY_BASIC_INFORMATION memory{};
        void* target = reinterpret_cast<void*>(addresses[i]);
        if (!VirtualQuery(target, &memory, sizeof(memory)) || memory.AllocationBase != game ||
            (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
            continue;
        if (MH_CreateHook(target, hooks[i], originals[i]) == MH_OK) MH_EnableHook(target);
    }
    auto* menuVisibleFlag = reinterpret_cast<volatile BYTE*>(0xBA67A4);
    MEMORY_BASIC_INFORMATION data{};
    if (VirtualQuery(const_cast<BYTE*>(menuVisibleFlag), &data, sizeof(data)) &&
        data.AllocationBase == game && (data.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)))
        *menuVisibleFlag = 0;
}

static void InstallGameHook() {
    HMODULE game = GetModuleHandleW(nullptr);
    if (reinterpret_cast<uintptr_t>(game) != 0x400000) {
        OutputDebugStringW(L"[IrAutoX] Unsupported executable base; GTA 1.0 US game hook skipped.\n");
        return;
    }
    MEMORY_BASIC_INFORMATION memory{};
    void* address = reinterpret_cast<void*>(0x53BEE0);
    if (!VirtualQuery(address, &memory, sizeof(memory)) || memory.AllocationBase != game ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        OutputDebugStringW(L"[IrAutoX] CGame::Process address validation failed.\n");
        return;
    }
    if (MH_CreateHook(address, &HookGameProcess, reinterpret_cast<void**>(&originalGameProcess)) == MH_OK &&
        MH_EnableHook(address) == MH_OK) {
        gameHookInstalled = true;
        OutputDebugStringW(L"[IrAutoX] GTA CGame::Process hook installed at 0x53BEE0.\n");
    }
}

static DWORD WINAPI InitializeHooks(void*) {
    if (MH_Initialize() != MH_OK) return 0;
    InstallGameHook();
    InstallLoadingHooks();
    HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
    if (!d3d9) return 0;
    auto target = reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9"));
    if (!target) return 0;
    if (MH_CreateHook(target, &HookDirect3DCreate9, reinterpret_cast<void**>(&originalDirect3DCreate9)) == MH_OK)
        MH_EnableHook(target);
    OutputDebugStringW(L"[IrAutoX] D3D9 API hook initialized.\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, InitializeHooks, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        ReleaseUIResources();
        ReleaseMenuCursor();
        MH_Uninitialize();
    }
    return TRUE;
}
