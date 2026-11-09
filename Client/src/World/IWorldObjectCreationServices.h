#pragma once

#include "World/WorldObjectRegistrations.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// Frontera síncrona de 0x606660 con 0x518cf0; contrato: 06 § Creación común de los objetos.
namespace Client::World
{
struct WorldObject;

struct WorldObjectModelEffectRequest
{
    std::int16_t type;
    // Prestada sólo durante CreateModelEffect: source.position en 180, local recalculado en 322.
    const glm::vec3& position;
    float scale;
    WorldObjectIdentity owner; // 0 conserva el argumento null, incluso con source vivo.
};

class IWorldObjectCreationServices
{
public:
    virtual ~IWorldObjectCreationServices() = default;

    // Ángulos y luz se toman prestados de source. Los otros argumentos nativos son subtipo 0,
    // key -1 y tres ceros. No retener request/referencias ni diferir la llamada: la siguiente emisión
    // re-lee source. true significa binding ejecutado (también si el pool está lleno); false,
    // dependencia aún no integrada. Binding y objeto de servicio estables y vivos durante el batch;
    // el caller conserva los efectos durables. El provider retorna normalmente sin lanzar: noexcept
    // exige ese contrato, no recupera excepciones ni fabrica un resultado parcial al terminar.
    virtual bool CreateModelEffect(const WorldObjectModelEffectRequest& request, WorldObject& source) noexcept = 0;
};
} // namespace Client::World
