#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Los tramos del cuerpo del tooltip de un ítem, 0x5b2aa0 (06 § Los tooltips, "El cuerpo del ítem"), en el orden en
// que los escribe: el nombre, las líneas de cada tipo, el daño y los stats, la durabilidad, los requisitos, las
// clases, las opciones y el conjunto. Cada uno escribe en TooltipWriter y devuelve el error de lo que MuRet no porta.
namespace Client::Scenes
{
struct GameHudContext;
class TooltipWriter;

// Lo que comparten los tramos: el ítem ([EBP+0x10]), su registro de Item.bmd ([EBP−0x18]), su nivel ((D+4 >> 3) &
// 0xf, [EBP−0x14]) y el color del nombre ([EBP−0x1c]).
struct ItemTooltipFrame
{
    GameHudContext& context;
    TooltipWriter& lines;
    const World::ItemState& item;
    std::span<const std::byte, Data::Tables::ItemRecordSize> record;
    std::int32_t level = 0;
    std::int32_t nameColor = 0;
};

// Un byte del registro de Item.bmd.
[[nodiscard]] std::uint8_t RecordByte(const ItemTooltipFrame& frame, std::size_t offset);
// D+0x1b & 0x3f: alguna opción excelente.
[[nodiscard]] bool IsExcellent(const World::ItemState& item);

// Lo que se lee de una entrada de Skill.bmd con su acceso abierto: el nombre (+0, hasta su '\0' o el fin de la
// entrada), el byte +0x21, el word +0x22 (el maná), el word +0x24 (el texto 360, "Stamina usada") y el byte +0x26 (el
// texto 174, "Range"), lo que 0x551c70 deja en sus punteros.
struct SkillEntryFields
{
    std::string name;
    std::uint8_t byte21 = 0;
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
    std::uint8_t range = 0;
};

// El acceso de la entrada `skill` de Skill.bmd (0x551c70 o el mismo código en línea): se abre, se leen sus campos y
// se cierra, con sus 60 rand() si su cuenta vuelve a 0.
[[nodiscard]] Core::Expected<SkillEntryFields, std::string>
ReadTheSkillEntry(GameHudContext& context, std::uint32_t skill, std::string_view where);
// Lo mismo desde un tramo: con un error ya guardado en el escritor, ese error, sin abrir el acceso.
[[nodiscard]] Core::Expected<SkillEntryFields, std::string>
ReadTheSkillEntry(ItemTooltipFrame& frame, std::uint32_t skill, std::string_view where);

// 0x40cf30(buffer, tipo, D+0x1c), un thiscall sobre [0x6eed3c]: con D+0x1c & 3 de 1 a 3, el conjunto del byte
// (& 3) − 1 de ItemSetType.bmd; si no es 0 ni 0xff, los 32 bytes del nombre de su registro de ItemSetOption.bmd y un
// espacio. Sin conjunto, nada. Un conjunto fuera de los 64 o un nombre sin '\0' leen fuera: error.
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string> ItemSetNameOf(const GameHudContext& context,
                                                                                    const World::ItemState& item);
// Lo mismo con el tipo y la pieza (& 3) dados, como la llama la ayuda del ítem con 1 y 2 (0x5b0ccd y 0x5b0d45).
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string>
ItemSetNameOf(const GameHudContext& context, std::uint16_t type, std::uint8_t piece);

// 0x5b371b..0x5b45f4: el renglón del nombre, en el color del nombre y en negrita, y un "\n".
[[nodiscard]] Core::Expected<void, std::string> WriteTheItemName(ItemTooltipFrame& frame);

// 0x5b45f5..0x5b4f92: las líneas del 0x1a13, de las misiones (0x1c17 a 0x1c1a), del 0x1c0c, del 0x1a14 y del
// 0x1c13.
[[nodiscard]] Core::Expected<void, std::string> WriteTheKindLines(ItemTooltipFrame& frame);

// 0x5b4f93..0x5b58e9: el daño, la defensa, las velocidades, la tasa de defensa y lo del 0x1c20 al 0x1c22 y del
// 0x1c0b.
[[nodiscard]] Core::Expected<void, std::string> WriteTheStats(ItemTooltipFrame& frame);

// 0x5b58ea..0x5b6a3b: los paquetes de joyas, las líneas de cada tipo y las alas.
[[nodiscard]] Core::Expected<void, std::string> WriteTheNotes(ItemTooltipFrame& frame);

// 0x5b6a3c..0x5b79bc: la durabilidad, el daño de las flechas, las resistencias y los requisitos.
[[nodiscard]] Core::Expected<void, std::string> WriteTheRequirements(ItemTooltipFrame& frame);

// 0x5af590(registro): las clases que usan el ítem de `record` (Item.bmd+0x45..+0x49, la evolución + 1 de cada una),
// desde el renglón que sigue de `lines`. Pone en 0 los colores de N + 2 y N + 3 y, con alguna clase, escribe un "\n"
// (con su medio) y: el MG (la clase 3 del héroe), el 61 o el 60 con el 23 según su byte +0x48 y su evolución; con una o
// dos clases, el 61 con el nombre de cada una (15 + 5 · byte + clase); con tres o cuatro, el 60 con el de cada clase
// que no lo usa (20 + clase); con cinco, nada más. Lo que el héroe no cumple va en rojo si su clase lo usa y en el 5 si
// no. La llaman el cuerpo del ítem (0x5b79d2) y la ayuda del ítem (0x5b0da8).
void WriteTheItemClasses(const GameHudContext& context, TooltipWriter& lines,
                         std::span<const std::byte, Data::Tables::ItemRecordSize> record);

// 0x5b2320(tipo, renglón, opción, valor, maná): el texto de la opción `tag` en el renglón que sigue de `lines`, sin su
// color ni su negrita. Las que leen el maná abren la entrada de la skill de la opción (0x551c70, que pisa el maná del
// argumento: 60 rand() si su cuenta vuelve a 0). Una opción que no está en la tabla de 0x5b2a00 deja el renglón como
// estaba. La llaman las opciones del ítem (0x5b7c62), la caja azul (0x5b7e49) y el tooltip de las mascotas (0x423b1d).
[[nodiscard]] Core::Expected<void, std::string> WriteAnOption(GameHudContext& context, TooltipWriter& lines,
                                                              std::int32_t type, std::uint8_t tag, std::uint8_t value);

// 0x5b79bd..0x5b801a: las clases (0x5af590), lo que suma cada tipo, el stat del conjunto (0x40dd80), las opciones
// (0x5b2320), las cajas 0x1a20 a 0x1a25 y las opciones del conjunto (0x40eee0).
[[nodiscard]] Core::Expected<void, std::string> WriteTheOptions(ItemTooltipFrame& frame);
} // namespace Client::Scenes
