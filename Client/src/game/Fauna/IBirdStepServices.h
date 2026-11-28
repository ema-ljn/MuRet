#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace Client::Fauna
{
inline constexpr std::size_t BirdRecordSize = 0x1d4;
using BirdRecord = std::span<std::byte, BirdRecordSize>;

// Borrowed views: the caller does not own actors, models, CRT or terrain.
struct BirdModelBorrow
{
    void* identity;
    std::uint8_t* action;
};

// 0x4455e0/0x445890/0x439680: spawn; 0x445a30/0x445a90/0x445b90/0x445d60/0x439aa0: dispatch; 0x445ec0: common;
// 0x4e9980: mandatory final; 0x4ea030: selection with argument 7.
enum class BirdHandler : std::uint8_t
{
    SpawnIcarus,
    SpawnAtlans, // 445890: mapa7
    SpawnKalima, // 439680: mapas24..29/36
    MoveBat,
    MoveButterfly,
    MoveBirdOrCrow,
    MoveRidingSpear,
    MoveMonster34,
    CommonMotion,
    Finalize,
    SelectAction,
};

// `value` keeps the full EAX, even an unnormalized AL; `error` cuts without rollback.
struct BirdCallResult
{
    std::int32_t value;
    std::error_code error;
};

// Call does not normalize AL/EAX: index only reaches 0x4455e0/0x445d60/0x445ec0; SelectAction uses argument 7. Each
// method must operate on the same globals/pool/CRT. BorrowModel takes base+model*224; the borrowed identity survives
// until Pose ends. Height/Wave hand over ST0 rounded R53 under the approved PC53/RN profile; no F32 spill.
// ConvertTileCoordinate receives R53 and keeps the backend's __ftol policy. The Main binding guarantees RN, binary53
// operations and compilation without fast-math/contraction; these preconditions are not probed or altered during the
// valid original walk. The BirdRecord storage must represent trivial native scalar fields, not non-trivial C++
// objects; the owner guarantees lifetime/logical alignment and exclusive access to the step. For CommonMotion,
// p/index match the same pool/base 40 used in 0x445ec0→0x4e9a40; the exclusion of 0x4e9a40 is by index, not by
// address. See StepBirds' precondition.
class IBirdStepServices
{
public:
    virtual ~IBirdStepServices() = default;
    virtual std::int32_t Map() = 0;
    virtual std::int32_t Mode() = 0;
    // Selects the current global actor. Each reference identifies the live storage of that CANONICAL object until
    // StepBirds ends, even after Random, effects and new queries. Querying another actor does not recycle/invalidate
    // the earlier borrow: do not return reused scratch. An early copy does not work either: late mutations of the
    // cached actor must be observed.
    virtual const glm::vec3& ActorPosition() = 0;
    virtual std::int32_t Random() = 0;
    virtual std::int32_t ConvertTileCoordinate(double value) = 0;
    virtual std::int32_t TileIndex(std::int32_t x, std::int32_t y) = 0;
    virtual const std::uint16_t& TileAttributes(std::int32_t index) = 0;
    virtual std::error_code
    PrefixEffect(const glm::vec3& position) = 0; // 0x518cf0 with 0xc2, rotation 0, light 1, 3, 0, -1, 0, 0, 0, 0
    virtual std::error_code PrefixLight(float x, float y) = 0; // 0x5f9780 (-.3,-.3,-.2), 16, canonical buffer
    virtual std::error_code Audio(std::int32_t id, std::byte* object) = 0; // 0x41ed10(id, object, 0), logical request
    virtual std::error_code PrepareSpecial() = 0;                          // 612d90(1f)
    virtual BirdCallResult Call(BirdHandler handler, BirdRecord bird, std::int32_t index) = 0;
    virtual BirdModelBorrow BorrowModel(std::int16_t model) = 0;
    virtual std::error_code Pose(BirdModelBorrow model, BirdRecord bird,
                                 float factor) = 0;                            // 4ec200: +114,+118,+112,factor,+10,+1c
    virtual std::error_code Rotation(BirdRecord bird) = 0;                     // 5fc8f0(+1c,+9c)
    virtual glm::vec3 Transform(BirdRecord bird, const glm::vec3& vector) = 0; // 5fcbd0(vector,+9c,delta)
    virtual double Height(float x, float y) = 0;
    // Mandatory backend of the PC53/RN profile: the internal FSIN result with 64 bits retained, then R53 and the F32
    // spill in that order; not std::sin(double) or a host long double cast. This signature claims no equivalence of NaN
    // payloads or flags/exceptions.
    virtual float SinSpill(float phase) = 0;
    virtual double Wave(float sine) = 0;                                           // 622110
    virtual bool AudioDistance(const glm::vec3& bird, const glm::vec3& actor) = 0; // FSQRT XY C0: <600 or unordered
};
} // namespace Client::Fauna
