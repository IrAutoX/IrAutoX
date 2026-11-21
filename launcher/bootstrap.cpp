#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include "embedded_resources.h"
static bool extract(HMODULE module,const EmbeddedFile& item,const std::filesystem::path& root){
 HRSRC r=FindResourceW(module,MAKEINTRESOURCEW(item.id),RT_RCDATA);if(!r)return false;
 HGLOBAL h=LoadResource(module,r);if(!h)return false;
 DWORD size=SizeofResource(module,r);const void* data=LockResource(h);if(!data||!size)return false;
 auto out=root/std::filesystem::path(item.path);std::error_code ec;std::filesystem::create_directories(out.parent_path(),ec);
 HANDLE f=CreateFileW(out.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
 DWORD written=0;BOOL ok=WriteFile(f,data,size,&written,nullptr);CloseHandle(f);return ok&&written==size;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 wchar_t local[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local)))return 2;
 auto root=std::filesystem::path(local)/L"IrAutoX"/L"QtRuntime";std::error_code ec;std::filesystem::create_directories(root,ec);
 for(const auto& item:embeddedFiles)if(!extract(instance,item,root)){MessageBoxW(nullptr,L"Could not prepare the embedded Qt runtime.",L"IRAutoX",MB_ICONERROR);return 3;}
 std::wstring path=root.wstring()+L";"+(root/L"platforms").wstring();SetEnvironmentVariableW(L"PATH",path.c_str());
 SetEnvironmentVariableW(L"QT_PLUGIN_PATH",(root/L"plugins").c_str());
 SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH",(root/L"plugins"/L"platforms").c_str());
 auto app=root/L"IrAutoXQt.exe";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};std::wstring command=L"\""+app.wstring()+L"\"";
 if(!CreateProcessW(app.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&si,&pi)){MessageBoxW(nullptr,L"Could not start the Qt launcher.",L"IRAutoX",MB_ICONERROR);return 4;}
 CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return 0;
}
