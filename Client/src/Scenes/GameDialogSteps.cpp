#include "Scenes/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Data/Text/DialogTable.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/DialogAnswers.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/JewelMix.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/NpcWindowSteps.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/MacroFile.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x41ed10(0x19, 0, 0): el sonido de los botones de los diálogos.
constexpr std::int32_t DialogSound = 0x19;
// 0x6c8dcc: el máximo de MouseUpdateTime que dejan los botones.
constexpr std::int32_t DialogMouseUpdateTimeMax = 6;

// 0x62dac4: la tabla de los cuatro primeros botones del menú de Esc; el 4 va derecho a 0x6240a1.
constexpr std::int32_t ExitGameButton = 0;
constexpr std::int32_t AnotherServerButton = 1;
constexpr std::int32_t AnotherCharacterButton = 2;
constexpr std::int32_t OptionsButton = 3;
// 0x623f9c..0x623fb3: 5 botones en la escena 5.
constexpr std::int32_t EscapeMenuButtons = 5;
// 0x623fe8..0x62400d: x en [0x104, 0x17c) e y en [(6 + 3i) · 10, + 0x16).
constexpr std::int32_t EscapeMenuLeft = 0x104;
constexpr std::int32_t EscapeMenuRight = 0x17c;
constexpr std::int32_t EscapeMenuButtonHeight = 0x16;
// 0x624087: el 592 de Text.bmd (0x76e879c), de error (tipo 2), cuando la caja del caos está abierta.
constexpr std::size_t ChaosBoxOpenText = 592;

// 0x459f00(0x104, y, 0x78, 0x16, 1): las filas de las opciones.
constexpr std::int32_t OptionsLeft = 0x104;
constexpr std::int32_t OptionsWidth = 0x78;
constexpr std::int32_t OptionsRowHeight = 0x16;
constexpr std::int32_t BackRowY = 0x1e;
constexpr std::int32_t AutoAttackRowY = 0x3c;
constexpr std::int32_t NoticeSoundRowY = 0x5a;
constexpr std::int32_t VolumeRowY = 0x78;
constexpr std::int32_t SlidesRowY = 0x96;
constexpr std::int32_t CloseRowY = 0xe8;
// 0x625715..0x625730: la barra del volumen, 0x32 × 8 desde su esquina; cada 5 píxeles, un punto.
constexpr std::int32_t VolumeBarWidth = 0x32;
constexpr std::int32_t VolumeBarHeight = 8;
constexpr std::int32_t VolumeStepPixels = 5;
// 0x625794..0x625799: el volumen va de 0 a 9.
constexpr std::int32_t MaxVolume = 9;
// 0x6257f5: 0x41eed0(−10000), el silencio de DirectSound.
constexpr std::int32_t SilentVolume = -10000;
// 0x6b87a8 (10,0f) y 0x6ba7a0 (−2000,0): −2000 · log10(10 / v).
constexpr double VolumeNumerator = 10.0;
constexpr double VolumeScale = -2000.0;
// 0x625864..0x625897: las cinco casillas del brillo, desde x = 0x10e de a 0x14 (hasta 0x172), de 0x12 de ancho y en
// y de 0xc8 a 0xda, con los bordes.
constexpr std::int32_t GlowBoxFirstX = 0x10e;
constexpr std::int32_t GlowBoxStep = 0x14;
constexpr std::int32_t GlowBoxEndX = 0x172;
constexpr std::int32_t GlowBoxWidth = 0x12;
constexpr std::int32_t GlowBoxTop = 0xc8;
constexpr std::int32_t GlowBoxBottom = 0xda;

