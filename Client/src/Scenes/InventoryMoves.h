#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "GameLogic/PathCell.h"
#include "Protocol/FloorItems.h"
#include "Protocol/Inventory.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Levantar, soltar y equipar el ítem del cursor (CL8 hito 4; 06 § El inventario, "El ítem del cursor"): lo que no
// es de la UI de 0x5bd750, 0x5be150, el tramo del inventario de 0x5ca250, 0x5bec50, 0x5c4110 y MoveHero, y las
// respuestas 24, 26, 27, 28, 2A, F3 13 y F3 14. La grilla es la del inventario (0x78a9bb0, 8 × 8) con el equipo en los
// lugares 0 a 11: las otras (comercio, almacén, caja del caos, tienda personal y la del entrenador) cortan con su
// dirección.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::UI
{
class PanelRegistry;
}

namespace Client::World
{
struct ItemState;
}

namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;

// Lo que los pasos de la UI y MoveHero piden afuera para mover ítems. Vacío o nullptr: lo que lo necesita corta.
struct InventoryServices
{
    // *[0x7758550], Item.bmd: el ancho (+0x24), el alto (+0x25), el lugar del equipo (+0x22) y +0x49 de cada tipo.
    const Data::Tables::ItemTable* items = nullptr;
    // 0x512a30 sobre el héroe publicado (DressTransitionHero), con el sonido que pida 0x4f2250 en lastSoundId.
    std::function<Core::Expected<void, std::string>()> dressTheHero{};
    // 0x420cf0: suelta el cuervo y, fuera de los mapas 18 a 23, lo crea de nuevo (ReplaceTheRaven).
    std::function<Core::Expected<void, std::string>()> replaceTheRaven{};
    // 0x555190(nombre, el texto `index` de Text.bmd, tipo): un mensaje en el chat, con el tipo 2 (UI::ErrorMessage)
    // o el 1 (UI::SystemMessage, las flechas que se equipan). Los nombres que pasa el original (0x77f3bdc a 0x77f3be4,
    // 0x78abb44, 0x78abb60 a 0x78abb6c) son buffers que nadie escribe: van vacíos (*inferido*, 08 P-C72).
    std::function<Core::Expected<void, std::string>(std::size_t index, std::int32_t type)> systemMessage{};
    // El C1 0E 24 en C3 por la sesión (ServerSession::SendItemMove), después de los dos rand() del envío.
    std::function<Core::Expected<void, std::string>(const Protocol::Inventory::ItemMove&)> sendItemMove{};
    // CL9 hito 5. Text.bmd, para los textos que se arman con sprintf.
    const Data::Text::TextTable* texts = nullptr;
    // 0x555190 con un texto ya armado (sprintf con un texto de Text.bmd como formato) y el nombre vacío.
    std::function<Core::Expected<void, std::string>(std::string_view text, std::int32_t type)> chatLine{};
    // El C1 06 23, el C1 05 22 y el C1 06 26 en C3 por la sesión, después de los dos rand() del envío.
    std::function<Core::Expected<void, std::string>(const Protocol::FloorItems::Drop&)> sendDrop{};
    std::function<Core::Expected<void, std::string>(std::uint16_t index)> sendPickUp{};
    std::function<Core::Expected<void, std::string>(const Protocol::FloorItems::UseItem&)> sendUseItem{};
    // 0x5fc260(1) y 0x5fac90 con el rayo del mouse, como MoveHero (0x59de04..0x59de78): la celda del terreno bajo el
    // mouse (0x7d366e0 y 0x7d366e4 · 0,01) o nada.
    std::function<Core::Expected<std::optional<GameLogic::PathCell>, std::string>()> pickTheGround{};
};

// Los servicios de `resources`, sin el envío: Item.bmd, 0x512a30 con Player.bmd, el cuervo y los mensajes.
[[nodiscard]] InventoryServices InventoryServicesOf(const WorldEntryResources& resources);

// El ancho (+0x24) y el alto (+0x25) del registro de Item.bmd de `type`.
struct ItemFootprint
{
    std::int32_t width = 0;
    std::int32_t height = 0;
};

