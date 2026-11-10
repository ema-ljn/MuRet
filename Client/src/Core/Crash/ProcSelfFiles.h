#pragma once

#include "Core/Crash/CrashReport.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Lo que el manejador de caídas de Linux lee de /proc/self, en vez de las fotos de toolhelp del
// original (CreateToolhelp32Snapshot, 0x67f9c9 y 0x67fdb9; Q-C241). Son funciones puras sobre el
// texto ya leído, sin memoria nueva, así que se prueban en cualquier sistema.
namespace Client::Core::Crash
{
// Los módulos de /proc/self/maps, como los de Module32First/Module32Next: cada archivo mapeado
// (una ruta que empieza con '/') es uno, con su nombre (lo que sigue a la última '/'), su ruta, la
// dirección donde empieza su primer mapeo y lo que mide hasta el final del último. Van en el orden
// del archivo, que es el de las direcciones, hasta 48; el resto se pierde, como en el original.
// Una dirección o un tamaño que no entra en 32 bits se guarda con los de abajo y el tamaño, con
// 0xFFFFFFFF. Devuelve cuántos llenó.
[[nodiscard]] std::size_t ModulesFromProcMaps(std::string_view maps, std::span<CrashModule, MaxCrashModules> modules);

// Los hilos del proceso, el campo 20 (num_threads) de /proc/self/stat: el cntThreads de
// PROCESSENTRY32 (0x67faf7). Vacío si el texto no tiene ese campo.
[[nodiscard]] std::optional<std::uint32_t> ThreadCountFromProcStat(std::string_view stat);
} // namespace Client::Core::Crash
