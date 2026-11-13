#include "Scenes/GameDialogDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameDialogSteps.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/HudPrimitives.h"
#include "UI/SprintfFormat.h"
#include "UI/Widget.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
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
using Render::BlendMode;
using Render::Color;

// Los glColor3f de 0x62db60: el blanco y el negro; el botón con el mouse encima (0,8; 0,6; 0,4); el gris del marco de
// las barras y de la flecha que no va (0,2); las casillas del brillo prendidas y las cifras de las apagadas (0,8;
// 0,6; 0,3); y la flecha con el mouse (0,7) o apretada (0,5).
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
constexpr Color HoverColor{0.8f, 0.6f, 0.4f, 1.0f};
constexpr Color FrameGrey{0.2f, 0.2f, 0.2f, 1.0f};
constexpr Color GlowColor{0.8f, 0.6f, 0.3f, 1.0f};
constexpr Color ArrowHover{0.7f, 0.7f, 0.7f, 1.0f};
constexpr Color ArrowPressed{0.5f, 0.5f, 0.5f, 1.0f};

// 0x62dba1 y 0x62db9b: los colores del texto (0x6c8e5c en −1 y 0x6c8e64 en 0).
constexpr Render::Text::TextColors DialogTextColors{.text = 0xffffffffu, .background = 0};

// Los slots de 0x61ccb0: Message_box (0xf0), ok, ok2 (0xf1 y 0xf2), Message_box1 (0xfb), Message_box2 (0xfc) y
// CursorSitDown (9), la flecha de las páginas.
constexpr std::uint16_t MessageBoxSlot = 0xf0;
constexpr std::uint16_t OkSlot = 0xf1;
constexpr std::uint16_t OkHoverSlot = 0xf2;
constexpr std::uint16_t BoxRowSlot = 0xfb;
constexpr std::uint16_t BoxEdgeSlot = 0xfc;
constexpr Render::TextureSlot ArrowSlot{9};

// La mitad de 640: los textos centrados de 0x554180 (0x140 − (cx · 640 / W) / 2).
constexpr int CenterX = 0x140;

// 0x62dbf0..0x62dcdc y 0x62df4a..0x62e04e: los botones del menú y las filas de las opciones, 0xf0 en (260, y, 120,
// alto) con las UV (0, 0, 0,83203125, 1), y el mouse encima con x en (0x103, 0x17c) e y en [y, y + alto).
constexpr float MenuButtonLeft = 260.0f;
constexpr float MenuButtonWidth = 120.0f;
constexpr float MessageBoxDu = 0.83203125f;
constexpr int MenuHoverLeft = 0x103;
constexpr int MenuHoverRight = 0x17c;
// 0x62dbd6..0x62dbe9: 5 botones en la escena 5, desde y = 60 de a 30, de 22 de alto.
constexpr int MenuButtons = 5;
constexpr int MenuFirstY = 60;
constexpr int MenuButtonStep = 30;
constexpr int MenuButtonHeight = 22;
// 0x62de0c..0x62df1a: los textos del menú (Text.bmd) y su y, en el orden en que salen.
constexpr std::array<std::size_t, 5> MenuTexts{381, 382, 383, 385, 384};
constexpr std::array<int, 5> MenuTextYs{0x41, 0x5f, 0x7d, 0x9b, 0xb9};

// 0x62df4a..0x62df8c: 7 filas en y = (3i + 3) · 10, de 22; la 5 de 44 y la 6 en y = 232.
constexpr int OptionRows = 7;
constexpr int TallOptionRow = 5;
constexpr int TallOptionRowHeight = 44;
constexpr int LastOptionRow = 6;
constexpr int LastOptionRowY = 232;
// Los textos de las opciones y su y.
constexpr std::size_t OptionsTitleText = 385;
constexpr int OptionsTitleY = 0x23;
constexpr std::size_t AutoAttackText = 386;
constexpr int AutoAttackY = 0x41;
constexpr std::size_t NoticeSoundText = 387;
constexpr int NoticeSoundY = 0x5f;
constexpr std::size_t VolumeText = 389;
constexpr int VolumeY = 0x7d;
constexpr std::size_t SlidesText = 919;
constexpr int SlidesY = 0x9b;
constexpr std::size_t GlowText = 1840;
constexpr int GlowTextY = 0xb9;
constexpr std::size_t CloseText = 388;
constexpr int CloseY = 0xed;
// 0x6d6298..0x6d62c0: lo que los formatos de las tres llaves ("%s  On" y "%s  Off") ponen después del texto.
constexpr std::string_view OnSuffix = "  On";
constexpr std::string_view OffSuffix = "  Off";
// 0x62e1f1..0x62e2a8: el texto del volumen en x = 0x140 − (cx + 0x36) / 2 y la barra en (x + 0x144 + cx, 0x7f), con
// cx en 640 × 480: el marco gris de 50 × 8, el fondo negro de 48 × 6 y lo lleno, (volumen + 1) · 4,8 (0x6b8bf0).
constexpr std::uint32_t DesignWidth = 640;
constexpr int VolumeTextSpace = 0x36;
constexpr int VolumeBarOffset = 0x144;
constexpr int VolumeBarTop = 0x7f;
constexpr float VolumeBarWidth = 50.0f;
constexpr float VolumeBarHeight = 8.0f;
constexpr float VolumeFillWidth = 48.0f;
constexpr float VolumeFillHeight = 6.0f;
constexpr float VolumeStepWidth = 4.8f;
// 0x62e33b..0x62e45a: el marco del brillo, gris en (268, 198, 102, 22) y negro en (269, 199, 100, 20); las 5
// casillas de 18 desde x = 0x10e de a 0x14 en y = 200; y las cifras 5, 7, 9, 11 y 13, de 12 × 12, desde x = 0x118
// en y = 202.
constexpr int GlowBoxes = 5;
constexpr int GlowBoxFirstX = 0x10e;
constexpr int GlowStep = 0x14;
constexpr float GlowBoxTop = 200.0f;
constexpr float GlowBoxSize = 18.0f;
constexpr int GlowDigitFirstX = 0x118;
constexpr float GlowDigitTop = 202.0f;
constexpr float GlowDigitSize = 12.0f;
constexpr int GlowFirstDigit = 5;
constexpr int GlowDigitStep = 2;

// 0x6310da..0x631202: el marco de las cajas, 0xfc de 213 × 5 en (213, 60), filas de 0xfb de 213 × 40 desde y = 65
// (por la tabla 0x632b00: 4 en el 0x8a, el 0x8c y el 0x9b; las del texto y las respuestas en el 0x8d; 7 en los demás)
// y otra vez 0xfc, con las UV (0, 0, 0,83203125, 0,625).
constexpr float BoxLeft = 213.0f;
constexpr float BoxTop = 60.0f;
constexpr float BoxWidth = 213.0f;
constexpr float BoxEdgeHeight = 5.0f;
constexpr float BoxRowHeight = 40.0f;
constexpr int BoxFirstRowY = 65;
constexpr int BoxRowStep = 40;
constexpr float BoxDv = 0.625f;
constexpr int ShortBoxRows = 4;
constexpr int TallBoxRows = 7;
// 0x631150..0x631182: las del 0x8d, ((renglones + respuestas) · 0x12 + 0x31) / 40, de 4 a 9.
constexpr int AnswersBoxRowsMaximum = 9;
constexpr int AnswersBoxRowsRound = 0x31;
// 0x631205..0x63127c: cada botón con estado, 0xf0 + estado en (x + 213, y + 60, w, h − 1) con las UV (0, 0, w / 128,
// h / 32) (0x6ba7a8, 0x6b86cc, 0x6b87d4, 0x6b8b08 y 0x6b8880).
constexpr float ButtonDu = 0.0078125f;
constexpr float ButtonDv = 0.03125f;
// 0x631297..0x6313fa: las flechas del 0x8c, la flecha de sentarse girada 270 y 90 grados, de 25 × 16, centradas en
// (253, 210) y (383, 210), y el mouse encima con x en (0xf8, 0x109) o (0x178, 0x189) e y en (0xc9, 0xdf).
constexpr float LeftArrowX = 253.0f;
constexpr float RightArrowX = 383.0f;
constexpr float ArrowY = 210.0f;
constexpr float ArrowWidth = 25.0f;
constexpr float ArrowHeight = 16.0f;
constexpr float LeftArrowDegrees = 270.0f;
constexpr float RightArrowDegrees = 90.0f;
constexpr int LeftArrowLeft = 0xf8;
constexpr int LeftArrowRight = 0x109;
constexpr int RightArrowLeft = 0x178;
constexpr int RightArrowRight = 0x189;
constexpr int ArrowTop = 0xc9;
constexpr int ArrowBottom = 0xdf;
// 0x632548..0x632596: las respuestas del 0x8d en amarillo, y en rojo la que tiene el mouse encima.
constexpr Color AnswerColor{1.0f, 1.0f, 0.0f, 1.0f};
constexpr Color HoveredAnswerColor{1.0f, 0.0f, 0.0f, 1.0f};