[[nodiscard]] Core::Expected<ItemFootprint, std::string> ItemFootprintOf(const Data::Tables::ItemTable* items,
                                                                         std::uint16_t type, std::string_view where);

// La casilla bajo el mouse para el ítem del cursor en una grilla desde (left, top): _ftol de
// (mouse − esquina) · 0.05f − tamaño · 0.5f + 0.5f, en la x87 (0x5ca2af..0x5ca2fd), que trunca hacia el 0.
struct GridCell
{
    std::int32_t column = 0;
    std::int32_t row = 0;
};

[[nodiscard]] GridCell CellUnderTheMouse(std::int32_t mouseX, std::int32_t mouseY, std::int32_t left, std::int32_t top,
                                         ItemFootprint footprint);

// 0x5c9b40 con la grilla del inventario: si el ítem del cursor entra en la casilla `cell`. Cuenta las casillas
// vacías (tipo 0xffff) de su huella dentro de la grilla; con todas, sí. Si no, con el mismo tipo en la casilla de
// arriba a la izquierda (fila · columnas + columna, sin mirar si la columna se pasa), los que se apilan:
// 0x1c00..0x1c08, 0x807 y 0x80f con el mismo +4, 0x1c1d y 0x1a20..0x1a22. Fuera del arreglo el original lee igual
// (Q-C474): MuRet no apila (*inferido*, 08 P-C72).
[[nodiscard]] bool ItemFitsInTheGrid(const World::ItemState& cursor, std::span<const World::ItemState> cells,
                                     std::int32_t columns, std::int32_t rows, GridCell cell, ItemFootprint footprint);

struct InventoryMoveContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const InventoryServices& services;
};

// 0x5bd750(lugar, 0x78a9bb0, 8): saca el ítem de `slot`. 0xff, nada. Del 12 en adelante, vacía la huella del ítem
// de esa casilla desde su columna y su fila (tipo 0xffff y +0x3c en 0), en la grilla del inventario hasta el 75 y en
// la segunda desde el 76. Del 0 al 11, con el registro propio abierto: vacío, nada; si no, el cuervo (0x421d60) si
// es el 0x1a05 del lugar 1, el tipo en 0xffff, +0x3c en −1 y +0x1b y +0x1c en 0; después, en los lugares 0 y 1, las
// flechas (0x561840) y la mano del héroe, y en el 8 los ayudantes (0x443d40) y la mascota del héroe.
[[nodiscard]] Core::Expected<void, std::string> RemoveInventoryItem(const InventoryMoveContext& context,
                                                                    std::int32_t slot);

// 0x5be150: con un ítem en el cursor, lo vuelve a su lugar ([0x78a6758]): la huella en la grilla, con +0x3c en 1 en
// la casilla de su columna y su fila, o el equipo (con la mascota, el cuervo y el anillo antes); después la cuenta
// en 0, 0x512a30 y el sonido 0x1d.
[[nodiscard]] Core::Expected<void, std::string> ReturnTheCursorItem(const InventoryMoveContext& context);

// El C1 0E 24 del ítem del cursor desde la grilla del inventario: la traba 0x78abb39 en 1, los dos rand() del envío
// en C3 y el paquete, del lugar [0x78a6758] a `targetSlot`, con los bytes 0, +4, +0x1a, +0x1b, +0x1c, el tipo >> 5
// & 0xf0 y 0.
[[nodiscard]] Core::Expected<void, std::string> SendTheCursorItem(const InventoryMoveContext& context,
                                                                  std::uint8_t targetSlot);

// 0x5c52b1..0x5c5320, desde 0x5c4110: levanta el ítem de la casilla `cell` de la grilla (la de su columna y su
// fila): el clic consumido, la copia al cursor desde la de arriba a la izquierda, la grilla de origen, el lugar,
// 0x5bd750 y el ítem bajo el mouse en nada. Lo que sigue (0x6599a0 del comercio y el sonido) es de la UI.
[[nodiscard]] Core::Expected<void, std::string> PickUpFromTheGrid(const InventoryMoveContext& context,
                                                                  std::size_t cell);

