// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

// Handle to this DLL module, captured at load time so the plugin can locate
// its config file (GalaxyATMSystem.json) next to the DLL regardless of EuroScope's
// current working directory.
HINSTANCE g_hModule = NULL;

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