// 0x630c51..0x630d4c: el marco del OK genérico, 0xf0 de 213 × 64 en (213, 60), y el OK, 0xf1 (0xf2 con el mouse en
// [0x11c, 0x161] × [0x62, 0x76]) de 70 × 21 en (284, 98) con las UV (0, 0, 0,546875, 0,65625). Sus textos van desde
// y = 60 + 0xf, o en 60 + 9 y 60 + 0x15 los de dos renglones.
constexpr float GenericBoxHeight = 64.0f;
constexpr float OkX = 284.0f;
constexpr float OkY = 98.0f;
constexpr float OkWidth = 70.0f;
constexpr float OkHeight = 21.0f;
constexpr float OkDu = 0.546875f;
constexpr float OkDv = 0.65625f;
constexpr int OkLeft = 0x11c;
constexpr int OkRight = 0x161;
constexpr int OkTop = 0x62;
constexpr int OkBottom = 0x76;
constexpr int GenericTextTop = 60;
constexpr int GenericTextY = GenericTextTop + 0xf;
constexpr int GenericFirstRowY = GenericTextTop + 9;
constexpr int GenericSecondRowY = GenericTextTop + 0x15;

// 0x630cc4..0x630da7: el marco de los de sí y no (CL9 hito 6), 0xfc de 213 × 5 en (213, 100), 3 filas de 0xfb de 213 ×
// 40 desde y = 105 y otra vez 0xfc, con las UV de las cajas. El 0xa3 lo pone en y = 210 (0x630cc9..0x630cd3) y el 0x9a
// lleva 4 filas (0x630d20..0x630d2c).
constexpr int YesNoBoxTop = 0x64;
constexpr int ServerTimeBoxTop = 0xd2;
constexpr int YesNoBoxRows = 3;
constexpr int ChaosCastleBoxRows = 4;
// 0x630db4..0x630deb: en el 0x9a, antes de cada botón, el sí en (1; 0,3; 0,3) sin el ítem (el dword 0x78a59cc, el +4 de
// 0x78a59c8, en 0xff) y si no en blanco. El color queda.
constexpr std::uint32_t NoDialogItem = 0xff;
constexpr Color NoItemYesColor{1.0f, 0.3f, 0.3f, 1.0f};
// 0x630e1f..0x630f53 y 0x630fdb..0x6310d5: los tres botones del 0xa8, con la mezcla normal y en negrita, 0x646 en (x +
// 213, y + 100, w, h) con las UV (0, 0, 0,765625, 1), y su texto ("%s" del 1412 y del 1903 en [EBP − 0x100], de 100
// bytes; el 229 tal cual el tercero) en x + 0xed − cx / 2 e y + 100 + 4, con 0x6c8e5c en 0xff000000 y 0x6c8e64 en
// 0xff. Vuelve el color del texto, no el del fondo, y la negrita queda. El tercero (0x7d36908) se dibuja sin mirar su
// estado.
constexpr std::uint16_t LuckButtonSlot = 0x646;
constexpr float LuckButtonDu = 0.765625f;
constexpr int LuckButtonTextX = 0xed;
constexpr int LuckButtonTextDrop = 4;
constexpr std::uint32_t LuckButtonTextColor = 0xff000000u;
constexpr std::uint32_t LuckButtonBackground = 0xffu;
constexpr std::array<std::size_t, 2> LuckButtonTexts{1412, 1903};
constexpr std::size_t LuckThirdButtonText = 229;
constexpr std::size_t LuckButtonRoom = 100;
// 0x630df1..0x630fbe: los dos botones con estado de 1 a 4, 0xf0 + estado en (x + 213, y + 100, w, h) con las UV (0,
// 0, w / 128, h / 32): sin el − 1 del alto de las cajas (0x631245).
constexpr std::int32_t YesNoLastButtonState = 4;
// 0x631412..0x631418 y 0x632637..0x632905: los renglones arrancan en x = 0x15e e y = arriba + 0xf + 0xf; con 0x78abb14
// en 3, en x = 0x140 e y = arriba + 0xf, y en 4, en x = 0x140 e y = arriba + 0xf + 0x1e. Al 0x96 no lo mueven; el 0x9a
// va en x = 0x140 sin mirar 0x78abb14 y el 0xa3 en x = 0x145, salvo que 0x78abb14 lo mueva.
constexpr int YesNoTextDrop = 0xf;
constexpr int YesNoTextTop = YesNoBoxTop + YesNoTextDrop;
constexpr int YesNoTextX = 0x15e;
constexpr int ServerTimeTextX = 0x145;
constexpr int LowerTextDrop = 0x1e;
constexpr int LowerYesNoTextY = YesNoTextTop + LowerTextDrop;
// 0x632683..0x6328e3: el título de la mezcla, en negrita y en (1, 1, 0), centrado en y = arriba + 0xf + 10, con el
// sprintf del texto del tipo (0x7d2dc98) en [EBP − 0x9c], de 100 bytes; los renglones siguen 0x22 más abajo, en 0x140.
constexpr int MixTitleDrop = 10;
constexpr int MixTitleRowsDrop = 0x22;
constexpr Color MixTitleColor{1.0f, 1.0f, 0.0f, 1.0f};
constexpr std::size_t MixTitleRoom = 100;
// Lo que 0x632637 lee de 0x78abb14 (NpcWindowState::cursorChoice).
constexpr std::int32_t RedFirstRowChoice = 1;
constexpr std::int32_t MixChoice = 2;
constexpr std::int32_t RaisedTextChoice = 3;
constexpr std::int32_t LoweredTextChoice = 4;
constexpr std::int32_t OnlyFirstRowBoldChoice = 6;
// 0x632922..0x6329bf: cada renglón en la fuente gruesa y (1; 0,7; 0); con 0x78abb14 en 6, el primero en rojo y los
// demás en la normal y en blanco; si no, el primero en rojo con 1, el cuarto en (1; 0,1; 0,1) con 3, y todos en (0,9;
// 0,9; 1) con 2.
constexpr std::int32_t RedFourthRow = 3;
constexpr Color YesNoRowColor{1.0f, 0.7f, 0.0f, 1.0f};
constexpr Color RedRowColor{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color RedFourthRowColor{1.0f, 0.1f, 0.1f, 1.0f};
constexpr Color MixRowColor{0.9f, 0.9f, 1.0f, 1.0f};
// 0x630477..0x6304d4: los de sí y no, que dibuja 0x630cc4 (el 0x94 entra por 0x630cbd, sin mirar el 0xa3).
constexpr auto YesNoDialogs =
    std::to_array<std::int32_t>({0x94, UI::ItemNameDialog, 0x98, UI::MoneyTextDialog, UI::ChaosCastleDialog,
                                 UI::ServerTimeDialog, UI::ItemNameWithLuckDialog, 0xa9, UI::NoticeQuestionDialog});
// 0x632a45..0x632aeb: los renglones del 0xa9 (la joya de bendición, CL9 hito 5) en la fuente gruesa y en amarillo,
// centrados desde y = 100 + 0xf + 0x1e.
constexpr std::int32_t BlessDialog = 0xa9;
constexpr Color BlessRowColor{1.0f, 1.0f, 0.0f, 1.0f};

// 0x62e65c..0x62e929: el modo de la caja del caos (0x8e). El marco, 0xfc de 213 × 5 en (213, 120), 5 filas de 0xfb de
// 213 × 40 desde y = 125 y otra vez 0xfc; los dos botones, 0xf0 de 150 × 35 en (245, 190 + 85i) con las UV (0, 0,
// 0,83203125, 1), en blanco con el mouse en [245, 395] × [y, y + 35] (con los bordes) y en 0,7 si no; su título (735
// + i) en negrita, centrado en y + 12, con 0xfffff0c8; y en la normal, con blanco, la explicación de cada uno en x =
// 0xf8 (872, 873 y 1923 en y = 150, 160 y 170; 870 y 871 en 240 y 250).
constexpr int MixModeTop = 120;
constexpr int MixModeRows = 5;
constexpr int MixModeButtons = 2;
constexpr int MixModeButtonLeft = 245;
constexpr int MixModeButtonRight = 395;
constexpr int MixModeButtonTop = 190;
constexpr int MixModeButtonStep = 85;
constexpr float MixModeButtonWidth = 150.0f;
constexpr int MixModeButtonHeight = 35;
constexpr int MixModeTitleDrop = 12;
constexpr Color MixModeIdle{0.7f, 0.7f, 0.7f, 1.0f};
constexpr std::uint32_t MixModeTitleColor = 0xfffff0c8u;
constexpr std::size_t FirstMixModeTitle = 735;
constexpr int MixModeNotesX = 0xf8;
struct MixModeNote
{
    int y = 0;
    std::size_t text = 0;
};
constexpr std::array<MixModeNote, 3> RegularMixNotes{{{0x96, 872}, {0xa0, 873}, {0xaa, 1923}}};
constexpr std::array<MixModeNote, 2> ChaosWeaponNotes{{{0xf0, 870}, {0xfa, 871}}};
// 0x632323..0x63236d: el título, el 734 en negrita con 0xff0080ff, centrado en y = 120 + 0xf.
constexpr std::size_t MixModeTitle = 734;
constexpr std::uint32_t MixModeHeadColor = 0xff0080ffu;
constexpr int MixModeTitleY = MixModeTop + 0xf;
// La cola de 0x62db60 (0x632323, 0x632372, ..) lleva los títulos de los diálogos de la CL9.3; los que no son de esta
// fase cortan en su dibujo.
constexpr std::int32_t ChaosMixModeDialog = 0x8e;

// La mezcla de joyas (CL9 hito 6, fase 4a): el menú (0xa1, 0x62e92e..0x62ed05), la joya y el paquete (0xa2,
// 0x62ed0a..0x62f36c), deshacer (0xa4, 0x62f371..0x62fae6), la espera del BC (0xa5, 0x62faeb..0x62fc1a) y el
// resultado (0xa6, 0x62fc1f..0x62febe). Todos con [EBP − 0x20] en 0x78: el marco desde y = 120, como el 0x8e.
constexpr std::int32_t JewelMenuDialog = 0xa1;
constexpr std::int32_t JewelChoiceDialog = 0xa2;
constexpr std::int32_t JewelUnmixDialog = 0xa4;
constexpr std::int32_t JewelWaitDialog = 0xa5;
constexpr std::int32_t JewelResultDialog = 0xa6;
constexpr int JewelMenuRows = 6;
constexpr int JewelChoiceRows = 7;
constexpr int JewelUnmixShortRows = 3;
constexpr int JewelUnmixRows = 7;
constexpr int JewelNoteRows = 2;
// Los colores de los botones: con el mouse, blanco; si no, 0,7 (0,8 en el cancelar del 0xa1 y en el OK del 0xa6);
// la joya elegida, (1; 1; 0,2), y sin elegir, 0,5.
constexpr Color JewelIdle = MixModeIdle;
constexpr Color JewelLightIdle{0.8f, 0.8f, 0.8f, 1.0f};
constexpr Color JewelChosen{1.0f, 1.0f, 0.2f, 1.0f};
constexpr Color JewelDisabled{0.5f, 0.5f, 0.5f, 1.0f};
constexpr std::uint32_t JewelButtonTextColor = MixModeTitleColor;
// El cancelar, centrado en x = 0x13f: el 1002 de Text.bmd.
constexpr int JewelCancelCenterX = 0x13f;
constexpr std::size_t JewelCancelText = 1002;
constexpr float JewelButtonHeight = 25.0f;
// 0x62ea12..0x62ebd1: los dos botones del 0xa1, de 150 × 35 en x = 245, desde y = 190 y de a 65; el 1801 (juntar)
// y el 1800 (deshacer) en y + 12; y antes del primero, el 1802 y el 1803 en x = 0xf8.
constexpr std::size_t MixText = 1801;
constexpr std::size_t UnmixText = 1800;
constexpr float JewelMenuButtonTop = 190.0f;
constexpr float JewelMenuButtonStep = 65.0f;
constexpr float JewelMenuTextDrop = 12.0f;
constexpr int JewelMenuNotesX = 0xf8;
constexpr std::array<MixModeNote, 2> JewelMenuNotes{{{0x96, 1802}, {0xa0, 1803}}};
// 0x62ebd7..0x62ecf2: el cancelar del 0xa1, de 81 × 25 en (279, 320), su texto en y = 0x148.
constexpr int JewelCancelLeft = 279;
constexpr int JewelCancelRight = 360;
constexpr float JewelCancelWidth = 81.0f;
constexpr int JewelMenuCancelTop = 320;
constexpr int JewelMenuCancelTextY = 0x148;
// 0x62edee..0x62f08f: las dos joyas del 0xa2 (1806 y 1807), de 80 × 25 en x = 235 + ftol(90 · k), y = 185, con el
// texto centrado en ftol(x + 275) y en y + 8; antes, el 1804 y el 1805 en x = 0xee. El paso elige desde 245 + 80 · k
// (07 Q-C951).
constexpr float JewelChoiceTop = 185.0f;
constexpr float JewelKindStep = 90.0f;
constexpr int JewelKindLeft = 235;
constexpr float JewelKindWidth = 80.0f;
constexpr int JewelKindCenter = 275;
constexpr float JewelKindTextDrop = 8.0f;
constexpr std::size_t FirstJewelKindText = 1806;
constexpr int JewelChoiceNotesX = 0xee;
constexpr std::array<MixModeNote, 2> JewelChoiceNotes{{{0x9b, 1804}, {0xa5, 1805}}};
constexpr float JewelKindRowStep = 40.0f;
// 0x62f0bd..0x62f271: los tres paquetes, de 170 × 35 en x = 235 y de a 45, con el 1808 escrito con 10 · i joyas y
// 500.000 · i de zen en un buffer de 0x800 bytes, centrado en y + 12.
constexpr int JewelBundles = 3;
constexpr int JewelBundleLeft = 235;
constexpr int JewelBundleRight = 405;
constexpr float JewelBundleWidth = 170.0f;
constexpr float JewelBundleHeight = 35.0f;
constexpr float JewelBundleStep = 45.0f;
constexpr std::size_t JewelBundleText = 1808;
constexpr std::int32_t JewelsPerBundle = 10;
constexpr std::int32_t ZenPerBundle = 500000;
constexpr std::size_t JewelBundleRoom = 0x800;
// 0x62f27e..0x62f36c: el cancelar del 0xa2 en (279, 365), su texto en y = 0x175.
constexpr int JewelChoiceCancelTop = 365;
constexpr int JewelChoiceCancelTextY = 0x175;
// 0x62f4ae..0x62f689: las dos joyas del 0xa4, de 80 × 25 desde x = 235 y de a 80, en y = 160, con el texto en
// ftol(x + 40) e y = 0xa8.
constexpr float JewelUnmixKindLeft = 235.0f;
constexpr int JewelUnmixKindTop = 160;
constexpr float JewelUnmixKindStep = 80.0f;
constexpr float JewelUnmixKindCenter = 40.0f;
constexpr int JewelUnmixKindTextY = 0xa8;
// 0x62f9c6..0x62fae6: sin joya, el cancelar de 80 × 25 en (279,5; 200), su texto en y = 0xd0.
constexpr float JewelUnmixCancelLeft = 279.5f;
constexpr float JewelUnmixCancelRight = 359.5f;
constexpr int JewelUnmixCancelTop = 200;
constexpr int JewelUnmixCancelTextY = 0xd0;
// 0x62f69c..0x62f715: con la joya, el 1821 con las casillas vacías (0x402de0) en un buffer de 300 bytes y el 1822,
// en x = 0xe4 con 0xc8c8c8c8, en la fuente que dejó la última joya (07 Q-C950).
constexpr std::size_t FreeCellsText = 1821;
constexpr std::size_t FreeCellsRoom = 300;
constexpr std::size_t UnmixListText = 1822;
constexpr int UnmixNotesX = 0xe4;
constexpr int FreeCellsY = 0xc5;
constexpr int UnmixListTextY = 0xd4;
constexpr std::uint32_t UnmixNotesColor = 0xc8c8c8c8u;
constexpr std::uint16_t EmptyCell = 0xffff;
// 0x62fbc9..0x62fc12: el 471 del 0xa5, en y = 0xa5.
constexpr std::size_t JewelWaitText = 471;
constexpr int JewelWaitTextY = 0xa5;
// 0x62fd09..0x62febe: el 0xa6. "%s%s %s" (0x6d62c8 y 0x6d62d0, iguales) con el 1801 o el 1800 según 0x6e5794, el
// 1816 y el 858 en un buffer de 256 bytes, en negrita en y = 0x91; el OK de 65 × 25 en (287, 170) con el 228 en
// y = 0xb2.
constexpr std::size_t JewelResultText = 1816;
constexpr std::size_t JewelResultTail = 858;
constexpr std::size_t JewelResultRoom = 256;
constexpr int JewelResultTextY = 0x91;
constexpr int JewelResultLeft = 287;
constexpr int JewelResultRight = 352;
constexpr int JewelResultTop = 170;
constexpr int JewelResultBottom = 195;
constexpr float JewelResultWidth = 65.0f;
constexpr std::size_t JewelResultOkText = 228;
constexpr int JewelResultOkTextY = 0xb2;
// 0x6304f2..0x6305b7: el marco del 0x91 ([EBP − 0x20] en 0x64): 0xfc en y = 100, 2 filas y 0xfc.
constexpr std::int32_t TypedTextDialog = 0x91;
constexpr int TypedTextTop = 100;
constexpr int TypedTextRows = 2;

// 0x63140b..0x631d66: los textos de Text.bmd de los del OK genérico que llegan a la CL8, uno o dos renglones. El 0x87 y
// el 0x88 los abre la caja de la contraseña del almacén (0x5e7d53, 0x5e7e83; CL9 hito 6): la tabla 0x632bf0 los lleva a
// 0x632290 y 0x6322c1, un renglón cada uno.
struct GenericText
{
    std::int32_t id = 0;
    std::size_t first = 0;
    std::optional<std::size_t> second{};
};
constexpr auto GenericTexts = std::to_array<GenericText>({
    {UI::DisconnectDialog, 402, std::nullopt},
    {0x6f, 405, 406},
    {0x1a, 405, 406},
    {0x1c, 409, std::nullopt},
    {0x87, 442, std::nullopt},
    {0x88, 445, std::nullopt},
    {0x8f, 401, std::nullopt},
});

struct Frame
{
    GameHudContext& context;
    WorldRuntime& runtime;
};

bool MouseIn(const Frame& frame, int left, int top, int right, int bottom)
{
    const UI::Point mouse = frame.context.mouse;
    return left <= mouse.x && mouse.x <= right && top <= mouse.y && mouse.y <= bottom;
}

// 0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa.
void Quad(Frame& frame, std::uint16_t slot, float x, float y, float width, float height, float du, float dv)
{
    frame.context.sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                                    .x = x,
                                                    .y = y,
                                                    .width = width,
                                                    .height = height,
                                                    .u = 0.0f,
                                                    .v = 0.0f,
                                                    .du = du,
                                                    .dv = dv,
                                                    .scaleSize = true,
                                                    .scalePosition = true,
                                                    .alpha = 0.0f});
}