// 0x5c2c65..0x5c2dd0, desde 0x5bec50 sin ítem en el cursor y con el clic: con 0x576794c, nada; con 0x78abb0c,
// reparar (corta); si no, consume el clic y, salvo lo que el mapa 10 o Chaos Castle no dejan sacar, pasa el ítem del
// lugar al cursor, viste al héroe, borra el ítem bajo el mouse, pide el sonido 0x1d y apaga los ayudantes (el 8) o el
// cuervo (el 0x1a05 del 1). El registro propio lo tiene abierto quien llama.
[[nodiscard]] Core::Expected<void, std::string> PickUpFromTheEquipment(const InventoryMoveContext& context,
                                                                       std::size_t slot);

// 0x59bd3e..0x59cd23, desde MoveHero (0x59bd01) con el cursor vacío, la mano derecha vacía y un arma en la izquierda:
// en un acceso al registro propio, el ítem del lugar 1 al cursor (los 0x48 bytes, con su +0x3c) y el lugar en 0xffff
// con +4 en 0; 0x512a30 sobre el héroe publicado; la grilla del inventario, el lugar 1 y el destino 0; y, sin la
// traba, el C1 0E 24 del 1 al 0.
[[nodiscard]] Core::Expected<void, std::string> MoveTheLeftWeaponToTheRight(const InventoryMoveContext& context);

// 0x561c50, desde el clic de ataque de MoveHero (0x59d65f, CL9 hito 3): si el arma de tiro tiene con qué tirar. Abre y
// cierra el registro propio para leer las manos; con una ballesta en la derecha (0x808..0x80e, 0x810 y 0x812..0x9ff)
// mira los virotes (0x807) de la izquierda con su durabilidad (+0xbce), y si no, con un arco en la izquierda
// (0x800..0x806, 0x811, 0x814 y 0x815), las flechas (0x80f) de la derecha con la suya (+0xb86): en otro acceso, sin
// ellas o gastadas, 0x561840 (que las busca en el inventario y las equipa) y falso; con ellas, verdadero. Sin arma de
// tiro, verdadero.
[[nodiscard]] Core::Expected<bool, std::string> CheckTheArrows(const InventoryMoveContext& context);

// 0x5c35ad, desde 0x5bec50 sin ítem en el cursor y sin el clic: la marca del lugar (3 si le falta un requisito,
// con +0x26 contra la fuerza, Q-C475; si no, 2), el ítem bajo el mouse y la esquina de su ayuda.
void HoverTheEquipment(const InventoryMoveContext& context, std::size_t slot, std::int32_t left, std::int32_t top,
                       float width);

// 0x5bee04..0x5bf2a0, desde 0x5bec50 con un ítem en el cursor y el mouse en el lugar: si va ahí (las manos, la
// clase del héroe publicado, los requisitos, el mapa y Crywolf). Si va, la marca 2 y, con el clic, el destino
// (0x77f3cc8), el aviso 1862 de Fenrir y el 24; si no, la marca 3 y, con el clic, 0x5be150. El registro propio lo
// tiene abierto quien llama.
[[nodiscard]] Core::Expected<void, std::string> DropOnTheEquipment(const InventoryMoveContext& context,
                                                                   std::size_t slot);

// 0x5ca250(x, y, 0x78a9bb0, 8, 8, 0), el tramo del inventario: la casilla bajo el mouse, si entra (0x5c9b40), las
// marcas de su huella dentro de la grilla y, con el clic, el 24 al lugar de esa casilla, la vuelta a su lugar o
// nada. Si no entra, con el inventario abierto, una joya de bendición, del alma o de la vida va sobre el ítem de esa
// casilla (0x5d82a8, CL9 hito 5): con su tipo, su nivel y las consultas, el C1 06 26 con el lugar de la joya y el del
// ítem; la de bendición sobre el 0x1a25, el diálogo 0xa9. Devuelve lo que devuelve el original: verdadero si el clic
// quedó en la grilla.
struct GridDrop
{
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    std::int32_t left = 0;
    std::int32_t top = 0;
    // IsOpen(0x4000000), que no saca rand().
    bool inventoryOpen = false;
    // Las consultas de 0x659290 de la joya (0x5d838c..0x5d83c2). Sin él, la joya que llega a ellas corta.
    UI::PanelRegistry* panels = nullptr;
};

