#pragma once

#include "Core/Expected.h"
#include "UI/Dialogs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// Los paneles del juego (06 § HUD y ventanas, "Los paneles"): bits de [0x576352c] en modo inmediato, no clases. El
// paso los atiende en 0x5ee140 y el HUD los dibuja con funciones sueltas. El registro reproduce sus funciones con
// el orden de consultas del original, porque las del comercio (0x20) y la tienda de NPC (0x10000) son bytes de la
// tabla de protección y cada consulta saca un rand(). El estado de cada id es un byte propio o uno que vive en otro
// objeto (DelegatedPanels): un id cuyo dueño no está portado queda cerrado y su consulta saca lo que saca. Lo que
// un panel hace al abrirse o al cerrarse y es de otra ola corta con su dirección (decisión 4 del plan de la CL8).
namespace Client::UI
{
using PanelId = std::uint32_t;

inline constexpr PanelId FriendsPanel = 0x1;    // la UI vieja: 0x490a00(viejo+0x48)
inline constexpr PanelId MovePanel = 0x2;       // M, la ventana /move: el modo 4 de 0x77f3b08
inline constexpr PanelId PartyPanel = 0x4;      // P
inline constexpr PanelId QuestPanel = 0x8;      // T
inline constexpr PanelId GuildPanel = 0x10;     // G
inline constexpr PanelId TradePanel = 0x20;     // byte protegido 0x7d53711
inline constexpr PanelId WarehousePanel = 0x40; // el almacén
inline constexpr PanelId ClosedPanel = 0x80;    // 0x659429: su rama da siempre 0
inline constexpr PanelId ChaosBoxPanel = 0x100;
inline constexpr PanelId CommandsPanel = 0x200; // D
inline constexpr PanelId PetPanel = 0x400;
inline constexpr PanelId PersonalShopPanel = 0x800;    // la tienda personal propia
inline constexpr PanelId EventNpcPanel = 0x1000;       // 0x7d53712 con 0x7d53714 en 0
inline constexpr PanelId Panel2000 = 0x2000;           // 0x7d53718, sin identificar
inline constexpr PanelId SecondEventNpcPanel = 0x4000; // 0x7d53712 con 0x7d53714 en 1
inline constexpr PanelId TrainerPanel = 0x8000;        // el NPC de la caja 0x723c80
inline constexpr PanelId NpcShopPanel = 0x10000;       // byte protegido 0x7d53710
inline constexpr PanelId OtherShopPanel = 0x20000;     // la tienda personal ajena
inline constexpr PanelId SecondGuildPanel = 0x40000;
inline constexpr PanelId CastleSiegePanel = 0x80000;
inline constexpr PanelId CastleSiegeListPanel = 0x100000;
inline constexpr PanelId SecondCastleSiegePanel = 0x200000;
inline constexpr PanelId SiegeWeaponPanel = 0x400000;       // 0x724598 en 1 (*inferido*: el arma de asedio)
inline constexpr PanelId SecondSiegeWeaponPanel = 0x800000; // 0x724598 en 2
inline constexpr PanelId SiegeGatePanel = 0x1000000;        // 0x7245a0 (*inferido*: la puerta)
inline constexpr PanelId CharacterPanel = 0x2000000;        // C
inline constexpr PanelId InventoryPanel = 0x4000000;        // I y V
// IsOpen(0): cualquiera de los bits 1..0x40000000, en ese orden, hasta el primero abierto.
inline constexpr PanelId AnyPanel = 0;
// 0x657a90: CloseByMask(−1, 0).
inline constexpr PanelId AllPanels = 0xffffffffu;

// 0x659290 con 2: la ventana /move es el modo 4 de las superposiciones.
inline constexpr std::int32_t MoveOverlayMode = 4;
// 0x5b0550: las ayudas de F1 (1 y 2) y la del ítem del chat (3).
inline constexpr std::int32_t FirstHelpMode = 1;
inline constexpr std::int32_t SecondHelpMode = 2;
inline constexpr std::int32_t ItemHelpMode = 3;

// 0x40a450 y 0x40a650: los doce comandos de la D, de los que el 0 no se elige ni se dibuja.
inline constexpr std::size_t CommandCount = 12;
// +0x14..+0x1f de [0x6e592c]: el estado de un comando que 0x406050 pone en cada paso y que la entrada al login y a la
// selección dejan así.
inline constexpr std::uint8_t CommandReady = 1;

// [0x6e592c] (0x40acd0 lo arma en la .data con +8 y +0x20 en −1): lo que la ventana de los comandos guarda además del
// +4 (DelegatedPanels::commands). La esquina (+0xc y +0x10) va en Scenes::HudState, como las de las otras ventanas.
struct CommandsWindowState
{
    // +8: a quién va el comando. Lo pone el comando de 0x406050 (0x406097.., CL9.4); MuRet lo deja en −1.
    std::int32_t target = -1;
    // +0x14..+0x1f: 0x406050 los pone en 1 en cada paso, antes de mirar el comando elegido. El dibujo (0x40abe5) da un
    // aviso con 0, 0xff o 0xfe. *Inferido*: arrancan en 1, porque la entrada al login (0x634bf0) y a la selección
    // (0x634271) los ponen así antes del primer paso del juego; el constructor no los toca.
    std::array<std::uint8_t, CommandCount> states{CommandReady, CommandReady, CommandReady, CommandReady,
                                                  CommandReady, CommandReady, CommandReady, CommandReady,
                                                  CommandReady, CommandReady, CommandReady, CommandReady};
    // +0x20: el comando elegido con el clic (0x40a50a), −1 sin ninguno.
    std::int32_t chosen = -1;
};

// Los ids cuyo estado vive en otro objeto. MuRet no tiene a sus dueños: quedan en 0, cerrados, hasta su ola.
struct DelegatedPanels
{
    // 1: 0x490a00(*(0x5763514)+0x48), la de amigos del administrador viejo (hito 7, CL9.4).
    std::uint8_t friends = 0;
    // 8: *(0x710148)+0x21383. IsOpen lo mira distinto de 0 y CanOpen no abre nada con 1.
    std::uint8_t quest = 0;
    // 0x10: 0x6522e0, el gremio ([0x7d536f0], CL9.4).
    std::uint8_t guild = 0;
    // 0x200: +4 de [0x6e592c], la ventana de los comandos (D, CL8 hito 6). La abre Open y la cierran CloseByMask y su
    // botón de cerrar (0x40a58a).
    std::uint8_t commands = 0;
    // Lo demás de [0x6e592c] que leen el paso (0x40a450) y el dibujo (0x40a650) de los comandos.
    CommandsWindowState commandsWindow{};
    // 0x400: 0x420cc0, 0x723911 en 2 (la mascota). 0x424230 lo vuelve a 0 en cada paso con el personaje cerrado.
    std::uint8_t pet = 0;
    // 0x8000: 0x449bf0, 0x724580 > 0. Con 3 (0x449c00), abrir el inventario no lo cierra.
    std::int32_t trainer = 0;
    // 0x40000: 0x6570a0, el gremio 2 ([0x7d536f4]).
    std::uint8_t secondGuild = 0;
    // 0x80000, 0x100000 y 0x200000: [0x7d536f8]+4, [0x7d536fc]+4 y [0x7d53700]+0x14 (Castle Siege, CL9.5).
    std::uint8_t castleSiege = 0;
    std::uint8_t castleSiegeList = 0;
    std::uint8_t secondCastleSiege = 0;
    // [0x7d536fc]+0x18: el modo de la lista de CS; con 3 va junto con el inventario.
    std::int32_t castleSiegeListMode = 0;
    // 0x400000 y 0x800000: 0x44dc10, 0x724598 en 1 o 2; distinto de 0, el ancho del mundo es 450 (0x44dc20).
    std::uint8_t siegeWeapon = 0;
    // 0x1000000: 0x44f0f0, 0x7245a0; distinto de 0, el ancho del mundo es 450 (0x44f100).
    std::uint8_t siegeGate = 0;
    // 0x661b90 sin ítem en el cursor: algún lugar de la grilla de la caja del caos (0x7d2d380) ocupado (CL9.3).
    bool chaosBoxHasItem = false;
    // [0x7d53704]+0x10: el diálogo de los paneles (0x65feb0; lo abre 0x6600d0 desde los paquetes, CL9.4, P-C90).
    // 0x657880 le da la tecla primero.
    std::uint8_t panelDialog = 0;
};

// Los bytes del registro y los globales que sus funciones escriben, todos en la .bss.
struct PanelState
{
    std::uint8_t inventory = 0;    // 0x7d53708
    std::uint8_t character = 0;    // 0x7d53709
    std::uint8_t party = 0;        // 0x7d5370a
    std::uint8_t warehouse = 0;    // 0x7d5370d
    std::uint8_t chaosBox = 0;     // 0x7d5370e
    std::uint8_t personalShop = 0; // 0x7d5370f, el de 0x800 y 0x20000
    // 0x7d53710 y 0x7d53711 viven en la tabla de protección: cada consulta los abre y los cierra, y el cierre que
    // deja la cuenta en 0 saca un rand() (06 § La tabla de protección).
    std::uint8_t npcShop = 0;
    std::uint8_t npcShopDepth = 0;
    std::uint8_t trade = 0;
    std::uint8_t tradeDepth = 0;
    std::uint8_t eventNpc = 0;     // 0x7d53712
    std::uint8_t eventNpcKind = 0; // 0x7d53714
    std::uint8_t panel2000 = 0;    // 0x7d53718
    // 0x77f3b08, el modo de las superposiciones de 0x5b0550.
    std::int32_t overlayMode = 0;
    // 0x77f3b0c: el ítem de la ayuda del modo 3, como lugar de Item.bmd. Lo escribe la ayuda del ítem del chat
    // (0x576939).
    std::int32_t helpItem = 0;
    // 0x78abb00: CanOpen no abre nada con otro valor que 0, el ancho del mundo lo cuenta como panel y SendMove
    // manda C1 03 97 (CL9.5). Los demás los pone en 0 cerrar el inventario (0x659250..0x65926a).
    std::int32_t field78abb00 = 0;
    std::int32_t field78abb04 = 0;
    std::uint8_t field78abb09 = 0;
    // 0x78abb0c: el modo de reparar (1 o 2) que dibuja el cursor (06 § El cursor). Lo ponen en 0 también la
    // entrada al mundo y los cambios de mapa.
    std::int32_t repairMode = 0;
    // 0x78abb27: lo que acompaña al modo de reparar, 2 con el modo prendido y 0 apagado (B y R, 0x5a4f1c y
    // 0x5a54e1).
    std::uint8_t repairCursor = 0;
    DelegatedPanels delegated{};
};

// Lo que las funciones del registro leen de afuera.
struct PanelContext
{
    Render::Models::CrtRandom& random;
    DialogState& dialogs;
    // 0x6c9dd0: CanOpen no abre ni los amigos ni los comandos en Chaos Castle (18 a 23).
    std::int32_t map = -1;
    // 0x7879fbc: con ítem en el cursor no se cierra el inventario (0x661b90).
    std::int32_t cursorItemCount = 0;
    // +0x1d4 & 7 del héroe: con 4 (Dark Lord), abrir el inventario pide la mascota (C1 A9, 0x65c039).
    std::uint8_t heroClass = 0;
    // 0x576793c: la salida pedida desde el menú de Esc (hito 6). Con 1, Open no abre.
    bool logoutRequested = false;
    // 0x6689d0, el héroe con +0x1f2 ≥ 0 y +0x368 en 0x80: solo lo lee el ancho del mundo con la lista de CS abierta.
    bool heroSiegeMark = false;
    // Los paneles con objeto del castillo (CL9 hito 12), 0x80000, 0x100000 y 0x200000: lo que hace su objeto al
    // abrirse (0x64ae10, 0x661310 y 0x6487c0, después de CloseByMask), al cerrarse en CloseByMask (0x64b200, 0x661bc0
    // y 0x6487d0) y su paso en 0x657930 (0x64d350, 0x6691c0 y 0x648a30). Sin ellos, cortan.
    std::function<Core::Expected<void, std::string>(PanelId)> objectOpened{};
    std::function<Core::Expected<void, std::string>(PanelId)> objectClosed{};
    std::function<Core::Expected<void, std::string>(PanelId)> objectStep{};
    // 0x555190("", Text.bmd[texto], tipo): el aviso de cerrar la lista de CS con un ítem en el cursor o en la caja del
    // caos (0x65cb95). Sin él, corta.
    std::function<Core::Expected<void, std::string>(std::size_t, std::int32_t)> message{};
    // 0x659aa0..0x659c3d: lo que abrir la party hace si cerró lo demás, antes de prenderla: 0x78aba48 (la cuenta de la
    // party) en 0 y el pedido de la lista, C1 03 42. Sin él, abrir la party corta.
    std::function<void()> partyOpened{};
    // 0x41ed10(id, 0, 0): el sonido, gancho de la CL10.
    std::function<void(std::int32_t)> sound{};
    // 0x65c966..0x65ca6d: lo que abrir el personaje hace fuera de los paneles, cierre o no lo demás: [0x6eed3c] de
    // +0x9304 a +0x9327 en 0, y con el registro propio abierto, 0x550d10 si un lugar del equipo tiene la durabilidad
    // en 0 (0x550cf0). Sin él, abrir el personaje corta.
    std::function<Core::Expected<void, std::string>()> characterOpened{};
};

class PanelRegistry
{
public:
    PanelRegistry(PanelState& state, PanelContext& context) : m_state(state), m_context(context) {}

