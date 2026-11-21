#include "discord_presence.h"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
namespace {
std::atomic<bool> running{false}; std::thread worker; std::mutex mutex; std::condition_variable wake;
std::string appId,pending; bool dirty=false;
std::string Utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string out(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
std::string Escape(const std::string& s){std::string o;for(unsigned char c:s){if(c=='"'||c=='\\'){o.push_back('\\');o.push_back(static_cast<char>(c));}else if(c>=0x20)o.push_back(static_cast<char>(c));}return o;}
bool WriteAll(HANDLE h,const void* d,DWORD n){auto p=static_cast<const BYTE*>(d);while(n){DWORD w=0;if(!WriteFile(h,p,n,&w,nullptr)||!w)return false;p+=w;n-=w;}return true;}
bool ReadAll(HANDLE h,void* d,DWORD n){auto p=static_cast<BYTE*>(d);while(n){DWORD r=0;if(!ReadFile(h,p,n,&r,nullptr)||!r)return false;p+=r;n-=r;}return true;}
bool Frame(HANDLE h,uint32_t op,const std::string& body){uint32_t header[2]{op,static_cast<uint32_t>(body.size())};return WriteAll(h,header,8)&&WriteAll(h,body.data(),static_cast<DWORD>(body.size()));}
void SendActivity(HANDLE h,const std::string& a){Frame(h,1,"{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":"+std::to_string(GetCurrentProcessId())+",\"activity\":"+a+"},\"nonce\":\"irx\"}");}
void Run(){HANDLE pipe=INVALID_HANDLE_VALUE;while(running){if(pipe==INVALID_HANDLE_VALUE){for(int i=0;i<10&&running;++i){auto name=L"\\\\?\\pipe\\discord-ipc-"+std::to_wstring(i);WaitNamedPipeW(name.c_str(),100);pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(pipe!=INVALID_HANDLE_VALUE)break;}if(pipe==INVALID_HANDLE_VALUE){std::unique_lock<std::mutex> l(mutex);wake.wait_for(l,std::chrono::seconds(3),[]{return !running;});continue;}if(!Frame(pipe,0,"{\"v\":1,\"client_id\":\""+Escape(appId)+"\"}")){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;continue;}uint32_t h[2]{};if(!ReadAll(pipe,h,8)||h[0]!=1||h[1]>65536){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;continue;}std::string ready(h[1],'\0');if(!ReadAll(pipe,ready.data(),h[1])){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;continue;}}
std::string activity;{std::unique_lock<std::mutex> l(mutex);wake.wait_for(l,std::chrono::milliseconds(250),[]{return dirty||!running;});if(!running)break;if(dirty){activity=pending;dirty=false;}}if(!activity.empty())SendActivity(pipe,activity);
DWORD available=0;if(PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)&&available>=8){uint32_t h[2]{};if(!ReadAll(pipe,h,8)||h[1]>65536){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;continue;}std::string body(h[1],'\0');if(!ReadAll(pipe,body.data(),h[1])){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;continue;}if(h[0]==3)Frame(pipe,4,body);}else if(GetLastError()==ERROR_BROKEN_PIPE){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;}}
if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);}
void Queue(std::string a){std::lock_guard<std::mutex> l(mutex);pending=std::move(a);dirty=true;wake.notify_one();}
}
namespace DiscordPresence {
void Initialize(const std::wstring& id){if(id.empty()||running.exchange(true))return;appId=Utf8(id);worker=std::thread(Run);SetBrowsing();}
void SetBrowsing(){Queue("{\"details\":\"Browsing SA-MP servers\",\"state\":\"In IrAutoX Launcher\"}");}
void SetJoining(const std::wstring& name,const std::wstring& endpoint){Queue("{\"details\":\"Joining "+Escape(Utf8(name))+"\",\"state\":\""+Escape(Utf8(endpoint))+"\"}");}
void Shutdown(){if(!running.exchange(false))return;wake.notify_all();if(worker.joinable())worker.join();}
}
