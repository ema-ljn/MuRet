#pragma once

#include "Kernel/WorldObject.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Kernel
{
struct WorldObject;
} // namespace Client::Kernel

// 0x606660's synchronous boundary with 0x518cf0; contract: 06 § Creación común de los objetos.
namespace Client::WorldObjects
{
struct WorldObjectModelEffectRequest
{
    std::int16_t type;
    // Borrowed only during CreateModelEffect: source.position at 180, local recomputed at 322.
    const glm::vec3& position;
    float scale;
    Kernel::WorldObjectIdentity owner; // 0 keeps the null argument, even with a live source.
};

class IWorldObjectCreationServices
{
public:
    virtual ~IWorldObjectCreationServices() = default;

    // Angles and light are borrowed from source; the other native arguments are subtype 0, key -1 and three zeros. Do
    // not retain request or references or defer the call: the next emission re-reads source. true means the binding ran
    // (also with the pool full); false, a dependency not yet integrated. Binding and service object stay stable and
    // alive during the batch; the caller keeps the durable effects. The provider returns normally without throwing:
    // noexcept demands that contract, it does not recover exceptions or fabricate a partial result on exit.
    virtual bool CreateModelEffect(const WorldObjectModelEffectRequest& request,
                                   Kernel::WorldObject& source) noexcept = 0;
};
} // namespace Client::WorldObjects
