#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <filesystem>
#include <iterator>

static HWND g_path;
static HWND g_status;
static std::wstring g_dllPath;

static void SetStatus(const std::wstring& value) {
    SetWindowTextW(g_status, value.c_str());
}

static std::wstring ReadPath() {
    wchar_t buffer[32768]{};
    GetWindowTextW(g_path, buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

static void SavePath(const std::wstring& value) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IrAutoX", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"GamePath", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

static std::wstring LoadPath() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IrAutoX", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return {};
    wchar_t buffer[32768]{};
    DWORD size = sizeof(buffer);
    DWORD type = REG_SZ;
    const auto result = RegQueryValueExW(key, L"GamePath", nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? buffer : L"";
}

static bool InjectDll(HANDLE process, const std::wstring& dllPath) {
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    SIZE_T written{};
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) || written != bytes) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel, "LoadLibraryW"));
    if (!loadLibrary) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, 10000);
    DWORD exitCode{};
    const bool loaded = GetExitCodeThread(thread, &exitCode) && exitCode != 0;
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return loaded;
}

static void LaunchGame() {
    const std::wstring gamePath = ReadPath();
    DWORD binaryType{};
    if (gamePath.empty() || !std::filesystem::exists(gamePath) ||
        !GetBinaryTypeW(gamePath.c_str(), &binaryType) || binaryType != SCS_32BIT_BINARY) {
        SetStatus(L"Select a valid 32-bit GTA San Andreas executable.");
        return;
    }

    const auto dll = std::filesystem::path(gamePath).parent_path() / L"IrAutoXHook.dll";
    if (!std::filesystem::exists(dll)) {
        SetStatus(L"IrAutoXHook.dll must be beside gta_sa.exe.");
        MessageBoxW(nullptr, (L"Copy IrAutoXHook.dll to:\n" + dll.wstring()).c_str(), L"IrAutoX", MB_ICONWARNING);
        return;
    }

    SavePath(gamePath);
    std::wstring command = L"\"" + gamePath + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(gamePath.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        std::filesystem::path(gamePath).parent_path().c_str(), &startup, &process)) {
        SetStatus(L"Could not start GTA San Andreas.");
        return;
    }

    SetStatus(L"Injecting IrAutoXHook...");
    const bool injected = InjectDll(process.hProcess, dll.wstring());
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (injected) {
        SetStatus(L"Game started. Press F7 in-game to open the IrAutoX menu.");
    } else {
        SetStatus(L"Game started, but DLL injection failed. Check the DLL and architecture.");
        MessageBoxW(nullptr, L"The game was started, but IrAutoXHook could not be injected.", L"IrAutoX", MB_ICONWARNING);
    }
}

static void Browse(HWND window) {
    wchar_t file[32768]{};
    const auto current = ReadPath();
    if (!current.empty()) lstrcpynW(file, current.c_str(), static_cast<int>(std::size(file)));
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"GTA San Andreas (gta_sa.exe)\\0gta_sa.exe\\0Executable files (*.exe)\\0*.exe\\0All files\\0*.*\\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(g_path, file);
        SetStatus(L"GTA path selected.");
        SavePath(file);
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"IRAutoX", WS_CHILD | WS_VISIBLE, 28, 22, 330, 42, window, nullptr, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Grand Theft Auto: San Andreas  |  32-bit test build", WS_CHILD | WS_VISIBLE, 30, 65, 420, 24, window, nullptr, nullptr, nullptr);
        g_path = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 30, 112, 360, 30, window, reinterpret_cast<HMENU>(101), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 400, 112, 90, 30, window, reinterpret_cast<HMENU>(102), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Launch GTA + Inject", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 165, 460, 42, window, reinterpret_cast<HMENU>(103), nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"Select the GTA SA executable. Any filename is supported.", WS_CHILD | WS_VISIBLE, 30, 225, 470, 44, window, nullptr, nullptr, nullptr);
        SetWindowTextW(g_path, LoadPath().c_str());
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == 102) Browse(window);
        if (LOWORD(wParam) == 103) LaunchGame();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const wchar_t className[] = L"IrAutoXLauncherWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(19, 17, 30));
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    HWND window = CreateWindowExW(0, className, L"IrAutoX Launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 550, 330, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
