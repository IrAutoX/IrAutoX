#include <windows.h>
#include <d3d9.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>

using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using EndSceneFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using GameProcessFn = void (__cdecl*)();
using FrontEndFn = void (__thiscall*)(void*);

static Direct3DCreate9Fn originalDirect3DCreate9{};
static CreateDeviceFn originalCreateDevice{};
static EndSceneFn originalEndScene{};
static GameProcessFn originalGameProcess{};
static FrontEndFn originalDrawFrontEnd{};
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

static const uint8_t* Glyph(char c) {
    static const uint8_t blank[7]{};
    static const uint8_t colon[7] = {0,0,4,0,0,4,0};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t letters[36][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return letters[26 + c - '0'];
    if (c == ':') return colon;
    if (c == '-') return dash;
    return blank;
}

static void Text(IDirect3DDevice9* device, float x, float y, const char* value, float scale, DWORD color) {
    for (const char* p = value; *p; ++p) {
        const auto rows = Glyph(*p);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (rows[row] & (1 << (4 - col)))
                    Quad(device, x + col * scale, y + row * scale, scale, scale, color);
        x += 6 * scale;
    }
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

static bool Button(IDirect3DDevice9* device, float x, float y, float w, float h,
                   float mouseX, float mouseY, bool mouseValid, const char* label) {
    const bool hovered = mouseValid && mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    const DWORD fill = hovered ? D3DCOLOR_ARGB(245, 226, 75, 55) : D3DCOLOR_ARGB(225, 28, 30, 34);
    Quad(device, x, y, w, h, fill);
    Quad(device, x, y, 3, h, D3DCOLOR_ARGB(255, 226, 75, 55));
    Text(device, x + 20, y + (h - 14) * 0.5f, label, 2.0f,
         hovered ? D3DCOLOR_ARGB(255, 255, 255, 255) : D3DCOLOR_ARGB(255, 225, 225, 220));
    return hovered && (GetAsyncKeyState(VK_LBUTTON) & 1);
}

static void DrawCustomMenu(IDirect3DDevice9* device, bool mainMenu) {
    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport))) return;
    AcquireMenuCursor();

    IDirect3DStateBlock9* state{};
    if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &state))) state->Capture();

    device->SetTexture(0, nullptr);
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    const float width = static_cast<float>(viewport.Width);
    const float height = static_cast<float>(viewport.Height);
    float mouseX{}, mouseY{};
    const bool mouseValid = MousePosition(mouseX, mouseY);

    Quad(device, 0, 0, width, height, D3DCOLOR_ARGB(205, 8, 10, 13));
    Quad(device, 0, height * 0.83f, width, height * 0.17f, D3DCOLOR_ARGB(100, 0, 0, 0));
    Quad(device, width * 0.065f, height * 0.12f, 5, height * 0.72f, D3DCOLOR_ARGB(255, 226, 75, 55));

    Text(device, width * 0.09f, height * 0.16f, "IRAUTOX", 4.0f, D3DCOLOR_ARGB(255, 245, 242, 232));
    Text(device, width * 0.092f, height * 0.25f, "SAN ANDREAS", 1.8f, D3DCOLOR_ARGB(255, 226, 75, 55));
    Text(device, width * 0.092f, height * 0.30f, "COMMUNITY EDITION", 1.2f, D3DCOLOR_ARGB(220, 205, 207, 205));

    const float bx = width * 0.09f;
    const float by = height * 0.43f;
    const float bw = width * 0.27f;
    const float bh = height * 0.075f;
    bool clicked = false;

    if (mainMenu && !InterlockedCompareExchange(&settingsPage, 0, 0)) {
        clicked = Button(device, bx, by, bw, bh, mouseX, mouseY, mouseValid, "PLAY");
        if (!clicked) clicked = Button(device, bx, by + bh + 12, bw, bh, mouseX, mouseY, mouseValid, "SETTINGS");
        if (!clicked) clicked = Button(device, bx, by + (bh + 12) * 2, bw, bh, mouseX, mouseY, mouseValid, "EXIT");
        if (clicked) {
            if (mouseY >= by + (bh + 12) * 2) {
                PostMessageW(gameWindow, WM_CLOSE, 0, 0);
            } else if (mouseY >= by + bh + 12) {
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
        Text(device, bx, by, "SETTINGS", 2.0f, D3DCOLOR_ARGB(255, 245, 242, 232));
        Text(device, bx, by + 45, "GRAPHICS AND AUDIO", 1.3f, D3DCOLOR_ARGB(255, 190, 194, 194));
        if (Button(device, bx, by + 100, bw, bh, mouseX, mouseY, mouseValid, "BACK"))
            InterlockedExchange(&settingsPage, 0);
    } else {
        Text(device, bx, by, "IRAUTOX OVERLAY", 2.0f, D3DCOLOR_ARGB(255, 245, 242, 232));
        Text(device, bx, by + 45, "F7 TO CLOSE", 1.3f, D3DCOLOR_ARGB(255, 190, 194, 194));
    }

    Text(device, width * 0.09f, height * 0.91f, "A NEW WAY TO PLAY SAN ANDREAS", 1.1f,
         D3DCOLOR_ARGB(190, 210, 210, 205));

    if (state) {
        state->Apply();
        state->Release();
    }
}

static void __fastcall HookDrawFrontEnd(void* self, void*) {
    if (InterlockedCompareExchange(&customMainMenuVisible, 0, 0)) return;
    originalDrawFrontEnd(self);
}

static HRESULT STDMETHODCALLTYPE HookEndScene(IDirect3DDevice9* device) {
    if (GetAsyncKeyState(VK_F7) & 1) {
        const LONG next = InterlockedXor(&menuVisible, 1) ^ 1;
        if (next) AcquireMenuCursor();
        else ReleaseMenuCursor();
    }
    const bool customMain = InterlockedCompareExchange(&customMainMenuVisible, 0, 0) != 0;
    const bool overlay = InterlockedCompareExchange(&menuVisible, 0, 0) != 0;
    if (customMain || overlay) DrawCustomMenu(device, customMain);
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
}

static void InstallFrontEndHook() {
    HMODULE game = GetModuleHandleW(nullptr);
    void* address = reinterpret_cast<void*>(0x57C290);
    MEMORY_BASIC_INFORMATION memory{};
    if (reinterpret_cast<uintptr_t>(game) != 0x400000 ||
        !VirtualQuery(address, &memory, sizeof(memory)) || memory.AllocationBase != game ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        OutputDebugStringW(L"[IrAutoX] GTA front-end draw hook validation failed.\\n");
        InterlockedExchange(&customMainMenuVisible, 0);
        return;
    }
    if (MH_CreateHook(address, &HookDrawFrontEnd, reinterpret_cast<void**>(&originalDrawFrontEnd)) == MH_OK &&
        MH_EnableHook(address) == MH_OK) {
        OutputDebugStringW(L"[IrAutoX] Custom GTA front-end menu hook installed.\\n");
    } else {
        InterlockedExchange(&customMainMenuVisible, 0);
        OutputDebugStringW(L"[IrAutoX] Could not install custom front-end menu hook.\\n");
    }
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
    InstallFrontEndHook();
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
        MH_Uninitialize();
    }
    return TRUE;
}
