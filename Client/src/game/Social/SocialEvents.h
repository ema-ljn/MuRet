#pragma once

#include "Core/Expected.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party/Party.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// Lo que hacen el comercio, la party y el duelo que llegan (CL9 hito 7; 04 § El comercio, § La party y § El duelo):
// los casos del dispatcher 0x4e2b10 y sus funciones, sobre los globales de SocialState. Lo que corta lo dice con la
// dirección y la ola.
namespace Client::Core
{
class Log;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct ItemState;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct TradeState;
struct WorldEntryResources;

// Un paquete que sale (Protocol::Outgoing), como lo manda la sesión.
using GamePacketSender = std::function<void(Protocol::Outgoing)>;

// 0x4d4af0 con el 36 cifrado. Sin la tienda personal (0x800) ni la ajena (0x20000) abiertas, que no sacan rand():
// 0x78aba26 en 0, el nombre en 0x78aba1c, el diálogo 0x78 (0x6236f0) y, con un ítem en el cursor, 0x5be150. Con alguna
// abierta, el C1 04 37 00 que devuelve para mandar en claro, sin tocar nada.
[[nodiscard]] Core::Expected<std::optional<Protocol::Outgoing>, std::string>
ApplyTradeRequest(const Protocol::Social::TradeRequest& request, const WorldEntryResources& resources);

// 0x4d4b22..0x4d5238, el 36 en claro: el aviso C1 06 F1 03 00 36 en C3, con sus dos rand() (el relleno y el serial).
[[nodiscard]] Protocol::Outgoing ApplyClearTradeRequest(Crt::CrtRandom& random);

// 0x4d5570: 0 el texto 492 y 2 el 493 (tipo 2); con 1, Open(0x20), el pedido de SetCursorPos, los zen y los OK en 0,
// 0x5bbdf0 si 0x78abaa5 está en 0 y el nombre cambió (strcmp), y el nombre, el gremio y el nivel del otro.
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeResponse(const Protocol::Social::TradeResponse& response,
                                                                   const WorldEntryResources& resources);

// 0x4e3d6a: 0x5bd750(lugar, 0x78a5a20, 8) (con 0x5bc5e0 antes y 0x5bc470 después) y el sonido 0x1d. Un lugar desde 32
// (que no sea 0xff), una huella que pasa la grilla o un undécimo ítem igual a su copia (Q-C961) llevan al original
// fuera de la grilla o de la pila: se descarta el paquete entero, sin sonido, con un aviso en el log (Q-C200).
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeItemRemoved(const Protocol::Social::TradeItemRemoved& removed,
                                                                      const WorldEntryResources& resources);

// 0x4e3d99: 0x5bc740(0x78a5a20, 8, 4, lugar, ítem, 1), con 0x5bc470 al final, y el sonido 0x1d. Una huella que pasa la
// casilla 31 (Q-C200) se descarta antes de escribir y de los rand() de 0x54bf40; un duodécimo ítem igual a su copia
// (0x5bc545, Q-C961), después de ponerlo en una copia del comercio, que se tira con los rand() que sacó. Los dos, sin
// sonido y con un aviso en el log. El undécimo se pierde de la lista y en su lugar se compara el 1 de la cuenta.
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeItemAdded(const Protocol::Social::TradeItemAdded& added,
                                                                    const WorldEntryResources& resources);

// 0x4e3dd0: 0x78abab8 con el zen mandado (0x57678ac) si lo aceptó, o 0.
void ApplyTradeMoneyResult(const Protocol::Social::TradeMoneyResult& result, Kernel::WorldState& world);

// 0x4e3df3: el zen del otro en 0x78abab4.
void ApplyTradeOtherMoney(const Protocol::Social::TradeOtherMoney& money, Kernel::WorldState& world);

// 0x4e3e0a: 0 apaga el OK del otro, 1 lo prende y 2 apaga el propio; siempre el sonido 0x19.
void ApplyTradeOkState(const Protocol::Social::TradeOkState& state, Kernel::WorldState& world);

// 0x4d57f0: con la traba 0x78abb39, lo guarda para 0x4d1400 (una vez); si no, los textos 494 (con 0x5bc5e0 en cada
// ítem del otro), 495 y 496 (con el diálogo 0), la cuenta del cursor en 0, el cierre del diálogo 0x73 y
// Close(0x20, 1), sin el 3D.
[[nodiscard]] Core::Expected<void, std::string> ApplyTradeResult(const Protocol::Social::TradeResult& result,
                                                                 const WorldEntryResources& resources);

// 0x5bc107..0x5bc13f, la cola de 0x5bbe10 (desde 0x633f20, al entrar a la selección, antes de cualquier comercio):
// 0x78abaa5 en 0 y las 32 entradas de las dos grillas (0x78a5a20 y 0x78a92a8) y de la copia (0x77f3dc8) con el tipo en
// −1 y +0x3c en 0. Sin rand().
void ClearTheTradeForSelection(TradeState& trade);

// 0x657fb5..0x6584a4, el cierre del comercio dentro de 0x657ab0: con `aux` en 0, el C1 03 3D en C3 (dos rand() antes
// de mandarlo por `send`; sin él corta); después las 32 casillas de 0x78a92a8 y de 0x78a5a20 vacías (tipo −1 y +0x3c
// en 0), con 0x5bbdf0 en cada vuelta si 0x78abaa5 está en 1.
[[nodiscard]] Core::Expected<void, std::string> CloseTheTradeGrids(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                   bool aux, const GamePacketSender& send);

// 0x5bc300(a, b): 2 con otro tipo; 0 con otro +9; si no, el nivel ((+4 >> 3) & 0xf), el bit 7 de +4, las
// excelentes (+0x1b & 0x3f), la cuenta de opciones (+0x2a), la opción de 0x50 a 0x53 (+0x2b, el valor en +0x33) y la
// durabilidad (+0x1a), en ese orden: −1 si `a` tiene menos, 1 si tiene más, 0 si todo es igual.
[[nodiscard]] std::int32_t CompareTradeItems(const Kernel::ItemState& a, const Kernel::ItemState& b);

// 0x4e3f41: la clave del que invita en 0x78aba4c y el diálogo 0x77.
void ApplyPartyRequest(const Protocol::Party::PartyRequest& request, Kernel::WorldState& world);

// 0x4e3f6f: los textos 497 a 501 y 535 (tipo 2) para 0 a 5, y el diálogo 0 siempre.
[[nodiscard]] Core::Expected<void, std::string>
ApplyPartyRequestResult(const Protocol::Party::PartyRequestResult& result, const WorldEntryResources& resources);

// 0x4d67e0: la cuenta en 0x78aba48 y cada miembro en su lugar de 0x77f3cd0, con 0 en +0xa. Con más de cinco el
// original escribe pasado el arreglo (Q-C960): se descarta el paquete entero, con un aviso en `log`.
void ApplyPartyList(const Protocol::Party::PartyList& list, Kernel::WorldState& world, Core::Log& log);

// 0x4e409b: la cuenta en 0, el texto 502 (tipo 2) y, con 0x6c8e50 en 0 o más, 0x6c8e50 en −1.
[[nodiscard]] Core::Expected<void, std::string> ApplyPartyLeft(const Protocol::Party::PartyLeft&,
                                                               const WorldEntryResources& resources);

// 0x4e4174: por cada byte, +0x18 del lugar del nibble alto con el bajo (hasta 10). Con un lugar de 5 o más el original
// escribe pasado el arreglo (Q-C960): se descarta el paquete entero, con un aviso en `log`.
void ApplyPartyLife(const Protocol::Party::PartyLife& life, Kernel::WorldState& world, Core::Log& log);

// 0x4dac90: con el resultado, el duelo en curso, la clave, el nombre, el texto 912 con el nombre (tipo 2) y el sonido
// 0x8d; sin él, el nombre y el texto 913.
[[nodiscard]] Core::Expected<void, std::string> ApplyDuelStart(const Protocol::Social::DuelStart& start,
                                                               const WorldEntryResources& resources);

// 0x4e4d10: el duelo terminado, la clave, el nombre, el texto 914 (tipo 2) y, con el tiempo del F3 22 del tipo 2
// (0x5767814), el tiempo en 0 (0x5767816): 0x5a7eb0 deja de dibujarlo.
[[nodiscard]] Core::Expected<void, std::string> ApplyDuelEnd(const Protocol::Social::DuelEnd& end,
                                                             const WorldEntryResources& resources);

// 0x4e4d75: con el diálogo 0x9a abierto, nada; si no, la clave, el nombre, el pedido (0x78abad9) y el sonido 0x8c.
void ApplyDuelRequest(const Protocol::Social::DuelRequest& request, Kernel::WorldState& world);

// 0x4e4dd1: los puntos según cuál de las dos claves es la del rival; con ninguna, nada.
void ApplyDuelScore(const Protocol::Social::DuelScore& score, Kernel::WorldState& world);

// 0x4dae80 (3F 00): por cada entrada, el personaje de la clave (0x5110d0); del 0 al 399, AddShopTitle con la clave, el
// personaje y el título.
void ApplyShopTitles(const Protocol::PersonalShop::ShopTitles& titles, Kernel::WorldState& world);

// 0x4db700 (3F 02): con 1, 0x78aba50 en 1 y AddShopTitle del héroe publicado (0x743a170) con su clave (MOVSX de +0x1f4)
// y el título propio (0x78aba64); si no, solo el log "Fault ReceiveCreatePersonalShop" (0x6c84d0).
void ApplyShopCreated(const Protocol::PersonalShop::ShopCreated& created, Kernel::WorldState& world);

// 0x4e4ea5 (3F 03): con 1, el personaje de la clave; del 0 al 399, si es el héroe publicado 0x78aba50 en 0, y su cartel
// sale del mapa (0x451430). Con otro resultado, solo el log (0x6c8504).
void ApplyShopClosed(const Protocol::PersonalShop::ShopClosed& closed, Kernel::WorldState& world);

// 0x4e4f5d (3F 08): el nombre en 10 bytes terminados en el 11 y sprintf(Text[1122], nombre) con el tipo 1 y el nombre
// vacío de 0x5767a8c. MuRet no usa el texto como formato más allá de un %s: con 0x70 bytes o más, corta.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopNotice(const Protocol::PersonalShop::ShopNotice& notice,
                                                                const WorldEntryResources& resources);

// 0x4dafe0 (3F 10): el personaje de la clave; del 0 al 399 y con el nombre igual (strncmp de 10 con +0x1d9),
// AddShopTitle con el título.
void ApplyOtherShopTitle(const Protocol::PersonalShop::ShopTitle& title, Kernel::WorldState& world);

// 0x4e4fcc (3F 12): con 0x6c931c en 2, Close(0x800, 0), Close(0x20000, 0) y el texto 1126 con el tipo 2.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopOwnerLeft(const Protocol::PersonalShop::ShopOwnerLeft&,
                                                                   const WorldEntryResources& resources,
                                                                   const GamePacketSender& send);

// 0x555190("", el texto `text` de Text.bmd, `type`): los avisos de los paquetes sociales, con el buffer vacío de la
// .bss como nombre (08 P-C72).
[[nodiscard]] Core::Expected<void, std::string> AddSocialNotice(const WorldEntryResources& resources, std::size_t text,
                                                                std::int32_t type, std::string_view where);
} // namespace Client::Scenes
