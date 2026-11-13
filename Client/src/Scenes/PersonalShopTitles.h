#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// Los carteles de las tiendas personales: el mapa del singleton de 0x7245c8 (0x451d10) por personaje (CL9 hito 7,
// fase 3; 06 § Lo social en pantalla, "Las tiendas personales"). Los métodos de acá cambian el mapa; el dibujo
// (0x451c20) y lo que mide para él están en Scenes/ShopSigns (fase 6b).
namespace Client::Scenes
{
class WorldState;
struct PersonalShopState;

// 0x4511f0 (AddShopTitle): con un personaje que no es jugador (+0x88 distinto de 1), solo el log (0x6c7938). Si no, sin
// cartel uno nuevo (0x4521d0 y 0x4524e0) con la clave, el nombre (+0x1d9) y el título; con cartel de la misma clave, el
// nombre y el título nuevos (0x452540); con otra clave, solo el log (0x6c7900). Las dos ramas que escriben miden: el
// texto 1104 y el nombre con la negrita (SelectObject del DC con 0x5763558, 0x452760 y 0x4531c6) y las líneas del
// título con la normal (0x5763554, 0x4531f7), que queda en el DC.
void AddShopTitle(WorldState& world, std::int32_t key, std::size_t index, std::string_view title);

// 0x451430: saca el cartel del personaje, si tiene.
void RemoveShopTitle(PersonalShopState& shops, std::size_t index);

// 0x451810: el personaje tiene cartel.
[[nodiscard]] bool HasShopTitle(const PersonalShopState& shops, std::size_t index);

// 0x451720 y 0x4516d0: esconde o muestra el cartel del personaje (+0x44), si tiene.
void HideShopTitle(PersonalShopState& shops, std::size_t index);
void ShowShopTitle(PersonalShopState& shops, std::size_t index);

// 0x451770: el personaje tiene cartel y está a la vista (+0x44).
[[nodiscard]] bool ShopTitleShown(const PersonalShopState& shops, std::size_t index);

// 0x451630: el personaje del primer cartel con la clave, en el orden del mapa; sin ninguno, nada (el 0 del original).
[[nodiscard]] std::optional<std::size_t> ShopTitleWithKey(const PersonalShopState& shops, std::int32_t key);

// 0x451c10 y 0x451fe0 (CheckKeyIntegrity), en cada frame con el mapa no vacío: saca el cartel cuyo personaje cambió de
// clave (MOVSX de +0x1f4 contra la del cartel) o dejó de ser jugador, con su log (0x6c79d8 y 0x6c7974).
void CheckTheShopKeys(WorldState& world);

// 0x451590, después de cada 0x510f70 de la clave del jugador (la carga del mapa en 0x61a5b5, también en la selección;
// el F3 04 en 0x4c4e86 y el 1C en 0x4c77eb): saca los carteles cuya clave no es la del héroe publicado (MOVSX de +0x1f4
// de 0x743a170, 0x4515ae). Solo queda el propio.
void KeepTheHeroShopTitle(WorldState& world);
} // namespace Client::Scenes
