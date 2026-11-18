#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/NpcWindows.h"

#include <cstddef>
#include <cstdint>
#include <string>

// El entrenador de las mascotas del Dark Lord (CL9 hito 6; 06 § HUD y ventanas, "El entrenador"): el panel 0x8000,
// su modo (0x724580, UI::DelegatedPanels::trainer) y su caja (0x723c80, NpcWindowState::trainerBox). El paso de
// 0x44b4e0 reparte por el modo: 1 el menú (0x44b540), 2 recuperar la vida del caballo oscuro (0x1a04, el lugar 8 del
// equipo) o del cuervo (0x1a05, el lugar 1) con el 34 (0x44b910), y 3 la caja, que mezcla el espíritu de una de las
// dos con joyas (0x44d010, con 0x44ae10). El dibujo de los tres modos es Scenes/PetTrainerDrawing.h.
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameUiContext;
struct InventoryServices;
class WorldState;

// 0x724580: los modos.
inline constexpr std::int32_t TrainerMenuMode = 1;
inline constexpr std::int32_t TrainerRecoveryMode = 2;
inline constexpr std::int32_t TrainerBoxMode = 3;

// 0x449940(modo): el modo en 0x724580 y la caja vacía (0x449c40). El 0x6599a0(0x8000) la llama con 1 (0x65be8b).
void OpenTheTrainer(WorldState& world, std::int32_t mode);

// 0x449c10: si alguna casilla de la caja tiene un tipo (distinto de 0xffff), sin mirar +0x3c.
[[nodiscard]] bool TrainerBoxHasItems(const WorldState& world);

// 0x4499b0: con algo en la caja, el texto 1211 de error (0x555190(0x6e57a4, .., 2)) y false; si no, 0x724584 y
// 0x724580 en 0 y el C1 03 31 en claro por `send`, y true. Sin `services` o sin su mensaje, el aviso corta.
[[nodiscard]] Core::Expected<bool, std::string> CloseTheTrainer(WorldState& world, const InventoryServices* services,
                                                                const NpcWindowSender& send);

// 0x44ae10: 0x724584 y 0x724585 en 0 y la mezcla de la caja (las casillas con un tipo y +0x3c distinto de 0): el
// espíritu del caballo (0x1a1f de nivel 0) con 5 joyas de bendición (0x1c0d), 5 del alma (0x1c0e), una de la creación
// (0x1c16) y una del caos (0x180f) es la 1, y el del cuervo (nivel 1) con 2, 2, 1 y 1 la 2. Con una de las dos, el
// registro propio abierto (3.848 rand() al cerrarlo sin otros accesos) y 0x724585 en 1 si el zen (+0xecc, sin signo)
// no alcanza para su precio (0x6b8c68: 5.000.000 y 1.000.000).
void ScanTheTrainerBox(WorldState& world, Render::Models::CrtRandom& random);

// 0x6b8c68: el precio de la mezcla `kind` de la caja (0, 1 o 2).
[[nodiscard]] std::uint32_t TrainerMixPrice(std::uint8_t kind);

// 0x421d90 (desde 0x44c633): pone en 0 los 0x48 bytes de 0x723918..0x72395f, lo último que pidió el tooltip de las
// mascotas (0x422990, P-C126). Es WorldRuntime::petTooltipQuery del hito 4, que esta rama no tiene: queda sin hacer
// nada hasta que la junten (CL9 hito 4).
void ForgetThePetTooltipQuery(WorldState& world);

// Lo que 0x44b190 deja en el buffer de quien llama: el texto de Text.bmd y lo que el sprintf le pasa.
enum class PetQuoteLine : std::uint8_t
{
    NoPet, // 1229, sin argumentos
    Full,  // 1230, sin argumentos: el precio en 0
    Short, // 1231 con lo que falta (precio − zen) con sus comas (0x5b1130)
    Cost,  // 1232 con el texto de 0x5b13e0, el precio sin el doble (Q-C955)
};

struct PetRecoveryQuote
{
    // −1 sin la mascota.
    std::uint32_t price = 0;
    PetQuoteLine line = PetQuoteLine::NoPet;
    std::string number;
};

// Lo que lee 0x44b190: el mundo, el rand(), la cuenta del registro propio (la real o la de un dibujo intermedio) e
// Item.bmd. Con `writesGlobals` en false (un dibujo intermedio) no pone 0x78abb10 en 0, aunque cuenta como si lo
// hubiera hecho.
struct PetQuoteContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    const Data::Tables::ItemTable* items = nullptr;
    bool writesGlobals = true;
};

// 0x44b190(botón + 1, buffer): el precio de recuperar la mascota del botón 0 (el caballo) o 1 (el cuervo), con un
// cierre del registro propio (3.848 rand()) y otro con un precio distinto de 0.
[[nodiscard]] Core::Expected<PetRecoveryQuote, std::string> QuoteThePetRecovery(const PetQuoteContext& context,
                                                                                std::size_t button);

// 0x44b4e0: con 0x724580 en 0, en 4 o fuera de 1..3, nada.
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainer(const GameUiContext& context, UI::PanelRegistry& panels);

// 0x44b540, el modo 1. El mouse en [0xd5, 0x1aa) × [0x5a, 0x122) es de la UI. Con el clic, los tres botones
// ([0x103, 0x17b) × [0xb4 + 0x1e · i, +0x16)) apagan el clic y el botón: el 0 pasa al modo 2 (0x449960), el 1 al 3 y
// abre el inventario (0x449970), y el 2, que el dibujo no muestra, nada más (Q-C957). Sin botón, el de cerrar
// ([0x189, 0x1a1) × [0x109, 0x121)) apaga los dos y cierra (0x4499b0).
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainerMenu(const GameUiContext& context,
                                                                   UI::PanelRegistry& panels);

// 0x44b910, el modo 2. El mouse en [0xd5, 0x1aa) × [0x5a, 0x122) es de la UI. Con el clic, los dos botones
// ([0x103, 0x17b) × [0x96 + 0x3c · i, +0x16)) apagan el clic y el botón y piden el precio (0x44b190); con el registro
// propio abierto, si el zen alcanza (sin signo) y el precio no es −1, mandan C1 05 34 <8 o 1> <el byte bajo del
// precio> con el XOR32 y en C3, cierran el entrenador (0x4499b0) y llaman a 0x421d90. Sin botón, el de cerrar apaga
// los dos y cierra. Los rand(): los 3.848 de cada cierre del registro propio que lo deja en 0 (uno o dos en 0x44b190
// y el del paso) y los dos del C3.
[[nodiscard]] Core::Expected<void, std::string> StepThePetRecovery(const GameUiContext& context);

// 0x44d010, el modo 3. El mouse en [0x104, 0x1c2) × [0, 0x1b1) es de la UI. Con el clic, el botón de mezclar
// ([0x14f, 0x17b) × [0x12c, 0x14c)) apaga el clic y el botón y, con la mezcla que dejó el 0x44ae10 del paso anterior
// (0x724584), manda C1 04 86 0D (la 1) o 0E (la 2) con el XOR32 y en claro, sin mirar el zen (Q-C956); el de cerrar
// ([0x118, 0x130) × [0x18b, 0x1a3)) apaga los dos y cierra el panel (0x65cae0(0x8000)). Siempre, al final, 0x44ae10.
[[nodiscard]] Core::Expected<void, std::string> StepTheTrainerBox(const GameUiContext& context,
                                                                  UI::PanelRegistry& panels);
} // namespace Client::Scenes