// 0x459ec0: glColor4f(1, 1, 1, 1) y glEnable(GL_TEXTURE_2D).
void WhiteWithTexture(Frame& frame)
{
    frame.context.sprites.SetColor(White);
    frame.context.sprites.EnableTexture();
}

Core::Expected<std::string_view, std::string> TextOf(const Frame& frame, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(frame.context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x62db60: sin el texto {} de Text.bmd", index));
    return *text;
}

// 0x554180(x, y, texto, 0, 0, 0): con la fuente del DC y los colores de 0x6c8e5c y 0x6c8e64; un texto vacío no se
// dibuja, pero se mide igual y su tamaño en 640 × 480 queda en 0x77f31b8 y 0x77f31bc.
void DrawText(Frame& frame, int x, int y, std::string_view text)
{
    HudState& hud = frame.context.hud;
    if (!text.empty())
        frame.context.text.Draw(x, y, text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    const Render::Text::TextExtent extent = frame.context.text.Measure(hud.dcFont, text);
    const App::PixelSize screen = frame.context.sprites.Screen();
    hud.arrowsTextWidth = Render::Text::DesignX(extent.width, screen);
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, screen);
}

// GetTextExtentPointA y 0x554180 en x = centro − (cx · 640 / W) / 2.
void DrawCenteredAt(Frame& frame, int centerX, int y, std::string_view text)
{
    const Render::Text::TextExtent extent = frame.context.text.Measure(frame.context.hud.dcFont, text);
    DrawText(frame, Render::Text::CenteredAt(centerX, extent, frame.context.sprites.Screen()), y, text);
}

void DrawCentered(Frame& frame, int y, std::string_view text)
{
    DrawCenteredAt(frame, CenterX, y, text);
}

Core::Expected<void, std::string> DrawCenteredText(Frame& frame, int y, std::size_t index)
{
    auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    DrawCentered(frame, y, text.Value());
    return {};
}

// Un sprintf a un buffer de la pila de 0x62db60: lo que no entra la pisa, y corta.
Core::Expected<std::string, std::string> Fits(std::string text, std::size_t room, std::string_view where)
{
    if (text.size() >= room)
        return Core::Unexpected(std::format("{}: el sprintf escribe {} bytes en un buffer de {} y pisa la pila", where,
                                            text.size() + 1, room));
    return text;
}

// Un botón del menú o una fila de las opciones, y con el mouse encima, otra vez en aditiva y con color.
void DrawMenuButton(Frame& frame, int y, int height)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto draw = [&]
    {
        Quad(frame, MessageBoxSlot, MenuButtonLeft, static_cast<float>(y), MenuButtonWidth, static_cast<float>(height),
             MessageBoxDu, 1.0f);
    };
    draw();
    const UI::Point mouse = frame.context.mouse;
    if (MenuHoverLeft < mouse.x && mouse.x < MenuHoverRight && y <= mouse.y && mouse.y < y + height)
    {
        sprites.SetColor(HoverColor);
        sprites.SetBlend(BlendMode::Additive);
        draw();
        sprites.SetColor(White);
        sprites.SetBlend(BlendMode::Opaque);
    }
}