// 0x6263f7..0x626417: el botón del OK genérico.
constexpr std::int32_t OkLeft = 0x11c;
constexpr std::int32_t OkRight = 0x162;
constexpr std::int32_t OkTop = 0x62;
constexpr std::int32_t OkBottom = 0x77;
// 0x6263f2: el 0x73 no mira el botón, solo Enter.
constexpr std::int32_t EnterOnlyDialog = 0x73;
// 0x626433..0x626451: los que cierran el juego (SendMessageA con WM_DESTROY, 0x62828e).
constexpr std::array<std::int32_t, 4> QuittingDialogs{0x70, 0x6f, 0x1a, 0x1c};
// 0x62825c: el que cierra y llama a 0x5e7aa0(6).
constexpr std::int32_t ReopeningDialog = 0x8f;
constexpr std::int32_t ReopenedKeypadMode = 6;
// 0x62843f..0x62845d: los de sí y no que solo dejan la respuesta en 0x6c932e (0x62a779).
constexpr std::int32_t AnswerDialog = 0x94;
constexpr std::int32_t ConfirmDialog = 0x98;
// 0x629c0e: la joya de bendición sobre el 0x1a25 (CL9 hito 5, 0x5d8324).
constexpr std::int32_t BlessDialog = 0xa9;
// 0x625910: el modo de la caja del caos, que abre 0x65aaf2 al abrirla.
constexpr std::int32_t ChaosMixModeDialog = 0x8e;
// 0x6259c6, 0x625b23 y 0x625f98: la mezcla de joyas.
constexpr std::int32_t JewelMenuDialog = 0xa1;
constexpr std::int32_t JewelChoiceDialog = 0xa2;
constexpr std::int32_t JewelResultDialog = 0xa6;
// 0x625929..0x625986: los dos botones del 0x8e, x en [245, 395] e y desde 180 de a 85, de 35 de alto, con los bordes.
constexpr std::int32_t MixModeLeft = 245;
constexpr std::int32_t MixModeRight = 395;
constexpr std::int32_t MixModeFirstTop = 180;
constexpr std::int32_t MixModeStep = 85;
constexpr std::int32_t MixModeHeight = 35;
constexpr std::int32_t MixModes = 2;
// 0x6282f1..0x628302: la y de los dos botones, 0x64 (0xd2 en el 0xa3), y su x, 0xd5.
constexpr std::int32_t YesNoButtonsTop = 0x64;
constexpr std::int32_t ServerTimeButtonsTop = 0xd2;
constexpr std::int32_t YesNoButtonsLeft = 0xd5;
// 0x62a79c y 0x62a7b0: las respuestas.
constexpr std::uint8_t YesAnswer = 1;
constexpr std::uint8_t NoAnswer = 2;
// 0x628b32..0x628b49: el sí del 0x96 abre el 0xa8 con 0x633300(1, 0x76e, 0xa8).
constexpr std::int32_t LuckDialogLines = 1;
constexpr std::int32_t LuckDialogText = 0x76e;
// 0x629c60..0x629c6c, 0x628bec..0x628bf8 y 0x6293c5..0x6293d1: 0x5af520 en falso, el aviso 474 (0x76dfd54) de error.
constexpr std::size_t BlessRefusedText = 474;
constexpr std::int32_t ErrorMessageType = 2;
// 0x628582..0x62858e: el sí del 0x9a sin el ítem (el dword 0x78a59cc en 0xff) no hace nada.
constexpr std::uint32_t NoDialogItem = 0xff;
// 0x628702..0x628710: el nivel del ítem, (0x78a59cc >> 3) & 0xf.
constexpr unsigned ItemLevelShift = 3;
constexpr std::uint32_t ItemLevelMask = 0xf;
// 0x628bb2..0x628bd7: el tercer botón del 0xa8 (o su Esc) elige 2, que cierra sin el sonido.
constexpr std::int32_t ThirdChoice = 2;
// 0x626112..0x626145 y 0x626200..0x626227: el OK y el cancelar del 0x91, con los bordes.
constexpr std::int32_t TypedOkLeft = 0xff;
constexpr std::int32_t TypedOkRight = 0x133;
constexpr std::int32_t TypedCancelLeft = 0x14a;
constexpr std::int32_t TypedCancelRight = 0x17e;
constexpr std::int32_t TypedButtonsTop = 0xa0;
constexpr std::int32_t TypedButtonsBottom = 0xb0;
// 0x626251..0x626293: lo que pide el 0x91 (0x78aba60).
constexpr std::int32_t TypedTitle = 1;
constexpr std::int32_t TypedPrice = 2;

// 0x61c2b6..0x61c2c3: los lugares de las teclas que recorre (de 0 a 0x71).
constexpr std::size_t OptionKeyPlaces = 0x72;
constexpr std::uint8_t NoKey = 0xff;
// Los bytes de la pila de 0x61c210: [10] las banderas, [11..13] Q, W y E y [14] el registro del chat.
constexpr std::size_t FlagsByte = 10;
constexpr std::size_t FirstQuickSlotByte = 11;
constexpr std::size_t ChatWindowByte = 14;
constexpr std::uint8_t AutoAttackOn = 0x01;
constexpr std::uint8_t AutoAttackOff = 0x02;
constexpr std::uint8_t NoticeSoundOn = 0x04;
constexpr std::uint8_t NoticeSoundOff = 0x08;
constexpr std::uint8_t SlidesOff = 0x10;
// 0x61c3b2..0x61c3c5: los renglones / 3 (IMUL por 0x55555556); 0x61c3d9: la transparencia · 10,0f.
constexpr std::int32_t RowsPerStep = 3;
constexpr double AlphaSteps = 10.0;
constexpr unsigned NibbleShift = 4;

