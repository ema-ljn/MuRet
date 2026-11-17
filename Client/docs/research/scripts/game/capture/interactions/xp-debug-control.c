#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define COBJMACROS
#include <windows.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <dbgeng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE* logfile;
static HRESULT STDMETHODCALLTYPE oq(IDebugOutputCallbacks* self, REFIID iid, void** out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDebugOutputCallbacks))
    {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE oa(IDebugOutputCallbacks* self)
{
    (void)self;
    return 2;
}
static ULONG STDMETHODCALLTYPE ore(IDebugOutputCallbacks* self)
{
    (void)self;
    return 1;
}
static HRESULT STDMETHODCALLTYPE oo(IDebugOutputCallbacks* self, ULONG mask, PCSTR text)
{
    (void)self;
    (void)mask;
    fputs(text, logfile);
    fflush(logfile);
    return S_OK;
}
static IDebugOutputCallbacksVtbl callbacks = {oq, oa, ore, oo};
static IDebugOutputCallbacks sink = {&callbacks};
static int metadata(DWORD pid)
{
    MODULEENTRY32 m;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    DWORD fg = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fg);
    fprintf(logfile, "PID=%lu FOREGROUND_PID=%lu\n", (unsigned long)pid, (unsigned long)fg);
    if (s == INVALID_HANDLE_VALUE)
    {
        fprintf(logfile, "SNAPSHOT_ERROR=%lu\n", GetLastError());
        return 3;
    }
    memset(&m, 0, sizeof m);
    m.dwSize = sizeof m;
    if (Module32First(s, &m))
        do
        {
            if (!_stricmp(m.szModule, "main.exe") || !_stricmp(m.szModule, "FoxAnti.dll"))
                fprintf(logfile, "MODULE=%s BASE=%p SIZE=%lu PATH=%s\n", m.szModule, (void*)m.modBaseAddr,
                        (unsigned long)m.modBaseSize, m.szExePath);
        } while (Module32Next(s, &m));
    CloseHandle(s);
    return 0;
}
int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR cmd, int show)
{
    IDebugClient* client = NULL;
    IDebugControl* control = NULL;
    HRESULT hr;
    char remote[256], command[8192];
    FILE* in;
    size_t n;
    ULONG state = DEBUG_STATUS_NO_DEBUGGEE;
    int result = 1, i;
    (void)a;
    (void)b;
    (void)cmd;
    (void)show;
    if (__argc != 5)
        return 2;
    if (GetFileAttributesA(__argv[4]) != INVALID_FILE_ATTRIBUTES)
        return 8;
    logfile = fopen(__argv[4], "w");
    if (!logfile)
        return 3;
    if (!strcmp(__argv[1], "meta"))
    {
        result = metadata(strtoul(__argv[2], NULL, 10));
        goto finish;
    }
    snprintf(remote, sizeof remote, "npipe:server=.,pipe=%s", __argv[2]);
    hr = DebugConnect(remote, &IID_IDebugClient, (void**)&client);
    fprintf(logfile, "CONNECT=%08lx\n", (unsigned long)hr);
    fflush(logfile);
    if (FAILED(hr))
        goto finish;
    client->lpVtbl->SetOutputCallbacks(client, &sink);
    hr = client->lpVtbl->QueryInterface(client, &IID_IDebugControl, (void**)&control);
    if (FAILED(hr))
        goto finish;
    if (!strcmp(__argv[1], "status"))
    {
        hr = control->lpVtbl->GetExecutionStatus(control, &state);
        fprintf(logfile, "STATE=%lu HRESULT=%08lx\n", (unsigned long)state, (unsigned long)hr);
        result = SUCCEEDED(hr) && state == DEBUG_STATUS_NO_DEBUGGEE ? 0 : 4;
        goto finish;
    }
    if (!strcmp(__argv[1], "interrupt"))
    {
        hr = control->lpVtbl->SetInterrupt(control, DEBUG_INTERRUPT_ACTIVE);
        fprintf(logfile, "INTERRUPT=%08lx\n", (unsigned long)hr);
        if (FAILED(hr))
            goto finish;
        for (i = 0; i < 100; i++)
        {
            hr = control->lpVtbl->GetExecutionStatus(control, &state);
            if (FAILED(hr) || state == DEBUG_STATUS_BREAK || state == DEBUG_STATUS_NO_DEBUGGEE)
                break;
            Sleep(50);
        }
        fprintf(logfile, "STATE=%lu HRESULT=%08lx\n", (unsigned long)state, (unsigned long)hr);
        if (FAILED(hr) || state != DEBUG_STATUS_BREAK)
        {
            result = 4;
            goto finish;
        }
    }
    if (strcmp(__argv[3], "-"))
    {
        in = fopen(__argv[3], "rb");
        if (!in)
        {
            result = 5;
            goto finish;
        }
        n = fread(command, 1, sizeof command - 1, in);
        fclose(in);
        while (n && (command[n - 1] == '\n' || command[n - 1] == '\r'))
            n--;
        command[n] = 0;
        if (n == sizeof command - 1)
        {
            result = 6;
            goto finish;
        }
        hr = control->lpVtbl->Execute(control, DEBUG_OUTCTL_ALL_CLIENTS, command,
                                      DEBUG_EXECUTE_ECHO | DEBUG_EXECUTE_NO_REPEAT);
        fprintf(logfile, "EXECUTE=%08lx\n", (unsigned long)hr);
        if (FAILED(hr))
            goto finish;
    }
    result = 0;
finish:
    if (control)
        control->lpVtbl->Release(control);
    if (client)
    {
        client->lpVtbl->SetOutputCallbacks(client, NULL);
        client->lpVtbl->Release(client);
    }
    fprintf(logfile, "EXIT=%d\n", result);
    fclose(logfile);
    return result;
}
