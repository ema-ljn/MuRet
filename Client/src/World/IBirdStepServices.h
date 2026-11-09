#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>

namespace Client::World
{
inline constexpr std::size_t BirdRecordSize = 0x1d4;
using BirdRecord = std::span<std::byte, BirdRecordSize>;

// Vistas prestadas: el caller no posee actores, modelos, CRT ni terreno.
struct BirdModelBorrow
{
    void* identity;
    std::uint8_t* action;
};

// 4455e0/445890/439680: spawn;445a30/445a90/445b90/445d60/439aa0: dispatch;
// 445ec0: común;4e9980: final obligatorio;4ea030: selección con argumento7.
enum class BirdHandler : std::uint8_t
{
    SpawnIcarus,
    SpawnAtlans, // 445890: mapa7
    SpawnKalima, // 439680: mapas24..29/36
    MoveAf,
    MoveAe,
    MoveAdBc,
    Move10f,
    Move173,
    CommonMotion,
    Finalize,
    SelectAction,
};

// value conserva EAX completo, incluso AL no normalizado. error corta sin rollback.
struct BirdCallResult
{
    std::int32_t value;
    std::error_code error;
};

// Call no normaliza AL/EAX: index sólo llega a4455e0/445d60/445ec0;
// SelectAction usa el argumento7. Cada método debe operar sobre los mismos globals/pool/CRT.
// BorrowModel toma base+signedType*224; la identidad prestada sobrevive hasta terminar Pose.
// Height/Wave entregan ST0 redondeado R53 bajo el perfil aprobado PC53/RN; no spillF32.
// ConvertTileCoordinate recibe R53 y conserva la política __ftol del backend.
// El binding Main garantiza RN, operaciones binarias53 y compilación sin fast-math/contracción.
// Estas precondiciones no se sondean ni alteran durante el recorrido original válido.
// El almacenamiento BirdRecord debe representar campos escalares nativos triviales; no objetos
// C++ no triviales. El dueño garantiza lifetime/alineación lógica y acceso exclusivo al step.
// Para CommonMotion, p/index corresponden al mismo pool/base40 usado en445ec0→4e9a40;
// la exclusión de4e9a40 es por índice, no por dirección. Véase precondición de StepBirds.
class IBirdStepServices
{
public:
    virtual ~IBirdStepServices() = default;
    virtual std::int32_t Map() = 0;
    virtual std::int32_t Mode() = 0;
    // Selecciona el actor global actual. Cada referencia identifica el almacenamiento vivo de ese
    // objeto CANÓNICO hasta terminar StepBirds, incluso tras Random, efectos y nuevas consultas.
    // Consultar otro actor no recicla/invalida el préstamo anterior: no devolver scratch reutilizado.
    // Una copia temprana tampoco sirve: se deben observar mutaciones tardías del actor cacheado.
    virtual const glm::vec3& ActorPosition() = 0;
    virtual std::int32_t Random() = 0;
    virtual std::int32_t ConvertTileCoordinate(double value) = 0;
    virtual std::int32_t TileIndex(std::int32_t x, std::int32_t y) = 0;
    virtual const std::uint16_t& TileAttributes(std::int32_t index) = 0;
    virtual std::error_code PrefixEffect(const glm::vec3& position) = 0;   // 518cf0 C2, rot0, light1, 3,0,-1,0,0,0,0
    virtual std::error_code PrefixLight(float x, float y) = 0;             // 5f9780 (-.3,-.3,-.2),16, buffer canónico
    virtual std::error_code Audio(std::int32_t id, std::byte* object) = 0; // 41ed10(id,object,0), solicitud lógica
    virtual std::error_code PrepareSpecial() = 0;                          // 612d90(1f)
    virtual BirdCallResult Call(BirdHandler handler, BirdRecord bird, std::int32_t index) = 0;
    virtual BirdModelBorrow BorrowModel(std::int16_t signedType) = 0;
    virtual std::error_code Pose(BirdModelBorrow model, BirdRecord bird,
                                 float factor) = 0;                            // 4ec200: +114,+118,+112,factor,+10,+1c
    virtual std::error_code Rotation(BirdRecord bird) = 0;                     // 5fc8f0(+1c,+9c)
    virtual glm::vec3 Transform(BirdRecord bird, const glm::vec3& vector) = 0; // 5fcbd0(vector,+9c,delta)
    virtual double Height(float x, float y) = 0;
    // Backend obligatorio del perfil PC53/RN: resultado interno FSIN con64 bits retenidos,
    // después R53 y spillF32 en ese orden; no std::sin(double) ni cast de long double del host.
    // No se afirma equivalencia de payloads NaN ni de flags/excepciones mediante esta firma.
    virtual float SinSpill(float phase) = 0;
    virtual double Wave(float sine) = 0;                                           // 622110
    virtual bool AudioDistance(const glm::vec3& bird, const glm::vec3& actor) = 0; // FSQRT XY C0: <600 o unordered
};
} // namespace Client::World
