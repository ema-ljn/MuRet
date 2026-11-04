#pragma once

#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/DoubleClick.h"
#include "Input/InputState.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Pose.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CreatePreview.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "UI/CreateCharacterWindow.h"
#include "UI/NameChecks.h"
#include "UI/NoticeWindow.h"
#include "UI/SelectionBar.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// La escena 4, la selección de personaje (0x633f20 al entrar, 0x6342c0 la lógica y 0x634680 el
// dibujo): los personajes de la lista en sus lugares, animados, el clic que elige el que está bajo el
// mouse, y Enter o el doble clic que entra al mundo con el elegido. Arriba, la barra de la selección
// (UI::SelectionBar) con conectar, crear y borrar, la ventana de crear (UI::CreateCharacterWindow) y
// los avisos. Sin el terreno, los objetos ni los efectos (CL7 y la ola de los efectos), sin la vista
// previa de la ventana de crear (CL6.5 PR B3), el menú y las opciones (CL8), los carteles de los
// personajes ni el área de mensajes.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La selección de personaje", y el plan de
// la CL6 (CL6.3, "Contrato").
namespace Client::Scenes
{
class CharacterSelectScene final : public IScene
{
public:
    // `data`, `models` y `session` tienen que vivir más. `session` es nullptr sin servidor: la escena
    // dibuja los fondos y no pide nada.
    // `gameSlots` es la tabla de los slots de la carga (GameModels::TextureSlots), que la vista previa
    // de la ventana de crear sigue desde 0x6f8; nullptr, una tabla vacía. Tiene que vivir más.
    CharacterSelectScene(const SceneContext& context, const GameData& data, const IModelLibrary& models,
                         App::ServerSession* session, const Render::Models::ModelTextureSlots* gameSlots = nullptr);

    // Como 0x633f20: su juego de texturas y sus modelos (0x61ca30: Logo05 y los NewFace de la vista
    // previa, SelectionModels), el de las ventanas, ninguno elegido, y el F3 00
    // (ServerSession::RequestCharacterList).
    Core::Expected<void, std::string> Enter() override;
    // 0x6342c0: nada hasta la lista; después, los personajes de la lista y su avance, las respuestas
    // del borrado y de crear, y la UI de arriba hacia abajo: el aviso si está abierto (con la ventana
    // de crear debajo, que sigue tomando lo que se escribe); si no, la ventana de crear, que es modal;
    // si no, la barra, el doble clic que entra, el clic que elige y Enter. Con el mouse sobre la
    // barra o una ventana abierta (+0x8de8), ni el clic ni el doble clic, y el contorno se queda en el
    // último; un aviso que se abre en el paso se atiende desde el que sigue.
    void Step(const Input::InputState& input) override;
    // 0x634680 sin el terreno, los objetos ni los efectos: con sesión, nada hasta la lista; el de bajo
    // el mouse, con el contorno. Después, la UI: la barra, la ventana de crear (su base, la vista
    // previa en su propio pase 3D, que no se oscurece, y lo demás) y el aviso.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta sus slots.
    void Leave() override;
    // WorldLoading después de entrar. Si la sesión falla, la escena no se va: abre el aviso 0x70.
    [[nodiscard]] std::optional<SceneId> Next() const override;
    // Con el OK del aviso 0x70, que cierra el juego (Q-C69).
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    // Un personaje de la lista en su lugar, con la entrada de la que salió.
    struct Character
    {
        std::uint8_t slot = 0;
        Protocol::Character::CharacterEntry entry;
        GameLogic::CharacterAppearance appearance;
        CharacterAnimation animation;
    };

