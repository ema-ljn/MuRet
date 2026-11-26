#pragma once

#include "HostBoot.h"

namespace MyDll
{
namespace Boot
{
BOOL CompleteBoot(BootResult result, const BootFailure& failure, unsigned relocationCount,
                  unsigned slotCount = NativeMapSlot);
void ReportTerrainLoad(bool complete, unsigned loaded, unsigned failedMap);
} // namespace Boot
} // namespace MyDll