// 0x62dbcf..0x62df1f: el menú de Esc.
Core::Expected<void, std::string> DrawTheEscapeMenu(Frame& frame)
{
    for (int button = 0; button < MenuButtons; ++button)
        DrawMenuButton(frame, MenuFirstY + button * MenuButtonStep, MenuButtonHeight);
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    for (std::size_t at = 0; at < MenuTexts.size(); ++at)
    {
        if (auto drawn = DrawCenteredText(frame, MenuTextYs[at], MenuTexts[at]); !drawn.HasValue())
            return drawn;
    }
    return {};
}

// 0x69fd9b(buffer, "%s  On" o "%s  Off", texto): una llave de las opciones.
Core::Expected<void, std::string> DrawTheSwitch(Frame& frame, int y, std::size_t index, bool on)
{
    auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    DrawCentered(frame, y, std::format("{}{}", text.Value(), on ? OnSuffix : OffSuffix));
    return {};
}

// 0x62e1ba..0x62e2a8: el texto del volumen y su barra. La esquina de la barra queda para el paso (0x62570a).
Core::Expected<void, std::string> DrawTheVolume(Frame& frame)
{
    auto text = TextOf(frame, VolumeText);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Render::SpriteBatch& sprites = frame.context.sprites;
    const App::PixelSize screen = sprites.Screen();
    const Render::Text::TextExtent extent = frame.context.text.Measure(frame.context.hud.dcFont, text.Value());
    // cx · 640 / W en 32 bits sin signo, como 0x62e1df; sin pantalla, el ancho tal cual.
    std::int32_t width = extent.width;
    if (screen.width > 0)
        width = static_cast<std::int32_t>(static_cast<std::uint32_t>(extent.width) * DesignWidth /
                                          static_cast<std::uint32_t>(screen.width));
    const std::int32_t x = -((width + VolumeTextSpace) / 2);
    DrawText(frame, x + CenterX, VolumeY, text.Value());
    const std::int32_t barX = x + VolumeBarOffset + width;
    const std::int32_t barY = VolumeBarTop;
    UI::GameUiState& ui = frame.runtime.ui;
    if (!frame.context.projecting)
    {
        ui.volumeBarX = barX;
        ui.volumeBarY = barY;
    }
    sprites.SetColor(FrameGrey);
    sprites.ColorQuad(static_cast<float>(barX), static_cast<float>(barY), VolumeBarWidth, VolumeBarHeight);
    sprites.SetColor(Black);
    sprites.ColorQuad(static_cast<float>(barX + 1), static_cast<float>(barY + 1), VolumeFillWidth, VolumeFillHeight);
    sprites.SetColor(White);
    // fild, fmul por el float de 0x6b8bf0 en la x87 y fstp en float.
    const float fill = static_cast<float>(static_cast<double>(ui.volume + 1) * static_cast<double>(VolumeStepWidth));
    sprites.ColorQuad(static_cast<float>(barX + 1), static_cast<float>(barY + 1), fill, VolumeFillHeight);
    WhiteWithTexture(frame);
    return {};
}

// 0x62e2ce..0x62e45a: el marco del brillo, sus casillas (las de hasta 0x6c9dcc prendidas) y sus cifras.
void DrawTheGlow(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const std::int32_t detail = frame.runtime.ui.glowDetail;
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(FrameGrey);
    sprites.ColorQuad(268.0f, 198.0f, 102.0f, 22.0f);
    sprites.SetColor(Black);
    sprites.ColorQuad(269.0f, 199.0f, 100.0f, 20.0f);
    for (int box = 0; box < GlowBoxes; ++box)
    {
        sprites.SetColor(detail < box ? Black : GlowColor);
        sprites.ColorQuad(static_cast<float>(GlowBoxFirstX + box * GlowStep), GlowBoxTop, GlowBoxSize, GlowBoxSize);
    }
    WhiteWithTexture(frame);
    sprites.SetColor(Black);
    for (int box = 0; box < GlowBoxes; ++box)
    {
        sprites.SetColor(detail < box ? GlowColor : Black);
        static_cast<void>(UI::DrawNumber(sprites, static_cast<float>(GlowDigitFirstX + box * GlowStep), GlowDigitTop,
                                         GlowFirstDigit + box * GlowDigitStep, GlowDigitSize, GlowDigitSize));
    }
    WhiteWithTexture(frame);
}

// 0x62df3f..0x62e4bb: las opciones.
Core::Expected<void, std::string> DrawTheOptions(Frame& frame)
{
    for (int row = 0; row < OptionRows; ++row)
    {
        const int y = row == LastOptionRow ? LastOptionRowY : (row * 3 + 3) * 10;
        DrawMenuButton(frame, y, row == TallOptionRow ? TallOptionRowHeight : MenuButtonHeight);
    }
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    if (auto drawn = DrawCenteredText(frame, OptionsTitleY, OptionsTitleText); !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawTheSwitch(frame, AutoAttackY, AutoAttackText, frame.runtime.ui.autoAttack); !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawTheSwitch(frame, NoticeSoundY, NoticeSoundText, frame.runtime.chat.noticeSound);
        !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawTheVolume(frame); !drawn.HasValue())
        return drawn;
    if (auto drawn = DrawTheSwitch(frame, SlidesY, SlidesText, frame.runtime.chat.slidesOn); !drawn.HasValue())
        return drawn;
    // 0x62e2c4: 0x69fd9b(buffer, texto 1840), el texto como formato. MuRet lo copia tal cual: un '%' saldría distinto.
    auto glow = TextOf(frame, GlowText);
    if (!glow.HasValue())
        return Core::Unexpected(std::move(glow).Error());
    DrawTheGlow(frame);
    DrawCentered(frame, GlowTextY, glow.Value());
    frame.context.sprites.SetColor(White);
    return DrawCenteredText(frame, CloseY, CloseText);
}

// Una flecha del 0x8c: gris en su tope; con el mouse encima, 0,7, o 0,5 con el clic, que gasta (Q-C553).
void DrawTheArrow(Frame& frame, bool atTheEnd, int left, int right, float x, float degrees)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const UI::Point mouse = frame.context.mouse;
    sprites.SetColor(White);
    bool& click = frame.runtime.movement.pendingClick;
    if (atTheEnd)
        sprites.SetColor(FrameGrey);
    else if (left < mouse.x && ArrowTop < mouse.y && mouse.x < right && mouse.y < ArrowBottom)
    {
        if (!click)
            sprites.SetColor(ArrowHover);
        else
        {
            sprites.SetColor(ArrowPressed);
            if (!frame.context.projecting)
                click = false;
        }
    }
    sprites.RotatedQuad(ArrowSlot, x, ArrowY, ArrowWidth, ArrowHeight, degrees);
}

