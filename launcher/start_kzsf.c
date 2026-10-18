/* Windowless entry point for the Killzone launcher. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR cmd,int show) {
    (void)instance; (void)previous; (void)cmd; (void)show;
    wchar_t root[32768], python[32768], args[32768];
    DWORD length=GetModuleFileNameW(NULL,root,32768);
    if (!length || length>=32768) return 1;
    wchar_t *slash=wcsrchr(root,L'\\');
    if (!slash) return 1;
    *slash=0;
    if (swprintf(python,32768,L"%ls\\runtime\\pythonw.exe",root)<0) return 1;
    if (GetFileAttributesW(python)==INVALID_FILE_ATTRIBUTES) {
        DWORD found=SearchPathW(NULL,L"pythonw.exe",NULL,32768,python,NULL);
        if (!found || found>=32768) python[0]=0;
    }
    if (GetFileAttributesW(python)==INVALID_FILE_ATTRIBUTES)
        ExpandEnvironmentStringsW(L"%USERPROFILE%\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\python\\pythonw.exe",python,32768);
    if (GetFileAttributesW(python)==INVALID_FILE_ATTRIBUTES)
        wcscpy(python,L"C:\\msys64\\clang64\\bin\\pythonw.exe");
    if (GetFileAttributesW(python)==INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(NULL,L"Python with Tkinter and Pillow is required. Install Python and add it to PATH, then try again.",L"Killzone launcher",MB_ICONERROR);
        return 1;
    }
    if (swprintf(args,32768,L"\"%ls\\launcher\\kzsf_local.py\"",root)<0) return 1;
    HINSTANCE result=ShellExecuteW(NULL,L"open",python,args,root,SW_SHOWNORMAL);
    if ((INT_PTR)result<=32) {
        MessageBoxW(NULL,L"Could not start the launcher. Try Launch-Killzone.bat for diagnostics.",L"Killzone launcher",MB_ICONERROR);
        return 1;
    }
    return 0;
}
