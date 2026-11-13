#include "Scenes/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/OptionData.h"
#include "Protocol/Party.h"
#include "Protocol/Trade.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SocialState.h"
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
// 0x62a7f7..0x62c560: sí y no. Los dos botones son de 0x46 × 0x15 en y 0x62; con 0x7d y 0x95 (el sí) o 0x71, 0x7d y
// 0x95 (el no), de 0x28 en otra x.
constexpr std::int32_t PartyRequestDialog = 0x77;
constexpr std::int32_t YesLeft = 0xea;
constexpr std::int32_t NoLeft = 0x14e;
constexpr std::int32_t NarrowYesLeft = 0x143;
constexpr std::int32_t NarrowNoLeft = 0x175;
constexpr std::int32_t YesNoWidth = 0x46;
constexpr std::int32_t NarrowWidth = 0x28;
constexpr std::int32_t YesNoTop = 0x62;
constexpr std::int32_t YesNoBottom = 0x77;
// 0x62c54c..0x62c560: los casos de la tabla del no (0x62db1c) que no son 0x77 ni 0x78; los demás ids van al cierre
// (0x62d745).
constexpr std::array<std::int32_t, 5> OtherNoCases{0x71, 0x76, 0x7d, 0x7f, 0x95};
// El relleno y el serial del C1 05 41 en C3.
constexpr std::uint32_t C3Randoms = 2;

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
    {0x8e, DialogStep::Cut, "0x625910", "CL9.3"},
    {0xa1, DialogStep::Cut, "0x6259c6", "CL9.3"},
    {0xa2, DialogStep::Cut, "0x625b23", "CL9.3"},
    {0xa4, DialogStep::Cut, "0x625cf4", "CL9.3"},
    {0xa6, DialogStep::Cut, "0x625f98", "CL9.3"},
    {0xa7, DialogStep::Cut, "0x62601e", "CL9.4"},
    {0x9c, DialogStep::Cut, "0x6260d1", "CL9.4"},
    {0x91, DialogStep::Cut, "0x6260f6", "CL9.3"},
    {UI::MessageBoxDialog, DialogStep::Buttons, "0x62d756", ""},
    {0x8d, DialogStep::Cut, "0x62d91d", "CL9.1"},
    {UI::PagedDialog, DialogStep::Buttons, "0x62d756", ""},
    {0x8b, DialogStep::Buttons, "0x62d756", ""},
    {0x97, DialogStep::Buttons, "0x62d756", ""},
    {0x9b, DialogStep::Buttons, "0x62d756", ""},
    {0x71, DialogStep::Cut, "0x62a810", "CL9.4"},
    {0x7d, DialogStep::Cut, "0x62a810", "CL9.4"},
    {0x95, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {0x76, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {UI::IgnoredOverChaosCastle, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {PartyRequestDialog, DialogStep::YesNo, "0x62a7f7", ""},
    {UI::ForcedDialog, DialogStep::YesNo, "0x62a7f7", ""},
    {0x94, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {UI::ItemNameDialog, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {0x98, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {UI::MoneyTextDialog, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {UI::ChaosCastleDialog, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {UI::ServerTimeDialog, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {UI::ItemNameWithLuckDialog, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {0xa9, DialogStep::Cut, "0x6282dc", "CL9.3"},
    {0xaa, DialogStep::Cut, "0x6282dc", "CL9.3"},
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

// 0x62d756..0x62da57: las cajas con botones; Enter o un botón las cierran.
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
    // 0x62da58..0x62da94.
    if (runtime.ui.enterPressed)
        runtime.ui.enterPressed = false;
    else if (selected == -1)
        return {};
    CloseTheDialog(runtime);
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
    if (id == EnterOnlyDialog)
        return Core::Unexpected(std::string("0x626460: el OK del diálogo 0x73 es de la CL9 hito 7, fase 2"));
    if (id == ReopeningDialog)
        return Core::Unexpected(std::string("0x628276: el OK del diálogo 0x8f (0x5e7aa0(6)) es de la CL9.3"));
    CloseTheDialog(runtime);
    return {};
}
// La respuesta del sí (1) o del no (0) a la invitación (0x77, el C1 05 41 con la clave de 0x78aba4c en C3) o al
// pedido de comercio (0x78, el C1 04 37 con el XOR32, en claro).
Core::Expected<void, std::string> Answer(const GameUiContext& context, std::int32_t id, std::uint8_t accepted,
                                         std::string_view where)
{
    auto requests = RequestsOf(context, where);
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    WorldRuntime& runtime = context.world.Runtime();
    if (id == PartyRequestDialog)
    {
        context.random.Discard(C3Randoms);
        const auto key = static_cast<std::uint16_t>(runtime.social.party.requestKey & 0xffff);
        requests.Value()->SendGamePacket(Protocol::Party::WritePartyRequestResult(accepted, key));
        return {};
    }
    requests.Value()->SendGamePacket(Protocol::Trade::WriteTradeResponse(accepted));
    return {};
}

// 0x62a7f7..0x62d745: el sí, que gasta el clic y cierra (0x62c4ac), y después el no con el diálogo que quedó, que
// también entra con Esc. Con otro caso de la tabla del no, su tramo corta; sin caso, solo el cierre.
Core::Expected<void, std::string> StepTheYesNo(const GameUiContext& context, bool escape)
{
    WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.movement.pendingClick;
    std::int32_t id = runtime.dialogs.current;
    const auto inButton = [&context](std::int32_t left, std::int32_t width)
    { return InBox(context, left, YesNoTop, width, YesNoBottom - YesNoTop); };
    const auto narrowYes = id == 0x7d || id == 0x95;
    if (inButton(narrowYes ? NarrowYesLeft : YesLeft, narrowYes ? NarrowWidth : YesNoWidth) && click)
    {
        click = false;
        if (auto said = Answer(context, id, 1, id == PartyRequestDialog ? "0x62bb1d" : "0x62c20a"); !said.HasValue())
            return said;
        CloseTheDialog(runtime);
        id = runtime.dialogs.current;
    }
    const bool narrowNo = id == 0x71 || id == 0x7d || id == 0x95;
    if (inButton(narrowNo ? NarrowNoLeft : NoLeft, narrowNo ? NarrowWidth : YesNoWidth) && click)
        click = false;
    else if (!escape)
        return {};
    if (std::ranges::find(OtherNoCases, id) != OtherNoCases.end())
        return Core::Unexpected(std::format("0x62c560: el no del diálogo {:#x} es de la CL9", id));
    if (id == PartyRequestDialog || id == UI::ForcedDialog)
    {
        if (auto said = Answer(context, id, 0, id == PartyRequestDialog ? "0x62cd52" : "0x62d4a2"); !said.HasValue())
            return said;
    }
    CloseTheDialog(runtime);
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
