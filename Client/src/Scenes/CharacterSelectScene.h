#pragma once

#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/DoubleClick.h"
#include "Input/InputState.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Pose.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "UI/NoticeWindow.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// La escena 4, la selección de personaje (0x633f20 al entrar, 0x6342c0 la lógica y 0x634680 el
// dibujo): los personajes de la lista en sus lugares, animados, el clic que elige el que está bajo el
// mouse, y Enter o el doble clic que entra al mundo con el elegido. Sin el terreno, los objetos ni los
// efectos (CL7 y la ola de los efectos) y sin la UI de crear y borrar (CL6.5).
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La selección de personaje", y el plan de
// la CL6 (CL6.3, "Contrato").
namespace Client::Scenes
{
class CharacterSelectScene final : public IScene
{
public:
    // `data`, `models` y `session` tienen que vivir más. `session` es nullptr sin servidor: la escena
    // dibuja los fondos y no pide nada.
    CharacterSelectScene(const SceneContext& context, const GameData& data, const IModelLibrary& models,
                         App::ServerSession* session);

    // Como 0x633f20: su juego de texturas (0x61ca30) y el de las ventanas, ninguno elegido, y el
    // F3 00 (ServerSession::RequestCharacterList).
    Core::Expected<void, std::string> Enter() override;
    // 0x6342c0: nada hasta la lista; después, el avance de cada personaje, el aviso si está abierto, y
    // si no, el doble clic que entra, el clic que elige y Enter.
    void Step(const Input::InputState& input) override;
    // 0x634680 sin el terreno, los objetos ni los efectos: con sesión, nada hasta la lista.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta sus slots.
    void Leave() override;
    // WorldLoading después de entrar; Login si la sesión falló.
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    // Un personaje de la lista en su lugar.
    struct Character
    {
        std::uint8_t slot = 0;
        GameLogic::CharacterAppearance appearance;
        Render::Models::AnimationState body;
        Render::Models::TimedAnimation pet;
    };

    // Si la escena todavía espera la lista: con sesión, hasta ChoosingCharacter.
    [[nodiscard]] bool WaitingForTheList() const;
    // La lista de la sesión a los personajes, una vez.
    void TakeTheList();
    void StepCharacters();
    // El lugar del personaje bajo el mouse, con el rayo del último pase dibujado.
    [[nodiscard]] std::optional<std::uint8_t> UnderTheMouse(const Input::InputState& input) const;
    void Choose(std::optional<std::uint8_t> slot);
    // Enter con uno elegido: el aviso 0x84 si está bloqueado, o el mundo.
    void TryToEnter();
    [[nodiscard]] Render::Models::ObjectTransform ObjectOf(const Character& character) const;

    SceneContext m_context;
    const Data::Text::TextTable* m_texts;
    const IModelLibrary& m_models;
    App::ServerSession* m_session;
    SceneTextures m_textures;
    SceneTextures m_windowTextures;
    UI::NoticeWindow m_notice;
    Input::DoubleClick m_doubleClick;
    std::vector<Character> m_characters;
    bool m_tookTheList = false;
    std::optional<Render::Pass3D> m_lastPass;
    App::PixelSize m_screen{640, 480};
    // Los pasos desde que se entró: el reloj del doble clic.
    std::uint64_t m_steps = 0;
    std::optional<SceneId> m_next;
};
} // namespace Client::Scenes