    // Si la escena todavía espera la lista: con sesión, hasta ChoosingCharacter.
    [[nodiscard]] bool WaitingForTheList() const;
    // La lista de la sesión a los personajes: la primera vez, todos, y el +0x543 de la barra; después,
    // el que crea la sesión entra y el que borra se va, y los que siguen guardan su animación.
    void SyncTheCharacters();
    [[nodiscard]] Character MakeCharacter(const Protocol::Character::CharacterEntry& entry) const;
    void StepCharacters();
    // Si abrió el aviso 0x70 en este paso.
    bool ShowTheFailure();
    // Con la respuesta del F3 01 (0x4c2290): con 1 se cierra el 0x5d; con 0 o 2, la ventana de crear
    // vuelve a abrirse con su aviso arriba. Si abrió uno en este paso.
    bool TakeTheCreationResult();
    // 0x63d880 con la ventana abierta y sin aviso arriba.
    void StepTheCreateWindow(const Input::InputState& input);
    // 0x63d990: los chequeos del nombre, y el F3 01 y el 0x5d, o el aviso del nombre.
    void SubmitTheCreation();
    // 0x63e430 al final del paso de la ventana, que solo corre con ella arriba: la clase de la ventana
    // y el avance. Con un aviso arriba la vista previa no avanza (el original la avanza también desde
    // la lógica de la escena, 0x508ea0: dos veces por paso con la ventana arriba; MuRet, una, D-C4).
    void StepThePreview();
    void DrawTheCreateWindow(SceneCanvas& canvas, float alpha);
    // Con la respuesta del F3 02, su aviso en lugar del 0x5d (0x4e2fe9), salvo que la sesión haya
    // caído. Si abrió uno en este paso.
    bool TakeTheDeletionResult();
    void StepTheNotice(const Input::InputState& input);
    // 0x63eb40: crear con un lugar libre de los cinco; conectar y borrar con uno elegido.
    void EnableTheBar();
    void OnTheBar(UI::SelectionBarClick click);
    // 0x63edf0: las revisiones del borrado con el elegido, y su aviso.
    void AskToDelete();
    // 0x643740: el F3 02 con el código del aviso 0x5f, y el 0x5d.
    void SendTheDeletion();
    // El lugar del personaje bajo el mouse, con el rayo del último pase dibujado.
    [[nodiscard]] std::optional<std::uint8_t> UnderTheMouse(const Input::InputState& input) const;
    void Choose(std::optional<std::uint8_t> slot);
    // Enter con uno elegido: el aviso 0x84 si está bloqueado, o el mundo.
    void TryToEnter();
    [[nodiscard]] Render::Models::ObjectTransform ObjectOf(const Character& character) const;
    // El buffer de un slot de las ventanas o de la escena.
    [[nodiscard]] TextureBufferSize BufferSize(Render::TextureSlot slot) const;

    SceneContext m_context;
    const Data::Text::TextTable* m_texts;
    const IModelLibrary& m_models;
    App::ServerSession* m_session;
    SceneTextures m_textures;
    SceneTextures m_windowTextures;
    UI::NoticeWindow m_notice;
    UI::SelectionBar m_bar;
    // Las palabras de FilterName.bmd y Filter.bmd de `data`, y los textos 457 y 458.
    UI::NameFilters m_nameFilters;
    UI::CreateCharacterWindow m_create;
    const Render::Models::ModelTextureSlots* m_gameSlots;
    SelectionModels m_selectionModels;
    // El objeto de la vista previa (0x7437050).
    CreatePreview m_preview;
    Input::DoubleClick m_doubleClick;
    std::vector<Character> m_characters;
    bool m_tookTheList = false;
    // Desde que se mandó el F3 02 (o el F3 01) hasta su respuesta.
    bool m_waitingForTheDeletion = false;
    bool m_waitingForTheCreation = false;
    std::optional<Render::Pass3D> m_lastPass;
    // El que estaba bajo el mouse en el último paso, que lleva el contorno: como el DAT_006c8e30
    // que 0x59ec80 deja para el frame siguiente.
    std::optional<std::uint8_t> m_hovered;
    App::PixelSize m_screen{640, 480};
    // Los pasos desde que se entró: el reloj del doble clic.
    std::uint64_t m_steps = 0;
    std::optional<SceneId> m_next;
    bool m_failureShown = false;
    bool m_quit = false;
};
} // namespace Client::Scenes
