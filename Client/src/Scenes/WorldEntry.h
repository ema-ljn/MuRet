#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Outgoing.h"
#include "Scenes/SceneId.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Client::Core
{
class Log;
class IClock;
} // namespace Client::Core

namespace Client::Data::Tables
{
struct ItemSetTables;
}

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Protocol::Character
{
struct CharacterSelectResult;
}

namespace Client::Render::Models
{
struct ClothWorld;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::World
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::World

// La respuesta del F3 03, 0x4c3940 con el paquete cifrado: la carga del mapa, el héroe y lo que
// sigue. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La entrada al mundo" (la tabla
// de 0x4c3940), "El héroe" y "La carga del mapa"; 04-protocolo.md, "La entrada al mundo".
namespace Client::Scenes
{
class IModelLibrary;
class IWorldEntryPublisher;
class SelectionState;
class WorldState;

// Lo que la entrada usa y dura toda la sesión. main lo arma una vez; los tests, con lo suyo.
struct WorldEntryResources
{
    // El pool, el registro propio, los globales y el mapa cargado.
    WorldState& world;
    // Quien presta el terreno vigente a la cámara: después de la carga, el del mapa nuevo.
    SelectionState& selection;
    std::filesystem::path dataRoot;
    // El rand() de la CRT y el contador de pasos (0x7d3a0f0) con su cuenta de accesos.
    Render::Models::ClothWorld& cloth;
    // La altura fija (0x6c9d5c) para 0x5f93c0; la selección también la mueve.
    World::GameCamera& camera;
    // El mundo en curso, el héroe publicado y el atributo de su celda, como los lee la cámara.
    World::GameCameraInput& cameraInput;
    // Player.bmd (SkeletonOf(0x20a)), que 0x4f2250 necesita para elegir la acción.
    const IModelLibrary& models;
    // Item.bmd, para la matriz de armas de 0x4f2250 y para 0x550d10; nullptr si no se cargó.
    const Data::Tables::ItemTable* items = nullptr;
    // ItemSetType.bmd e ItemSetOption.bmd (0x40c600), para 0x40e1b0 desde 0x550d10; nullptr si no se
    // cargaron: 0x550d10 corta en 0x40e1b0.
    const Data::Tables::ItemSetTables* itemSetTables = nullptr;
    // Q-C144 avisa acá.
    Core::Log& log;
    Core::IClock* clock = nullptr;
    // El B9 02 que 0x42a760 manda al cargar el 30 con el socket del GS abierto: SessionEventApplier lo ata a la
    // sesión que aplica la respuesta. Vacío, como el socket en −1, no sale nada.
    std::function<void()> requestCastleSiegeState{};
    // Los paquetes de las ventanas de los NPC (CL9 hito 6, Scenes/NpcWindows.h): SessionEventApplier y la escena lo
    // atan a la sesión (App::ServerSession::SendNpcWindowPacket). Vacío, lo que manda corta con error.
    std::function<Core::Expected<void, std::string>(Protocol::Outgoing)> sendNpcWindowPacket{};
    // 0x419900, el C1 03 31 en claro que manda cerrar las misiones (0x657a90 desde el A1, el A2 y los cambios de mapa).
    // Lo presta el sink con la sesión; sin él, cerrar las misiones abiertas corta.
    std::function<void()> sendWindowClose{};
    // Lo que miden los mensajes que llegan (00, 01, 02, 0C, 0D) al entrar al chat (0x555190, 0x554610, 0x45bb20): el
    // medidor del frame y el tamaño de la ventana. Sin medidor, un mensaje corta con error.
    const Render::Text::ITextMeasure* textMeasure = nullptr;
    std::function<App::PixelSize()> screen{};
    // 0x6d5dc8, la escena en curso, que miran el 00 (la 2) y el 0D (la 4 y la 5). Vacía, ninguna.
    std::function<std::optional<SceneId>()> scene{};
    // Text.bmd: el 482 del 0C y el 483 del 0D con 2. nullptr si no se cargó: esos mensajes cortan con error.
    const Data::Text::TextTable* texts = nullptr;
};

// Lo que la entrada pide afuera y MuRet todavía no hace (sin audio hasta la CL10): los sonidos de
// 0x41ee00(id, 1) y el de Fenrir de 0x4f2250, en el orden en que los pide.
struct WorldEntryResult
{
    std::vector<std::int32_t> sounds;
};

// 0x4c3940 con el paquete cifrado, en el orden nativo de la tabla de 06: los stats en el registro
// propio; el mundo en curso y la carga (LoadMap y ArmEventMapObject); el héroe en chars[rand() % 400],
// construido (ConstructActor) con la clave del jugador (`playerKey`, DAT_057678bc), la clase, el PK y
// el CtlCode; y 0x512a30 con el equipo todavía viejo. Si 0x512a30 vuelve, sigue con SettleWorldEntry.
// Lo que MuRet no tiene corta con error y deja lo que el original hizo antes: el héroe sin publicar,
// el contador sin tocar. Los tres cierres del registro propio en línea (0x4c41c4, 0x4c46f0 y
// 0x4c481e) gastan sus 3.848 rand() si lo dejan sin accesos, como el de adentro de 0x512a30.
// Una segunda respuesta, con el héroe ya en el mundo, no es otro camino: nada la frena (0x4e306b) y
// corre todo de nuevo sobre lo que dejó la primera, con la carga del mapa siempre, otro lugar para el
// héroe y el anterior vivo si no cae en el suyo (06, "Una segunda respuesta").
[[nodiscard]] Core::Expected<WorldEntryResult, std::string>
ApplyWorldEntry(const Protocol::Character::CharacterSelectResult& reply, std::uint16_t playerKey,
                const WorldEntryResources& resources, IWorldEntryPublisher& publisher);

// Lo que sigue a 0x512a30 (0x4c4725..0x4c4b60) sobre el héroe de WorldRuntime::heroSlot, ya vestido:
// lo publica (0x743a170) y le copia el nombre, vacía el equipo del registro, crea el primer efecto de
// llegada, publica el 0x3d en la sesión, pone el contador en 30 (4 rand() al cerrar), mira el cuervo
// (0x420d60), crea el segundo efecto, baja el alpha a 0, cierra las ventanas (0x657a90: 2 rand() con
// todas cerradas), la acción de parado (0x4f2250) y los sonidos de los mapas fuera de evento. Va
// aparte para probarlo sin 0x512a30 (que sin las tablas de los conjuntos corta en 0x550d10, en
// 0x40e1b0); recibe lo que ya se pidió.
[[nodiscard]] Core::Expected<WorldEntryResult, std::string>
SettleWorldEntry(WorldEntryResult result, const WorldEntryResources& resources, IWorldEntryPublisher& publisher);
} // namespace Client::Scenes