// La tabla de despacho, en el orden de 0x623f78..0x6263d9. Las filas del OK genérico son las que el original
// compara antes de llegar a él (0x73 y 0x8f) y su tramo los atiende aparte.
constexpr auto Routes = std::to_array<DialogRoute>({
    {UI::EscapeMenuDialog, DialogStep::EscapeMenu, "0x623f90", ""},
    {UI::OptionsDialog, DialogStep::Options, "0x62556f", ""},
    {ChaosMixModeDialog, DialogStep::ChaosMixMode, "0x625910", ""},
    {JewelMenuDialog, DialogStep::JewelMenu, "0x6259c6", ""},
    {JewelChoiceDialog, DialogStep::JewelChoice, "0x625b23", ""},
    {0xa4, DialogStep::Cut, "0x625cf4", "fase 3 del hito 6 (08 P-C198)"},
    {JewelResultDialog, DialogStep::JewelResult, "0x625f98", ""},
    {0xa7, DialogStep::Cut, "0x62601e", "CL9.4"},
    {0x9c, DialogStep::Cut, "0x6260d1", "CL9.4"},
    {0x91, DialogStep::TypedText, "0x6260f6", ""},
    {UI::MessageBoxDialog, DialogStep::Buttons, "0x62d756", ""},
    {UI::AnswersDialog, DialogStep::Buttons, "0x62d756", ""},
    {UI::PagedDialog, DialogStep::Buttons, "0x62d756", ""},
    {0x8b, DialogStep::Buttons, "0x62d756", ""},
    {0x97, DialogStep::Buttons, "0x62d756", ""},
    {0x9b, DialogStep::Buttons, "0x62d756", ""},
    {0x71, DialogStep::Cut, "0x62a810", "CL9.4"},
    {0x7d, DialogStep::Cut, "0x62a810", "CL9.4"},
    {0x95, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {0x76, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {UI::IgnoredOverChaosCastle, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {0x77, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {UI::ForcedDialog, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {AnswerDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::ItemNameDialog, DialogStep::YesNo, "0x6282dc", ""},
    {ConfirmDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::MoneyTextDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::ChaosCastleDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::ServerTimeDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::ItemNameWithLuckDialog, DialogStep::YesNo, "0x6282dc", ""},
    {BlessDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::NoticeQuestionDialog, DialogStep::YesNo, "0x6282dc", ""},
    {UI::DisconnectDialog, DialogStep::GenericOk, "0x6263df", ""},
    {0x6f, DialogStep::GenericOk, "0x6263df", ""},
    {0x1a, DialogStep::GenericOk, "0x6263df", ""},
    {0x1c, DialogStep::GenericOk, "0x6263df", ""},
    {EnterOnlyDialog, DialogStep::GenericOk, "0x6263df", ""},
    {ReopeningDialog, DialogStep::GenericOk, "0x6263df", ""},
});

bool InBox(const GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t width, std::int32_t height)
{
    return context.mouseX >= left && context.mouseX < left + width && context.mouseY >= top &&
           context.mouseY < top + height;
}

// Lo que dejan los botones: MouseUpdateTime (0x77f3b10) en 0, su máximo en 6 y el sonido.
void PressTheButton(WorldRuntime& runtime)
{
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = DialogMouseUpdateTimeMax;
    runtime.lastSoundId = DialogSound;
}

// 0x62da70 y 0x628280: el que espera pasa a abierto.
void CloseTheDialog(WorldRuntime& runtime)
{
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    PressTheButton(runtime);
}

Core::Expected<IGameUiRequests*, std::string> RequestsOf(const GameUiContext& context, std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: sin el cable ni Macro.txt", where));
    return context.requests;
}

// 0x61c210 y 0x61c170(Macro.txt): las opciones al GS y las macros al disco.
Core::Expected<void, std::string> SaveTheOptions(const GameUiContext& context, std::string_view where)
{
    auto requests = RequestsOf(context, where);
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    auto body = OptionBodyOf(context.world, context.random, context.chosenSlot);
    if (!body.HasValue())
        return Core::Unexpected(std::move(body).Error());
    requests.Value()->SendOptionData(body.Value());
    return requests.Value()->SaveMacros(UI::WrittenMacros(context.world.Runtime().chat.input.macros));
}

// 0x659290(0x100): la caja del caos abierta, sin rand().
bool ChaosBoxOpen(const GameUiContext& context)
{
    GamePanels panels(context.world, context.random);
    return panels.Registry().IsOpen(UI::ChaosBoxPanel);
}

// 0x555190(0x7d3a14c, Text[592], 2): el nombre es una cadena vacía de la .bss.
Core::Expected<void, std::string> SayTheChaosBoxIsOpen(const GameUiContext& context, std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, ChaosBoxOpenText);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, ChaosBoxOpenText));
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el mensaje del texto {} sin medidor", where, ChaosBoxOpenText));
    return AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {}, *text, UI::ErrorMessage);
}

// 0x62411c..: 0x576793c en 1 y el C3 F1 02 con `kind`: un rand() del relleno (0x62449a) y el serial (0x5767913) por la
// tabla de protección (0x6244d0 y 0x6244f2), un byte que se vuelve a cifrar con otro.
Core::Expected<void, std::string> RequestTheLogout(const GameUiContext& context, std::uint8_t kind,
                                                   std::string_view where)
{
    auto requests = RequestsOf(context, where);
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    context.world.Runtime().ui.logoutRequested = true;
    static_cast<void>(context.random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Render::Models::LogicalAccess serial(serialDepth, context.random, Render::Models::SerialCloseRandoms);
    }
    requests.Value()->RequestLogout(kind);
    return {};
}

Core::Expected<void, std::string> PressTheEscapeMenuButton(const GameUiContext& context, std::int32_t button)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DialogState& dialogs = runtime.dialogs;
    switch (button)
    {
    case ExitGameButton:
    {
        // 0x624036: el log (0x41fa40 y 0x41fc10(1)); en la escena 5, el F3 30, Macro.txt (0x6d6220) y la caja del
        // caos.
        if (auto requests = RequestsOf(context, "0x624036"); requests.HasValue())
            requests.Value()->Log("> Menu - Exit game.");
        if (auto saved = SaveTheOptions(context, "0x62405d"); !saved.HasValue())
            return saved;
        if (ChaosBoxOpen(context))
            return SayTheChaosBoxIsOpen(context, "0x624093");
        return RequestTheLogout(context, Protocol::Connection::LogoutQuitsTheGame, "0x62411c");
    }
    case AnotherServerButton:
    {
        // 0x6247ee: el log; la caja del caos o el F1 02 con 2, y después, en la escena 5, el F3 30 y Macro.txt
        // (0x6d6250, 0x624ec0).
        if (auto requests = RequestsOf(context, "0x6247ee"); requests.HasValue())
            requests.Value()->Log("> Menu - Join another server.");
        if (ChaosBoxOpen(context))
        {
            if (auto said = SayTheChaosBoxIsOpen(context, "0x624844"); !said.HasValue())
                return said;
        }
        else if (auto logout = RequestTheLogout(context, Protocol::Connection::LogoutReturnsToServerList, "0x624851");
                 !logout.HasValue())
            return logout;
        return SaveTheOptions(context, "0x624ed5");
    }
    case AnotherCharacterButton:
    {
        // 0x624eec: el log; en la escena 5, el F3 30 y Macro.txt (0x6d6288); la caja del caos o el F1 02 con 1.
        if (auto requests = RequestsOf(context, "0x624eec"); requests.HasValue())
            requests.Value()->Log("> Menu - Join with another character.");
        if (auto saved = SaveTheOptions(context, "0x624f13"); !saved.HasValue())
            return saved;
        if (ChaosBoxOpen(context))
            return SayTheChaosBoxIsOpen(context, "0x624f45");
        return RequestTheLogout(context, Protocol::Connection::LogoutReturnsToSelection, "0x624f4a");
    }
    case OptionsButton:
        // 0x625538: con un diálogo abierto (el menú), las opciones esperan y 0x6240a1 las abre; sin ninguno, se abren
        // sin pasar por el cierre.
        if (dialogs.current != 0)
            dialogs.pending = UI::OptionsDialog;
        else
            dialogs.current = UI::OptionsDialog;
        return {};
    default:
        return {};
    }
}

