#pragma once

#include "HostBoot.h"

namespace MyDll
{
namespace Boot
{
struct ObserverOptions
{
    bool enabled;
    bool receive;
    bool send;
    bool includeSubcode;
    bool logMalformed;
    unsigned maxRows;
};

ObserverOptions ReadObserverOptions(const char* ini);
bool OpenObserver(const char* path, const ObserverOptions& options);
void CloseObserver();
void ConfigureObserver(BootSession& session);
DWORD __cdecl ObserveReceive(DWORD proto, DWORD packet, DWORD length, int index, DWORD encrypted, DWORD serial);
DWORD __cdecl ObserveSend(int index, char* packet, DWORD length);
} // namespace Boot
} // namespace MyDll
