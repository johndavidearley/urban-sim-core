// This bootstrap deliberately has no Vulkan or GLFW link dependency. It can
// explain a missing driver/runtime before the renderer's DLL imports execute.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int) {
  std::wstring module(32768, L'\0');
  const DWORD length=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
  if(length==0 || length>=module.size()) return 1;
  module.resize(length);
  const auto separator=module.find_last_of(L"\\/");
  if(separator==std::wstring::npos) return 1;
  const auto root=module.substr(0,separator);
  const auto executable=root+L"\\bin\\UrbanSimCore-visualizer.exe";
  const auto attributes=GetFileAttributesW(executable.c_str());
  const bool present=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
  // Non-interactive packaging check; it does not claim GPU/driver readiness.
  if(arguments && std::wstring(arguments)==L"--verify-package") return present?0:2;
  if(!present) {
    MessageBoxW(nullptr,L"Game files are missing. Verify the installed game files in Steam, then try again.",L"UrbanSimCore",MB_OK|MB_ICONERROR);
    return 2;
  }
  const HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!loader) {
    MessageBoxW(nullptr,L"UrbanSimCore needs a graphics driver with Vulkan support. Install or update the driver from your graphics-card manufacturer, then restart the game. You do not need the Vulkan SDK.",L"UrbanSimCore - graphics driver required",MB_OK|MB_ICONERROR);
    return 3;
  }
  FreeLibrary(loader);
  std::wstring command=L"\""+executable+L"\"";
  if(arguments && *arguments) command+=L" "+std::wstring(arguments);
  STARTUPINFOW startup{};startup.cb=static_cast<DWORD>(sizeof(startup));
  PROCESS_INFORMATION process{};
  if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&startup,&process)) {
    const auto message=L"UrbanSimCore could not start. Verify the installed game files and update your graphics driver. If the problem continues, report it with the package manifest. Windows error: "+std::to_wstring(GetLastError());
    MessageBoxW(nullptr,message.c_str(),L"UrbanSimCore - launch failed",MB_OK|MB_ICONERROR);
    return 4;
  }
  CloseHandle(process.hThread);
  const DWORD waited=WaitForSingleObject(process.hProcess,INFINITE);
  DWORD code=1;
  const bool exited=waited==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&code)!=0;
  CloseHandle(process.hProcess);
  return exited?static_cast<int>(code):1;
}