// 0x630e14..0x6313ff: el marco de las cajas, sus botones y las flechas del 0x8c.
int BoxRowsOf(const UI::DialogBoxState& box, std::int32_t id)
{
    if (id == UI::MessageBoxDialog || id == UI::PagedDialog || id == 0x9b)
        return ShortBoxRows;
    if (id != UI::AnswersDialog)
        return TallBoxRows;
    const int rows = ((box.lineCount + box.answerCount) * UI::DialogLineStep + AnswersBoxRowsRound) / BoxRowStep;
    return std::clamp(rows, ShortBoxRows, AnswersBoxRowsMaximum);
}

void DrawTheBox(Frame& frame, std::int32_t id)
{
    const int rows = BoxRowsOf(frame.runtime.dialogBox, id);
    Quad(frame, BoxEdgeSlot, BoxLeft, BoxTop, BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    int y = BoxFirstRowY;
    for (int row = 0; row < rows; ++row, y += BoxRowStep)
        Quad(frame, BoxRowSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxRowHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    for (const UI::DialogButton& button : frame.runtime.dialogBox.buttons)
    {
        if (button.state <= 0)
            continue;
        const float width = static_cast<float>(button.width);
        const float height = static_cast<float>(button.height);
        Quad(frame, static_cast<std::uint16_t>(MessageBoxSlot + button.state), static_cast<float>(button.x) + BoxLeft,
             static_cast<float>(button.y) + BoxTop, width, height - 1.0f, width * ButtonDu, height * ButtonDv);
    }
    if (id != UI::PagedDialog)
        return;
    const UI::DialogBoxState& box = frame.runtime.dialogBox;
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    DrawTheArrow(frame, box.page == 0, LeftArrowLeft, LeftArrowRight, LeftArrowX, LeftArrowDegrees);
    DrawTheArrow(frame, box.page == box.lastPage, RightArrowLeft, RightArrowRight, RightArrowX, RightArrowDegrees);
    frame.context.sprites.SetColor(White);
}

// 0x632472..0x632636: los renglones del 0x8a, del 0x8c y del 0x8d, centrados desde DialogTextTop; en el 0x8d, debajo,
// cada respuesta con su color y, si su primer byte no es 0, su renglón (la y baja solo con las que se dibujan). Al
// final, el blanco.
Core::Expected<void, std::string> DrawTheBoxText(Frame& frame, std::int32_t id)
{
    const UI::DialogBoxState& box = frame.runtime.dialogBox;
    std::int32_t y = UI::DialogTextTop(box, id);
    for (std::int32_t row = 0; row < box.lineCount; ++row, y += UI::DialogLineStep)
        DrawCentered(frame, y, UI::DialogRow(box, static_cast<std::size_t>(row)));
    if (id == UI::AnswersDialog)
    {
        if (box.answerCount > static_cast<std::int32_t>(UI::DialogAnswerRows))
            return Core::Unexpected(
                std::format("0x632563: {} respuestas pasan los 10 renglones de 0x7d3691c", box.answerCount));
        // 0x632515..0x632535: la respuesta bajo el mouse, con la división hacia el cero, o −1 arriba de la primera.
        const UI::Point mouse = frame.context.mouse;
        const std::int32_t offset = mouse.y - y;
        const std::int32_t hovered = offset < 0 ? -1 : offset / UI::DialogLineStep;
        const bool inside = std::abs(UI::AnswersCenterX - mouse.x) <= UI::AnswersHalfWidth;
        for (std::int32_t answer = 0; answer < box.answerCount; ++answer)
        {
            frame.context.sprites.SetColor(answer == hovered && inside ? HoveredAnswerColor : AnswerColor);
            const std::size_t first = static_cast<std::size_t>(answer) * UI::DialogRowBytes;
            if (box.answers[first] == '\0')
                continue;
            DrawCentered(frame, y, UI::DialogAnswer(box, static_cast<std::size_t>(answer)));
            y += UI::DialogLineStep;
        }
    }
    frame.context.sprites.SetColor(White);
    return {};
}

// El marco de los diálogos de la caja del caos, de la mezcla de joyas y del 0x91: 0xfc de 213 × 5 en (213, top), las
// filas de 0xfb de 213 × 40 desde top + 5 y otra vez 0xfc, con las UV (0, 0, 0,83203125, 0,625).
void DrawTheFrame(Frame& frame, int top, int rows)
{
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(top), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    int y = top + static_cast<int>(BoxEdgeHeight);
    for (int row = 0; row < rows; ++row, y += BoxRowStep)
        Quad(frame, BoxRowSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxRowHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
}

// 0x62e65c..0x62e929: el modo de la caja del caos, sin el título de la cola.
Core::Expected<void, std::string> DrawTheMixModes(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    HudState& hud = frame.context.hud;
    sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, MixModeRows);
    int top = MixModeButtonTop;
    for (int button = 0; button < MixModeButtons; ++button, top += MixModeButtonStep)
    {
        const bool hovered = MouseIn(frame, MixModeButtonLeft, top, MixModeButtonRight, top + MixModeButtonHeight);
        sprites.SetColor(hovered ? White : MixModeIdle);
        Quad(frame, MessageBoxSlot, static_cast<float>(MixModeButtonLeft), static_cast<float>(top), MixModeButtonWidth,
             static_cast<float>(MixModeButtonHeight), MessageBoxDu, 1.0f);
        hud.dcFont = Render::Text::FontKind::Bold;
        hud.textColors.text = MixModeTitleColor;
        if (auto drawn =
                DrawCenteredText(frame, top + MixModeTitleDrop, FirstMixModeTitle + static_cast<std::size_t>(button));
            !drawn.HasValue())
            return drawn;
        hud.dcFont = Render::Text::FontKind::Normal;
        hud.textColors.text = DialogTextColors.text;
        const std::span<const MixModeNote> notes = button == 0 ? std::span<const MixModeNote>(RegularMixNotes)
                                                               : std::span<const MixModeNote>(ChaosWeaponNotes);
        for (const MixModeNote& note : notes)
        {
            auto text = TextOf(frame, note.text);
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            DrawText(frame, MixModeNotesX, note.y, text.Value());
        }
    }
    sprites.SetColor(White);
    return {};
}

// 0x632323..0x63236d (el 734 del 0x8e), 0x632372..0x6323bc (el 1801 del 0xa1 y del 0xa2) y 0x6323c1..0x63240b (el
// 1800 del 0xa4): el título en negrita con 0xff0080ff, centrado en y = 0x78 + 0xf, que deja la negrita y su color.
Core::Expected<void, std::string> DrawTheHeadTitle(Frame& frame, std::size_t index)
{
    HudState& hud = frame.context.hud;
    hud.dcFont = Render::Text::FontKind::Bold;
    hud.textColors.text = MixModeHeadColor;
    return DrawCenteredText(frame, MixModeTitleY, index);
}

// x en [left, right] e y en [top, bottom], con los bordes: los FCOM de los floats con el mouse entero.
bool MouseWithin(const Frame& frame, float left, float top, float right, float bottom)
{
    const auto x = static_cast<float>(frame.context.mouse.x);
    const auto y = static_cast<float>(frame.context.mouse.y);
    return left <= x && x <= right && top <= y && y <= bottom;
}

// Un botón de la mezcla de joyas: el glColor3f y 0xf0 con las UV (0, 0, 0,83203125, 1).
void DrawTheJewelButton(Frame& frame, Color color, float x, float y, float width, float height)
{
    frame.context.sprites.SetColor(color);
    Quad(frame, MessageBoxSlot, x, y, width, height, MessageBoxDu, 1.0f);
}

// Los textos de Text.bmd en la fuente de ahora, en x fija.
Core::Expected<void, std::string> DrawTheNotes(Frame& frame, int x, std::span<const MixModeNote> notes)
{
    for (const MixModeNote& note : notes)
    {
        auto text = TextOf(frame, note.text);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawText(frame, x, note.y, text.Value());
    }
    return {};
}

// El texto de un cancelar o de un OK de la mezcla de joyas: en negrita con 0xfffff0c8, centrado en `centerX`, y la
// normal después (el color queda).
Core::Expected<void, std::string> DrawTheJewelButtonText(Frame& frame, int centerX, int y, std::size_t index)
{
    HudState& hud = frame.context.hud;
    auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    hud.dcFont = Render::Text::FontKind::Bold;
    hud.textColors.text = JewelButtonTextColor;
    DrawCenteredAt(frame, centerX, y, text.Value());
    hud.dcFont = Render::Text::FontKind::Normal;
    return {};
}

// 0x62e92e..0x62ed05: el menú de la mezcla de joyas (0xa1). Deja el texto en 0xfffff0c8 y el blanco.
Core::Expected<void, std::string> DrawTheJewelMenu(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    HudState& hud = frame.context.hud;
    sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, JewelMenuRows);
    float y = JewelMenuButtonTop;
    for (int button = 0; button < MixModeButtons; ++button)
    {
        if (button == 0)
        {
            if (auto drawn = DrawTheNotes(frame, JewelMenuNotesX, JewelMenuNotes); !drawn.HasValue())
                return drawn;
        }
        const bool hovered = MouseWithin(frame, static_cast<float>(MixModeButtonLeft), y,
                                         static_cast<float>(MixModeButtonRight), y + MixModeButtonHeight);
        DrawTheJewelButton(frame, hovered ? White : JewelIdle, static_cast<float>(MixModeButtonLeft), y,
                           MixModeButtonWidth, static_cast<float>(MixModeButtonHeight));
        auto text = TextOf(frame, button == 0 ? MixText : UnmixText);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        hud.dcFont = Render::Text::FontKind::Bold;
        hud.textColors.text = JewelButtonTextColor;
        DrawCentered(frame, static_cast<int>(y + JewelMenuTextDrop), text.Value());
        y += JewelMenuButtonStep;
        hud.dcFont = Render::Text::FontKind::Normal;
        hud.textColors.text = DialogTextColors.text;
    }
    const bool hovered = MouseIn(frame, JewelCancelLeft, JewelMenuCancelTop, JewelCancelRight,
                                 JewelMenuCancelTop + static_cast<int>(JewelButtonHeight));
    DrawTheJewelButton(frame, hovered ? White : JewelLightIdle, static_cast<float>(JewelCancelLeft),
                       static_cast<float>(JewelMenuCancelTop), JewelCancelWidth, JewelButtonHeight);
    if (auto drawn = DrawTheJewelButtonText(frame, JewelCancelCenterX, JewelMenuCancelTextY, JewelCancelText);
        !drawn.HasValue())
        return drawn;
    sprites.SetColor(White);
    return {};
}

// 0x62ee33..0x62f089 y 0x62f4bc..0x62f689: una joya del 0xa2 o del 0xa4. La elegida (0x6c62e0 igual a la suya) va en
// negrita, en (1; 1; 0,2) y con 0xfffff0c8; si no, con el mouse en blanco y con 0xfffff0c8, y si no en 0,5. Ni la
// fuente ni el color del texto vuelven entre las dos (Q-C950).
void PaintTheJewelKind(Frame& frame, int kind, bool hovered)
{
    HudState& hud = frame.context.hud;
    if (frame.runtime.npcWindows.jewels.kind == kind)
    {
        hud.dcFont = Render::Text::FontKind::Bold;
        frame.context.sprites.SetColor(JewelChosen);
        hud.textColors.text = JewelButtonTextColor;
        return;
    }
    if (hovered)
    {
        frame.context.sprites.SetColor(White);
        hud.textColors.text = JewelButtonTextColor;
        return;
    }
    frame.context.sprites.SetColor(JewelDisabled);
}

// 0x62ed0a..0x62f36c: la joya y el paquete del 0xa2. Deja el texto en 0xfffff0c8 y el blanco.
Core::Expected<void, std::string> DrawTheJewelChoice(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    HudState& hud = frame.context.hud;
    const std::uint8_t chosen = frame.runtime.npcWindows.jewels.kind;
    sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, JewelChoiceRows);
    float y = JewelChoiceTop;
    if (auto drawn = DrawTheNotes(frame, JewelChoiceNotesX, JewelChoiceNotes); !drawn.HasValue())
        return drawn;
    for (int kind = 0; kind < 2; ++kind)
    {
        // 0x62ee33..0x62ee41: ftol(k · 90), entero, y de vuelta a float para la x.
        const int offset = static_cast<int>(static_cast<float>(kind) * JewelKindStep);
        const float x = static_cast<float>(offset) + static_cast<float>(JewelKindLeft);
        PaintTheJewelKind(frame, kind, MouseWithin(frame, x, y, x + JewelKindWidth, y + JewelButtonHeight));
        Quad(frame, MessageBoxSlot, x, y, JewelKindWidth, JewelButtonHeight, MessageBoxDu, 1.0f);
        auto text = TextOf(frame, FirstJewelKindText + static_cast<std::size_t>(kind));
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawCenteredAt(frame, static_cast<int>(static_cast<float>(offset) + static_cast<float>(JewelKindCenter)),
                       static_cast<int>(y + JewelKindTextDrop), text.Value());
    }
    hud.dcFont = Render::Text::FontKind::Normal;
    y += JewelKindRowStep;
    hud.textColors.text = DialogTextColors.text;
    auto format = TextOf(frame, JewelBundleText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    for (int bundle = 1; bundle <= JewelBundles; ++bundle)
    {
        // 0x62f0bd..0x62f15c: sin joya, solo el gris; con una, el mouse y la negrita con 0xfffff0c8.
        if (chosen == NoDialogItem)
            sprites.SetColor(JewelDisabled);
        else
        {
            const bool hovered = MouseWithin(frame, static_cast<float>(JewelBundleLeft), y,
                                             static_cast<float>(JewelBundleRight), y + JewelBundleHeight);
            sprites.SetColor(hovered ? White : JewelIdle);
            hud.dcFont = Render::Text::FontKind::Bold;
            hud.textColors.text = JewelButtonTextColor;
        }
        Quad(frame, MessageBoxSlot, static_cast<float>(JewelBundleLeft), y, JewelBundleWidth, JewelBundleHeight,
             MessageBoxDu, 1.0f);
        auto written = Fits(UI::Sprintf(format.Value(), bundle * JewelsPerBundle, bundle * ZenPerBundle),
                            JewelBundleRoom, "0x62f1dc");
        if (!written.HasValue())
            return Core::Unexpected(std::move(written).Error());
        DrawCentered(frame, static_cast<int>(y + JewelMenuTextDrop), written.Value());
        hud.dcFont = Render::Text::FontKind::Normal;
        y += JewelBundleStep;
        hud.textColors.text = DialogTextColors.text;
    }
    const bool hovered = MouseIn(frame, JewelCancelLeft, JewelChoiceCancelTop, JewelCancelRight,
                                 JewelChoiceCancelTop + static_cast<int>(JewelButtonHeight));
    DrawTheJewelButton(frame, hovered ? White : JewelIdle, static_cast<float>(JewelCancelLeft),
                       static_cast<float>(JewelChoiceCancelTop), JewelCancelWidth, JewelButtonHeight);
    if (auto drawn = DrawTheJewelButtonText(frame, JewelCancelCenterX, JewelChoiceCancelTextY, JewelCancelText);
        !drawn.HasValue())
        return drawn;
    sprites.SetColor(White);
    return {};
}

// 0x402de0: las casillas vacías (tipo −1) de las 64 del inventario.
std::int32_t FreeCellsOf(const WorldState& world)
{
    return static_cast<std::int32_t>(std::ranges::count_if(world.Inventory().cells, [](const World::ItemState& cell)
                                                           { return cell.id == EmptyCell; }));
}

// 0x62f371..0x62fae6: deshacer un paquete (0xa4). Sin joya, las dos joyas y el cancelar, y el texto en blanco al
// final; con una, las joyas, el 1821 y el 1822, y la lista de 0x6e5660 corta.
Core::Expected<void, std::string> DrawTheJewelUnmix(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    HudState& hud = frame.context.hud;
    const std::uint8_t chosen = frame.runtime.npcWindows.jewels.kind;
    sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, chosen == NoDialogItem ? JewelUnmixShortRows : JewelUnmixRows);
    float x = JewelUnmixKindLeft;
    for (int kind = 0; kind < 2; ++kind)
    {
        PaintTheJewelKind(frame, kind,
                          MouseWithin(frame, x, static_cast<float>(JewelUnmixKindTop), x + JewelKindWidth,
                                      static_cast<float>(JewelUnmixKindTop) + JewelButtonHeight));
        Quad(frame, MessageBoxSlot, x, static_cast<float>(JewelUnmixKindTop), JewelKindWidth, JewelButtonHeight,
             MessageBoxDu, 1.0f);
        auto text = TextOf(frame, FirstJewelKindText + static_cast<std::size_t>(kind));
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawCenteredAt(frame, static_cast<int>(x + JewelUnmixKindCenter), JewelUnmixKindTextY, text.Value());
        x += JewelUnmixKindStep;
    }
    if (chosen == NoDialogItem)
    {
        const bool hovered =
            MouseWithin(frame, JewelUnmixCancelLeft, static_cast<float>(JewelUnmixCancelTop), JewelUnmixCancelRight,
                        static_cast<float>(JewelUnmixCancelTop) + JewelButtonHeight);
        DrawTheJewelButton(frame, hovered ? White : JewelIdle, JewelUnmixCancelLeft,
                           static_cast<float>(JewelUnmixCancelTop), JewelKindWidth, JewelButtonHeight);
        if (auto drawn = DrawTheJewelButtonText(frame, JewelCancelCenterX, JewelUnmixCancelTextY, JewelCancelText);
            !drawn.HasValue())
            return drawn;
        hud.textColors.text = DialogTextColors.text;
        // 0x62e914: el blanco.
        sprites.SetColor(White);
        return {};
    }
    auto format = TextOf(frame, FreeCellsText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    auto written = Fits(UI::Sprintf(format.Value(), FreeCellsOf(frame.context.world)), FreeCellsRoom, "0x62f6bd");
    if (!written.HasValue())
        return Core::Unexpected(std::move(written).Error());
    hud.textColors.text = UnmixNotesColor;
    sprites.SetColor(White);
    DrawText(frame, UnmixNotesX, FreeCellsY, written.Value());
    auto list = TextOf(frame, UnmixListText);
    if (!list.HasValue())
        return Core::Unexpected(std::move(list).Error());
    DrawText(frame, UnmixNotesX, UnmixListTextY, list.Value());
    // 0x62f71a..0x62f9c1: la esquina de la lista (0x45a9d0 en (0xe4, [0x6e56bc] + 0xf0)), su dibujo, el OK con el
    // elegido de la vtable + 0x64 y el cancelar.
    return Core::Unexpected(std::string("0x62f739: el dibujo de la lista de 0x6e5660 (0x402b20 → 0x4832e0, la vtable "
                                        "0x6b85f8) y el OK con su elegido (+0x64) son del hito 6 y no están "
                                        "(08 P-C338)"));
}

// 0x62faeb..0x62fc1a: la espera del BC (0xa5), el 471 centrado en x = 0x13f.
Core::Expected<void, std::string> DrawTheJewelWait(Frame& frame)
{
    frame.context.sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, JewelNoteRows);
    auto text = TextOf(frame, JewelWaitText);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    DrawCenteredAt(frame, JewelCancelCenterX, JewelWaitTextY, text.Value());
    return {};
}

