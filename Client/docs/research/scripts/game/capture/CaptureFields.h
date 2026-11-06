#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

// Lectura y formato compartidos por las dos herramientas de observación.
typedef struct FieldSpec
{
    const char* name;
    DWORD address;
    SIZE_T size;
} FieldSpec;

enum
{
    MaxFieldBytes = 12,
    SignatureBytes = 16,
    HeroPositionOffset = 0x10,
    HeroPositionBytes = 12
};

/* main.exe c32bbd5f..., ImageBase 0x00400000: exactamente 16 bytes por función.
 */
static const unsigned char CameraSignature[SignatureBytes] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x58, 0xa0, 0x99,
                                                              0x68, 0xd3, 0x07, 0xc7, 0x05, 0x48, 0x5d, 0x6d};
static const unsigned char InverseSignature[SignatureBytes] = {0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x0c, 0x8b, 0x45,
                                                               0x08, 0x8b, 0x55, 0x10, 0xd9, 0x41, 0x20, 0xd8};

static int ParsePid(const char* text, DWORD* pid)
{
    DWORD value = 0;
    const unsigned char* cursor = (const unsigned char*)text;
    if (*cursor == '\0')
        return 0;
    for (; *cursor != '\0'; ++cursor)
    {
        DWORD digit;
        if (*cursor < '0' || *cursor > '9')
            return 0;
        digit = (DWORD)(*cursor - '0');
        if (value > (MAXDWORD - digit) / 10)
            return 0;
        value = value * 10 + digit;
    }
    if (value == 0)
        return 0;
    *pid = value;
    return 1;
}

/* La clase MU la registra el cliente (02 § arranque, RegisterClassA). No enumera procesos. */
static int MuWindowPid(DWORD* pid)
{
    HWND window = FindWindowA("MU", NULL);
    if (window == NULL)
    {
        puts("error field=FindWindowA reason=mu_window_not_found");
        return 0;
    }
    if (GetWindowThreadProcessId(window, pid) == 0 || *pid == 0)
    {
        puts("error field=GetWindowThreadProcessId reason=pid_unavailable");
        return 0;
    }
    return 1;
}

static int ReadExact(HANDLE process, DWORD address, void* output, SIZE_T size, const char* field)
{
    SIZE_T bytesRead = 0;
    BOOL success = ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)address, output, size, &bytesRead);
    if (!success || bytesRead != size)
    {
        DWORD error = success ? ERROR_PARTIAL_COPY : GetLastError();
        printf("error field=%s address=%08lx requested=%08lx read=%08lx win32=%08lx\n", field, (unsigned long)address,
               (unsigned long)size, (unsigned long)bytesRead, (unsigned long)error);
        return 0;
    }
    return 1;
}

static int MatchSignature(HANDLE process, DWORD address, const unsigned char* expected, const char* field)
{
    unsigned char actual[SignatureBytes];
    if (!ReadExact(process, address, actual, sizeof(actual), field))
        return 0;
    if (memcmp(actual, expected, sizeof(actual)) != 0)
    {
        printf("error field=%s address=%08lx reason=mismatch\n", field, (unsigned long)address);
        return 0;
    }
    return 1;
}

/* Las palabras se imprimen como bits numéricos little-endian, sin convertir los
 * floats. */
static void PrintField(const char* name, const unsigned char* bytes, SIZE_T size)
{
    SIZE_T offset;
    printf("%s=", name);
    for (offset = 0; offset < size; offset += 4)
    {
        SIZE_T width = size - offset;
        SIZE_T remaining;
        if (width > 4)
            width = 4;
        if (offset != 0)
            putchar(',');
        for (remaining = width; remaining != 0; --remaining)
            printf("%02x", (unsigned int)bytes[offset + remaining - 1]);
    }
    putchar('\n');
}