// 0x623f90..0x62410b: el menú de Esc (el diálogo 0x6e) en la escena 5, con cinco botones.
Core::Expected<void, std::string> StepTheEscapeMenu(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DialogState& dialogs = runtime.dialogs;
    if (runtime.cursorItemCount > 0)
        return {};
    for (std::int32_t button = 0; button < EscapeMenuButtons; ++button)
    {
        // 0x623fdf..0x623fe6: (6 + 3i) · 10.
        const std::int32_t top = (6 + 3 * button) * 10;
        if (!InBox(context, EscapeMenuLeft, top, EscapeMenuRight - EscapeMenuLeft, EscapeMenuButtonHeight) ||
            !runtime.movement.pendingClick)
            continue;
        runtime.movement.pendingClick = false;
        // 0x62402f: la tabla 0x62dac4 para los cuatro primeros; los tres que piden algo vuelven a 0x62409b.
        const bool openedTheOptions = button == OptionsButton && dialogs.current == 0;
        if (auto pressed = PressTheEscapeMenuButton(context, button); !pressed.HasValue())
            return pressed;
        // 0x6240a1: con otro que las opciones, el que espera pasa a abierto. 0x625554 salta a 0x6240bf.
        if (!openedTheOptions && dialogs.current != UI::OptionsDialog)
        {
            dialogs.current = dialogs.pending;
            dialogs.pending = 0;
        }
        PressTheButton(runtime);
    }
    return {};
}

// 0x6257ca..0x6257fa: 0x41eed0, el volumen de DirectSound (el gancho de la CL10).
std::int32_t DirectSoundVolume(std::int32_t volume)
{
    if (volume <= 0)
        return SilentVolume;
    const double ratio = VolumeNumerator / static_cast<double>(std::min(volume, MaxVolume));
    return static_cast<std::int32_t>(VolumeScale * std::log10(ratio));
}

// 0x62568a..0x625804: la fila del volumen.
void StepTheVolume(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    bool& click = runtime.movement.pendingClick;
    if (!InBox(context, OptionsLeft, VolumeRowY, OptionsWidth, OptionsRowHeight))
        return;
    std::int32_t volume = ui.volume;
    const bool wasNotDragging = ui.volumeDragging == 0;
    const auto onTheBar = [&](bool lookAtY)
    {
        return context.mouseX >= ui.volumeBarX && context.mouseX < ui.volumeBarX + VolumeBarWidth &&
               (!lookAtY || (context.mouseY >= ui.volumeBarY && context.mouseY < ui.volumeBarY + VolumeBarHeight));
    };
    // 0x625737..0x625759: el clic vuelve a 1 y la barra queda agarrada.
    const auto grab = [&]
    {
        click = true;
        ui.volumeDragging = 1;
        volume = (context.mouseX - ui.volumeBarX) / VolumeStepPixels;
    };
    if (click)
    {
        click = false;
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = DialogMouseUpdateTimeMax;
        if (wasNotDragging)
            runtime.lastSoundId = DialogSound;
        if (onTheBar(true))
            grab();
        else if (ui.volumeDragging != 0)
        {
            ui.volumeDragging = 0;
            if (onTheBar(false))
                grab();
        }
    }
    else
        ui.volumeDragging = 0;
    // 0x625782..0x6257ab: la rueda, que se gasta.
    if (ui.wheel > 0)
    {
        ui.wheel = 0;
        volume = std::min(volume + 1, MaxVolume);
    }
    else if (ui.wheel < 0)
    {
        ui.wheel = 0;
        volume = std::max(volume - 1, 0);
    }
    if (volume == ui.volume)
        return;
    ui.volume = volume;
    ui.volumeRequest = DirectSoundVolume(volume);
}

// 0x625563..0x62590f: las opciones (el diálogo 0x93).
Core::Expected<void, std::string> StepTheOptions(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    UI::DialogState& dialogs = runtime.dialogs;
    bool& click = runtime.movement.pendingClick;
    if (runtime.cursorItemCount > 0)
        return {};
    const auto pressed = [&](std::int32_t y)
    {
        if (!InBox(context, OptionsLeft, y, OptionsWidth, OptionsRowHeight) || !click)
            return false;
        click = false;
        PressTheButton(runtime);
        return true;
    };
    // 0x6255ca..0x6255de: volver deja el menú esperando y las opciones abiertas (Q-C550).
    if (pressed(BackRowY))
        (dialogs.current != 0 ? dialogs.pending : dialogs.current) = UI::EscapeMenuDialog;
    if (pressed(AutoAttackRowY))
        ui.autoAttack = !ui.autoAttack;
    if (pressed(NoticeSoundRowY))
        runtime.chat.noticeSound = !runtime.chat.noticeSound;
    StepTheVolume(context);
    if (pressed(SlidesRowY))
        runtime.chat.slidesOn = !runtime.chat.slidesOn;
    // 0x625856..0x62589b: la primera casilla del brillo con el mouse y el clic, sin gastarlo ni sonar.
    std::uint8_t box = 0;
    for (std::int32_t x = GlowBoxFirstX; x < GlowBoxEndX; x += GlowBoxStep, ++box)
    {
        if (context.mouseX >= x && context.mouseX <= x + GlowBoxWidth && context.mouseY >= GlowBoxTop &&
            context.mouseY <= GlowBoxBottom && click)
        {
            ui.glowDetail = box;
            break;
        }
    }
    // 0x6258a1..0x6258fa: cerrar.
    if (InBox(context, OptionsLeft, CloseRowY, OptionsWidth, OptionsRowHeight) && click)
    {
        click = false;
        CloseTheDialog(runtime);
    }
    return {};
}

