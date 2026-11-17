#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Outgoing.h"
#include "UI/PanelRegistry.h"

#include <functional>
#include <string>

namespace Client::Data::Tables
{
struct ItemSetTables;
}

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class IGameUiRequests;
class WorldState;

// Lo que 0x550d10 lee de la carga cuando abrir el personaje la llama (0x65ca1e): Item.bmd (*[0x7758550]) e
// ItemSetType.bmd con ItemSetOption.bmd (0x40c600). Solo las presta el paso de la UI, el único que abre el personaje;
// con alguna en nullptr, 0x550d10 corta con error en lo primero que la lee.
struct GamePanelTables
{
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::ItemSetTables* itemSetTables = nullptr;
    // Text.bmd: el 609 de las respuestas vacías de 0x419710, cuando abrir las misiones (0x419840) muestra un diálogo
    // sin respuestas. nullptr: ese diálogo corta.
    const Data::Text::TextTable* texts = nullptr;
};

// El registro de paneles ([0x576352c]) sobre los globales de WorldRuntime, con lo que sus funciones leen del
// mundo en el momento de armarlo: el mapa (0x6c9dd0), el ítem del cursor (0x7879fbc), la clase del héroe
// (+0x1d4 & 7) y la salida pedida (0x576793c). Los sonidos (0x41ed10) quedan en WorldRuntime::lastSoundId, el
// gancho de la CL10. Es de vida corta: se arma donde el original llama a una función del registro.
class GamePanels
{
public:
    GamePanels(WorldState& world, Render::Models::CrtRandom& random, GamePanelTables tables = {});
    GamePanels(const GamePanels&) = delete;
    GamePanels& operator=(const GamePanels&) = delete;
    GamePanels(GamePanels&&) = delete;
    GamePanels& operator=(GamePanels&&) = delete;
    ~GamePanels() = default;

    [[nodiscard]] UI::PanelRegistry& Registry()
    {
        return m_registry;
    }

    // 0x659aa0..0x659c3d: con `requests`, abrir la party pone en 0 la cuenta de `world` (HudState::partyCount) y pide
    // la lista (C1 03 42). Lo presta el paso de la UI, el que abre la party; sin él, o con nullptr, abrirla corta.
    void LendThePartyList(WorldState& world, IGameUiRequests* requests);
    // 0x659ff6..0x65a8a3 y 0x65c039..0x65c946: con `send`, abrir la mascota, o el inventario con un Dark Lord, manda
    // los pedidos del cuervo y del caballo (C1 06 A9 00 00 01 y A9 01 00 08, Protocol::Pet::WritePetInfoRequest). Lo
    // presta el paso de la UI y LendTheSession; sin él, abrirlos corta.
    void LendThePetRequests(std::function<void(Protocol::Outgoing)> send);
    // 0x659f3d: abrir el panel 1 llama a 0x491080(0x186, 0x106) (Scenes::OpenTheFriends), con lo que lee del paso (la
    // vista y los globales de las ventanas viejas). Lo presta el paso de la UI; sin él, abrirlo corta.
    void LendTheFriends(std::function<Core::Expected<void, std::string>()> opened);

    // 0x419900: cerrar las misiones manda el C1 03 31 con `send`, y también 0x658a18 y 0x658c0e al cerrar un NPC de
    // evento (0x1000 o 0x4000, CL9 hito 11). Lo prestan el paso de la UI, los handlers del A1 y del A2 (0x657a90) y los
    // cierres de la entrada al mundo, del teleport, del regen y de los eventos; sin él, cerrarlos corta.
    void LendTheWindowClose(std::function<void()> send);
    // Lo mismo con el C1 03 31 de `requests` (IGameUiRequests::SendWindowClose); con nullptr, sin él.
    void LendTheWindowClose(IGameUiRequests* requests);

    // 0x65ac87, 0x65b359 y 0x5eb17f: el C1 04 F3 10 al abrir una tienda con 0x6c931c distinto de −1 y el 3F 07 al
    // cerrar la de otro con su dueño (Scenes::OpenThePersonalShop y CloseThePersonalShop), por `send`. Sin él, esos
    // envíos cortan; lo demás de abrir y cerrar las tiendas no lo necesita.
    void LendThePersonalShops(WorldState& world, Render::Models::CrtRandom& random,
                              std::function<void(Protocol::Outgoing)> send);

    // Todo lo que un cierre del original manda al GS desde 0x657a90, 0x657ab0, 0x657880 y 0x65cae0: el C1 03 31 de
    // las misiones y de los NPC de evento (`sendWindowClose`), el C1 03 3D del comercio (0x657fe1), el F3 10 y el
    // 3F 07 de las tiendas y los A9 del inventario de un Dark Lord (`send`, que el comercio y las tiendas abren). Lo
    // prestan los caminos que abren o cierran ventanas fuera del paso de la UI: Esc (0x623f17), caminar (0x56b39d), el
    // 37 (0x659e46), el 3F 05 (0x65b319), la entrada, el teleport, el regen, los eventos, las misiones y la caja 0x9c.
    // Sin `send`, o sin `sendWindowClose`, quedan los cortes de cada uno.
    void LendTheSession(WorldState& world, Render::Models::CrtRandom& random,
                        std::function<void(Protocol::Outgoing)> send, std::function<void()> sendWindowClose);
    // Lo mismo con `requests` (SendGamePacket y SendWindowClose); con nullptr, nada.
    void LendTheSession(WorldState& world, Render::Models::CrtRandom& random, IGameUiRequests* requests);

private:
    UI::PanelContext m_context;
    UI::PanelRegistry m_registry;
};
} // namespace Client::Scenes
