#pragma once

#include "Data/Tables/ItemTable.h"
#include "UI/PanelRegistry.h"

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
class WorldState;

// Lo que 0x550d10 lee de la carga cuando abrir el personaje la llama (0x65ca1e): Item.bmd (*[0x7758550]) e
// ItemSetType.bmd con ItemSetOption.bmd (0x40c600). Solo las presta el paso de la UI, el único que abre el personaje;
// con alguna en nullptr, 0x550d10 corta con error en lo primero que la lee.
struct GamePanelTables
{
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::ItemSetTables* itemSetTables = nullptr;
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

private:
    UI::PanelContext m_context;
    UI::PanelRegistry m_registry;
};
} // namespace Client::Scenes