// 0x62d91d..0x62da57: las respuestas del 0x8d. Con el mouse sobre una (a no más de 0x6a de x = 0x13f) y el clic, el
// clic se apaga, los botones dejan lo suyo y el diálogo que espera pasa a abierto; con el +0x130 de la respuesta en
// Dialog.bmd de 0 o más, 0x632f70 lo muestra.
Core::Expected<void, std::string> StepTheAnswers(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::DialogBoxState& box = runtime.dialogBox;
    const std::int32_t answersTop =
        UI::DialogTextTop(box, UI::AnswersDialog) + box.lineCount * UI::DialogLineStep; // 0x62d943..0x62d94a
    // 0x62d948: la y del mouse negativa sale.
    if (context.mouseY < 0)
        return {};
    const std::int32_t offset = context.mouseY - answersTop;
    if (offset >= box.answerCount * UI::DialogLineStep)
        return {};
    if (std::abs(UI::AnswersCenterX - context.mouseX) > UI::AnswersHalfWidth)
        return {};
    bool& click = runtime.movement.pendingClick;
    if (!click)
        return {};
    // 0x62d98c..0x62d9a9: la división con signo, hacia el cero; un desvío negativo sale.
    const std::int32_t answer = offset / UI::DialogLineStep;
    if (offset < 0 || answer < 0)
        return {};
    // 0x62d9af..0x62d9ca: la cuenta del registro de 0x6d5e28 en Dialog.bmd (+0x12c), con 1 como mínimo.
    const Data::Text::DialogTable& table = runtime.dialogTable;
    if (box.dialogIndex < 0 || static_cast<std::size_t>(box.dialogIndex) >= table.size())
        return Core::Unexpected(std::format("0x62d9b8: el diálogo {} no está en Dialog.bmd", box.dialogIndex));
    const Data::Text::DialogRecord& dialog = table[static_cast<std::size_t>(box.dialogIndex)];
    if (answer >= std::max(dialog.answerCount, 1))
        return {};
    if (static_cast<std::size_t>(answer) >= dialog.next.size())
        return Core::Unexpected(std::format("0x62d9fe: la respuesta {} pasa las 10 de Dialog.bmd", answer));
    click = false;
    PressTheButton(runtime);
    const std::int32_t next = dialog.next[static_cast<std::size_t>(answer)];
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    if (next < 0)
        return {};
    return UI::OpenTheAnswersDialog(runtime.dialogBox, runtime.dialogs, &table, context.texts, next);
}

// 0x62d756..0x62da57: las cajas con botones; Enter o un botón las cierran, salvo el 0x8d, que sigue en sus respuestas.
Core::Expected<void, std::string> StepTheButtons(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DialogBoxState& box = runtime.dialogBox;
    const UI::Point mouse{context.mouseX, context.mouseY};
    const std::int32_t selected =
        UI::StepTheDialogButtons(box, mouse, runtime.movement.pendingClick, runtime.movement.leftHeld);
    // 0x62d7eb: las flechas leen el clic de después de los botones.
    if (runtime.dialogs.current == UI::PagedDialog)
    {
        if (auto paged = UI::StepTheDialogPages(box, context.texts, mouse, runtime.movement.pendingClick);
            !paged.HasValue())
            return paged;
    }
    // 0x62d911: el 0x8d no llega al cierre de 0x62da58 (ni Enter ni sus botones lo cierran).
    if (runtime.dialogs.current == UI::AnswersDialog)
        return StepTheAnswers(context);
    // 0x62da58..0x62da94.
    if (runtime.ui.enterPressed)
        runtime.ui.enterPressed = false;
    else if (selected == -1)
        return {};
    CloseTheDialog(runtime);
    return {};
}

// 0x625910..0x6259c5: el modo de la caja del caos (0x8e). Con el clic, que no apaga, el botón bajo el mouse deja su
// número en 0x7d3a114 (el modo que leen 0x5fd4b0 y el dibujo de la caja) y cierra, sin sonido ni MouseUpdateTime. El
// dibujo pone los botones 10 píxeles más abajo (Q-C945).
Core::Expected<void, std::string> StepTheChaosMixMode(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.movement.pendingClick)
        return {};
    for (std::int32_t mode = 0; mode < MixModes; ++mode)
    {
        const std::int32_t top = MixModeFirstTop + mode * MixModeStep;
        if (context.mouseY < top || context.mouseY > top + MixModeHeight || context.mouseX < MixModeLeft ||
            context.mouseX > MixModeRight)
            continue;
        runtime.npcWindows.chaosMixMode = mode;
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
        return {};
    }
    return {};
}

