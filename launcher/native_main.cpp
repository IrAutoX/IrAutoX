#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <winhttp.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "discord_presence.h"
using Microsoft::WRL::ComPtr;
using json = nlohmann::json;

struct Server { std::string name, ip, mode; int port=7777, players=0, maxPlayers=0; bool password=false; };
static HWND g_hwnd{};
static ID3D11Device* g_device{};
static ID3D11DeviceContext* g_context{};
static IDXGISwapChain* g_swap{};
static ID3D11RenderTargetView* g_target{};
static std::vector<ComPtr<ID3D11ShaderResourceView>> g_gif;
static std::vector<unsigned> g_gifDelay;
static std::chrono::steady_clock::time_point g_started;
static std::atomic<int> g_progress{4};
static std::atomic<bool> g_loading{true};
static std::atomic<bool> g_fetching{false};
static std::mutex g_serverMutex;
static std::vector<Server> g_servers;
static std::vector<std::string> g_favorites, g_recent;
static std::wstring g_gta, g_samp, g_sampcmd, g_nick=L"DeathAmir";
static std::string g_search, g_toast;
static bool g_toastError=false, g_dark=true, g_showSettings=false, g_sortPlayers=true;
static int g_page=0;
static std::atomic<bool> g_fetchDone{false};
static std::string g_masterStatus="Connecting";