[[nodiscard]] Core::Expected<bool, std::string> DropOnTheInventoryGrid(const InventoryMoveContext& context,
                                                                       const GridDrop& drop);

// 0x5df449..0x5e070e, desde 0x5d8d50 con el clic fuera de las ventanas: lo que no se tira al piso (el aviso 269 y
// 0x5be150), las cajas de 0x1a20 a 0x1a25 (el aviso 1915 y 0x5be150) y los paquetes de joyas y el Jewel of Guardian
// (0x402b30 y 0x5e06b6: 0x5be150, con el aviso 269 salvo con el inventario abierto y el mouse a su derecha). Lo demás
// (CL9 hito 5): las siete consultas de las ventanas con el mouse sobre ellas, en su orden (con el rand() de 0x10000 y
// el de 0x20), el anillo del mago con el registro propio abierto (el aviso 923 con 80 o 40 por el nivel; el nivel 3
// no se tira), el 0x1c1c (0x4379f0, del hito 6: corta) y, con todo eso y el comercio cerrado (otro rand()), el terreno
// bajo el mouse: si lo hay, los dos rand() del C3 y el C1 06 23 con su celda y el lugar [0x78a6758], que queda en
// 0x6c82ec, y la espera del mouse (0x77f3b10 en 0 y 0x6c8dcc en 6); si no, nada. Si no se tira, 0x5be150.
struct GroundDrop
{
    std::int32_t mouseX = 0;
    // IsOpen(0x4000000), que no saca rand().
    bool inventoryOpen = false;
    // Las otras consultas de 0x659290 (0x5df5b6..0x5df6c2 y 0x5dfda8). Sin él, lo que las necesita corta.
    UI::PanelRegistry* panels = nullptr;
};

[[nodiscard]] Core::Expected<void, std::string> DropOnTheGround(const InventoryMoveContext& context,
                                                                const GroundDrop& drop);

// CL9 hito 5. Usar el ítem de la casilla `cell` de la grilla del inventario, lo que repiten las ranuras rápidas de
// 0x59ff10 (0x5a2bc8.., 0x5a35e0.., 0x5a3d64.. y 0x5a44eb..) y la grilla (0x5c58c5.., 0x5c609b.. y 0x5c7418..): con el
// almacén, 0x80 o el comercio abiertos (0x5af520, con el rand() del comercio), el aviso 474 del tipo 2; si no, con
// 0x5767954 en más de 0, nada; si no, 0x5767954 en 10, los dos rand() del C3, el C1 06 26 con la casilla + 12, 0 y
// [0x78abb26], y el sonido por el tipo de la casilla (0x21 la 0x1c00 y 0x20 las 0x1c01..0x1c09).
[[nodiscard]] Core::Expected<void, std::string> UseInventoryItem(const InventoryMoveContext& context,
                                                                 UI::PanelRegistry& panels, std::int32_t cell);

// Lo mismo con el lugar y el destino del 26 que se pasan, como el clic derecho de 0x5c4110 sobre los libros, las
// esferas y el 0x1a0b (lugar = casilla + 12, destino 0) y la joya sobre un ítem de 0x5ca250 (el lugar de la joya,
// [0x78a6758], y el del ítem). Del lugar y del destino salen sus bytes de abajo. `where` es la dirección de las tres
// consultas, la que lleva el aviso 474.
struct InventoryUse
{
    std::int32_t slot = 0;
    std::int32_t target = 0;
    std::string_view where;
};

[[nodiscard]] Core::Expected<void, std::string> UseInventoryItemOn(const InventoryMoveContext& context,
                                                                   UI::PanelRegistry& panels, const InventoryUse& use);

// 0x5bc740(0x78a9bb0, 8, 8, lugar, ítem, 0) (0x5bcd0b.. y su cola): el ítem en su lugar y, con el registro abierto,
// los ayudantes sin mascota, el cuervo sin el lugar 1 y la capa sin el anillo de nivel 3. Un tipo 0x1fff no pone nada.
[[nodiscard]] Core::Expected<void, std::string>
PlaceMovedItem(std::uint8_t slot, const Protocol::Inventory::ItemData& item, const WorldEntryResources& resources);

