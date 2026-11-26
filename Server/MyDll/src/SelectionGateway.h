#pragma once

#include <windows.h>

namespace MyDll
{
namespace Boot
{
struct BootSession;

typedef void(__cdecl* NativeReceive)(DWORD, DWORD, DWORD, int, DWORD, DWORD);
typedef void(__cdecl* NativeList)(unsigned char*);

struct SelectionHost
{
    NativeReceive receive;
    DWORD(__cdecl* send)(int, char*, DWORD);
    NativeList list;
    const unsigned char* objects;
};

bool ConfigureSelection(BootSession& session);
bool BindSelection(BootSession& session, const SelectionHost& host, unsigned mgLevel, unsigned dlLevel);
} // namespace Boot
} // namespace MyDll