// 0x6263df..0x62645a y 0x62825c..0x6282c8: el OK genérico.
Core::Expected<void, std::string> StepTheGenericOk(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t id = runtime.dialogs.current;
    const bool enter = runtime.ui.enterPressed;
    runtime.ui.enterPressed = false;
    bool& click = runtime.movement.pendingClick;
    if (id != EnterOnlyDialog && InBox(context, OkLeft, OkTop, OkRight - OkLeft, OkBottom - OkTop) && click)
        click = false;
    else if (!enter)
        return {};
    if (std::ranges::find(QuittingDialogs, id) != QuittingDialogs.end())
    {
        // 0x628299: SendMessageA(hWnd, WM_DESTROY, 0, 0), y sigue con el cierre.
        runtime.ui.destroyRequested = true;
        CloseTheDialog(runtime);
        return {};
    }
    // 0x626460..0x628215: el zen del almacén (Scenes::ConfirmTheMoneyDialog), con su cola.
    if (id == EnterOnlyDialog)
        return ConfirmTheMoneyDialog(context);
    if (id == ReopeningDialog)
    {
        // 0x628264..0x62827b: cierra y abre el teclado del almacén en 6.
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
        OpenTheKeypad(runtime, context.random, ReopenedKeypadMode);
        PressTheButton(runtime);
        return {};
    }
    CloseTheDialog(runtime);
    return {};
}

// [x + 0xd5, x + w + 0xd5] × [y + arriba, y + h + arriba], con los bordes.
bool OverTheYesNoButton(const GameUiContext& context, const UI::DialogButton& button, std::int32_t top)
{
    return context.mouseX >= button.x + YesNoButtonsLeft && context.mouseY >= button.y + top &&
           context.mouseX <= button.x + button.width + YesNoButtonsLeft &&
           context.mouseY <= button.y + button.height + top;
}

// 0x6282dc..0x62843f: los dos botones de sí y no. Cada uno arranca en su estado (1 y 3) y, con el mouse encima, suma 1
// y, con el clic, queda elegido. En el 0xa8, el tercero (0x7d36908, sin estado) con el mouse y el clic deja el primero
// en 2 (0x6283a1..0x628409). Después el clic y el botón abajo en 0 y, con Esc, el segundo en 1 (en 2 en el 0xa8,
// 0x62841e). Devuelve lo elegido en cada uno: 0 el sí, 1 el no, 2 el tercero del 0xa8 y −1 nada.
std::array<std::int32_t, 2> StepTheYesNoButtons(const GameUiContext& context, bool escape)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DialogBoxState& box = runtime.dialogBox;
    const std::int32_t id = runtime.dialogs.current;
    const std::int32_t top = id == UI::ServerTimeDialog ? ServerTimeButtonsTop : YesNoButtonsTop;
    std::array<std::int32_t, 2> chosen{-1, -1};
    for (std::size_t index = 0; index < chosen.size(); ++index)
    {
        UI::DialogButton& button = box.buttons[index];
        button.state = static_cast<std::int32_t>(1 + 2 * index);
        if (!OverTheYesNoButton(context, button, top))
            continue;
        if (runtime.movement.pendingClick)
            chosen[index] = static_cast<std::int32_t>(index);
        ++button.state;
    }
    if (id == UI::ItemNameWithLuckDialog && OverTheYesNoButton(context, box.thirdButton, top) &&
        runtime.movement.pendingClick)
        chosen[0] = ThirdChoice;
    runtime.movement.pendingClick = false;
    runtime.movement.leftHeld = false;
    if (escape)
        chosen[1] = id == UI::ItemNameWithLuckDialog ? ThirdChoice : 1;
    return chosen;
}

// 0x5af520: ni el almacén, ni 0x80, ni el comercio abiertos, cortando en el primero abierto. Solo el comercio, un byte
// protegido (UI::PanelRegistry::IsOpen), saca su rand().
bool TheItemCanBeUsed(const GameUiContext& context)
{
    GamePanels panels(context.world, context.random, GamePanelTables{.texts = context.texts});
    UI::PanelRegistry& registry = panels.Registry();
    return !registry.IsOpen(UI::WarehousePanel) && !registry.IsOpen(UI::ClosedPanel) &&
           !registry.IsOpen(UI::TradePanel);
}

// El sí del 0xa9 (0x629c57..0x629c85) y los dos botones del 0xa8 (0x628bdc..0x628c11 y 0x6293b5..0x6293ea): con
// 0x5af520 en falso, el aviso 474 de error; con la espera de 0x5767954, nada; si no, el C1 06 26 del hito 5, que
// corta. Después cierra, con el sonido.
Core::Expected<void, std::string> UseTheDialogItem(const GameUiContext& context, std::string_view cut)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (!TheItemCanBeUsed(context))
    {
        if (context.inventory == nullptr || !context.inventory->systemMessage)
            return Core::Unexpected(std::string("0x555190: el aviso 474 sin los mensajes"));
        if (auto shown = context.inventory->systemMessage(BlessRefusedText, ErrorMessageType); !shown.HasValue())
            return shown;
    }
    else if (runtime.ui.field5767954 <= 0)
        return Core::Unexpected(std::string(cut));
    CloseTheDialog(runtime);
    return {};
}

Core::Expected<void, std::string> SendTheAnswer(const GameUiContext& context, Protocol::Outgoing packet,
                                                std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// 0x628521..0x628ac7: el 0x9a. El no cierra; el sí, con el ítem de los diálogos, manda el AF 01 y cierra.
Core::Expected<void, std::string> AnswerTheChaosCastle(const GameUiContext& context, std::int32_t entry)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (entry == 1)
    {
        CloseTheDialog(runtime);
        return {};
    }
    const std::uint32_t item = runtime.dialogItem.scalar1Low;
    if (item == NoDialogItem)
        return {};
    const auto level = static_cast<std::uint8_t>((item >> ItemLevelShift) & ItemLevelMask);
    const auto slot = static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff);
    if (auto sent = SendTheAnswer(context, Protocol::DialogAnswers::WriteChaosCastleEntry(level, slot),
                                  "0x6289b9: el C1 06 AF 01");
        !sent.HasValue())
        return sent;
    CloseTheDialog(runtime);
    return {};
}

