#pragma once

#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Los globales del comercio, la party y el duelo (CL9 hito 7, 04 § El comercio, § La party y § El duelo), en la .bss
// salvo los que dicen otra cosa. Los escriben los paquetes (Scenes/SocialEvents) y los leen sus ventanas.
namespace Client::Scenes
{
// 0x78a5a20 y 0x78a92a8: las dos grillas del comercio, 8 × 4 de 0x48 bytes; 0x77f3dc8, la copia de 0x5bc5e0, 32 más.
inline constexpr std::size_t TradeGridCells = 0x20;
inline constexpr std::int32_t TradeGridColumns = 8;
inline constexpr std::int32_t TradeGridRows = 4;
// 0x78aba1c..0x78aba26: el nombre del otro (10 bytes) y su terminador.
inline constexpr std::size_t TradeNameSize = 11;
// +0x44 de una casilla de 0x78a5a20 que vuelve peor que su copia (0x5bc470); el paso del inventario no la pisa
// (0x5e5172).
inline constexpr std::uint8_t ChangedFromTheCopy = 99;
// 0x77f3cd0: los lugares de la party, de 0x24 bytes. El 42 y el 44 no les ponen tope (Q-C960).
inline constexpr std::size_t PartySlots = 5;
// 0x78abae0..0x78abaea: el nombre del rival y su terminador.
inline constexpr std::size_t DuelNameSize = 11;

struct TradeState
{
    std::array<std::byte, TradeNameSize> otherName{};
    // 0x78abab0: el nivel del otro (el WORD del 37, en un DWORD).
    std::uint32_t otherLevel = 0;
    // 0x6c9324 (.data −1): el gremio del otro.
    std::int32_t otherGuild = -1;
    // 0x78abab4: el zen del otro (3B).
    std::uint32_t otherMoney = 0;
    // 0x78abab8: el zen propio que el GS aceptó (3A).
    std::uint32_t ownMoney = 0;
    // 0x57678ac: el zen que el diálogo 0x73 mandó en el último 3A.
    std::uint32_t sentMoney = 0;
    // 0x78abac0 y 0x78abac1: el OK del otro y el propio.
    std::uint8_t otherOk = 0;
    std::uint8_t ownOk = 0;
    // 0x78abac4: lo pone en 0 el 37; lo lee el paso (0x5ecde0).
    std::int32_t field78abac4 = 0;
    // 0x4d55bd..0x4d562b: el 37 con 1 llama a SetCursorPos(mouse x · 260 / 640 · ancho / 640, mouse y · alto / 480)
    // con el mouse y la pantalla del momento. MuRet lo deja pedido acá y no mueve el cursor (08 P-C202).
    std::uint8_t cursorMoveRequested = 0;
    // 0x78abac8: la espera del botón de OK, que pone en 0x96 el zen (0x626b20) y baja el paso.
    std::int32_t okWait = 0;
    // 0x78abaa5: algo cambió en la grilla del otro (0x5bc470) o se apretó el OK (0x5ecf95).
    std::uint8_t changed = 0;
    // 0x78abacc: el botón del zen la pone en 2 (0x5ece9a); la lee el OK del 0x73 sin el comercio abierto (0x6270ed,
    // el zen del almacén, hito 6). Los demás lectores quedan para la VM (08 P-C203).
    std::int32_t field78abacc = 0;
    // 0x78abb14 y 0x6c932e (.data): el aviso del OK con la grilla del otro cambiada (0x5ecf67), que pide 0x633300(4,
    // 0x173, 0x94).
    std::int32_t warningKind = 0;
    std::uint8_t warningByte = 0;
    std::array<World::ItemState, TradeGridCells> otherGrid{};
    std::array<World::ItemState, TradeGridCells> ownGrid{};
    std::array<World::ItemState, TradeGridCells> backup{};
    // 0x5767968 y 0x57635bc: el 3D que llegó con la traba 0x78abb39, para 0x4d1400.
    std::uint8_t resultDeferred = 0;
    std::array<std::uint8_t, 4> deferredHead{};
};

// Un lugar de 0x77f3cd0 (0x4d67e0): el nombre en +0 con su terminador en +0xa, el número (+0xb), el mapa (+0xc), la
// celda (+0xd y +0xe), la vida (+0x10), la máxima (+0x14) y los décimos del 44 (+0x18).
struct PartySlot
{
    std::array<std::byte, TradeNameSize> name{};
    std::uint8_t number = 0;
    std::uint8_t map = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint32_t life = 0;
    std::uint32_t maxLife = 0;
    std::uint8_t tenths = 0;
    // +0x1c: el personaje del viewport con su nombre, o −1 (no está), −2 (sin buscar: lo deja 0x5e4140) o −3 (el
    // héroe publicado no se ve). Lo escriben 0x59e3b0 y 0x59e700 en cada frame (PartyActors.h); la .bss empieza en 0 y
    // el 42 no lo toca.
    std::int32_t actorIndex = 0;
    // +0x20: el +0x78 de ese personaje cuando lo encontró, o 0. Con 8, la lista de la derecha lo tiñe de verde
    // (0x5f14c1).
    std::uint32_t actorFlags = 0;
};

struct PartyState
{
    std::array<PartySlot, PartySlots> slots{};
    // 0x78aba4c: la clave del que invita (40), que el diálogo 0x77 devuelve en el 41.
    std::int32_t requestKey = 0;
    // 0x6c8e50 (.data −1): el miembro que se sigue; el 43 lo vuelve a −1.
    std::int32_t followTarget = -1;
};

// 0x78abad8, 0x78abadc y 0x78abae0 también los lee el cursor de ataque (0x558bb0): el hito 3 los guarda en
// AttackRules (duelActive, duelKey y duelName) y espera que el hito 7 los escriba ahí. Al juntar las dos ramas queda
// una sola copia (08 P-C206).
struct DuelState
{
    // 0x78abad8: el duelo en curso (el marcador de 0x5a6cc0).
    std::uint8_t active = 0;
    // 0x78abadc: la clave del rival.
    std::int32_t rivalKey = 0;
    std::array<std::byte, DuelNameSize> rivalName{};
    // 0x78abaf8 y 0x78abafc: los puntos propios y los del rival (AD).
    std::int32_t ownScore = 0;
    std::int32_t rivalScore = 0;
    // 0x5767814 y 0x5767816: el AB, con el primero en 2, pone el WORD en 0 (*estático*; qué son queda para la VM,
    // 08 P-C201).
    std::uint8_t byte5767814 = 0;
    std::uint16_t word5767816 = 0;
};

struct SocialState
{
    TradeState trade{};
    PartyState party{};
    DuelState duel{};
    // 0x6c8e78 (.data 10), 0x77f3b6a y 0x77f3b6c: el largo del campo del diálogo, el modo del zen y el zen escrito
    // (0x5ece58 abre el 0x73 con 8, 1 y 0; 0x4d58da lo cierra con 0x2a y 0).
    std::int32_t dialogFieldLimit = 10;
    std::uint8_t moneyField = 0;
    std::int32_t typedMoney = 0;
};
} // namespace Client::Scenes