// 0x62fc1f..0x62febe y 0x6313fa: el resultado (0xa6). Deja el texto en 0xfffff0c8 y el blanco.
Core::Expected<void, std::string> DrawTheJewelResult(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    HudState& hud = frame.context.hud;
    sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, JewelNoteRows);
    auto title = TextOf(frame, frame.runtime.npcWindows.jewels.mode == 0 ? MixText : UnmixText);
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    auto result = TextOf(frame, JewelResultText);
    if (!result.HasValue())
        return Core::Unexpected(std::move(result).Error());
    auto tail = TextOf(frame, JewelResultTail);
    if (!tail.HasValue())
        return Core::Unexpected(std::move(tail).Error());
    auto written =
        Fits(UI::Sprintf("%s%s %s", title.Value(), result.Value(), tail.Value()), JewelResultRoom, "0x62fd43");
    if (!written.HasValue())
        return Core::Unexpected(std::move(written).Error());
    hud.dcFont = Render::Text::FontKind::Bold;
    DrawCenteredAt(frame, JewelCancelCenterX, JewelResultTextY, written.Value());
    const bool hovered = MouseIn(frame, JewelResultLeft, JewelResultTop, JewelResultRight, JewelResultBottom);
    DrawTheJewelButton(frame, hovered ? White : JewelLightIdle, static_cast<float>(JewelResultLeft),
                       static_cast<float>(JewelResultTop), JewelResultWidth, JewelButtonHeight);
    auto ok = TextOf(frame, JewelResultOkText);
    if (!ok.HasValue())
        return Core::Unexpected(std::move(ok).Error());
    hud.textColors.text = JewelButtonTextColor;
    DrawCenteredAt(frame, JewelCancelCenterX, JewelResultOkTextY, ok.Value());
    hud.dcFont = Render::Text::FontKind::Normal;
    sprites.SetColor(White);
    return {};
}

