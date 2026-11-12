#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::GameLogic
{
struct CharacterAppearance;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// 0x505820, el conjunto completo +9 de un personaje, y lo que deja en [0x743a178] y [0x743a17c] para
// 0x550990 y para el dibujo de los personajes. Contrato: Client/docs/research/06-render-escenas-y-ui.md,
// "El conjunto completo de 0x505820".
namespace Client::Scenes
{
struct ActorState;
struct OwnCharacterRecord;
class WorldState;

// [0x743a178] y [0x743a17c], en la .bss: 0 al arrancar. Solo los escribe 0x505820, los dos en cada
// llamada; los leen 0x550990 (0x550b1e..0x550b2a) y el dibujo de los personajes (0x50ff2a..0x510313).
struct Scan505820
{
    std::int32_t dword743a178 = 0;
    std::uint8_t byte743a17c = 0;
};

// Lo que 0x505820 lee de una parte del cuerpo: el modelo (+0, con signo) y el nivel (+2, & 0xf).
struct ScannedPart
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
};

inline constexpr std::size_t ScannedPartCount = 5;

// Lo que lee del personaje: la clase (+0x1d4, & 7) y las cinco partes del cuerpo, de +0x218 a +0x298 de
// a 0x20, en el orden de GameLogic::BodyPart (del casco a las botas).
struct ScannedCharacter
{
    std::uint8_t characterClass = 0;
    std::array<ScannedPart, ScannedPartCount> body{};
};

[[nodiscard]] ScannedCharacter ScannedCharacterOf(const ActorState& actor);
// Un personaje de la lista o el del login: lo que 0x517270 y 0x5139f0 dejan en esas partes.
[[nodiscard]] ScannedCharacter ScannedCharacterOf(const GameLogic::CharacterAppearance& appearance);

// Lo que deja 0x505820: lo que devuelve en al y los dos globales.
struct FullSetScan
{
    bool complete = false;
    Scan505820 globals;
};

// 0x505820(personaje), cdecl. Con alguna parte en 0xffff, o con una de nivel menor que 9 o de otro índice
// ((modelo − 0x212) con el resto con signo de 512) que las botas, no hay conjunto: [0x743a178] en 0. Si
// no, [0x743a178] es el nivel más chico. La clase 3 no mira el casco. [0x743a17c] queda en 1, salvo con
// la clase 3 y el conjunto: abre el registro propio (Render::Models::LogicalAccess sobre
// `ownRecordAccesses`), lo pone en 0 si la armadura del registro (+0xc44) no es 0x100f, 0x1014, 0x1017
// ni 0x1020, y lo cierra, con 3.848 rand() de `random` si deja la cuenta en 0.
[[nodiscard]] FullSetScan ScanFullSet(const ScannedCharacter& character, const OwnCharacterRecord& own,
                                      std::uint8_t& ownRecordAccesses, Render::Models::CrtRandom& random);

// 0x50c904..0x50c926: lo que 0x50ba30 hace con un 0x20a con +0x3e4 en 0, en cada frame del original que lo
// dibuja, `frames` veces: 0x505820 con el registro propio y la cuenta de accesos de `world`, y los
// globales en WorldRuntime::fullSetScan. Devuelve lo del último, que lee el conjunto completo de la cola de
// efectos (0x50fddb). Con `frames` en 0 (un dibujo entre frames), lo mismo sobre copias del rand() y de la
// cuenta, sin publicar nada.
[[nodiscard]] FullSetScan ScanTheDrawnCharacter(WorldState& world, const ScannedCharacter& character,
                                                Render::Models::CrtRandom& random, int frames);
} // namespace Client::Scenes
