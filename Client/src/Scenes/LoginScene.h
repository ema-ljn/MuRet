#pragma once

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneWorlds.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// La escena 2 (CL5.4): el login de 0x634c60, que entra con 0x6349a0 y dibuja con 0x6350b0 (06 § El
// login, al entrar y § El login, el dibujo). Sin su UI todavía (las ventanas, la lista y la cuenta
// son de la CL5.5), sin el logo ni el personaje (CL6) y sin la música (CL10).
namespace Client::Scenes
{
// Los tres textos de abajo, por su índice en Text.bmd.
inline constexpr std::size_t LoginLeftText = 454;
inline constexpr std::size_t LoginRightText = 455;
inline constexpr std::size_t LoginVersionText = 456;

// El 456 con la versión, como el sprintf de 0x635464 pero sin usar el texto como formato (Q-C61):
// el primer "%s" pasa a ser `version` y lo demás queda como está, otros "%" también. Sin "%s", el
// texto tal cual.
[[nodiscard]] std::string PlaceVersion(std::string_view text, std::string_view version);

class LoginScene final : public IScene
{
public:
    // `data` y `session` tienen que vivir más que la escena. `session` es nullptr sin servidor
    // (Main sin host): la escena dibuja y no conecta. `version` es la de MuRet (MURET_VERSION).
    LoginScene(const SceneContext& context, const GameData& data, App::ServerSession* session,
               std::string_view version);

    // Como 0x6349a0, cada vez que se entra: sube LoginTextureSet (0x61c7c0) y después conecta al
    // ConnectServer (0x4c1340, ServerSession::Start). Una textura que falta corta antes de
    // conectar.
    Core::Expected<void, std::string> Enter() override;
    // La lógica de 0x634c60 que no es de la UI: nada, todavía. La sesión la corre el SceneManager.
    void Step(const Input::InputState& input) override;
    // 0x6350b0 sin la cámara, el fundido (Q-C66) ni lo común a las escenas.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta sus 18 slots.
    void Leave() override;
    // La selección con el login aceptado (SessionState::LoggedIn), como 0x634dce con el 0x14.
    [[nodiscard]] std::optional<SceneId> Next() const override;

private:
    void DrawTexts(SceneCanvas& canvas) const;

    SceneContext m_context;
    App::ServerSession* m_session;
    SceneTextures m_textures;
    // El de SceneWorlds.h: el 55.
    std::optional<SceneWorld> m_world;
    // Los tres textos, o vacíos sin Text.bmd. El de la versión, ya armado.
    std::optional<std::string_view> m_left;
    std::optional<std::string_view> m_right;
    std::optional<std::string> m_version;
};
} // namespace Client::Scenes
