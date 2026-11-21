#include <windows.h>
#include <commdlg.h>
#include <windowsx.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include "sciter-x.h"

struct Server { std::string name, ip, mode; int port=7777, players=0, maxPlayers=0; bool password=false; };
static HWND g_main{},g_splash{};
static HINSTANCE g_instance{};
static std::vector<Server> g_servers;
static std::mutex g_mutex;
static std::atomic<bool> g_fetchDone{false};
static std::atomic<int> g_progress{0};
static std::atomic<bool> g_fetchOk{false};
static std::wstring g_game,g_samp,g_sampcmd,g_nick=L"DeathAmir";
static std::wstring g_baseUrl;
static std::wstring g_exeDir;
static bool g_ready=false;
static HMODULE g_sciterModule{};
static ULONGLONG g_splashStarted{};
#define IDR_SCITER_DLL 101
#define IDR_LOADING_GIF 102
#define IDR_LAUNCHER_UI 103

static bool ExtractResource(UINT id,const std::filesystem::path& destination){
 HRSRC resource=FindResourceW(g_instance,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)return false;
 HGLOBAL loaded=LoadResource(g_instance,resource);if(!loaded)return false;
 DWORD size=SizeofResource(g_instance,resource);const void* data=LockResource(loaded);if(!data||!size)return false;
 HANDLE file=CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 DWORD written=0;BOOL ok=WriteFile(file,data,size,&written,nullptr);CloseHandle(file);return ok&&written==size;
}
static bool PrepareRuntime(){
 wchar_t local[MAX_PATH]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);if(!n||n>=MAX_PATH)return false;
 try{
  std::filesystem::path dir=std::filesystem::path(local)/L"IrAutoX"/L"Runtime";std::filesystem::create_directories(dir);
  auto dll=dir/L"sciter.dll";auto gif=dir/L"LA.gif";auto ui=dir/L"ui.html";
  if(!ExtractResource(IDR_SCITER_DLL,dll)||!ExtractResource(IDR_LOADING_GIF,gif)||!ExtractResource(IDR_LAUNCHER_UI,ui))return false;
  g_exeDir=dir.wstring();SetDllDirectoryW(g_exeDir.c_str());g_sciterModule=LoadLibraryW(dll.c_str());if(!g_sciterModule)return false;
  g_baseUrl=L"file:///"+g_exeDir+L"/";std::replace(g_baseUrl.begin(),g_baseUrl.end(),L'\\',L'/');return true;
 }catch(...){return false;}
}
static std::wstring ReadReg(const wchar_t* name,const std::wstring& fallback={}) {
 HKEY key{};if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\IrAutoX",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return fallback;
 wchar_t value[32768]{};DWORD size=sizeof(value),type=REG_SZ;LONG result=RegQueryValueExW(key,name,nullptr,&type,(BYTE*)value,&size);RegCloseKey(key);return result==ERROR_SUCCESS?value:fallback;
}
static void WriteReg(const wchar_t* name,const std::wstring& value) {
 HKEY key{};if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\IrAutoX",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),(DWORD)((value.size()+1)*sizeof(wchar_t)));RegCloseKey(key);}
}
static std::wstring Utf8Wide(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n);return out;}
static std::string WideUtf8(const std::wstring& value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),out.data(),n,nullptr,nullptr);return out;}
static std::string Html(std::string s){std::string o;for(char c:s){switch(c){case '&':o+="&amp;";break;case '<':o+="&lt;";break;case '>':o+="&gt;";break;case '"':o+="&quot;";break;case '\'':o+="&#39;";break;default:o+=c;}}return o;}
static std::wstring UrlDecode(std::wstring s){std::wstring o;for(size_t i=0;i<s.size();i++){if(s[i]==L'%'&&i+2<s.size()){wchar_t h[3]={s[i+1],s[i+2],0};o.push_back((wchar_t)wcstol(h,nullptr,16));i+=2;}else if(s[i]==L'+')o.push_back(L' ');else o.push_back(s[i]);}return o;}
static std::wstring QueryValue(const std::wstring& uri,const std::wstring& key){auto p=uri.find(key+L"=");if(p==std::wstring::npos)return {};p+=key.size()+1;auto e=uri.find(L'&',p);return UrlDecode(uri.substr(p,e==std::wstring::npos?e:e-p));}
static bool HttpGet(std::string& body){
 HINTERNET session=WinHttpOpen(L"IRAutoX/2.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)return false;
 WinHttpSetTimeouts(session,2500,2500,3500,3500);
 HINTERNET conn=WinHttpConnect(session,L"api.open.mp",INTERNET_DEFAULT_HTTPS_PORT,0);if(!conn){WinHttpCloseHandle(session);return false;}
 HINTERNET req=WinHttpOpenRequest(conn,L"GET",L"/servers",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);bool ok=false;
 if(req&&WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(req,nullptr)){
  DWORD status=0,n=sizeof(status);WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&n,WINHTTP_NO_HEADER_INDEX);
  if(status==200){DWORD avail=0;while(WinHttpQueryDataAvailable(req,&avail)&&avail){std::string part(avail,'\0');DWORD read=0;if(!WinHttpReadData(req,part.data(),avail,&read)||!read)break;part.resize(read);body+=part;}ok=true;}
 }
 if(req)WinHttpCloseHandle(req);WinHttpCloseHandle(conn);WinHttpCloseHandle(session);return ok;
}
static void FetchServers(){
 std::thread([]{std::string body;bool ok=HttpGet(body);std::vector<Server> result;
  if(ok)try{auto root=nlohmann::json::parse(body);if(root.is_object()&&root.contains("servers"))root=root["servers"];if(root.is_object()&&root.contains("data"))root=root["data"];
   if(root.is_array())for(auto& v:root){Server s;s.ip=v.value("ip",v.value("address",v.value("ipv4",std::string{})));s.port=v.value("port",v.value("p",7777));s.name=v.value("hn",v.value("hostname",v.value("name",s.ip)));s.mode=v.value("gm",v.value("gamemode",std::string("SA-MP")));s.players=v.value("pc",v.value("players",0));s.maxPlayers=v.value("pm",v.value("maxplayers",0));s.password=v.value("password",v.value("pr",false));if(!s.ip.empty()&&s.port>0&&s.port<65536)result.push_back(std::move(s));}
  }catch(...){ok=false;}
  {std::lock_guard<std::mutex> lock(g_mutex);g_servers=std::move(result);}g_fetchOk=ok;g_fetchDone=true;if(g_splash)PostMessageW(g_splash,WM_APP+1,0,0);
 }).detach();
}
static void DetectPaths(){
 g_game=ReadReg(L"GamePath");g_samp=ReadReg(L"SampPath");g_sampcmd=ReadReg(L"SampCmdPath");g_nick=ReadReg(L"Nickname",L"DeathAmir");
 wchar_t pf[MAX_PATH]{},pf86[MAX_PATH]{};GetEnvironmentVariableW(L"ProgramFiles",pf,MAX_PATH);GetEnvironmentVariableW(L"ProgramFiles(x86)",pf86,MAX_PATH);
 std::vector<std::filesystem::path> roots={std::filesystem::path(pf)/L"Rockstar Games"/L"GTA San Andreas",std::filesystem::path(pf86)/L"Rockstar Games"/L"GTA San Andreas",std::filesystem::path(pf)/L"Steam"/L"steamapps"/L"common"/L"Grand Theft Auto San Andreas",std::filesystem::path(pf86)/L"Steam"/L"steamapps"/L"common"/L"Grand Theft Auto San Andreas",L"C:\\Games\\GTA San Andreas",L"D:\\Games\\GTA San Andreas"};
 if(g_game.empty())for(auto& p:roots)if(std::filesystem::is_regular_file(p/L"gta_sa.exe")){g_game=(p/L"gta_sa.exe").wstring();break;}
 std::vector<std::filesystem::path> clients;if(!g_game.empty())clients.push_back(std::filesystem::path(g_game).parent_path());clients.push_back(std::filesystem::path(pf)/L"SA-MP");clients.push_back(std::filesystem::path(pf86)/L"SA-MP");
 for(auto& p:clients){if(g_samp.empty()&&std::filesystem::is_regular_file(p/L"samp.exe"))g_samp=(p/L"samp.exe").wstring();if(g_sampcmd.empty()&&std::filesystem::is_regular_file(p/L"sampcmd.exe"))g_sampcmd=(p/L"sampcmd.exe").wstring();}
 WriteReg(L"GamePath",g_game);WriteReg(L"SampPath",g_samp);WriteReg(L"SampCmdPath",g_sampcmd);
}
static std::wstring PickExe(const wchar_t* filter,const std::wstring& current){
 wchar_t file[32768]{};if(!current.empty())lstrcpynW(file,current.c_str(),32768);OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=g_main;d.lpstrFilter=filter;d.lpstrFile=file;d.nMaxFile=32768;d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;return GetOpenFileNameW(&d)?file:L"";
}
static void ConnectServer(const std::wstring& host,int port){
 if(g_game.empty()||!std::filesystem::is_regular_file(g_game)){MessageBoxW(g_main,L"Select gta_sa.exe in Settings first.",L"IrAutoX",MB_ICONWARNING);return;}
 std::wstring client=!g_sampcmd.empty()&&std::filesystem::is_regular_file(g_sampcmd)?g_sampcmd:g_samp;
 if(client.empty()||!std::filesystem::is_regular_file(client)){MessageBoxW(g_main,L"Select samp.exe or sampcmd.exe in Settings first.",L"IrAutoX",MB_ICONWARNING);return;}
 std::wstring args;if(client==g_sampcmd)args=L"-c -h \""+host+L"\" -p "+std::to_wstring(port)+L" -n \""+g_nick+L"\"";else args=L"\""+host+L":"+std::to_wstring(port)+L"\" -n \""+g_nick+L"\" -c";
 auto dir=std::filesystem::path(g_game).parent_path().wstring();SHELLEXECUTEINFOW x{};x.cbSize=sizeof(x);x.fMask=SEE_MASK_NOCLOSEPROCESS;x.hwnd=g_main;x.lpVerb=L"open";x.lpFile=client.c_str();x.lpParameters=args.c_str();x.lpDirectory=dir.c_str();x.nShow=SW_SHOWNORMAL;
 if(ShellExecuteExW(&x)){if(x.hProcess)CloseHandle(x.hProcess);}else MessageBoxW(g_main,L"Could not start the SA-MP client.",L"IrAutoX",MB_ICONERROR);
}
static std::string BuildServers(){
 std::vector<Server> list;{std::lock_guard<std::mutex> lock(g_mutex);list=g_servers;}
 if(list.empty())return "<div class='empty'><div class='empty-icon'>⌁</div><h3>No servers loaded</h3><p>Check your connection and refresh the masterlist.</p><a class='button primary' href='irx://refresh'>Refresh servers</a></div>";
 std::sort(list.begin(),list.end(),[](const Server&a,const Server&b){return a.players>b.players;});std::ostringstream out;int count=0;
 for(const auto& s:list){if(count++>=80)break;std::string host=Html(s.ip);std::string name=Html(s.name);std::string mode=Html(s.mode);out<<"<article class='server-card'><div class='server-main'><div class='server-mark'>IR</div><div class='server-copy'><h3>"<<name<<"</h3><p>"<<host<<":"<<s.port<<" <span>·</span> "<<mode<<"</p></div></div><div class='server-online'><strong>"<<s.players<<"</strong><small> / "<<s.maxPlayers<<" players</small></div><a class='button join' href='irx://connect?host="<<host<<"&port="<<s.port<<"'>CONNECT</a></article>";}
 return out.str();
}
static void ReplaceAll(std::string& value,const std::string& from,const std::string& to){if(from.empty())return;size_t pos=0;while((pos=value.find(from,pos))!=std::string::npos){value.replace(pos,from.size(),to);pos+=to.size();}}
static std::string MainHtml(){
 std::ifstream file(std::filesystem::path(g_exeDir)/L"ui.html",std::ios::binary);std::string html((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
 if(html.empty())return "<html><body style='background:#090a0f;color:white;font:16px Segoe UI;padding:40px'>IRAutoX interface could not be loaded.</body></html>";
 std::string servers=BuildServers();std::string nick=Html(WideUtf8(g_nick));size_t count=0;int players=0;{std::lock_guard<std::mutex> lock(g_mutex);count=g_servers.size();for(const auto& server:g_servers)players+=server.players;}
 ReplaceAll(html,"{{SERVERS}}",servers);ReplaceAll(html,"__IRX_NICK__",nick);ReplaceAll(html,"{{SERVER_COUNT}}",std::to_string(count));ReplaceAll(html,"{{PLAYER_COUNT}}",std::to_string(players));return html;
}
static std::string SplashHtml(){
 return R"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
*{box-sizing:border-box}html,body{margin:0;width:100%;height:100%;overflow:hidden;font-family:Segoe UI,Arial;background:#0c0c0b;color:#f6f1e5}body{border:1px solid #403722;border-radius:14px;padding:25px 30px 20px;display:flex;flex-direction:column;background:#10100e}.loader-container{width:120px;height:120px;position:relative;margin:0 auto 24px;display:flex;align-items:center;justify-content:center}.logo{position:absolute;width:104px;height:104px;object-fit:contain;z-index:3}.ring{position:absolute;border-radius:50%;border:1px solid rgba(219,185,105,.55);inset:5px;animation:ringPulse 2.8s ease-in-out infinite}.ring-two{inset:-5px;border-color:rgba(219,185,105,.22);animation:ringPulse 2.8s ease-in-out infinite reverse}@keyframes ringPulse{0%,100%{transform:scale(.94);opacity:.35}50%{transform:scale(1.06);opacity:1}}.loader{position:absolute;inset:0;border-radius:50%;animation:rotate 2s linear infinite}.loader span{position:absolute;width:9px;height:9px;border-radius:50%;background:#d9b96e;box-shadow:0 0 12px #d9b96e}.loader span:nth-child(1){top:0;left:55px}.loader span:nth-child(2){right:0;top:55px;opacity:.75}.loader span:nth-child(3){bottom:0;left:55px;opacity:.55}.loader span:nth-child(4){left:0;top:55px;opacity:.35}@keyframes rotate{to{transform:rotate(360deg)}}.task{font-size:12px;font-weight:600;color:#e8e0cd;text-align:center;min-height:18px}.spacer{flex:1}.download-label{font-size:10px;font-weight:600;color:#d9d0bd;margin-bottom:7px}.progress-track{height:8px;width:100%;background:#29271f;border-radius:100px;overflow:hidden;box-shadow:0 1px 4px #000}.progress-fill{height:100%;width:38%;background:linear-gradient(90deg,#9d7937,#f0d18a,#9d7937);border-radius:100px;animation:progressMove 1.6s ease-in-out infinite}@keyframes progressMove{0%{transform:translateX(-110%)}100%{transform:translateX(280%)}}.progress-meta{display:flex;justify-content:space-between;margin-top:6px;color:#928a78;font-size:10px}.drag{position:absolute;left:0;top:0;width:100%;height:100%;z-index:5;behavior:window-drag}
@media(prefers-color-scheme:light){html,body{background:#fff;color:#29261f}body{background:#fff;border-color:#e4d9bd}.task{color:#39352c}.loader span{background:#ad8538;box-shadow:0 0 12px #d4b36d}.progress-track{background:#eee9de}.progress-meta{color:#827b6e}.download-label{color:#625b4d}}
</style></head><body><div class="loader-container"><div class="loader loader-dark"><span></span><span></span><span></span><span></span></div><div class="ring ring-one"></div><div class="ring ring-two"></div><img class="logo" src="LA.gif"></div><div class="task">Initializing IrAutoX...</div><div class="spacer"></div><div class="download-label">Preparing launcher</div><div class="progress-track"><div class="progress-fill"></div></div><div class="progress-meta"><span>Starting services</span><span>PLEASE WAIT</span></div></body></html>)HTML";
}
static void SetRound(HWND hwnd,int radius){RECT r{};GetClientRect(hwnd,&r);HRGN region=CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,radius,radius);SetWindowRgn(hwnd,region,TRUE);}
static UINT SC_CALLBACK HostCallback(LPSCITER_CALLBACK_NOTIFICATION note,LPVOID){
 if(!note||note->code!=SC_LOAD_DATA)return 0;auto load=(LPSCN_LOAD_DATA)note;std::wstring uri=load->uri?load->uri:L"";
 if(uri.rfind(L"irx://",0)!=0)return LOAD_OK;
 if(uri.find(L"irx://close")==0)PostMessageW(note->hwnd,WM_CLOSE,0,0);
 else if(uri.find(L"irx://minimize")==0)ShowWindow(note->hwnd,SW_MINIMIZE);
 else if(uri.find(L"irx://refresh")==0){g_ready=false;g_fetchDone=false;FetchServers();}
 else if(uri.find(L"irx://connect")==0){std::wstring host=QueryValue(uri,L"host");int port=_wtoi(QueryValue(uri,L"port").c_str());if(!host.empty()&&port>0&&port<65536)ConnectServer(host,port);}
 else if(uri.find(L"irx://page?name=settings")==0){auto game=PickExe(L"GTA San Andreas\\0gta_sa.exe\\0Executables\\0*.exe\\0\\0",g_game);if(!game.empty()){g_game=game;WriteReg(L"GamePath",g_game);}auto samp=PickExe(L"SA-MP Client\\0samp.exe\\0Executables\\0*.exe\\0\\0",g_samp);if(!samp.empty()){g_samp=samp;WriteReg(L"SampPath",g_samp);}auto cmd=PickExe(L"SA-MP Command Client\\0sampcmd.exe\\0Executables\\0*.exe\\0\\0",g_sampcmd);if(!cmd.empty()){g_sampcmd=cmd;WriteReg(L"SampCmdPath",g_sampcmd);}}
 return LOAD_DISCARD;
}
static LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 SBOOL handled=FALSE;LRESULT sc=SciterProcND(hwnd,msg,wp,lp,&handled);if(handled)return sc;
 switch(msg){
 case WM_NCHITTEST:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);if(p.y<58)return HTCAPTION;break;}
 case WM_SIZE:if(wp!=SIZE_MINIMIZED)SetRound(hwnd,18);break;
 case WM_TIMER:
  if(hwnd==g_main&&wp==2){if(g_fetchDone&&!g_ready){std::string updated=MainHtml();SciterLoadHtml(g_main,(LPCBYTE)updated.data(),(UINT)updated.size(),g_baseUrl.c_str());g_ready=true;}return 0;}
  if(hwnd==g_splash&&wp==1){
   ULONGLONG elapsed=GetTickCount64()-g_splashStarted;
   if(elapsed>=5000){
    KillTimer(hwnd,1);
    if(!g_ready){std::string updated=MainHtml();SciterLoadHtml(g_main,(LPCBYTE)updated.data(),(UINT)updated.size(),g_baseUrl.c_str());g_ready=true;}
    ShowWindow(g_main,SW_SHOW);UpdateWindow(g_main);SetForegroundWindow(g_main);DestroyWindow(hwnd);g_splash=nullptr;
   }
   return 0;
  }
  break;
 case WM_APP+1:return 0;
 case WM_CLOSE:DestroyWindow(hwnd);return 0;
 case WM_DESTROY:if(hwnd==g_main){if(g_sciterModule){FreeLibrary(g_sciterModule);g_sciterModule=nullptr;}PostQuitMessage(0);}return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
static HWND CreateSciterWindow(const wchar_t* title,DWORD ex,DWORD style,int w,int h){
 RECT r{0,0,w,h};AdjustWindowRectEx(&r,style,FALSE,ex);HWND hwnd=CreateWindowExW(ex,L"IRAutoXSciter",title,style,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,g_instance,nullptr);
 if(hwnd){SciterSetCallback(hwnd,HostCallback,nullptr);SetRound(hwnd,18);}return hwnd;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 g_instance=instance;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(!PrepareRuntime()){MessageBoxW(nullptr,L"Could not initialize the embedded Sciter runtime.",L"IrAutoX",MB_ICONERROR);return 1;}
 DetectPaths();
 WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);wc.lpszClassName=L"IRAutoXSciter";if(!RegisterClassExW(&wc))return 1;
 g_main=CreateSciterWindow(L"IrAutoX",WS_EX_APPWINDOW,WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX,1120,760);if(!g_main)return 2;
 std::string main=MainHtml();if(!SciterLoadHtml(g_main,(LPCBYTE)main.data(),(UINT)main.size(),g_baseUrl.c_str())){MessageBoxW(nullptr,L"Sciter could not load the launcher interface.",L"IrAutoX",MB_ICONERROR);return 3;}
 RECT desktop{};SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);int sw=430,sh=270;int x=desktop.left+(desktop.right-desktop.left-sw)/2,y=desktop.top+(desktop.bottom-desktop.top-sh)/2;
 g_splash=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"IRAutoXSciter",L"IrAutoX Loading",WS_POPUP,x,y,sw,sh,nullptr,nullptr,instance,nullptr);if(!g_splash)return 4;SciterSetCallback(g_splash,HostCallback,nullptr);SetRound(g_splash,20);
 std::string splash=SplashHtml();SciterLoadHtml(g_splash,(LPCBYTE)splash.data(),(UINT)splash.size(),g_baseUrl.c_str());
 ShowWindow(g_splash,SW_SHOWNOACTIVATE);UpdateWindow(g_splash);g_splashStarted=GetTickCount64();SetTimer(g_splash,1,40,nullptr);SetTimer(g_main,2,250,nullptr);FetchServers();
 MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
 CoUninitialize();return (int)msg.wParam;
}