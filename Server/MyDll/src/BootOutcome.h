#pragma once

#include "HostBoot.h"

namespace MyDll
{
namespace Boot
{
BOOL CompleteBoot(BootResult result, const BootFailure& failure, unsigned relocationCount);
} // namespace Boot
} // namespace MyDll
