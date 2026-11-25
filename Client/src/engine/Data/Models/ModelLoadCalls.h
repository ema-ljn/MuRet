#pragma once

#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextureCalls.h"
#include "EngineApi.h"

#include <span>

// Las cargas globales de efectos que alcanzan Castle Siege y Crywolf; 0x615700 desde avance 5 de 0x61ccb0.
namespace Client::Data::Models
{
// 0x6161ea, 0x616333/349, 0x6163c2/3d8 y 0x61679e..0x616854, con archivos BMD del modo Data\\.
[[nodiscard]] ENGINE_API std::span<const ModelFile> SiegeModelFiles();
// 0x616370/38d: después de SetNext(0x69), Gate02 y los dos StoneCoffin. Gate02 se consulta dos veces.
[[nodiscard]] ENGINE_API std::span<const ModelTextureCall> SiegeBeforeSkillTextureCalls();
// Las llamadas explícitas 0x616b7e/9c y 0x616c7a..0x616d58, seguidas por las de estos modelos en el lazo 0x616e20.
// El cache por nombre de 0x60d550 conserva el primer path y slot, incluso cuando el lazo vuelve a pedir Skill\\.
[[nodiscard]] ENGINE_API std::span<const ModelTextureCall> SiegeTextureCalls();
} // namespace Client::Data::Models
