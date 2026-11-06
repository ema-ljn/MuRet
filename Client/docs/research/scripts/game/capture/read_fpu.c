#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char** argv)
{
    static const unsigned char signature[16] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x58, 0xa0, 0x99,
                                                0x68, 0xd3, 0x07, 0xc7, 0x05, 0x48, 0x5d, 0x6d};
    unsigned char actual[16];
    HWND window;
    HANDLE process, thread;
    DWORD pid = 0, tid, prior, resumed, readError, started, ended, resumeError, exitCode;
    SIZE_T count = 0;
    CONTEXT context;
    BOOL read;
    if (argc != 3 || strcmp(argv[1], "--output") || !freopen(argv[2], "w", stdout))
        return 2;
    window = FindWindowA("MU", NULL);
    if (!window)
    {
        puts("error field=MU_window");
        return 1;
    }
    tid = GetWindowThreadProcessId(window, &pid);
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process)
    {
        printf("error field=OpenProcess win32=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    read = ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)0x00635ae0, actual, sizeof(actual), &count);
    if (!CloseHandle(process))
    {
        puts("error field=CloseProcess");
        return 1;
    }
    if (!read || count != sizeof(actual) || memcmp(actual, signature, sizeof(actual)))
    {
        puts("error field=signature");
        return 1;
    }
    thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
    if (!thread)
    {
        printf("error field=OpenThread win32=%lu\n", (unsigned long)GetLastError());
        return 1;
    }
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_FLOATING_POINT;
    started = GetTickCount();
    prior = SuspendThread(thread);
    if (prior == (DWORD)-1)
    {
        printf("error field=SuspendThread win32=%lu\n", (unsigned long)GetLastError());
        CloseHandle(thread);
        return 1;
    }
    /* Sin salida ni asignaciones mientras esta pausado. Nunca SetThreadContext. */
    read = GetThreadContext(thread, &context);
    readError = read ? 0 : GetLastError();
    resumed = ResumeThread(thread);
    resumeError = resumed == (DWORD)-1 ? GetLastError() : 0;
    if (resumed == (DWORD)-1)
    {
        /* El controlador conserva el handle y reintenta SU incremento, nunca hasta cero.
         * Root monitoriza este proceso; no se mata mientras recovery_pending este activo. */
        printf("recovery_pending pid=%lu tid=%lu prior=%lu win32=%lu\n", (unsigned long)pid, (unsigned long)tid,
               (unsigned long)prior, (unsigned long)resumeError);
        fflush(stdout);
        do
        {
            exitCode = STILL_ACTIVE;
            if (GetExitCodeThread(thread, &exitCode) && exitCode != STILL_ACTIVE)
            {
                puts("error field=target_exited_during_recovery");
                fflush(stdout);
                CloseHandle(thread);
                return 4;
            }
            Sleep(250);
            resumed = ResumeThread(thread);
            resumeError = resumed == (DWORD)-1 ? GetLastError() : 0;
        } while (resumed == (DWORD)-1);
        puts("recovery_completed");
    }
    ended = GetTickCount();
    if (!CloseHandle(thread))
    {
        puts("error field=CloseThread");
        return 1;
    }
    if (!read)
    {
        printf("error field=GetThreadContext win32=%lu\n", (unsigned long)readError);
        return 1;
    }
    printf("thread_snapshot "
           "1\npid=%08lx\ntid=%08lx\neip=%08lx\ncontrolWord=%04lx\npriorSuspendCount=%lu\nresumePreviousCount=%"
           "lu\nticksBefore=%08lx\nticksAfter=%08lx\nend\n",
           (unsigned long)pid, (unsigned long)tid, (unsigned long)context.Eip,
           (unsigned long)(context.FloatSave.ControlWord & 0xffff), (unsigned long)prior, (unsigned long)resumed,
           (unsigned long)started, (unsigned long)ended);
    if (fflush(stdout) || ferror(stdout))
        return 1;
    return 0;
}
