#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace Client::Render::Models
{
class CrtRandom;
}

// La tabla de sonidos de los modelos de los monstruos (CL9 hito 3, fase 2c): lo que 0x612d90 escribe con 0x612c80.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los sonidos del modelo". *Estático*.
namespace Client::Scenes
{
// 0x612c80: las diez WORD de sonido de un modelo (+0xaa..+0xbc de su BMD en [0x5769ad8]). 0x4f37c0 lee la 0 y la 1
// (caminar), 0x4f4a10 y 0x4f53a0 la 2 y la 3 (pegar y el golpe) y 0x4f5660 la 4 (morir); −1 (0xffff) es sin sonido.
// Nadie más escribe esas WORD.
using ModelSounds = std::array<std::int16_t, 10>;
// Lo que 0x612c80 escribió en cada modelo. Un modelo que no está, nadie lo escribió: en el original sus diez WORD
// quedan en 0, como todo el arreglo de 0x5769ad8 después de 0x60dbe0 (el REP STOSD de 0x60dc6e..0x60dc80).
using ModelSoundTable = std::map<std::uint16_t, ModelSounds>;

// 0x612d90(monster), su tabla de sonidos: el switch de 0x613127..0x613130 (la tabla de 0x615340, del 0 al 0x56) llama
// a 0x612c80 en el modelo 0x152 + monster con las diez WORD de su caso, salvo el 0xc y el 0xd, que escriben las de
// 0x15e y 0x15f, las dos. Los casos sin 0x612c80 (0x26, 0x38, 0x3c, 0x3d, 0x4a, 0x4b, 0x4e a 0x55 y del 0x57 en
// adelante) no escriben nada. Lo de antes del switch (las acciones, la escala y el salto de un modelo ya cargado, en
// 0x612dba..0x612dee) es de la carga del modelo, que MuRet no hace (08, P-C176): escribe siempre, y con los mismos
// valores da lo mismo que el original.
void WriteMonsterSounds(std::int32_t monster, ModelSoundTable& table);

// Lo que 0x612c80 escribió en `model`, o nada si nadie lo escribió.
[[nodiscard]] std::optional<ModelSounds> WrittenSounds(const ModelSoundTable& table, std::uint16_t model);

// Las diez WORD que lee el original: las de `sounds` o, si nadie las escribió, las diez en 0 de 0x60dc80.
[[nodiscard]] ModelSounds SoundsOrZeros(const std::optional<ModelSounds>& sounds);

// El sonido de dos WORD (0x4f3efa..0x4f3f6c, caminar, desde la 0; 0x4f51f1..0x4f5262, pegar, y 0x4f553d..0x4f55ac, el
// golpe, desde la 2): con la WORD `word` en −1, ninguno y sin rand(); si no, rand() % 2 (con signo, de un rand() que no
// es negativo) elige la `word` o la siguiente, con signo, más 5 con el subtipo 9 y más 0xd2. Sin la tabla, las diez
// WORD en 0 (SoundsOrZeros): el rand() y el sonido 0xd2 (0xd7 con el subtipo 9). *Estático*.
[[nodiscard]] std::optional<std::int32_t> DrawModelSound(const std::optional<ModelSounds>& sounds, std::size_t word,
                                                         std::int32_t subtype, Render::Models::CrtRandom& random);
} // namespace Client::Scenes
