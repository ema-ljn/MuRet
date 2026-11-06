#include "CaptureFields.h"
#include "CaptureHandle.h"
#include "CaptureOutput.h"

#include <windows.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

// Herramienta didáctica propia: solo observa los campos autorizados de la cámara.
namespace Client::Capture
{
namespace
{
struct CameraSample
{
    DWORD ticksBefore = 0;
    DWORD ticksAfter = 0;
    DWORD heroPointer = 0;
    FieldValues<CameraFieldCount> values{};
    std::array<unsigned char, HeroPositionBytes> heroPosition{};
};

bool ReadSample(HANDLE process, CameraSample& sample)
{
    // El intervalo delimita lecturas secuenciales: no es una captura atómica.
    sample.ticksBefore = GetTickCount();
    if (!ReadFields(process, CameraFields, sample.values) ||
        !ReadValue(process, HeroPointerAddress, sample.heroPointer, "heroPointer"))
        return false;
    if (sample.heroPointer != 0)
    {
        if (!AddressFits(sample.heroPointer, HeroFields[HeroPositionField]))
        {
            std::puts("error field=heroPosition reason=address_overflow");
            return false;
        }
        if (!ReadExact(process, sample.heroPointer + HeroPositionOffset, sample.heroPosition, "heroPosition"))
            return false;
    }
    sample.ticksAfter = GetTickCount();
    return true;
}

void PrintSample(DWORD pid, const CameraSample& sample)
{
    std::printf("pid=%08lx\n", static_cast<unsigned long>(pid));
    std::printf("ticksBefore=%08lx\nticksAfter=%08lx\n", static_cast<unsigned long>(sample.ticksBefore),
                static_cast<unsigned long>(sample.ticksAfter));
    PrintFields(CameraFields, sample.values);
    std::printf("heroPointer=%08lx\n", static_cast<unsigned long>(sample.heroPointer));
    if (sample.heroPointer != 0)
        PrintField("heroPosition", sample.heroPosition);
}

int CaptureCamera(DWORD pid)
{
    CaptureHandle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (process.Get() == nullptr)
    {
        std::printf("error field=OpenProcess win32=%08lx\n", static_cast<unsigned long>(GetLastError()));
        return 1;
    }
    CameraSample sample;
    bool complete = MatchCameraSignatures(process.Get());
    if (complete)
    {
        std::puts("match camera=00635ae0 inverse=005fcc30");
        complete = ReadSample(process.Get(), sample);
    }
    if (const auto error = process.Close())
    {
        std::printf("error field=CloseHandle win32=%08lx\n", static_cast<unsigned long>(*error));
        return 1;
    }
    if (!complete)
        return 1;
    PrintSample(pid, sample);
    return 0;
}

int RunCamera(int argc, char** argv)
{
    const bool hasOutput = argc == 4 && std::strcmp(argv[2], "--output") == 0;
    CaptureOutput output;
    // Redirigir antes de cualquier salida, incluso errores de argumentos.
    if (hasOutput && !output.Redirect(argv[3]))
    {
        std::fprintf(stderr, "error field=freopen errno=%08x\n", static_cast<unsigned int>(errno));
        return 1;
    }
    DWORD pid = 0;
    int status = 1;
    if (argc != 2 && !hasOutput)
        std::puts("error reason=invalid_arguments expected=PID_or_--mu-window_then_optional_--output_PATH");
    else if (std::strcmp(argv[1], "--mu-window") == 0)
    {
        if (MuWindowPid(pid))
            status = CaptureCamera(pid);
    }
    else if (ParsePid(argv[1], pid))
        status = CaptureCamera(pid);
    else
        std::puts("error reason=invalid_pid expected=nonzero_decimal_dword");
    if (!output.Finish())
    {
        std::fputs("error field=stdout reason=write_failed\n", stderr);
        return 1;
    }
    return status;
}
} // namespace
} // namespace Client::Capture

int main(int argc, char** argv)
{
    return Client::Capture::RunCamera(argc, argv);
}
