#pragma once

#include "GameLogic/MovementSpeed.h"
#include "GameLogic/GateDecision.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "World/LeafWeather.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <optional>

namespace Client::Scenes
{
// Valores en claro y profundidades persistentes del port. Sus defaults son entradas del modelo,
// no observaciones del arranque nativo; los tests varían 0/1/255 y conservan ese límite de P.
struct WorldRuntime
{
    std::int32_t map = -1;
    GameLogic::MovementSpeedSharedState movementSpeed;
    PlayerActionSpeeds playerActionSpeeds;
    // Disparadores lógicos observables; la reproducción de audio esCL10.
    std::optional<std::int32_t> lastSoundId;
    std::uint64_t soundRequestCount = 0;
    // Metadata runtime de05769ad8+type*e0+aa. Ausencia no se convierte en sentinelffff.
    std::map<std::uint16_t, std::array<std::int16_t, 2>> modelSoundIds;
    std::optional<std::uint8_t> stopTileFlags;
    std::int32_t ownKey = 0;
    std::optional<std::uint8_t> state7239Ec;
    std::size_t hero = 0;
    std::size_t actorConstructionIndex = 0; // 057678b0, distinto del punteroHero publicado después.
    std::uint32_t lastTeleportTick = 0;
    bool warpPending = false;
    bool gateRequestBlocked = false;
    std::uint32_t mouseUpdateTime = 0;
    std::uint32_t mouseUpdateTimeMax = 0;
    std::array<std::int32_t, 5> targets{};
    std::int32_t warpCounter = 0;
    std::uint8_t ownRecordDepth = 0;
    std::uint8_t warpDepth = 0;
    std::uint8_t loadingOnceFlag = 0;
    bool cameraPathActive = false;
    // Input vivo006c8e3c; default del DTO, no inicial nativo acreditado.
    std::int32_t headTargetLock = 0;
    std::vector<GameLogic::GateWarning> gateWarnings;
    std::int16_t alternateMount = -1;
    std::int32_t alternateMountActive = 0;
    bool uiMovementBlocked = false;
    bool inputActionBlocked = false;
    bool window8000Open = false;
    bool pendingLeftPress = false;
    bool forcedLeftRelease = false;
    float mouseHoldStart = 0.0f;
    float mouseHoldDuration = 0.0f;
    std::uint32_t actionCommand = 0;
    std::uint32_t heroTerrainLayer1 = 0;
    bool drawingEnabled = false;
    bool enteredWorld = false;
    // Globales de530490: sobreviven a Release y a cambios de escena/mapa.
    // 530490 § datos: W tiene DWORD30 en la imagen; sólo inicializa, nunca sustituye Evaluate.
    World::LeafWeather leafWeather{.firstWave = 30};
    // 05763358: cero de imagen sólo al crear la instancia;430510 es su productor de dibujo.
    std::int32_t leafSelector = 0;
    // 05767b88: publicado desde qT raw32 por Main; ausente hasta la primera muestra válida.
    std::optional<float> worldTime;
};
} // namespace Client::Scenes