// 0x6304f2..0x6305c7: el marco del 0x91. Con 0x6c7b8c en 1 (1 en la .data; su único escritor, 0x4b93a0, pone 1)
// dibuja el EDIT del diálogo, que es de la CL9.4, y corta; la rama del 0 (0x6305eb..0x6306e4) no corre (07 Q-C953).
// Detrás quedan el OK (0xf1 o 0xf2 en (255, 160)) y el cancelar (0xf3 o 0xf4 en (330, 160)) de 0x6306ee..0x630813,
// y en la cola (0x631d2f) el 1128 o el 1129 según 0x78aba60.
Core::Unexpected<std::string> DrawTheTypedText(Frame& frame)
{
    DrawTheFrame(frame, TypedTextTop, TypedTextRows);
    return Core::Unexpected(std::string("0x6305c9: el dibujo del 0x91 sigue con el EDIT del diálogo ([0x5763504], "
                                        "la vtable + 0x30 con 0 y + 0x14), que es de la CL9.4"));
}

// 0x630c51..0x630d4c: el marco del OK genérico y su botón.
void DrawTheGenericBox(Frame& frame)
{
    Quad(frame, MessageBoxSlot, BoxLeft, BoxTop, BoxWidth, GenericBoxHeight, MessageBoxDu, 1.0f);
    const std::uint16_t slot = MouseIn(frame, OkLeft, OkTop, OkRight, OkBottom) ? OkHoverSlot : OkSlot;
    Quad(frame, slot, OkX, OkY, OkWidth, OkHeight, OkDu, OkDv);
}

// La y del marco de los de sí y no ([EBP − 0x20], 0x630cc4..0x630cd8).
int YesNoTopOf(std::int32_t id)
{
    return id == UI::ServerTimeDialog ? ServerTimeBoxTop : YesNoBoxTop;
}

// 0x630e1f..0x630f50 y 0x630fdb..0x6310d0: un botón del 0xa8, su texto y la vuelta del color y de la mezcla.
void DrawTheLuckButton(Frame& frame, const UI::DialogButton& button, int top, std::string_view text)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    HudState& hud = frame.context.hud;
    sprites.SetBlend(BlendMode::Alpha);
    hud.dcFont = Render::Text::FontKind::Bold;
    Quad(frame, LuckButtonSlot, static_cast<float>(button.x) + BoxLeft, static_cast<float>(button.y + top),
         static_cast<float>(button.width), static_cast<float>(button.height), LuckButtonDu, 1.0f);
    const std::uint32_t saved = hud.textColors.text;
    hud.textColors.text = LuckButtonTextColor;
    hud.textColors.background = LuckButtonBackground;
    DrawCenteredAt(frame, button.x + LuckButtonTextX, top + button.y + LuckButtonTextDrop, text);
    hud.textColors.text = saved;
    sprites.SetBlend(BlendMode::Opaque);
}

// 0x630cc4..0x6310d5: el marco de los de sí y no (0x94, 0x96, 0x98, 0x99, 0x9a, 0xa3, 0xa8, 0xa9 y 0xaa) y sus botones
// con estado de 1 a 4; los del 0xa8 con su texto, y después su tercero.
Core::Expected<void, std::string> DrawTheYesNoBox(Frame& frame, std::int32_t id)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const int top = YesNoTopOf(id);
    const int rows = id == UI::ChaosCastleDialog ? ChaosCastleBoxRows : YesNoBoxRows;
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(top), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    int y = top + static_cast<int>(BoxEdgeHeight);
    for (int row = 0; row < rows; ++row, y += BoxRowStep)
        Quad(frame, BoxRowSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxRowHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    const UI::DialogBoxState& box = frame.runtime.dialogBox;
    for (std::size_t index = 0; index < box.buttons.size(); ++index)
    {
        if (id == UI::ChaosCastleDialog)
        {
            const bool noItem = frame.runtime.dialogItem.scalar1Low == NoDialogItem && index == 0;
            sprites.SetColor(noItem ? NoItemYesColor : White);
        }
        const UI::DialogButton& button = box.buttons[index];
        if (button.state < 1 || button.state > YesNoLastButtonState)
            continue;
        if (id == UI::ItemNameWithLuckDialog)
        {
            auto text = TextOf(frame, LuckButtonTexts[index]);
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            auto written = Fits(UI::Sprintf("%s", text.Value()), LuckButtonRoom, "0x630ed3");
            if (!written.HasValue())
                return Core::Unexpected(std::move(written).Error());
            DrawTheLuckButton(frame, button, top, written.Value());
            continue;
        }
        const float width = static_cast<float>(button.width);
        const float height = static_cast<float>(button.height);
        Quad(frame, static_cast<std::uint16_t>(MessageBoxSlot + button.state), static_cast<float>(button.x) + BoxLeft,
             static_cast<float>(button.y + top), width, height, width * ButtonDu, height * ButtonDv);
    }
    if (id != UI::ItemNameWithLuckDialog)
        return {};
    auto third = TextOf(frame, LuckThirdButtonText);
    if (!third.HasValue())
        return Core::Unexpected(std::move(third).Error());
    DrawTheLuckButton(frame, box.thirdButton, top, third.Value());
    return {};
}