// 0x628b8f..0x629bfd: el 0xa8. El 2 (el tercero o Esc) cierra sin el sonido; el 0 y el 1 quedan en 0x78abb26 y usan
// el ítem.
Core::Expected<void, std::string> AnswerTheLuck(const GameUiContext& context, std::int32_t entry)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (entry == ThirdChoice)
    {
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = DialogMouseUpdateTimeMax;
        return {};
    }
    runtime.ui.field78abb26 = static_cast<std::uint8_t>(entry);
    return UseTheDialogItem(context, std::format("{:#x}: el C1 06 26 del 0xa8 (0x5767954 en 10, 0x78a59d1 + 12, 0 y "
                                                 "0x78abb26 en C3, con un rand(), y el sonido del ítem, 0x629365) lo "
                                                 "escribe Protocol::...::WriteUseItem del hito 5 (08 P-C193)",
                                                 entry == 0 ? 0x6293ea : 0x628c11));
}

// 0x62a42b..0x62a771: el 0xaa. El no cierra; el sí manda el D0 00 y cierra.
Core::Expected<void, std::string> AnswerTheNotice(const GameUiContext& context, std::int32_t entry)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (entry == 0)
    {
        if (auto sent = SendTheAnswer(context, Protocol::DialogAnswers::WriteNoticeYes(), "0x62a65a: el C1 04 D0 00");
            !sent.HasValue())
            return sent;
    }
    CloseTheDialog(runtime);
    return {};
}

// 0x6282dc..0x62a7f6: los de sí y no, cada elegido en el orden de los botones.
Core::Expected<void, std::string> StepTheYesNo(const GameUiContext& context, bool escape)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t id = runtime.dialogs.current;
    const std::array<std::int32_t, 2> chosen = StepTheYesNoButtons(context, escape);
    for (const std::int32_t entry : chosen)
    {
        if (id == UI::ItemNameWithLuckDialog)
        {
            if (entry < 0 || entry > ThirdChoice)
                continue;
            if (auto answered = AnswerTheLuck(context, entry); !answered.HasValue())
                return answered;
            continue;
        }
        if (entry != 0 && entry != 1)
            continue;
        const bool yes = entry == 0;
        if (id == UI::ChaosCastleDialog)
        {
            if (auto answered = AnswerTheChaosCastle(context, entry); !answered.HasValue())
                return answered;
        }
        else if (id == UI::NoticeQuestionDialog)
        {
            if (auto answered = AnswerTheNotice(context, entry); !answered.HasValue())
                return answered;
        }
        else if (id == AnswerDialog || id == ConfirmDialog || id == UI::MoneyTextDialog)
        {
            // 0x62a779..0x62a7d7.
            runtime.npcWindows.dialogAnswer = yes ? YesAnswer : NoAnswer;
            CloseTheDialog(runtime);
        }
        else if (id == UI::ItemNameDialog)
        {
            // 0x628ae4..0x628b63: el sí cierra y abre el 0xa8 con el ítem de los diálogos.
            if (!yes)
            {
                CloseTheDialog(runtime);
                continue;
            }
            runtime.dialogs.current = runtime.dialogs.pending;
            runtime.dialogs.pending = 0;
            if (auto opened = UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, LuckDialogLines,
                                                 LuckDialogText, UI::ItemNameWithLuckDialog, &runtime.dialogItem);
                !opened.HasValue())
                return opened;
            PressTheButton(runtime);
        }
        else if (id == UI::ServerTimeDialog)
        {
            // 0x62846f..0x628500: la pregunta de la mezcla de joyas.
            if (auto answered = AnswerTheJewelMix(context, yes); !answered.HasValue())
                return answered;
        }
        else if (yes)
        {
            // 0x629c57..0x629c85.
            if (auto used = UseTheDialogItem(context, "0x629c85: el C1 06 26 de la joya de bendición (0x5767954 en "
                                                      "10, [0x7d3a0dc] + 12, [0x7d3a0e0] y 0x78abb26, en C3) lo "
                                                      "escribe Protocol::...::WriteUseItem del hito 5 (08 P-C193)");
                !used.HasValue())
                return used;
        }
        else
        {
            // 0x629c41..0x629c4d: el no suelta la joya y la vuelve a su lugar.
            runtime.dialogBox.jewelCell = 0;
            runtime.dialogBox.jewelTarget = 0;
            const InventoryServices none{};
            const InventoryServices& services = context.inventory != nullptr ? *context.inventory : none;
            if (auto returned = ReturnTheCursorItem(
                    InventoryMoveContext{.world = context.world, .random = context.random, .services = services});
                !returned.HasValue())
                return returned;
            CloseTheDialog(runtime);
        }
    }
    return {};
}

