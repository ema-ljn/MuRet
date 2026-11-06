#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Herramienta didáctica propia: solo observa los campos autorizados de la
 * cámara. */
#include "CaptureFields.h"

static const FieldSpec Fields[] = {
    {"map", 0x006c9dd0, 4},       {"scene", 0x006d5dc8, 4},          {"worldTime", 0x05767b88, 4},
    {"angles", 0x07d36868, 12},   {"position", 0x07d36884, 12},      {"zoom", 0x07d3689c, 4},
    {"distance", 0x07d36ba4, 4},  {"targetDistance", 0x006d5ee0, 4}, {"farPlane", 0x006d5d44, 4},
    {"nearPlane", 0x006d5d40, 4}, {"fieldOfView", 0x006d5d48, 4},    {"fixedHeight", 0x006c9d5c, 4},
    {"level", 0x07d3a0ec, 2},     {"effectTarget", 0x00724588, 12},  {"shake", 0x07d325c8, 4}};

enum
{
    FieldCount = sizeof(Fields) / sizeof(Fields[0])
};

int main(int argc, char** argv)
{
    DWORD pid = 0;
    HANDLE process;
    DWORD ticksBefore;
    DWORD ticksAfter;
    DWORD heroPointer;
    unsigned char values[FieldCount][MaxFieldBytes];
    unsigned char heroPosition[HeroPositionBytes];
    SIZE_T field;
    int complete = 0;
    int hasOutput = argc == 4 && strcmp(argv[2], "--output") == 0;

    /* La redirección antecede a cualquier salida, incluso a los errores de argumentos. */
    if (hasOutput && freopen(argv[3], "w", stdout) == NULL)
    {
        fprintf(stderr, "error field=freopen errno=%08x\n", (unsigned int)errno);
        return 1;
    }
    if (argc != 2 && !hasOutput)
    {
        puts("error reason=invalid_arguments expected=PID_or_--mu-window_then_optional_--output_PATH");
        return 1;
    }
    if (strcmp(argv[1], "--mu-window") == 0)
    {
        if (!MuWindowPid(&pid))
            return 1;
    }
    else if (!ParsePid(argv[1], &pid))
    {
        puts("error reason=invalid_pid expected=nonzero_decimal_dword");
        return 1;
    }
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == NULL)
    {
        printf("error field=OpenProcess win32=%08lx\n", (unsigned long)GetLastError());
        return 1;
    }
    if (!MatchSignature(process, 0x00635ae0, CameraSignature, "camera_signature") ||
        !MatchSignature(process, 0x005fcc30, InverseSignature, "inverse_signature"))
        goto finish;
    puts("match camera=00635ae0 inverse=005fcc30");

    /* El intervalo delimita lecturas secuenciales: no es una captura atómica del
     * cliente. */
    ticksBefore = GetTickCount();
    for (field = 0; field < FieldCount; ++field)
    {
        if (!ReadExact(process, Fields[field].address, values[field], Fields[field].size, Fields[field].name))
            goto finish;
    }
    if (!ReadExact(process, 0x0743a170, &heroPointer, sizeof(heroPointer), "heroPointer"))
        goto finish;
    /* Un puntero nulo omite xyz. Si no es nulo, la muestra exige los 12 bytes
     * completos. */
    if (heroPointer != 0)
    {
        if (heroPointer > MAXDWORD - HeroPositionOffset - (HeroPositionBytes - 1))
        {
            puts("error field=heroPosition reason=address_overflow");
            goto finish;
        }
        if (!ReadExact(process, heroPointer + HeroPositionOffset, heroPosition, sizeof(heroPosition), "heroPosition"))
            goto finish;
    }
    ticksAfter = GetTickCount();
    complete = 1;

finish:
    if (!CloseHandle(process))
    {
        printf("error field=CloseHandle win32=%08lx\n", (unsigned long)GetLastError());
        return 1;
    }
    if (!complete)
        return 1;
    printf("pid=%08lx\n", (unsigned long)pid);
    printf("ticksBefore=%08lx\nticksAfter=%08lx\n", (unsigned long)ticksBefore, (unsigned long)ticksAfter);
    for (field = 0; field < FieldCount; ++field)
        PrintField(Fields[field].name, values[field], Fields[field].size);
    printf("heroPointer=%08lx\n", (unsigned long)heroPointer);
    if (heroPointer != 0)
        PrintField("heroPosition", heroPosition, sizeof(heroPosition));
    if (fflush(stdout) != 0 || ferror(stdout))
    {
        fputs("error field=stdout reason=write_failed\n", stderr);
        return 1;
    }
    return 0;
}
