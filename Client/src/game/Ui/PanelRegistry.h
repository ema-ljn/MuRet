#pragma once

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Ui/Dialogs.h"
#include "Ui/PanelDialog.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

// Los paneles del juego (06 § HUD y ventanas, "Los paneles"): bits de [0x576352c] en modo inmediato, no clases. El
// paso los atiende en 0x5ee140 y el HUD los dibuja con funciones sueltas. El registro reproduce sus funciones con
// el orden de consultas del original, porque las del comercio (0x20) y la tienda de NPC (0x10000) son bytes de la
// tabla de protección y cada consulta saca un rand(). El estado de cada id es un byte propio o uno que vive en otro
// objeto (DelegatedPanels): un id cuyo dueño no está portado queda cerrado y su consulta saca lo que saca. Lo que
// un panel hace al abrirse o al cerrarse y es de otra ola corta con su dirección (decisión 4 del plan de la CL8).
namespace Client::Ui
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
// +4 (DelegatedPanels::commands). La esquina (+0xc y +0x10) va en Kernel::HudState, como las de las otras ventanas.
struct CommandsWindowState
{
    // +8: a quién va el comando: 0x406050 lo pone en −1 y, con un jugador a dos celdas o menos, en el elegido
    // (0x4060d0 y 0x4061a7); lo vuelven a −1 los comandos que cierran la ventana (0x406af7 y 0x4072b8).
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
    // 1: 0x490a00(*(0x5763514)+0x48), la de amigos del administrador viejo. En el juego la mira
    // PanelContext::friendsShown (CL9 hito 9); sin él, este byte.
    std::uint8_t friends = 0;
    // 8: *(0x710148)+0x21383. IsOpen lo mira distinto de 0 y CanOpen no abre nada con 1.
    std::uint8_t quest = 0;
    // 0x10: 0x6522e0, +0x7c del gremio ([0x7d536f0], CL9 hito 8). Lo abren 0x6522d0 y lo cierra 0x6522f0.
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
    // 0x724584 (byte): la mezcla que 0x44ae10 encontró en la caja del entrenador (1 o 2), que leen su modo 3 (0x44d010)
    // y su dibujo (0x44d680); cerrar el entrenador (0x4499b0) lo pone en 0.
    std::uint8_t field724584 = 0;
    // 0x40000: 0x6570a0, +0x7c del gremio 2 ([0x7d536f4], CL9 hito 8). Lo abren 0x657090 y lo cierra 0x6570b0.
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
    // [0x7d53704]: el diálogo de los paneles (0x65feb0; lo abre 0x6600d0 desde los paquetes, CL9 hito 8). 0x657880 le
    // da la tecla primero.
    PanelDialogState panelDialog{};
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
    // 0x78abb08: un byte del panel 0x2000, que su paso (0x5e74f0, CL9) da vuelta y que abrirlo (0x65b259), cerrarlo
    // (0x658bea) y el no del diálogo del 4 de 0x78abb14 (0x5d9c56) ponen en 0.
    std::uint8_t field78abb08 = 0;
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
    Crt::CrtRandom& random;
    DialogState& dialogs;
    // 0x6c9dd0: CanOpen no abre ni los amigos ni los comandos en Chaos Castle (18 a 23).
    std::int32_t map = -1;
    // 0x7879fbc: con ítem en el cursor no se cierra el inventario (0x661b90).
    std::int32_t cursorItemCount = 0;
    // +0x1d4 & 7 del héroe: con 4 (Dark Lord), abrir el inventario pide el cuervo y el caballo (petRequests,
    // 0x65c039).
    Kernel::CharacterClass heroClass = Kernel::CharacterClass::DarkWizard;
    // 0x576793c: la salida pedida desde el menú de Esc (hito 6). Con 1, Open no abre.
    bool logoutRequested = false;
    // 0x6689d0, el héroe con +0x1f2 ≥ 0 y +0x368 en 0x80: solo lo lee el ancho del mundo con la lista de CS abierta.
    bool heroSiegeMark = false;
    // 0x661b90, cuando se presta el mundo: consulta viva de los 32 tipos WORD y el cursor.
    std::function<bool()> inventoryBusy{};
    // 0x659aa0..0x659c3d: lo que abrir la party hace si cerró lo demás, antes de prenderla: 0x78aba48 (la cuenta de la
    // party) en 0 y el pedido de la lista, C1 03 42. Sin él, abrir la party corta.
    std::function<void()> partyOpened{};
    // 0x658a18..0x658bc7 y 0x658c0e..0x658dbe: cerrar un NPC de evento (0x1000 o 0x4000) manda el C1 03 31 en claro
    // antes de poner 0x7d53712 en 0. Sin él, cerrarlos corta.
    std::function<void()> eventNpcClosed{};
    // 0x41ed10(id, 0, 0): el sonido, gancho de la CL10.
    std::function<void(Audio::SoundId)> sound{};
    // 0x659c8c..0x659e3c: lo que abrir el gremio hace si cerró lo demás, antes de abrirlo: el pedido de la lista, C1 03
    // 52 (Protocol::Guild::WriteMemberListRequest). Sin él, abrir el gremio corta.
    std::function<void()> guildOpened{};
    // 0x6522f0 y 0x6570b0, la vtable + 0x2c de las dos ventanas del gremio, que CloseByMask llama con su panel abierto
    // (Scenes/GuildPanels): el sonido, sus diálogos y, la 2, el C1 04 54 00. Sin ellos, cerrarlas corta.
    std::function<Core::Expected<void, std::string>()> guildClosed{};
    std::function<Core::Expected<void, std::string>()> secondGuildClosed{};
    // La función de quien abrió el diálogo de los paneles (0x4d6b60 o 0x653c00), que llaman 0x6603c0, 0x660410 y
    // 0x6605b0. Sin ella, responder corta.
    PanelDialogAnswer panelDialogAnswer{};
    // 0x6605b0 con el mouse, los globales de la UI vieja y el reloj del paso (Scenes/GuildPanels). Sin él, el paso con
    // el diálogo abierto corta.
    std::function<Core::Expected<void, std::string>()> panelDialogStep{};
    // 0x657930 con [0x7d536f4] abierta: su vtable + 0x18 (0x45aa80(0), que llama a 0x656df0) con el mouse, el reloj y
    // los filtros del paso (Scenes/SecondGuildWindow). Sin él, el paso con la ventana 2 del gremio abierta corta.
    std::function<Core::Expected<void, std::string>()> secondGuildStep{};
    // 0x657977: el paso de la ventana del gremio (0x45aa80(0) y 0x651840, Guild::StepTheGuildWindow), con el mouse y
    // el cable del paso. Sin él, con el panel abierto, el paso corta.
    std::function<Core::Expected<void, std::string>()> guildStep{};
    // 0x65bef0..0x65bf0d: con los demás paneles cerrados, 0x64ae10 pide B2 03 y abre el registro del castillo.
    std::function<Core::Expected<void, std::string>()> castleRegistrationOpened{};
    // 0x44dcf0/0x44f770: los controles de arma y puerta; aux es la clave de la puerta.
    std::function<Core::Expected<void, std::string>(PanelId, std::uint32_t)> siegeControlOpened{};
    std::function<Core::Expected<void, std::string>(PanelId)> siegeControlClosed{};
    // 0x657b94: 0x64b200 envía C1 03 31 y después cierra el registro y vuelve a su primera pestaña.
    std::function<Core::Expected<void, std::string>()> castleRegistrationClosed{};
    // 0x661310/0x6487c0 y 0x661bc0/0x6487d0. false informa el bloqueo de Senior; CloseByMask no mira ese retorno
    // (llamada void nativa) ni baja su flag de éxito por este panel.
    std::function<Core::Expected<void, std::string>(PanelId)> castleWindowOpened{};
    std::function<Core::Expected<bool, std::string>(PanelId)> castleWindowClosing{};
    std::function<Core::Expected<void, std::string>()> castleSeniorBlocked{};
    // El orden de 0x657930: registro, Senior y zona de caza, después de las dos ventanas del gremio.
    std::function<Core::Expected<void, std::string>(PanelId)> castleWindowStep{};
    // 0x65c966..0x65ca6d: lo que abrir el personaje hace fuera de los paneles, cierre o no lo demás: [0x6eed3c] de
    // +0x9304 a +0x9327 en 0, y con el registro propio abierto, 0x550d10 si un lugar del equipo tiene la durabilidad
    // en 0 (0x550cf0). Sin él, abrir el personaje corta.
    std::function<Core::Expected<void, std::string>()> characterOpened{};
    // 0x657fb5..0x6584a4: lo que cerrar el comercio hace entre la consulta y la escritura del byte protegido: con aux
    // en 0 el C1 03 3D en C3 (dos rand()), y las dos grillas (0x78a92a8 y 0x78a5a20) vacías, con 0x5bbdf0 en cada
    // vuelta si 0x78abaa5 está en 1 (Social::CloseTheTradeGrids). Sin él, cerrar el comercio corta.
    std::function<Core::Expected<void, std::string>(bool aux)> tradeClosed{};
    // 0x65ac65..0x65b21d y 0x65b33a..0x65ba37: lo que abrir una tienda personal (0x800 o 0x20000) hace si cerró lo
    // demás, después del inventario y antes de prender 0x7d5370f: con 0x6c931c distinto de −1 el C1 04 F3 10 en C3 (dos
    // rand()), los dos mapas de los precios (0x7245e4 y 0x7245e0) si no estaban y 0x6c931c en 1 o 2
    // (PersonalShop::OpenThePersonalShop). Sin él, abrir una tienda corta.
    std::function<Core::Expected<void, std::string>(PanelId id)> personalShopOpened{};
    // 0x6589cb y 0x6589ef: 0x5eb020 al cerrar cualquiera de las dos, antes de apagar 0x7d5370f
    // (PersonalShop::CloseThePersonalShop). Sin él, cerrar una tienda corta.
    std::function<Core::Expected<void, std::string>()> personalShopClosed{};
    // Los pedidos del cuervo y del caballo, C1 06 A9 00 00 01 y A9 01 00 08, con el XOR32 y por send() (CL9 hito 4):
    // lo que abrir la mascota hace si cerró lo demás, después de 0x420cb0 (0x659ff6..0x65a8a3), y abrir el inventario
    // de un Dark Lord después de prenderlo (0x65c039..0x65c946, el mismo código repetido). Sin él, los dos cortan.
    std::function<void()> petRequests{};
    // Lo que abrir y cerrar el almacén (0x40), la caja del caos (0x100), el entrenador (0x8000) y la tienda de NPC
    // (0x10000) hacen fuera de los paneles (CL9 hito 6): las grillas, el ítem del cursor, los envíos y los mensajes
    // (Scenes/NpcWindows.h). Sin ellos, abrirlos y cerrarlos corta.
    // Abrir, con lo demás cerrado y antes de abrir el inventario: lo de 0x40 (0x659f8d..0x659f94) y lo de 0x100
    // (0x65a94f..0x65ac07 y 0x65ac28), que 0x6599a0(0x4000000) no lee; el 0x8000 no abre el inventario (0x449940(1),
    // 0x65be8b).
    std::function<Core::Expected<void, std::string>(PanelId)> npcWindowOpened{};
    // Cerrar, desde 0x657ab0 con el panel abierto: false si no cierra (la traba del 24 en 0x40, la caja del caos con
    // algo en 0x100, la del entrenador con algo en 0x8000), y el registro deja el byte como estaba.
    std::function<Core::Expected<bool, std::string>(PanelId)> npcWindowClosing{};
    // 0x659c68..0x659c70: abrir las misiones, si cerró lo demás, es 0x419840(−1) sobre [0x710148] (la lista, con su
    // diálogo). Sin él, abrir las misiones corta.
    std::function<Core::Expected<void, std::string>()> questOpened{};
    // 0x657b2b..0x657b31: cerrar las misiones es 0x419900, que pone la ventana en 0 (lo hace el registro) y manda el
    // C1 03 31 en claro. Sin él, cerrar las misiones corta.
    std::function<void()> questClosed{};
    // El panel 1, la ventana de amigos del administrador viejo: 0x490a00(+0x48) para saber si está (sin él,
    // DelegatedPanels::friends), 0x491080(0x186, 0x106) al abrirlo (0x659f3d; sin él, abrirlo corta) y 0x4914e0 al
    // cerrarlo (0x6584e5; sin él, cerrarlo corta).
    std::function<bool()> friendsShown{};
    std::function<Core::Expected<void, std::string>()> friendsOpened{};
    std::function<Core::Expected<void, std::string>()> friendsClosed{};
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
    [[nodiscard]] Core::Expected<bool, std::string> Open(PanelId id, std::uint32_t auxiliary = 0);
    // 0x65cae0(id, 0): false si estaba cerrado, sin sonidos.
    [[nodiscard]] Core::Expected<bool, std::string> Close(PanelId id);
    // 0x65cae0(id, aux): `aux` llega a 0x657ab0; solo lo lee el comercio (con 1, sin el 3D: 0x4d5906).
    [[nodiscard]] Core::Expected<bool, std::string> Close(PanelId id, bool aux);
    // 0x657ab0(mask, 0): las 26 consultas en su orden fijo, cada una solo si está en la máscara.
    [[nodiscard]] Core::Expected<bool, std::string> CloseByMask(PanelId mask);
    [[nodiscard]] Core::Expected<bool, std::string> CloseByMask(PanelId mask, bool aux);
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
    // 0x6599a0 con 0x40, 0x100 y 0x10000: con lo demás cerrado, lo de afuera, el inventario y el byte del panel.
    [[nodiscard]] Core::Expected<void, std::string> OpenTheNpcWindow(PanelId id, PanelId mask);
    // 0x657ab0 con 0x40, 0x100, 0x8000 y 0x10000 abiertos: false si no cerró.
    [[nodiscard]] Core::Expected<bool, std::string> CloseTheNpcWindow(PanelId id);

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
} // namespace Client::Ui