// 0x6260f6..0x6262e8: el 0x91, el título o el precio de la tienda personal (0x78aba60, hito 7). El OK (con el clic en
// [0xff, 0x133] × [0xa0, 0xb0]) lee el EDIT del diálogo y corta; el cancelar ([0x14a, 0x17e] × [0xa0, 0xb0]) contesta
// 2, y Enter (0x5763588), como el OK sin leer el EDIT, contesta 1 con un título o un precio y 2 sin ellos. Después
// vacía el campo, cierra el chat, devuelve el foco a la ventana del juego y cierra el diálogo, sin MouseUpdateTime ni
// sonido.
Core::Expected<void, std::string> StepTheTypedText(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    bool& click = runtime.movement.pendingClick;
    const auto within = [&context](std::int32_t left, std::int32_t right)
    {
        return context.mouseX >= left && context.mouseY >= TypedButtonsTop && context.mouseX <= right &&
               context.mouseY <= TypedButtonsBottom;
    };
    std::int32_t chosen = -1;
    if (click)
    {
        if (within(TypedOkLeft, TypedOkRight))
            return Core::Unexpected(std::string("0x626172: el OK del 0x91 lee el EDIT del diálogo ([0x5763504], la "
                                                "vtable + 0x50, con 0x6c7b8c en 1) y valida el título con 0x5ebbd0: "
                                                "son de la CL9.4"));
        if (within(TypedCancelLeft, TypedCancelRight))
        {
            click = false;
            chosen = 0;
        }
    }
    if (runtime.ui.enterPressed)
    {
        runtime.ui.enterPressed = false;
        chosen = 1;
    }
    else if (chosen < 0)
        return {};
    npc.dialogAnswer = NoAnswer;
    if (chosen == 1)
    {
        if (npc.field78aba60 == TypedTitle && !npc.field78aba64.empty())
            npc.dialogAnswer = YesAnswer;
        else if (npc.field78aba60 == TypedPrice && npc.typedMoney > 0)
            npc.dialogAnswer = YesAnswer;
    }
    // 0x62629a..0x6262d0: 0x553570(0), 0x6c8e68 en 0 y, con 0x6c7b8c en 1, SetFocus([0x5763544]).
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.ui.chatOpen = false;
    runtime.chat.input.focus = UI::ChatFocus::Game;
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    return {};
}
} // namespace

std::span<const DialogRoute> DialogRoutes()
{
    return Routes;
}

DialogRoute DialogRouteOf(std::int32_t id)
{
    const auto found = std::ranges::find(Routes, id, &DialogRoute::id);
    if (found != Routes.end())
        return *found;
    return DialogRoute{.id = id, .step = DialogStep::GenericOk, .address = "0x6263df", .wave = ""};
}

Core::Expected<void, std::string> StepTheOpenDialog(const GameUiContext& context, bool escape)
{
    const std::int32_t id = context.world.Runtime().dialogs.current;
    if (id == 0)
        return {};
    const DialogRoute route = DialogRouteOf(id);
    switch (route.step)
    {
    case DialogStep::EscapeMenu:
        return StepTheEscapeMenu(context);
    case DialogStep::Options:
        return StepTheOptions(context);
    case DialogStep::Buttons:
        return StepTheButtons(context);
    case DialogStep::GenericOk:
        return StepTheGenericOk(context);
    case DialogStep::YesNo:
        return StepTheYesNo(context, escape);
    case DialogStep::ChaosMixMode:
        return StepTheChaosMixMode(context);
    case DialogStep::JewelMenu:
        return StepTheJewelMenu(context);
    case DialogStep::JewelChoice:
        return StepTheJewelChoice(context);
    case DialogStep::JewelResult:
        return StepTheJewelResult(context);
    case DialogStep::TypedText:
        return StepTheTypedText(context);
    case DialogStep::Cut:
        break;
    }
    return Core::Unexpected(std::format("{}: el diálogo {:#x} es de la {}", route.address, id, route.wave));
}

Core::Expected<Protocol::OptionData::OptionBody, std::string>
OptionBodyOf(WorldState& world, Render::Models::CrtRandom& random, std::optional<std::uint8_t> chosenSlot)
{
    WorldRuntime& runtime = world.Runtime();
    if (!chosenSlot || *chosenSlot >= OwnSkillKeyPlaces)
        return Core::Unexpected(std::string("0x61c2c6: sin elegido (0x6d5edc), el F3 30 lee fuera de las teclas de "
                                            "las skills (+0x16c del registro propio)"));
    Protocol::OptionData::OptionBody body{};
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        const OwnCharacterRecord& record = world.OwnCharacter();
        const auto& keys = record.skillKeys[*chosenSlot];
        for (std::size_t at = 0; at < OptionKeyPlaces; ++at)
        {
            const std::uint8_t key = keys[at];
            if (key == NoKey)
                continue;
            if (key >= body.size())
                return Core::Unexpected(std::format("0x61c2e8: la tecla {:#x} del lugar {:#x} escribe fuera de los "
                                                    "24 bytes de la pila de 0x61c210",
                                                    key, at));
            body[key] = OwnSkillByte(record, at);
        }
    }
    const UI::GameUiState& ui = runtime.ui;
    std::uint8_t flags = body[FlagsByte];
    flags = static_cast<std::uint8_t>(flags | (ui.autoAttack ? AutoAttackOn : AutoAttackOff));
    flags = static_cast<std::uint8_t>(flags | (runtime.chat.noticeSound ? NoticeSoundOn : NoticeSoundOff));
    if (!runtime.chat.slidesOn)
        flags = static_cast<std::uint8_t>(flags | SlidesOff);
    body[FlagsByte] = flags;
    for (std::size_t slot = 0; slot < runtime.hud.quickSlots.types.size(); ++slot)
        body[FirstQuickSlotByte + slot] = static_cast<std::uint8_t>(runtime.hud.quickSlots.types[slot] & 0xff);
    // 0x61c3ad: la vtable + 0x38 del registro (0x472010, +0x104), dividido con signo.
    std::uint8_t rows = static_cast<std::uint8_t>((runtime.chat.log.rows / RowsPerStep) & 0xff);
    if (ui.chatModes.placement == 0)
        rows = 0;
    // 0x61c3d3..0x61c3df: FLD del float, FMUL por 10,0f y _ftol: el producto es exacto y se trunca.
    const auto alpha = static_cast<std::uint8_t>(
        static_cast<std::int32_t>(static_cast<double>(runtime.chat.log.alpha) * AlphaSteps) & 0xff);
    body[ChatWindowByte] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(rows << NibbleShift) | alpha);
    return body;
}
} // namespace Client::Scenes