// 0x632683..0x632877: el título de la mezcla, del tipo (0x7d2dc98; ECX es el tipo, o 0 si es negativo, y 7 con el 0xb).
// Cada texto es el formato de un sprintf; el 0x1c con otra capa (0x7d2dc84) que la 3 o la 4 deja el buffer como
// estaba en la pila, y corta.
Core::Expected<std::string, std::string> TheMixTitle(const Frame& frame)
{
    const NpcWindowState& npc = frame.runtime.npcWindows;
    const std::int32_t kind = npc.mixKind;
    const auto format = [&frame](std::size_t index) { return TextOf(frame, index); };
    std::size_t index = 0;
    switch (kind)
    {
    case 0xc:
        index = 618;
        break;
    case 0x16:
    case 0x17:
    {
        auto creative = format(664);
        if (!creative.HasValue())
            return Core::Unexpected(std::move(creative).Error());
        return UI::Sprintf(creative.Value(), kind - 10);
    }
    case 0xf:
        index = 1413;
        break;
    case 0x10:
        index = 1414;
        break;
    case 0x11:
        index = 1415;
        break;
    case 0x19:
        index = 1921;
        break;
    case 0x1a:
        index = 1922;
        break;
    case 0x1b:
        index = 1923;
        break;
    case 0x1c:
    {
        // 0x6327bb..0x63280f: "%s(%s)" (0x6d6344 y 0x6d634c) con el 1923 y el 1924 (capa 3) o el 1925 (capa 4).
        if (npc.mixCapeKind != 3 && npc.mixCapeKind != 4)
            return Core::Unexpected(std::format("0x6327eb: el título del 0x1c con la capa {} deja el buffer de la pila "
                                                "sin escribir",
                                                npc.mixCapeKind));
        auto first = format(1923);
        auto cape = format(npc.mixCapeKind == 3 ? 1924 : 1925);
        if (!first.HasValue())
            return Core::Unexpected(std::move(first).Error());
        if (!cape.HasValue())
            return Core::Unexpected(std::move(cape).Error());
        return UI::Sprintf("%s(%s)", first.Value(), cape.Value());
    }
    case 0x18:
        index = 1172;
        break;
    default:
    {
        // 0x6326b9..0x6326cd y 0x63282c..0x632837: el 601 + ECX hasta 4 y el 607 + ECX después (el 0xb, con 7, el
        // 614).
        const std::int32_t shown = kind == 0xb ? 7 : std::max(kind, 0);
        index = static_cast<std::size_t>(shown) + (shown <= 4 ? 601u : 607u);
        break;
    }
    }
    auto text = format(index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    return UI::Sprintf(text.Value());
}

// 0x632637..0x632a2a: los renglones de los de sí y no que van a 0x632637, cada uno centrado en su x. No vuelve al
// blanco ni a la fuente normal. Con 0x78abb14 en 2, salvo en el 0x96 y el 0x9a, antes va el título de la mezcla.
Core::Expected<void, std::string> DrawTheYesNoRows(Frame& frame, std::int32_t id)
{
    const std::int32_t choice = frame.runtime.npcWindows.cursorChoice;
    const int top = YesNoTopOf(id) + YesNoTextDrop;
    int x = YesNoTextX;
    int y = top + YesNoTextDrop;
    Render::SpriteBatch& sprites = frame.context.sprites;
    if (id == UI::ChaosCastleDialog)
        x = CenterX;
    else if (id != UI::ItemNameDialog)
    {
        if (id == UI::ServerTimeDialog)
            x = ServerTimeTextX;
        if (choice == MixChoice)
        {
            x = CenterX;
            const int titleY = top + MixTitleDrop;
            frame.context.hud.dcFont = Render::Text::FontKind::Bold;
            sprites.SetColor(MixTitleColor);
            auto title = TheMixTitle(frame);
            if (!title.HasValue())
                return Core::Unexpected(std::move(title).Error());
            auto written = Fits(std::move(title).Value(), MixTitleRoom, "0x632855");
            if (!written.HasValue())
                return Core::Unexpected(std::move(written).Error());
            DrawCentered(frame, titleY, written.Value());
            y = titleY + MixTitleRowsDrop;
            frame.context.hud.dcFont = Render::Text::FontKind::Normal;
        }
        else if (choice == RaisedTextChoice)
        {
            x = CenterX;
            y = top;
        }
        else if (choice == LoweredTextChoice)
        {
            x = CenterX;
            y = top + LowerTextDrop;
        }
    }
    const UI::DialogBoxState& box = frame.runtime.dialogBox;
    for (std::int32_t row = 0; row < box.lineCount; ++row, y += UI::DialogLineStep)
    {
        sprites.SetColor(YesNoRowColor);
        frame.context.hud.dcFont = Render::Text::FontKind::Bold;
        if (choice == OnlyFirstRowBoldChoice)
        {
            if (row == 0)
                sprites.SetColor(RedRowColor);
            else
            {
                frame.context.hud.dcFont = Render::Text::FontKind::Normal;
                sprites.SetColor(White);
            }
        }
        else
        {
            if (row == 0 && choice == RedFirstRowChoice)
                sprites.SetColor(RedRowColor);
            else if (row == RedFourthRow && choice == RaisedTextChoice)
                sprites.SetColor(RedFourthRowColor);
            if (choice == MixChoice)
                sprites.SetColor(MixRowColor);
        }
        DrawCenteredAt(frame, x, y, UI::DialogRow(box, static_cast<std::size_t>(row)));
    }
    return {};
}

// 0x632a45..0x632aeb: los renglones del 0xa9, y al final la fuente normal (sin volver al blanco).
void DrawTheBlessRows(Frame& frame)
{
    const UI::DialogBoxState& box = frame.runtime.dialogBox;
    frame.context.hud.dcFont = Render::Text::FontKind::Bold;
    frame.context.sprites.SetColor(BlessRowColor);
    int y = LowerYesNoTextY;
    for (std::int32_t row = 0; row < box.lineCount; ++row, y += UI::DialogLineStep)
        DrawCentered(frame, y, UI::DialogRow(box, static_cast<std::size_t>(row)));
    frame.context.hud.dcFont = Render::Text::FontKind::Normal;
}

// Los dibujos de la mezcla de joyas, por diálogo.
using JewelDrawing = Core::Expected<void, std::string> (*)(Frame&);
std::optional<JewelDrawing> JewelDrawingOf(std::int32_t id)
{
    switch (id)
    {
    case JewelMenuDialog:
        return &DrawTheJewelMenu;
    case JewelChoiceDialog:
        return &DrawTheJewelChoice;
    case JewelUnmixDialog:
        return &DrawTheJewelUnmix;
    case JewelWaitDialog:
        return &DrawTheJewelWait;
    case JewelResultDialog:
        return &DrawTheJewelResult;
    default:
        return std::nullopt;
    }
}

// El corte de un diálogo que no es de la CL8, con la ola de su paso.
Core::Unexpected<std::string> CutTheDialog(std::int32_t id)
{
    const DialogRoute route = DialogRouteOf(id);
    std::string_view wave = "CL9";
    if (route.step == DialogStep::Cut)
        wave = route.wave;
    // 0x630c7b: el 0x73 dibuja el EDIT de su campo (0x5763504), como corta su paso.
    if (id == 0x73)
        wave = "CL9.4";
    return Core::Unexpected(std::format("0x62db60: el dibujo del diálogo {:#x} es de la {}", id, wave));
}
} // namespace

Core::Expected<void, std::string> DrawTheGameDialogs(GameHudContext& context)
{
    Frame frame{.context = context, .runtime = context.world.Runtime()};
    Render::SpriteBatch& sprites = context.sprites;
    // 0x62db60..0x62dba1: 0x6c8e70 en 0x100 (lo lee 0x553ae0, que acá no corre), sin mezcla, en blanco, la fuente
    // normal en el DC y los colores del texto.
    sprites.SetBlend(BlendMode::Opaque);
    sprites.SetColor(White);
    context.hud.dcFont = Render::Text::FontKind::Normal;
    context.hud.textColors = DialogTextColors;
    const std::int32_t id = frame.runtime.dialogs.current;
    std::optional<GenericText> generic{};
    for (const GenericText& entry : GenericTexts)
    {
        if (entry.id == id)
            generic = entry;
    }
    const bool box = id == UI::MessageBoxDialog || id == UI::PagedDialog || id == UI::AnswersDialog || id == 0x8b ||
                     id == 0x97 || id == 0x9b;
    // 0x630477..0x6304d4: los de sí y no van a 0x630cc4.
    const bool yesNo = std::ranges::find(YesNoDialogs, id) != YesNoDialogs.end();
    if (id == UI::EscapeMenuDialog)
    {
        if (auto drawn = DrawTheEscapeMenu(frame); !drawn.HasValue())
            return drawn;
    }
    else if (id == UI::OptionsDialog)
    {
        if (auto drawn = DrawTheOptions(frame); !drawn.HasValue())
            return drawn;
    }
    else if (id == ChaosMixModeDialog)
    {
        if (auto drawn = DrawTheMixModes(frame); !drawn.HasValue())
            return drawn;
    }
    else if (const auto jewel = JewelDrawingOf(id))
    {
        if (auto drawn = (*jewel)(frame); !drawn.HasValue())
            return drawn;
    }
    else if (id == TypedTextDialog)
        return DrawTheTypedText(frame);
    else if (box)
        DrawTheBox(frame, id);
    else if (yesNo)
    {
        if (auto drawn = DrawTheYesNoBox(frame, id); !drawn.HasValue())
            return drawn;
    }
    else if (generic)
        DrawTheGenericBox(frame);
    else if (id != 0)
        return CutTheDialog(id);
    // 0x63140b: la mezcla normal y los textos de la cola.
    sprites.SetBlend(BlendMode::Alpha);
    // 0x631432: la tabla 0x632bf0 lleva el 0x8e a 0x632323, el 0x8a, el 0x8c, el 0x8d y el 0xaa a 0x632472, y las otras
    // cajas a 0x632a2b.
    if (id == ChaosMixModeDialog)
        return DrawTheHeadTitle(frame, MixModeTitle);
    // 0x632372 y 0x6323c1; el 0xa5 y el 0xa6 van a 0x632aec, sin nada.
    if (id == JewelMenuDialog || id == JewelChoiceDialog)
        return DrawTheHeadTitle(frame, MixText);
    if (id == JewelUnmixDialog)
        return DrawTheHeadTitle(frame, UnmixText);
    if (id == UI::MessageBoxDialog || id == UI::PagedDialog || id == UI::AnswersDialog ||
        id == UI::NoticeQuestionDialog)
        return DrawTheBoxText(frame, id);
    // 0x632637 y 0x632a45: los renglones de los de sí y no.
    if (yesNo && id == BlessDialog)
    {
        DrawTheBlessRows(frame);
        return {};
    }
    if (yesNo)
        return DrawTheYesNoRows(frame, id);
    if (box)
    {
        // 0x632a2b: [0x723c60] + 0x10, el texto de la caja del objeto del evento (0x40bdd0, 0x40c440 o 0x405220).
        // Sin el objeto, nada.
        if (frame.runtime.eventObject)
            return Core::Unexpected(std::format("0x632a2b: el texto de la caja {:#x} del evento es de la CL9.5", id));
    }
    else if (generic)
    {
        if (!generic->second)
            return DrawCenteredText(frame, GenericTextY, generic->first);
        if (auto drawn = DrawCenteredText(frame, GenericFirstRowY, generic->first); !drawn.HasValue())
            return drawn;
        return DrawCenteredText(frame, GenericSecondRowY, *generic->second);
    }
    return {};
}
} // namespace Client::Scenes
