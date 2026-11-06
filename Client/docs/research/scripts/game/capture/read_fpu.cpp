#include "CaptureFields.h"
#include "CaptureHandle.h"
#include "CaptureOutput.h"
#include "ThreadSuspension.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace Client::Capture
{
namespace
{
bool CheckProcessSignature(DWORD pid)
{
    CaptureHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (process.Get() == nullptr)
    {
        std::printf("error field=OpenProcess win32=%lu\n", static_cast<unsigned long>(GetLastError()));
        return false;
    }
    std::array<unsigned char, SignatureBytes> actual{};
    SIZE_T count = 0;
    const BOOL read =
        ReadProcessMemory(process.Get(), reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(CameraSignatureAddress)),
                          actual.data(), actual.size(), &count);
    if (process.Close().has_value())
    {
        std::puts("error field=CloseProcess");
        return false;
    }
    if (!read || count != actual.size() || actual != CameraSignature)
    {
        std::puts("error field=signature");
        return false;
    }
    return true;
}

bool CloseThread(ThreadSuspension& suspension)
{
    if (suspension.Close().has_value())
    {
        std::puts("error field=CloseThread");
        return false;
    }
    return true;
}

struct RecoveryResult
{
    bool resumed;
    bool outputOk;
};

RecoveryResult RecoverSuspension(ThreadSuspension& suspension, DWORD pid, DWORD tid)
{
    const auto& result = suspension.Result();
    std::printf("recovery_pending pid=%lu tid=%lu prior=%lu win32=%lu\n", static_cast<unsigned long>(pid),
                static_cast<unsigned long>(tid), static_cast<unsigned long>(result.prior),
                static_cast<unsigned long>(result.resumeError));
    // Los fallos de salida se propagan después de satisfacer la obligación.
    const bool outputOk = std::fflush(stdout) == 0;
    do
    {
        if (suspension.ConfirmTargetExit())
        {
            std::puts("error field=target_exited_during_recovery");
            const bool flushed = std::fflush(stdout) == 0;
            return {false, outputOk && flushed};
        }
        Sleep(ThreadSuspension::RecoveryMs);
    } while (!suspension.TryResume());
    std::puts("recovery_completed");
    return {true, outputOk};
}

void PrintSnapshot(DWORD pid, DWORD tid, const CONTEXT& context, const ThreadSuspension::CaptureResult& result,
                   DWORD started, DWORD ended)
{
    constexpr DWORD ControlWordMask = 0xffff;
    std::printf("thread_snapshot "
                "1\npid=%08lx\ntid=%08lx\neip=%08lx\ncontrolWord=%04lx\npriorSuspendCount=%lu\nresumePreviousCount=%"
                "lu\nticksBefore=%08lx\nticksAfter=%08lx\nend\n",
                static_cast<unsigned long>(pid), static_cast<unsigned long>(tid),
                static_cast<unsigned long>(context.Eip),
                static_cast<unsigned long>(context.FloatSave.ControlWord & ControlWordMask),
                static_cast<unsigned long>(result.prior), static_cast<unsigned long>(result.resumed),
                static_cast<unsigned long>(started), static_cast<unsigned long>(ended));
}

int CaptureThread(DWORD pid, DWORD tid)
{
    ThreadSuspension suspension(
        OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid));
    if (!suspension.IsOpen())
    {
        std::printf("error field=OpenThread win32=%lu\n", static_cast<unsigned long>(GetLastError()));
        return 1;
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_FLOATING_POINT;
    const DWORD started = GetTickCount();
    if (const auto error = suspension.SuspendAndCapture(context))
    {
        std::printf("error field=SuspendThread win32=%lu\n", static_cast<unsigned long>(*error));
        static_cast<void>(CloseThread(suspension));
        return 1;
    }
    RecoveryResult recovery{true, true};
    if (suspension.IsPending())
    {
        recovery = RecoverSuspension(suspension, pid, tid);
        if (!recovery.resumed)
        {
            static_cast<void>(CloseThread(suspension));
            return 4;
        }
    }
    const DWORD ended = GetTickCount();
    if (!CloseThread(suspension))
        return 1;
    const auto& result = suspension.Result();
    if (!result.read)
    {
        std::printf("error field=GetThreadContext win32=%lu\n", static_cast<unsigned long>(result.readError));
        return 1;
    }
    PrintSnapshot(pid, tid, context, result, started, ended);
    return recovery.outputOk ? 0 : 1;
}

int CaptureFpu()
{
    HWND window = FindWindowA("MU", nullptr);
    if (window == nullptr)
    {
        std::puts("error field=MU_window");
        return 1;
    }
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(window, &pid);
    if (!CheckProcessSignature(pid))
        return 1;
    return CaptureThread(pid, tid);
}

int RunFpu(int argc, char** argv)
{
    CaptureOutput output;
    if (argc != 3 || std::strcmp(argv[1], "--output") != 0 || !output.Redirect(argv[2]))
        return 2;
    const int status = CaptureFpu();
    const bool written = output.Finish();
    // Exit 4 conserva la señal de salida del hilo durante la recuperación.
    return status == 4 ? 4 : written ? status : 1;
}
} // namespace
} // namespace Client::Capture

int main(int argc, char** argv)
{
    return Client::Capture::RunFpu(argc, argv);
}