    // 0x659290. Solo 0x20 y 0x10000 sacan un rand(); con 0 recorre los bits.
    [[nodiscard]] bool IsOpen(PanelId id);
    // 0x659860.
    [[nodiscard]] bool CanOpen(PanelId id);
    // 0x6599a0(id, aux): false si no lo intentó; true con los dos sonidos aunque el caso haya fallado (Q-C445).
    [[nodiscard]] Core::Expected<bool, std::string> Open(PanelId id);
    // 0x65cae0(id, 0): false si estaba cerrado, sin sonidos.
    [[nodiscard]] Core::Expected<bool, std::string> Close(PanelId id);
    // 0x657ab0(mask, 0): las 26 consultas en su orden fijo, cada una solo si está en la máscara.
    [[nodiscard]] Core::Expected<bool, std::string> CloseByMask(PanelId mask);
    // 0x657880(vk): con Esc cierra el abierto de bit más bajo.
    [[nodiscard]] Core::Expected<bool, std::string> PanelKey(std::int32_t virtualKey);
    // 0x657a90: todos, y el diálogo de los paneles (0x6603c0, que cerrado no hace nada).
    [[nodiscard]] Core::Expected<void, std::string> CloseAll();
    // 0x657930: el paso de los paneles con objeto (el diálogo de los paneles, los dos gremios y Castle Siege).
    [[nodiscard]] Core::Expected<void, std::string> StepTheObjectPanels();
    // 0x5bb8d0: el ancho del mundo en píxeles de 640: 640 sin paneles, 450 con uno a la derecha y 260 con dos.
    [[nodiscard]] std::int32_t WorldWidth();

private:
    void PlayTheSounds() const;

    PanelState& m_state;
    PanelContext& m_context;
};

// Lo que el original repite en cinco lugares sobre [0x6e592c]: +4 en 0, +8 y +0x20 en −1 y los doce estados en 1.
// CloseByMask (0x65897a..0x658999), el botón de cerrar (0x40a58a..0x40a5a1) y las entradas a la selección (0x634271)
// y al login (0x634bf0); el quinto, el cambio de mapa de Chaos Castle (0x4c7b34), corta antes en MuRet (0x4906a0).
void ResetTheCommands(DelegatedPanels& delegated);

// 0x5bb8d0: los tres anchos del mundo.
inline constexpr std::int32_t FullWorldWidth = 0x280;
inline constexpr std::int32_t OnePanelWorldWidth = 0x1c2;
inline constexpr std::int32_t TwoPanelWorldWidth = 0x104;
} // namespace Client::UI