// 0x4d1400, el 24 cifrado: suelta la traba; con 0xff, 0x5be150; si no, la cuenta en 0 y, en el inventario,
// 0x5bc740 con el último argumento en 0 y el sonido 0x1d.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemMoveResult(const Protocol::Inventory::ItemMoveResult& result,
                                                                    const WorldEntryResources& resources);
// 0x4c5ec0, el 28: 0x5bd750 con el lugar (los de la tienda personal cortan) y el fin de la espera.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemDelete(const Protocol::Inventory::ItemDelete& deleted,
                                                                const WorldEntryResources& resources);
// 0x4d3cc0, el 2A: la durabilidad del lugar del equipo, con el registro abierto, o de toda la huella en la grilla, y
// el fin de la espera.
[[nodiscard]] Core::Expected<void, std::string> ApplyDurability(const Protocol::Inventory::Durability& durability,
                                                                const WorldEntryResources& resources);
// 0x4d3510, el 26, con el registro propio abierto (3.848 rand() al cerrarlo si queda sin accesos): 0xfd, el fin de
// la espera (0x5767954 en 0); 0xfe, la vida máxima (+0x26); 0xff, la vida (+0x22); otro, la poción del lugar: la
// durabilidad (+0x1a) de la casilla lugar − 12 de la grilla baja 1 si no es 0 y, en 0, la casilla queda vacía (tipo
// 0xffff y +0x3c en 0). Fuera de la grilla el original escribe igual: MuRet corta (0x4d35f6).
[[nodiscard]] Core::Expected<void, std::string> ApplyLifeRefill(const Protocol::Inventory::LifeRefill& refill,
                                                                WorldState& world, Render::Models::CrtRandom& random);
// 0x4d3820, el 27, con el registro propio abierto: 0xfe, el maná máximo (+0x28) y el BP máximo (+0x3a); 0xff, el
// maná (+0x24) y el BP (+0x38); otro, el maná y la poción del lugar, como el 26 (0x4d3917). El 0xfd no es el fin de
// la espera: es un lugar más.
[[nodiscard]] Core::Expected<void, std::string> ApplyManaRefill(const Protocol::Inventory::ManaRefill& refill,
                                                                WorldState& world, Render::Models::CrtRandom& random);
// 0x4e31a3, el F3 14: la cuenta del cursor en 0, 0x5bc740 con el último argumento en 0 y el sonido 0x45.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemPut(const Protocol::Inventory::ItemPut& put,
                                                             const WorldEntryResources& resources);
// 0x4d10d0, el 23 (CL9 hito 5): con 0, 0x5be150; si no, con un lugar del equipo (menos de 12), con el registro propio
// abierto (3.848 rand() al cerrarlo sin accesos), el cuervo (0x421d60) si es el 0x1a05 del lugar 1, el tipo en
// 0xffff, +0x3c y +0x44 en 0, 0x512a30 y, en el 8, los ayudantes (0x443d40); con otro, 0x5bd750. Después la cuenta
// del cursor en 0. Siempre, 0x6c82ec en −1.
[[nodiscard]] Core::Expected<void, std::string> ApplyDropResult(const Protocol::FloorItems::DropResult& result,
                                                                const WorldEntryResources& resources);
// Un 23 corto (07 Q-C1240): de 0x4d10d0, el lugar tirado (0x6c82ec) en −1, que escribe en todos los caminos, y con
// pkt[3] distinto de 0 el cursor vacío (0x7879fbc en 0, 0x4d13c7), también en todos los que leen pkt[4].
void ApplyShortDropResult(const Protocol::FloorItems::ShortDropResult& result, WorldState& world);
// 0x4e316e, el F3 13: el personaje de la clave (0x5110d0) y, si es un jugador, su equipo (0x5139f0, CL9.2): corta.
// Si no lo es, o sin ninguno, nada.
[[nodiscard]] Core::Expected<void, std::string> ApplyEquipmentChange(const Protocol::Inventory::EquipmentChange& change,
                                                                     const WorldEntryResources& resources);
} // namespace Client::Scenes
