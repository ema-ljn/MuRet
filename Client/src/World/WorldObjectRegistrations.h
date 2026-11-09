#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 06 § Lo propio de Lorencia: registro auxiliar de 0x606630, separado de los objetos y actores.
namespace Client::World
{
// Identidad moderna no poseedora: 0 representa el argumento null, no un objeto vivo.
using WorldObjectIdentity = std::uint64_t;
inline constexpr std::size_t WorldObjectRegistrationCount = 200;

struct WorldObjectRegistration
{
    std::uint8_t active = 0;
    // +1..+7 originales: el registro no interpreta ni reinicializa esos bytes.
    std::array<std::uint8_t, 7> metadata{};
    WorldObjectIdentity object = 0;

    friend bool operator==(const WorldObjectRegistration&, const WorldObjectRegistration&) = default;
};

// Estado durable del caller. Los defaults eligen un estado vacío del port; no prueban el reset nativo.
// `nextIdentity` es el siguiente ID libre en este mundo, también al restaurar entradas preexistentes.
// El caller no reutiliza IDs mientras una entrada pueda referirse a ellos ni permite overflow.
// Las entradas guardan identidades, no direcciones: mover/realocar objetos no las invalida. Copiar un
// WorldObject conserva la identidad de una instantánea; no crea otra entidad del mundo.
struct WorldObjectRegistrations
{
    std::array<WorldObjectRegistration, WorldObjectRegistrationCount> entries{};
    WorldObjectIdentity nextIdentity = 1;
};

// Primer active == 0: activa y guarda el argumento; null y duplicados son válidos. Si está lleno no
// cambia nada. No toca los objetos ni calcula poses. El caller conserva metadata y vida de entidades.
void RegisterWorldObject(WorldObjectRegistrations& registrations, WorldObjectIdentity object);
} // namespace Client::World