static std::wstring ReadReg(const wchar_t* name,const std::wstring& fallback={}) {
 HKEY k{}; if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\IrAutoX",0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return fallback;
 wchar_t b[32768]{}; DWORD n=sizeof(b),t=REG_SZ; LONG r=RegQueryValueExW(k,name,nullptr,&t,(BYTE*)b,&n); RegCloseKey(k); return r==ERROR_SUCCESS?b:fallback;
}
static void WriteReg(const wchar_t* name,const std::wstring& value) {
 HKEY k{}; if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\IrAutoX",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)==ERROR_SUCCESS){RegSetValueExW(k,name,0,REG_SZ,(const BYTE*)value.c_str(),(DWORD)((value.size()+1)*sizeof(wchar_t)));RegCloseKey(k);}
}
static std::wstring PickExe(const wchar_t* filter,const std::wstring& current) {
 wchar_t file[32768]{}; if(!current.empty())lstrcpynW(file,current.c_str(),32768);
 OPENFILENAMEW d{};d.lStructSize=sizeof(d);d.hwndOwner=g_hwnd;d.lpstrFilter=filter;d.lpstrFile=file;d.nMaxFile=32768;d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 return GetOpenFileNameW(&d)?file:L"";
}
static void DetectPaths() {
 g_gta=ReadReg(L"GamePath");g_samp=ReadReg(L"SampPath");g_sampcmd=ReadReg(L"SampCmdPath");g_nick=ReadReg(L"Nickname",L"DeathAmir");
 std::vector<std::filesystem::path> roots;
 wchar_t pf[MAX_PATH]{},pf86[MAX_PATH]{};GetEnvironmentVariableW(L"ProgramFiles",pf,MAX_PATH);GetEnvironmentVariableW(L"ProgramFiles(x86)",pf86,MAX_PATH);
 for(auto p:{std::filesystem::path(pf)/L"Rockstar Games"/L"GTA San Andreas",std::filesystem::path(pf86)/L"Rockstar Games"/L"GTA San Andreas",std::filesystem::path(pf)/L"Steam"/L"steamapps"/L"common"/L"Grand Theft Auto San Andreas",std::filesystem::path(pf86)/L"Steam"/L"steamapps"/L"common"/L"Grand Theft Auto San Andreas",std::filesystem::path(L"C:\\Games\\GTA San Andreas"),std::filesystem::path(L"D:\\Games\\GTA San Andreas")})roots.push_back(p);
 if(g_gta.empty())for(auto p:roots)if(std::filesystem::is_regular_file(p/L"gta_sa.exe")){g_gta=(p/L"gta_sa.exe").wstring();break;}
 std::vector<std::filesystem::path> clients;if(!g_gta.empty())clients.push_back(std::filesystem::path(g_gta).parent_path());
 clients.insert(clients.end(),{std::filesystem::path(pf)/L"SA-MP",std::filesystem::path(pf86)/L"SA-MP",std::filesystem::path(L"C:\\Games\\SA-MP"),std::filesystem::path(L"D:\\Games\\SA-MP")});
 for(auto p:clients){if(g_samp.empty()&&std::filesystem::is_regular_file(p/L"samp.exe"))g_samp=(p/L"samp.exe").wstring();if(g_sampcmd.empty()&&std::filesystem::is_regular_file(p/L"sampcmd.exe"))g_sampcmd=(p/L"sampcmd.exe").wstring();}
 WriteReg(L"GamePath",g_gta);WriteReg(L"SampPath",g_samp);WriteReg(L"SampCmdPath",g_sampcmd);
}
static std::wstring Utf8ToWide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring o(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),o.data(),n);return o;}
static std::string WideToUtf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string o(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),o.data(),n,nullptr,nullptr);return o;}
static void Toast(std::string s,bool error=false){g_toast=std::move(s);g_toastError=error;}
static bool HttpGet(const wchar_t* host,const wchar_t* path,std::string& out) {
 HINTERNET session=WinHttpOpen(L"iRx/2.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);if(!session)return false;
 HINTERNET conn=WinHttpConnect(session,host,INTERNET_DEFAULT_HTTPS_PORT,0);if(!conn){WinHttpCloseHandle(session);return false;}
 HINTERNET req=WinHttpOpenRequest(conn,L"GET",path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
 bool ok=false;if(req&&WinHttpSendRequest(req,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(req,nullptr)){
  DWORD status=0,size=sizeof(status);WinHttpQueryHeaders(req,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
  if(status==200){DWORD available=0;while(WinHttpQueryDataAvailable(req,&available)&&available){std::string chunk(available,'\0');DWORD read=0;if(!WinHttpReadData(req,chunk.data(),available,&read)||!read)break;chunk.resize(read);out+=chunk;}ok=true;}
 }
 if(req)WinHttpCloseHandle(req);WinHttpCloseHandle(conn);WinHttpCloseHandle(session);return ok;
}
static void FetchServers(){
 if(g_fetching.exchange(true))return;g_fetchDone=false;g_masterStatus="Loading";
 std::thread([]{std::string body;bool ok=HttpGet(L"api.open.mp",L"/servers",body);std::vector<Server> parsed;
  if(ok)try{auto root=json::parse(body);if(root.is_object()&&root.contains("servers"))root=root["servers"];if(root.is_object()&&root.contains("data"))root=root["data"];
   if(root.is_array())for(auto& v:root){Server s;s.ip=v.value("ip",v.value("address",v.value("ipv4",std::string{})));s.port=v.value("port",v.value("p",7777));s.name=v.value("hn",v.value("hostname",v.value("name",s.ip)));s.mode=v.value("gm",v.value("gamemode",std::string("SA-MP")));s.players=v.value("pc",v.value("players",0));s.maxPlayers=v.value("pm",v.value("maxplayers",0));s.password=v.value("password",v.value("pr",false));if(!s.ip.empty()&&s.port>0&&s.port<65536)parsed.push_back(std::move(s));}
  }catch(...){ok=false;}
  {std::lock_guard<std::mutex> l(g_serverMutex);g_servers=std::move(parsed);}g_masterStatus=ok?"Online":"Unavailable";g_fetching=false;g_fetchDone=true;
 }).detach();
}
static void Connect(const Server& s){
 if(g_gta.empty()||!std::filesystem::is_regular_file(g_gta)){Toast("Select gta_sa.exe first.",true);return;}
 if((g_sampcmd.empty()||!std::filesystem::is_regular_file(g_sampcmd))&&(g_samp.empty()||!std::filesystem::is_regular_file(g_samp))){Toast("Select samp.exe or sampcmd.exe first.",true);return;}
 std::wstring host=Utf8ToWide(s.ip),args;
 SHELLEXECUTEINFOW x{};x.cbSize=sizeof(x);x.fMask=SEE_MASK_NOCLOSEPROCESS;x.hwnd=g_hwnd;x.lpVerb=L"open";x.nShow=SW_SHOWNORMAL;
 auto dir=std::filesystem::path(g_gta).parent_path().wstring();
 if(!g_sampcmd.empty()&&std::filesystem::is_regular_file(g_sampcmd)){x.lpFile=g_sampcmd.c_str();args=L"-c -h \""+host+L"\" -p "+std::to_wstring(s.port)+L" -n \""+g_nick+L"\"";}
 else{x.lpFile=g_samp.c_str();args=L"\""+host+L":"+std::to_wstring(s.port)+L"\" -n \""+g_nick+L"\" -c";}
 x.lpParameters=args.c_str();x.lpDirectory=dir.c_str();if(ShellExecuteExW(&x)){if(x.hProcess)CloseHandle(x.hProcess);DiscordPresence::SetJoining(host+L":"+std::to_wstring(s.port),host+L":"+std::to_wstring(s.port));Toast("Launching "+s.name);g_recent.insert(g_recent.begin(),s.ip+":"+std::to_string(s.port));if(g_recent.size()>12)g_recent.resize(12);}else Toast("Could not start SA-MP.",true);
}
static void CreateTarget(){ID3D11Texture2D* back{};g_swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&back);g_device->CreateRenderTargetView(back,nullptr,&g_target);back->Release();}
static bool InitD3D(HWND h){
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=h;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
 D3D_FEATURE_LEVEL level{};return SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&g_swap,&g_device,&level,&g_context));
}
static void LoadGif(){
 ComPtr<IWICImagingFactory> factory;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return;
 wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);PathRemoveFileSpecW(path);PathAppendW(path,L"LA.gif");
 ComPtr<IWICBitmapDecoder> decoder;if(FAILED(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)))return;
 UINT count=0;decoder->GetFrameCount(&count);count=std::min<UINT>(count,100);
 for(UINT i=0;i<count;i++){ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> cv;if(FAILED(decoder->GetFrame(i,&frame))||FAILED(factory->CreateFormatConverter(&cv)))continue;
  if(FAILED(cv->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))continue;
  UINT w=0,h=0;cv->GetSize(&w,&h);if(!w||!h||w>1024||h>1024)continue;std::vector<BYTE> pixels((size_t)w*h*4);if(FAILED(cv->CopyPixels(nullptr,w*4,(UINT)pixels.size(),pixels.data())))continue;
  D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init{};init.pSysMem=pixels.data();init.SysMemPitch=w*4;ComPtr<ID3D11Texture2D> tex;if(FAILED(g_device->CreateTexture2D(&td,&init,&tex)))continue;
  ComPtr<ID3D11ShaderResourceView> view;if(SUCCEEDED(g_device->CreateShaderResourceView(tex.Get(),nullptr,&view)))g_gif.push_back(view);
  g_gifDelay.push_back(90);
 }
}
static void Style(){
 ImGuiStyle& s=ImGui::GetStyle();s.WindowRounding=15;s.ChildRounding=12;s.FrameRounding=8;s.PopupRounding=9;s.ScrollbarRounding=9;s.GrabRounding=8;s.WindowPadding=ImVec2(18,16);s.FramePadding=ImVec2(11,8);s.ItemSpacing=ImVec2(10,10);s.WindowBorderSize=0;s.FrameBorderSize=0;
 auto& c=s.Colors;c[ImGuiCol_WindowBg]=ImVec4(.045f,.045f,.045f,1);c[ImGuiCol_ChildBg]=ImVec4(.075f,.073f,.067f,1);c[ImGuiCol_PopupBg]=ImVec4(.09f,.085f,.072f,1);c[ImGuiCol_Border]=ImVec4(.25f,.21f,.14f,.5f);c[ImGuiCol_Text]=ImVec4(.96f,.94f,.88f,1);c[ImGuiCol_TextDisabled]=ImVec4(.55f,.53f,.48f,1);c[ImGuiCol_FrameBg]=ImVec4(.13f,.12f,.10f,1);c[ImGuiCol_FrameBgHovered]=ImVec4(.22f,.19f,.13f,1);c[ImGuiCol_Button]=ImVec4(.53f,.39f,.16f,1);c[ImGuiCol_ButtonHovered]=ImVec4(.68f,.51f,.22f,1);c[ImGuiCol_ButtonActive]=ImVec4(.78f,.62f,.31f,1);c[ImGuiCol_Header]=ImVec4(.28f,.22f,.12f,1);c[ImGuiCol_HeaderHovered]=ImVec4(.38f,.30f,.16f,1);c[ImGuiCol_CheckMark]=ImVec4(.91f,.73f,.37f,1);c[ImGuiCol_SliderGrab]=ImVec4(.85f,.66f,.29f,1);
}
static bool GoldButton(const char* label,ImVec2 size={0,0}){return ImGui::Button(label,size);}
static void DrawHeader(){
 ImGui::SetCursorPos(ImVec2(18,12));ImGui::TextColored(ImVec4(.91f,.73f,.37f,1),"IR");
 ImGui::SameLine();ImGui::Text("AUTOX");ImGui::SameLine();ImGui::TextDisabled("/ CLIENT");
 ImGui::SameLine(ImGui::GetWindowWidth()-94);if(ImGui::SmallButton("_"))ShowWindow(g_hwnd,SW_MINIMIZE);ImGui::SameLine();if(ImGui::SmallButton("X"))PostMessageW(g_hwnd,WM_CLOSE,0,0);
 ImGui::GetWindowDrawList()->AddLine(ImVec2(0,42),ImVec2(ImGui::GetWindowWidth(),42),IM_COL32(74,62,39,110));
}
static void DrawPaths(){
 ImGui::Text("INSTALLATION PATHS");ImGui::TextDisabled("Detected automatically · You can change these at any time");ImGui::Spacing();
 ImGui::BeginChild("paths",ImVec2(0,116),true);
 ImGui::Text("GTA SAN ANDREAS");ImGui::SameLine(250);if(GoldButton("Browse##gta")){auto p=PickExe(L"GTA San Andreas\\0gta_sa.exe\\0Executables\\0*.exe\\0\\0",g_gta);if(!p.empty()){g_gta=p;WriteReg(L"GamePath",p);}}
 ImGui::TextDisabled("%s",WideToUtf8(g_gta).c_str());ImGui::Separator();
 ImGui::Text("SA-MP CLIENT");ImGui::SameLine(250);if(GoldButton("samp.exe##samp")){auto p=PickExe(L"SA-MP\\0samp.exe\\0Executables\\0*.exe\\0\\0",g_samp);if(!p.empty()){g_samp=p;WriteReg(L"SampPath",p);}}
 ImGui::SameLine();if(GoldButton("sampcmd.exe##cmd")){auto p=PickExe(L"SA-MP Command\\0sampcmd.exe\\0Executables\\0*.exe\\0\\0",g_sampcmd);if(!p.empty()){g_sampcmd=p;WriteReg(L"SampCmdPath",p);}}
 ImGui::TextDisabled("%s",WideToUtf8(!g_sampcmd.empty()?g_sampcmd:g_samp).c_str());ImGui::EndChild();
}
static void DrawApp(){
 ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
 ImGui::Begin("IrAutoXRoot",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
 DrawHeader();ImGui::SetCursorPos(ImVec2(18,56));ImGui::BeginChild("sidebar",ImVec2(178,ImGui::GetContentRegionAvail().y),false);
 ImGui::Dummy(ImVec2(0,9));ImGui::TextColored(ImVec4(.91f,.73f,.37f,1),"iRx");ImGui::TextDisabled("IRAUTOX CLIENT");ImGui::Spacing();ImGui::Spacing();
 const char* nav[]={"Server Browser","Favorites","Recently Played","Settings"};for(int i=0;i<4;i++){if(g_page==i)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.30f,.23f,.12f,1));if(ImGui::Button(nav[i],ImVec2(-1,38)))g_page=i;if(g_page==i)ImGui::PopStyleColor();}
 ImGui::SetCursorPosY(ImGui::GetWindowHeight()-70);ImGui::TextColored(ImVec4(.35f,.82f,.57f,1),"●");ImGui::SameLine();ImGui::Text("Launcher ready");ImGui::TextDisabled("%s",WideToUtf8(g_nick).c_str());ImGui::EndChild();
 ImGui::SameLine();ImGui::BeginChild("main",ImVec2(0,0),false);
 ImGui::TextColored(ImVec4(.91f,.73f,.37f,1),"SAN ANDREAS MULTIPLAYER");ImGui::SameLine(ImGui::GetWindowWidth()-120);if(GoldButton("Refresh"))FetchServers();ImGui::Spacing();
 if(g_page==3){ImGui::Text("Settings");char nick[64]{};std::string n=WideToUtf8(g_nick);strncpy_s(nick,n.c_str(),sizeof(nick)-1);ImGui::SetNextItemWidth(280);if(ImGui::InputText("Nickname",nick,sizeof(nick))){g_nick=Utf8ToWide(nick);WriteReg(L"Nickname",g_nick);}ImGui::Spacing();DrawPaths();}
 else {
  ImGui::Text(g_page==1?"Favorite servers":g_page==2?"Recently played":"Server Browser");ImGui::TextDisabled("Discover and join SA-MP communities");ImGui::Spacing();
  ImGui::BeginChild("hero",ImVec2(0,94),true);ImGui::TextColored(ImVec4(.94f,.78f,.44f,1),"YOUR CITY. YOUR STORY.");ImGui::Text("Explore the SA-MP community and jump into the action.");ImGui::TextDisabled("Live open.mp masterlist");ImGui::EndChild();
  int total=0,players=0;std::vector<Server> list;{std::lock_guard<std::mutex> l(g_serverMutex);list=g_servers;}for(auto& s:list)players+=s.players;total=(int)list.size();
  ImGui::Text("SERVERS  %d",total);ImGui::SameLine(180);ImGui::Text("PLAYERS ONLINE  %d",players);ImGui::SameLine(420);ImGui::TextColored(g_masterStatus=="Online"?ImVec4(.35f,.82f,.57f,1):ImVec4(.85f,.68f,.34f,1),"MASTERLIST  %s",g_masterStatus.c_str());
  ImGui::Spacing();char search[160]{};strncpy_s(search,g_search.c_str(),sizeof(search)-1);ImGui::SetNextItemWidth(300);if(ImGui::InputTextWithHint("##search","Search servers, game mode, IP...",search,sizeof(search)))g_search=search;
  ImGui::SameLine();if(GoldButton(g_sortPlayers?"Players ↓":"Name A-Z"))g_sortPlayers=!g_sortPlayers;ImGui::Separator();
  list.erase(std::remove_if(list.begin(),list.end(),[&](const Server& s){std::string q=g_search,all=s.name+" "+s.ip+" "+s.mode;std::transform(q.begin(),q.end(),q.begin(),::tolower);std::transform(all.begin(),all.end(),all.begin(),::tolower);if(!q.empty()&&all.find(q)==std::string::npos)return true;std::string id=s.ip+":"+std::to_string(s.port);if(g_page==1&&std::find(g_favorites.begin(),g_favorites.end(),id)==g_favorites.end())return true;if(g_page==2&&std::find(g_recent.begin(),g_recent.end(),id)==g_recent.end())return true;return false;}),list.end());
  std::sort(list.begin(),list.end(),[](const Server&a,const Server&b){return g_sortPlayers?a.players>b.players:a.name<b.name;});
  ImGui::BeginChild("serverlist",ImVec2(0,ImGui::GetContentRegionAvail().y-150),true);
  if(list.empty()){ImGui::SetCursorPosY(38);ImGui::TextDisabled(g_fetching?"Fetching servers...":"No servers found. Press Refresh to try again.");}
  for(auto& s:list){ImGui::PushID((s.ip+std::to_string(s.port)).c_str());ImGui::BeginChild("server",ImVec2(0,69),true);ImGui::TextColored(ImVec4(.91f,.73f,.37f,1),"%s",s.name.c_str());ImGui::SameLine(360);ImGui::Text("%d / %d",s.players,s.maxPlayers);ImGui::SameLine(475);ImGui::TextDisabled("%s",s.mode.c_str());ImGui::SameLine(ImGui::GetWindowWidth()-170);if(GoldButton("JOIN",ImVec2(62,29)))Connect(s);ImGui::SameLine();std::string id=s.ip+":"+std::to_string(s.port);bool fav=std::find(g_favorites.begin(),g_favorites.end(),id)!=g_favorites.end();if(ImGui::SmallButton(fav?"★":"☆")){if(fav)g_favorites.erase(std::remove(g_favorites.begin(),g_favorites.end(),id),g_favorites.end());else g_favorites.push_back(id);}ImGui::TextDisabled("%s:%d",s.ip.c_str(),s.port);ImGui::EndChild();ImGui::PopID();}
  ImGui::EndChild();DrawPaths();
 }
 ImGui::EndChild();ImGui::End();
}
static void DrawLoading(){
 ImGui::SetNextWindowSize(ImVec2(390,224));ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x*.5f,ImGui::GetIO().DisplaySize.y*.5f),ImGuiCond_Always,ImVec2(.5f,.5f));
 ImGui::Begin("##loading",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize);
 ImGui::SetCursorPos(ImVec2(18,15));ImGui::TextColored(ImVec4(.91f,.73f,.37f,1),"IRAUTOX");ImGui::SameLine(ImGui::GetWindowWidth()-65);ImGui::Text("%d%%",g_progress.load());
 ImGui::Spacing();ImGui::SetCursorPosX((ImGui::GetWindowWidth()-92)*.5f);
 if(!g_gif.empty()){auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-g_started).count();size_t idx=(size_t)(elapsed/90)%g_gif.size();ImGui::Image((ImTextureID)g_gif[idx].Get(),ImVec2(92,92));}
 else {ImGui::Dummy(ImVec2(92,92));}
 ImGui::SetCursorPosX(18);ImGui::Text("Preparing your San Andreas experience");
 ImGui::SetCursorPosX(18);ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.87f,.68f,.31f,1));ImGui::ProgressBar(g_progress.load()/100.0f,ImVec2(-18,7),"");ImGui::PopStyleColor();
 ImGui::SetCursorPosX(18);ImGui::TextDisabled("%s",g_progress<30?"Initializing graphics...":g_progress<65?"Loading launcher modules...":g_progress<90?"Preparing server browser...":"Almost ready...");
 ImGui::End();
}
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(ImGui_ImplWin32_WndProcHandler(h,m,w,l))return 1;
 if(m==WM_SIZE&&g_device&&w!=SIZE_MINIMIZED){if(g_target){g_target->Release();g_target=nullptr;}g_swap->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0);CreateTarget();return 0;}
 if(m==WM_NCHITTEST){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&p);if(p.y<42&&p.x<450)return HTCAPTION;}
 if(m==WM_DESTROY){DiscordPresence::Shutdown();PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);DetectPaths();DiscordPresence::Initialize(ReadReg(L"DiscordAppId"));g_started=std::chrono::steady_clock::now();
 WNDCLASSEXW wc{sizeof(wc)};wc.style=CS_CLASSDC;wc.lpfnWndProc=WndProc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.lpszClassName=L"IrAutoXNative";RegisterClassExW(&wc);
 g_hwnd=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"iRx | IrAutoX",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1180,790,nullptr,nullptr,instance,nullptr);
 if(!g_hwnd||!InitD3D(g_hwnd))return 1;CreateTarget();int corner=2;DwmSetWindowAttribute(g_hwnd,33,&corner,sizeof(corner));
 IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;Style();ImGui_ImplWin32_Init(g_hwnd);ImGui_ImplDX11_Init(g_device,g_context);LoadGif();FetchServers();ShowWindow(g_hwnd,show);UpdateWindow(g_hwnd);
 MSG msg{};while(msg.message!=WM_QUIT){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(msg.message==WM_QUIT)break;
  int p=(int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-g_started).count()/17;g_progress=std::min(100,std::max(4,p));if(g_progress>=100)g_loading=false;
  ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();DrawApp();if(g_loading)DrawLoading();ImGui::Render();
  const float clear[4]={.035f,.034f,.030f,1};g_context->OMSetRenderTargets(1,&g_target,nullptr);g_context->ClearRenderTargetView(g_target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());g_swap->Present(1,0);
 }
 ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();if(g_target)g_target->Release();if(g_swap)g_swap->Release();if(g_context)g_context->Release();if(g_device)g_device->Release();CoUninitialize();return 0;
}