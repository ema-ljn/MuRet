#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
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
    // 0x659f3d: abrir el panel 1 llama a 0x491080(0x186, 0x106) (Scenes::OpenTheFriends), con lo que lee del paso (la
    // vista y los globales de las ventanas viejas). Lo presta el paso de la UI; sin él, abrirlo corta.
    void LendTheFriends(std::function<Core::Expected<void, std::string>()> opened);

    // 0x419900: cerrar las misiones manda el C1 03 31 con `send`. Lo prestan el paso de la UI y los handlers del A1 y
    // del A2 (0x657a90); sin él, cerrarlas corta.
    void LendTheWindowClose(std::function<void()> send);
    // Lo mismo con el C1 03 31 de `requests` (IGameUiRequests::SendWindowClose); con nullptr, sin él.
    void LendTheWindowClose(IGameUiRequests* requests);

private:
    UI::PanelContext m_context;
    UI::PanelRegistry m_registry;
};
} // namespace Client::Scenes
