#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <wrl.h>
#include <WebView2.h>
#include "ui_bundle.h"
#include "discord_presence.h"
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <iterator>
#include <cstring>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static HWND g_window{};
static ComPtr<ICoreWebView2Environment> g_environment;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static std::wstring g_gtaPath;
static std::wstring g_sampcmdPath;
static std::wstring g_sampPath;
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


static std::wstring RegistryString(HKEY root, const wchar_t* keyPath, const wchar_t* valueName) {
    HKEY key{}; if(RegOpenKeyExW(root,keyPath,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return {};
    wchar_t value[32768]{};DWORD bytes=sizeof(value),type=0;
    LONG result=RegQueryValueExW(key,valueName,nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);RegCloseKey(key);
    if(result!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ))return {};
    std::wstring out(value);if(type==REG_EXPAND_SZ){wchar_t expanded[32768]{};ExpandEnvironmentStringsW(out.c_str(),expanded,32768);return expanded;}return out;
}
static std::wstring FindExecutable(const std::vector<std::filesystem::path>& roots,const wchar_t* exe) {
    for(auto root:roots){if(root.filename()!=exe)root/=exe;std::error_code ec;if(std::filesystem::is_regular_file(root,ec))return root.wstring();}return {};
}
static void AutoDetect() {
    wchar_t pf[MAX_PATH]{},pf86[MAX_PATH]{};GetEnvironmentVariableW(L"ProgramFiles",pf,MAX_PATH);GetEnvironmentVariableW(L"ProgramFiles(x86)",pf86,MAX_PATH);
    std::vector<std::filesystem::path> roots;
    for(const wchar_t* key:{L"Software\\Rockstar Games\\GTA San Andreas",L"Software\\WOW6432Node\\Rockstar Games\\GTA San Andreas",L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Grand Theft Auto San Andreas"})
        for(const wchar_t* val:{L"InstallFolder",L"InstallLocation",L"InstallDir",L"Path"}){auto p=RegistryString(HKEY_LOCAL_MACHINE,key,val);if(p.empty())p=RegistryString(HKEY_CURRENT_USER,key,val);if(!p.empty())roots.emplace_back(p);}
    roots.insert(roots.end(),{std::filesystem::path(pf)/L"Rockstar Games"/L"GTA San Andreas",std::filesystem::path(pf86)/L"Rockstar Games"/L"GTA San Andreas",std::filesystem::path(pf)/L"Steam"/L"steamapps"/L"common"/L"Grand Theft Auto San Andreas",std::filesystem::path(pf86)/L"Steam"/L"steamapps"/L"common"/L"Grand Theft Auto San Andreas",L"C:\\Games\\GTA San Andreas",L"C:\\Games\\GTA SA",L"D:\\Games\\GTA San Andreas",L"D:\\Games\\GTA SA",L"E:\\Games\\GTA San Andreas",L"F:\\Games\\GTA San Andreas"});
    if(g_gtaPath.empty()||!std::filesystem::is_regular_file(g_gtaPath)){auto p=FindExecutable(roots,L"gta_sa.exe");if(!p.empty()){g_gtaPath=p;SaveValue(L"GamePath",p);}}
    std::vector<std::filesystem::path> clients;if(!g_gtaPath.empty())clients.emplace_back(std::filesystem::path(g_gtaPath).parent_path());
    for(const wchar_t* key:{L"Software\\SA-MP",L"Software\\WOW6432Node\\SA-MP",L"Software\\San Andreas Multiplayer"})
        for(const wchar_t* val:{L"Install_Dir",L"InstallDir",L"Path"}){auto p=RegistryString(HKEY_CURRENT_USER,key,val);if(p.empty())p=RegistryString(HKEY_LOCAL_MACHINE,key,val);if(!p.empty())clients.emplace_back(p);}
    clients.insert(clients.end(),{std::filesystem::path(pf)/L"SA-MP",std::filesystem::path(pf86)/L"SA-MP",std::filesystem::path(pf)/L"San Andreas Multiplayer",std::filesystem::path(pf86)/L"San Andreas Multiplayer",L"C:\\Games\\SA-MP",L"D:\\Games\\SA-MP"});
    if(g_sampPath.empty()||!std::filesystem::is_regular_file(g_sampPath)){auto p=FindExecutable(clients,L"samp.exe");if(!p.empty()){g_sampPath=p;SaveValue(L"SampPath",p);}}
    if(g_sampcmdPath.empty()||!std::filesystem::is_regular_file(g_sampcmdPath)){auto p=FindExecutable(clients,L"sampcmd.exe");if(!p.empty()){g_sampcmdPath=p;SaveValue(L"SampCmdPath",p);}}
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
    } else if (kind == L"samp") {
        auto path = PickFile(L"SA-MP client (samp.exe)\\0samp.exe\\0Executable files (*.exe)\\0*.exe\\0\\0", g_sampPath);
        if (!path.empty()) { g_sampPath = path; SaveValue(L"SampPath", path); Send(L"samp|" + path); }
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
    if(p.size()<4)return;
    const std::wstring host=Decode(p[1]),portText=Decode(p[2]),nickname=Decode(p[3]);
    if(g_gtaPath.empty()||!std::filesystem::is_regular_file(g_gtaPath)){Send(L"error|GTA San Andreas was not detected. Select gta_sa.exe.");return;}
    if((g_sampcmdPath.empty()||!std::filesystem::is_regular_file(g_sampcmdPath))&&(g_sampPath.empty()||!std::filesystem::is_regular_file(g_sampPath))){Send(L"error|SA-MP was not detected. Select samp.exe or sampcmd.exe.");return;}
    wchar_t* end{};const long port=wcstol(portText.c_str(),&end,10);
    if(host.empty()||host.find_first_of(L" \t\r\n\"")!=std::wstring::npos||port<1||port>65535||!end||*end){Send(L"error|Invalid server address or port.");return;}
    if(nickname.empty()||nickname.size()>24||nickname.find_first_of(L" \t\r\n\"")!=std::wstring::npos){Send(L"error|Nickname must be 1-24 characters without spaces.");return;}
    SaveValue(L"Nickname",nickname);
    const auto folder=std::filesystem::path(g_gtaPath).parent_path().wstring();
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;info.hwnd=g_window;info.lpVerb=L"open";
    std::wstring args;
    if(!g_sampcmdPath.empty()&&std::filesystem::is_regular_file(g_sampcmdPath)){info.lpFile=g_sampcmdPath.c_str();args=L"-c -h \""+host+L"\" -p "+std::to_wstring(port)+L" -n \""+nickname+L"\"";}
    else{info.lpFile=g_sampPath.c_str();args=L"\""+host+L":"+std::to_wstring(port)+L"\" -n \""+nickname+L"\" -c";}
    info.lpParameters=args.c_str();info.lpDirectory=folder.c_str();info.nShow=SW_SHOWNORMAL;
    if(ShellExecuteExW(&info)){if(info.hProcess)CloseHandle(info.hProcess);DiscordPresence::SetJoining(host+L":"+std::to_wstring(port),host+L":"+std::to_wstring(port));Send(L"status|Starting SA-MP connection to "+host+L":"+std::to_wstring(port));}
    else Send(L"error|Windows could not start the SA-MP client. Verify the selected executable.");
}
static void HandleMessage(const std::wstring& message) {
    auto p = Split(message);
    if (p.empty()) return;
    if (p[0] == L"browse" && p.size() > 1) Browse(p[1]);
    else if (p[0] == L"connect") Connect(p);
    else if (p[0] == L"save" && p.size() > 2) {
        g_nickname = Decode(p[1]);
        g_gtaPath = Decode(p[2]);
        if (p.size() > 3) g_sampcmdPath = Decode(p[3]);
        if (p.size() > 4) g_sampPath = Decode(p[4]);
        SaveValue(L"Nickname", g_nickname);
        SaveValue(L"GamePath", g_gtaPath);
        SaveValue(L"SampCmdPath", g_sampcmdPath);
        SaveValue(L"SampPath", g_sampPath);
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
    if(msg==WM_NCHITTEST){POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);RECT r{};GetClientRect(hwnd,&r);constexpr int e=7;bool l=p.x<e,rt=p.x>=r.right-e,t=p.y<e,b=p.y>=r.bottom-e;if(t&&l)return HTTOPLEFT;if(t&&rt)return HTTOPRIGHT;if(b&&l)return HTBOTTOMLEFT;if(b&&rt)return HTBOTTOMRIGHT;if(l)return HTLEFT;if(rt)return HTRIGHT;if(t)return HTTOP;if(b)return HTBOTTOM;}
    if (msg == WM_DESTROY) { DiscordPresence::Shutdown(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_gtaPath = ReadValue(L"GamePath");
    g_sampcmdPath = ReadValue(L"SampCmdPath");
    g_nickname = ReadValue(L"Nickname", L"DeathAmir");
    g_sampPath = ReadValue(L"SampPath");
    AutoDetect();
    DiscordPresence::Initialize(ReadValue(L"DiscordAppId"));

    const wchar_t className[] = L"IrAutoXWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    g_window = CreateWindowExW(WS_EX_APPWINDOW, className, L"iRx | IrAutoX", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1380, 900, nullptr, nullptr, instance, nullptr);
    if (!g_window) return 1;
    const int corner = 2;
    DwmSetWindowAttribute(g_window, 33, &corner, sizeof(corner));

    wchar_t local[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    const auto webData = std::filesystem::path(local) / L"IrAutoX" / L"WebView2";
    std::error_code ec;
    std::filesystem::create_directories(webData, ec);
    CreateCoreWebView2EnvironmentWithOptions(nullptr, webData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    MessageBoxW(g_window, L"Microsoft Edge WebView2 Runtime is required. Install the Evergreen Runtime and restart iRx.", L"iRx", MB_ICONERROR);
                    PostMessageW(g_window, WM_CLOSE, 0, 0);
                    return S_OK;
                }
                g_environment = env;
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
                            g_webview->AddWebResourceRequestedFilter(L"https://irautox.local/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                            g_webview->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                [](ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                    ComPtr<ICoreWebView2WebResourceRequest> request;
                                    args->get_Request(&request);
                                    LPWSTR uri{};
                                    if (request && SUCCEEDED(request->get_Uri(&uri)) && uri) {
                                        if (wcsncmp(uri, L"https://irautox.local/", 21) == 0) {
                                            IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(kIrAutoXHtml), static_cast<UINT>(strlen(kIrAutoXHtml)));
                                            if (stream) {
                                                ComPtr<ICoreWebView2WebResourceResponse> response;
                                                HRESULT hr = g_environment->CreateWebResourceResponse(stream, 200, L"OK", L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n", &response);
                                                stream->Release();
                                                if (SUCCEEDED(hr)) args->put_Response(response.Get());
                                            }
                                        }
                                        CoTaskMemFree(uri);
                                    }
                                    return S_OK;
                                }).Get(), nullptr);
                            g_webview->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT { Send(L"config|" + g_gtaPath + L"|" + g_sampcmdPath + L"|" + g_nickname + L"|" + g_sampPath); return S_OK; }).Get(), nullptr);
                            g_webview->Navigate(L"https://irautox.local/index.html");
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
