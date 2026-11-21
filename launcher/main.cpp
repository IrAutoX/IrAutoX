#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <iterator>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static HWND g_window{};
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static std::wstring g_gtaPath;
static std::wstring g_sampcmdPath;
static std::wstring g_nickname = L"DeathAmir";

static std::wstring ReadValue(const wchar_t* name, const std::wstring& fallback = {}) {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IrAutoX", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return fallback;
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value), type = REG_SZ;
    const LONG result = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS ? std::wstring(value) : fallback;
}

static void SaveValue(const wchar_t* name, const std::wstring& value) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IrAutoX", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
}

static std::wstring Decode(const std::wstring& value) {
    std::wstring out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'%' && i + 2 < value.size()) {
            wchar_t hex[3]{value[i + 1], value[i + 2], 0};
            out.push_back(static_cast<wchar_t>(wcstol(hex, nullptr, 16)));
            i += 2;
        } else if (value[i] == L'+') out.push_back(L' ');
        else out.push_back(value[i]);
    }
    return out;
}

static std::vector<std::wstring> Split(const std::wstring& value) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(L'|', start);
        parts.push_back(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return parts;
}

static void Send(const std::wstring& value) {
    if (g_webview) g_webview->PostWebMessageAsString(value.c_str());
}

static std::wstring PickFile(const wchar_t* filter, const std::wstring& current) {
    wchar_t file[32768]{};
    if (!current.empty()) lstrcpynW(file, current.c_str(), static_cast<int>(std::size(file)));
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::wstring(file) : L"";
}

static void Browse(const std::wstring& kind) {
    if (kind == L"gta") {
        auto path = PickFile(L"GTA San Andreas (gta_sa.exe)\\0gta_sa.exe\\0Executable files (*.exe)\\0*.exe\\0\\0", g_gtaPath);
        if (!path.empty()) {
            g_gtaPath = path;
            SaveValue(L"GamePath", path);
            Send(L"gta|" + path);
        }
    } else if (kind == L"sampcmd") {
        auto path = PickFile(L"SA-MP command launcher (sampcmd.exe)\\0sampcmd.exe\\0Executable files (*.exe)\\0*.exe\\0\\0", g_sampcmdPath);
        if (!path.empty()) {
            g_sampcmdPath = path;
            SaveValue(L"SampCmdPath", path);
            Send(L"sampcmd|" + path);
        }
    }
}

static void Connect(const std::vector<std::wstring>& p) {
    if (p.size() < 4) return;
    const std::wstring host = Decode(p[1]);
    const std::wstring portText = Decode(p[2]);
    const std::wstring nickname = Decode(p[3]);
    if (g_gtaPath.empty() || !std::filesystem::exists(g_gtaPath)) {
        Send(L"error|Choose your GTA San Andreas executable first.");
        return;
    }
    if (g_sampcmdPath.empty() || !std::filesystem::exists(g_sampcmdPath)) {
        Send(L"error|Choose sampcmd.exe first.");
        return;
    }
    if (std::filesystem::path(g_gtaPath).parent_path() != std::filesystem::path(g_sampcmdPath).parent_path()) {
        Send(L"error|Put sampcmd.exe in the GTA San Andreas folder, beside gta_sa.exe.");
        return;
    }
    wchar_t* end{};
    const long port = wcstol(portText.c_str(), &end, 10);
    if (host.empty() || host.find_first_of(L" \t\r\n\"") != std::wstring::npos || port < 1 || port > 65535 || !end || *end) {
        Send(L"error|Invalid server address or port.");
        return;
    }
    if (nickname.empty() || nickname.size() > 24 || nickname.find_first_of(L" \t\r\n\"") != std::wstring::npos) {
        Send(L"error|Nickname must be 1-24 characters without spaces.");
        return;
    }
    SaveValue(L"Nickname", nickname);
    std::wstring args = L"-c -h \"" + host + L"\" -p " + std::to_wstring(port) + L" -n \"" + nickname + L"\"";
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.hwnd = g_window;
    info.lpVerb = L"open";
    info.lpFile = g_sampcmdPath.c_str();
    info.lpParameters = args.c_str();
    const auto folder = std::filesystem::path(g_gtaPath).parent_path().wstring();
    info.lpDirectory = folder.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) {
        if (info.hProcess) CloseHandle(info.hProcess);
        Send(L"status|Launching SA-MP via sampcmd...");
    } else {
        Send(L"error|Could not start sampcmd.exe. Check the selected files and permissions.");
    }
}

static void HandleMessage(const std::wstring& message) {
    auto p = Split(message);
    if (p.empty()) return;
    if (p[0] == L"browse" && p.size() > 1) Browse(p[1]);
    else if (p[0] == L"connect") Connect(p);
    else if (p[0] == L"save" && p.size() > 3) {
        g_nickname = Decode(p[1]);
        g_gtaPath = Decode(p[2]);
        g_sampcmdPath = Decode(p[3]);
        SaveValue(L"Nickname", g_nickname);
        SaveValue(L"GamePath", g_gtaPath);
        SaveValue(L"SampCmdPath", g_sampcmdPath);
        Send(L"saved|1");
    } else if (p[0] == L"minimize") ShowWindow(g_window, SW_MINIMIZE);
    else if (p[0] == L"drag") { ReleaseCapture(); SendMessageW(g_window, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
    else if (p[0] == L"close") PostMessageW(g_window, WM_CLOSE, 0, 0);
}

static void Resize() {
    if (!g_controller || !g_window) return;
    RECT rc{};
    GetClientRect(g_window, &rc);
    g_controller->put_Bounds(rc);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE) { Resize(); return 0; }
    if (msg == WM_GETMINMAXINFO) {
        auto* m = reinterpret_cast<MINMAXINFO*>(lp);
        m->ptMinTrackSize = {1000, 660};
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_gtaPath = ReadValue(L"GamePath");
    g_sampcmdPath = ReadValue(L"SampCmdPath");
    g_nickname = ReadValue(L"Nickname", L"DeathAmir");

    const wchar_t className[] = L"IrAutoXWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    g_window = CreateWindowExW(0, className, L"iRx | IrAutoX", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1380, 900, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    const int corner = 2;
    DwmSetWindowAttribute(g_window, 33, &corner, sizeof(corner));

    const auto exeDir = std::filesystem::path(std::wstring(GetCommandLineW()).substr(1, std::wstring(GetCommandLineW()).find(L'"', 1) - 1)).parent_path();
    const auto webData = exeDir / L"WebView2";
    CreateCoreWebView2EnvironmentWithOptions(nullptr, webData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    MessageBoxW(g_window, L"Microsoft Edge WebView2 Runtime is required. Install the Evergreen Runtime and restart iRx.", L"iRx", MB_ICONERROR);
                    PostMessageW(g_window, WM_CLOSE, 0, 0);
                    return S_OK;
                }
                env->CreateCoreWebView2Controller(g_window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(result) || !ctrl) return S_OK;
                            g_controller = ctrl;
                            g_controller->get_CoreWebView2(&g_webview);
                            Resize();
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR raw{};
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                            HandleMessage(raw);
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            g_webview->SetVirtualHostNameToFolderMapping(L"app.irautox.local", L"ui", COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                            g_webview->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT { Send(L"config|" + g_gtaPath + L"|" + g_sampcmdPath + L"|" + g_nickname); return S_OK; }).Get(), nullptr);
                            g_webview->Navigate(L"https://app.irautox.local/index.html");
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    ShowWindow(g_window, show);
    UpdateWindow(g_window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_webview.Reset();
    g_controller.Reset();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
