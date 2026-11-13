#pragma once

#include "Core/Expected.h"
#include "Protocol/PersonalShop.h"
#include "Scenes/SocialEvents.h"
#include "UI/PanelRegistry.h"

#include <string>

// Las listas de ítems de las tiendas personales (CL9 hito 7, fase 4; 04 § Las tiendas personales y 06 § Lo social en
// pantalla, "Las tiendas personales"): abrir y cerrar las dos ventanas (0x800, la propia, y 0x20000, la de otro), los
// mapas de los precios (0x7245e4 y 0x7245e0, del lugar al precio) y los 3F 01, 05, 06 y 0x13 que llegan. El GameServer
// 0.97 no tiene el 3F: el oráculo es el ASM del cliente (*estático*). El paso de la ventana (0x5ea290), lo que se hace
// con el mouse sobre las grillas (0x5c5110, 0x5c5242 y 0x5dec19) y las respuestas de sus avisos están en
// Scenes/PersonalShopWindow (fase 5); su dibujo (0x5f3030), en Scenes/GameInventoryHud (fase 6a).
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class WorldState;
struct WorldEntryResources;

// 0x65ac65..0x65b21d (0x800) y 0x65b33a..0x65ba37 (0x20000), después de abrir el inventario: con 0x6c931c distinto de
// −1, el C1 04 F3 10 en C3 (dos rand(), el relleno y el serial); los dos mapas si ninguno estaba (los arma juntos, y
// nadie los suelta); 0x6c931c en 1 (la propia) o 2 (la de otro).
[[nodiscard]] Core::Expected<void, std::string>
OpenThePersonalShop(WorldState& world, Render::Models::CrtRandom& random, UI::PanelId id, const GamePacketSender& send);

// 0x5eb020, al cerrar cualquiera de las dos: con 0x6c931c en 2, la grilla de la tienda propia vuelve de su copia
// (0x78a6768 → 0x78ab0b8), el título de la ventana (0x78aba64) vuelve al del cartel del héroe (0x451810 y 0x451860) o
// queda vacío, y con la clave del dueño (0x78a6514) distinta de 0, el 3F 07 en C3. Después, con el diálogo 0x91 o el
// 0x94 con 0x78abb14 en 5, el diálogo 0 (0x6236f0); 0x6c931c en −1 y la copia del dueño (0x78a6320) en ceros.
[[nodiscard]] Core::Expected<void, std::string>
CloseThePersonalShop(WorldState& world, Render::Models::CrtRandom& random, const GamePacketSender& send);

// 0x4db120 (3F 01): con un resultado distinto de 1 y 0x6c931c en 1, 0x5be150 con un ítem en el cursor, el lugar
// sale del mapa de la tienda propia (0x7245e4) y el C1 04 F3 10 en C3. Si no, nada (el log 0x6c84a0 siempre).
[[nodiscard]] Core::Expected<void, std::string> ApplyPriceResult(const Protocol::PersonalShop::PriceResult& result,
                                                                 const WorldEntryResources& resources,
                                                                 const GamePacketSender& send);

// 0x4db890 (3F 05): con 1, Close(0x40) y Close(0x4000000) si están abiertas, la copia del dueño en ceros, la grilla
// propia a su copia si 0x20000 no estaba abierta, el título (strncpy de 0x24), la grilla de la tienda vacía,
// Open(0x20000) y el mapa de la de otro vacío; por cada entrada, con un precio de 0 o menos, Close(0x800) y
// Close(0x20000) y sale; si no, 0x5bc740(0x78a9bb0, 8, 8, lugar, ítem, 0) y el precio en el mapa. Al final, la copia
// del dueño si está en el viewport (0x5110d0). Con 3, el texto 1120 con el tipo 2; con otro, el log.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopItemList(const Protocol::PersonalShop::ShopItemList& list,
                                                                  const WorldEntryResources& resources,
                                                                  const GamePacketSender& send);

// 0x4dbcb0 (3F 06): con 1 y 0x6c931c en 2, el lugar de 0x78a6758 sale del mapa de la de otro y de la grilla
// (0x5bd750); con 1 y otro modo, ese mapa vacío; con 1, el ítem comprado en su lugar (0x5bc740 con el último argumento
// en 0). Con 6, el texto 1166 y Close(0x800) y Close(0x20000); con 7, el 423; con 8, el 375 (todos con el tipo 2); con
// 7, 8 u otro, 0x5be150.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopPurchase(const Protocol::PersonalShop::ShopPurchase& purchase,
                                                                  const WorldEntryResources& resources,
                                                                  const GamePacketSender& send);

// 0x4dbb40 (3F 0x13): con 1 y 0x6c931c en 2, la grilla de la tienda vacía, el mapa de la de otro vacío y cada entrada
// en su lugar con su precio, sin mirar el precio. Si no, el log.
[[nodiscard]] Core::Expected<void, std::string> ApplyShopRefresh(const Protocol::PersonalShop::ShopRefresh& refresh,
                                                                 const WorldEntryResources& resources);
} // namespace Client::Scenes
