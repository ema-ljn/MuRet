#include "Hud/Dialogs/GameDialogDrawing.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Events/EventDrawing.h"
#include "Hud/Dialogs/GameDialogSteps.h"
#include "Ui/GameHudContext.h"
#include "Guild/GuildState.h"
#include "Render/Frame/ITextDrawer.h"
#include "ChaosMix/JewelBundleList.h"
#include "Npc/NpcWindowState.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/HudPrimitives.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Chat/ChatRuntime.h"
#include "Events/EventMapObject.h"

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

namespace Client::Hud::Dialogs
{
namespace
{
using Render::BlendMode;
using Render::Color;

// 0x62db60's glColor3f: white and black; the hovered button (0.8; 0.6; 0.4); the gray of the bars' frame and of the
// arrow that does not go (0.2); the glow boxes that are on and the digits of those that are off (0.8; 0.6; 0.3); and
// the arrow hovered (0.7) or pressed (0.5).
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
constexpr Color HoverColor{0.8f, 0.6f, 0.4f, 1.0f};
constexpr Color FrameGrey{0.2f, 0.2f, 0.2f, 1.0f};
constexpr Color GlowColor{0.8f, 0.6f, 0.3f, 1.0f};
constexpr Color ArrowHover{0.7f, 0.7f, 0.7f, 1.0f};
constexpr Color ArrowPressed{0.5f, 0.5f, 0.5f, 1.0f};

// 0x62dba1 and 0x62db9b: the text colors (0x6c8e5c at −1 and 0x6c8e64 at 0).
constexpr Render::Text::TextColors DialogTextColors{.text = 0xffffffffu, .background = 0};

// 0x61ccb0's slots: Message_box (0xf0), ok, ok2 (0xf1 and 0xf2), Message_box1 (0xfb), Message_box2 (0xfc) and
// CursorSitDown (9), the page arrow.
constexpr std::uint16_t MessageBoxSlot = 0xf0;
constexpr std::uint16_t OkSlot = 0xf1;
constexpr std::uint16_t OkHoverSlot = 0xf2;
constexpr std::uint16_t BoxRowSlot = 0xfb;
constexpr std::uint16_t BoxEdgeSlot = 0xfc;
constexpr Render::TextureSlot ArrowSlot{9};

// Half of 640: 0x554180's centered texts (0x140 − (cx · 640 / W) / 2).
constexpr int CenterX = 0x140;

// 0x62dbf0..0x62dcdc and 0x62df4a..0x62e04e: the menu buttons and the option rows, 0xf0 at (260, y, 120, height)
// with the UVs (0, 0, 0.83203125, 1), and the mouse over them with x in (0x103, 0x17c) and y in [y, y + height).
constexpr float MenuButtonLeft = 260.0f;
constexpr float MenuButtonWidth = 120.0f;
constexpr float MessageBoxDu = 0.83203125f;
constexpr int MenuHoverLeft = 0x103;
constexpr int MenuHoverRight = 0x17c;
// 0x62dbd6..0x62dbe9: 5 buttons in scene 5, from y = 60 every 30, 22 high.
constexpr int MenuButtons = 5;
constexpr int MenuFirstY = 60;
constexpr int MenuButtonStep = 30;
constexpr int MenuButtonHeight = 22;
// 0x62de0c..0x62df1a: the menu texts (Text.bmd) and their y, in the order they come out.
constexpr std::array<std::size_t, 5> MenuTexts{381, 382, 383, 385, 384};
constexpr std::array<int, 5> MenuTextYs{0x41, 0x5f, 0x7d, 0x9b, 0xb9};

// 0x62df4a..0x62df8c: 7 rows at y = (3i + 3) · 10, 22 high; row 5 is 44 high and row 6 is at y = 232.
constexpr int OptionRows = 7;
constexpr int TallOptionRow = 5;
constexpr int TallOptionRowHeight = 44;
constexpr int LastOptionRow = 6;
constexpr int LastOptionRowY = 232;
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
// 0x6d6298..0x6d62c0: what the three switches' formats ("%s  On" and "%s  Off") put after the text.
constexpr std::string_view OnSuffix = "  On";
constexpr std::string_view OffSuffix = "  Off";
// 0x62e1f1..0x62e2a8: the volume text at x = 0x140 − (cx + 0x36) / 2 and the bar at (x + 0x144 + cx, 0x7f), with cx
// in 640 × 480: the gray 50 × 8 frame, the black 48 × 6 background and the fill, (volume + 1) · 4.8 (0x6b8bf0).
constexpr std::uint32_t DesignWidth = 640;
constexpr int VolumeTextSpace = 0x36;
constexpr int VolumeBarOffset = 0x144;
constexpr int VolumeBarTop = 0x7f;
constexpr float VolumeBarWidth = 50.0f;
constexpr float VolumeBarHeight = 8.0f;
constexpr float VolumeFillWidth = 48.0f;
constexpr float VolumeFillHeight = 6.0f;
constexpr float VolumeStepWidth = 4.8f;
// 0x62e33b..0x62e45a: the glow frame, gray at (268, 198, 102, 22) and black at (269, 199, 100, 20); the 5 boxes of 18
// from x = 0x10e every 0x14 at y = 200; and the digits 5, 7, 9, 11 and 13, 12 × 12, from x = 0x118 at y = 202.
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

// 0x6310da..0x631202: the boxes' frame, 0xfc of 213 × 5 at (213, 60), rows of 0xfb of 213 × 40 from y = 65 (by the
// table 0x632b00: 4 in 0x8a, 0x8c and 0x9b; those of the text and the answers in 0x8d; 7 in the rest) and 0xfc again,
// with the UVs (0, 0, 0.83203125, 0.625).
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
// 0x631150..0x631182: 0x8d's, ((lines + answers) · 0x12 + 0x31) / 40, from 4 to 9.
constexpr int AnswersBoxRowsMaximum = 9;
constexpr int AnswersBoxRowsRound = 0x31;
// 0x631205..0x63127c: each button with a state, 0xf0 + state at (x + 213, y + 60, w, h − 1) with the UVs (0, 0,
// w / 128, h / 32) (0x6ba7a8, 0x6b86cc, 0x6b87d4, 0x6b8b08 and 0x6b8880).
constexpr float ButtonDu = 0.0078125f;
constexpr float ButtonDv = 0.03125f;
// 0x631297..0x6313fa: 0x8c's arrows, the sit-down arrow rotated 270 and 90 degrees, 25 × 16, centered at (253, 210)
// and (383, 210), and the mouse over them with x in (0xf8, 0x109) or (0x178, 0x189) and y in (0xc9, 0xdf).
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
// 0x632548..0x632596: 0x8d's answers in yellow, and the hovered one in red.
constexpr Color AnswerColor{1.0f, 1.0f, 0.0f, 1.0f};
constexpr Color HoveredAnswerColor{1.0f, 0.0f, 0.0f, 1.0f};

// 0x630818..0x63084c and 0x63093e..0x6309cf: the generic OK's frame, 0xf0 of 213 × 64 at (213, 60), and the OK, 0xf1
// (0xf2 with the mouse in [0x11c, 0x161] × [0x62, 0x76]) of 70 × 21 at (284, 98) with the UVs (0, 0, 0.546875,
// 0.65625). Its texts go from y = 60 + 0xf, or at 60 + 9 and 60 + 0x15 the two-line ones.
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

// 0x630cc4..0x630da7: the yes/no dialogs' frame (CL9 milestone 6), 0xfc of 213 × 5 at (213, 100), 3 rows of 0xfb of
// 213 × 40 from y = 105 and 0xfc again, with the boxes' UVs. 0xa3 puts it at y = 210 (0x630cc9..0x630cd3) and 0x9a has
// 4 rows (0x630d20..0x630d2c).
constexpr int YesNoBoxTop = 0x64;
constexpr int ServerTimeBoxTop = 0xd2;
constexpr int YesNoBoxRows = 3;
constexpr int ChaosCastleBoxRows = 4;
// 0x630db4..0x630deb: in 0x9a, before each button, the yes in (1; 0.3; 0.3) without the item (the dword 0x78a59cc,
// +4 of 0x78a59c8, at 0xff) and otherwise in white. The color stays.
constexpr std::uint32_t NoDialogItem = 0xff;
constexpr Color NoItemYesColor{1.0f, 0.3f, 0.3f, 1.0f};
// 0x630e1f..0x630f53 and 0x630fdb..0x6310d5: 0xa8's three buttons, with the normal blend and in bold, 0x646 at
// (x + 213, y + 100, w, h) with the UVs (0, 0, 0.765625, 1), and their text ("%s" of 1412 and 1903 in [EBP − 0x100],
// of 100 bytes; 229 as is for the third) at x + 0xed − cx / 2 and y + 100 + 4, with 0x6c8e5c at 0xff000000 and
// 0x6c8e64 at 0xff. The text color comes back, not the background's, and the bold stays. The third (0x7d36908) is
// drawn without looking at its state.
constexpr std::uint16_t LuckButtonSlot = 0x646;
constexpr float LuckButtonDu = 0.765625f;
constexpr int LuckButtonTextX = 0xed;
constexpr int LuckButtonTextDrop = 4;
constexpr std::uint32_t LuckButtonTextColor = 0xff000000u;
constexpr std::uint32_t LuckButtonBackground = 0xffu;
constexpr std::array<std::size_t, 2> LuckButtonTexts{1412, 1903};
constexpr std::size_t LuckThirdButtonText = 229;
constexpr std::size_t LuckButtonRoom = 100;
// 0x630df1..0x630fbe: the two buttons with state 1 to 4, 0xf0 + state at (x + 213, y + 100, w, h) with the UVs (0, 0,
// w / 128, h / 32): without the boxes' height − 1 (0x631245).
constexpr std::int32_t YesNoLastButtonState = 4;
// 0x631412..0x631418 and 0x632637..0x632905: the lines start at x = 0x15e and y = top + 0xf + 0xf; with 0x78abb14 at
// 3, at x = 0x140 and y = top + 0xf, and at 4, at x = 0x140 and y = top + 0xf + 0x1e. 0x96 is not moved; 0x9a goes at
// x = 0x140 regardless of 0x78abb14 and 0xa3 at x = 0x145, unless 0x78abb14 moves it.
constexpr int YesNoTextDrop = 0xf;
constexpr int YesNoTextTop = YesNoBoxTop + YesNoTextDrop;
constexpr int YesNoTextX = 0x15e;
constexpr int ServerTimeTextX = 0x145;
constexpr int LowerTextDrop = 0x1e;
constexpr int LowerYesNoTextY = YesNoTextTop + LowerTextDrop;
// 0x632683..0x6328e3: the mix title, in bold and in (1, 1, 0), centered at y = top + 0xf + 10, with the sprintf of the
// kind's text (0x7d2dc98) in [EBP − 0x9c], of 100 bytes; the lines follow 0x22 lower, at 0x140.
constexpr int MixTitleDrop = 10;
constexpr int MixTitleRowsDrop = 0x22;
constexpr Color MixTitleColor{1.0f, 1.0f, 0.0f, 1.0f};
constexpr std::size_t MixTitleRoom = 100;
// What 0x632637 reads from 0x78abb14 (TradeState::warningKind).
constexpr std::int32_t RedFirstRowChoice = 1;
constexpr std::int32_t MixChoice = 2;
constexpr std::int32_t RaisedTextChoice = 3;
constexpr std::int32_t LoweredTextChoice = 4;
constexpr std::int32_t OnlyFirstRowBoldChoice = 6;
// 0x632922..0x6329bf: each line in the bold font and (1; 0.7; 0); with 0x78abb14 at 6, the first in red and the rest
// in the normal font and white; otherwise, the first in red with 1, the fourth in (1; 0.1; 0.1) with 3, and all in
// (0.9; 0.9; 1) with 2.
constexpr std::int32_t RedFourthRow = 3;
constexpr Color YesNoRowColor{1.0f, 0.7f, 0.0f, 1.0f};
constexpr Color RedRowColor{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color RedFourthRowColor{1.0f, 0.1f, 0.1f, 1.0f};
constexpr Color MixRowColor{0.9f, 0.9f, 1.0f, 1.0f};
// 0x630477..0x6304d4: the yes/no dialogs, which 0x630cc4 draws (0x94 enters through 0x630cbd, without looking at
// 0xa3).
constexpr auto YesNoDialogs =
    std::to_array<std::int32_t>({0x94, Ui::ItemNameDialog, 0x98, Ui::MoneyTextDialog, Ui::ChaosCastleDialog,
                                 Ui::ServerTimeDialog, Ui::ItemNameWithLuckDialog, 0xa9, Ui::NoticeQuestionDialog});
// 0x632a45..0x632aeb: 0xa9's lines (the jewel of bless, CL9 milestone 5) in the bold font and yellow, centered from
// y = 100 + 0xf + 0x1e.
constexpr std::int32_t BlessDialog = 0xa9;
constexpr Color BlessRowColor{1.0f, 1.0f, 0.0f, 1.0f};

// 0x62e65c..0x62e929: the chaos box mode (0x8e). The frame, 0xfc of 213 × 5 at (213, 120), 5 rows of 0xfb of 213 × 40
// from y = 125 and 0xfc again; the two buttons, 0xf0 of 150 × 35 at (245, 190 + 85i) with the UVs (0, 0, 0.83203125,
// 1), white with the mouse in [245, 395] × [y, y + 35] (edges included) and 0.7 otherwise; their title (735 + i) in
// bold, centered at y + 12, with 0xfffff0c8; and in the normal font, in white, each one's explanation at x = 0xf8
// (872, 873 and 1923 at y = 150, 160 and 170; 870 and 871 at 240 and 250).
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
// 0x632323..0x63236d: the title, 734 in bold with 0xff0080ff, centered at y = 120 + 0xf.
constexpr std::size_t MixModeTitle = 734;
constexpr std::uint32_t MixModeHeadColor = 0xff0080ffu;
constexpr int MixModeTitleY = MixModeTop + 0xf;
// 0x62db60's tail (0x632323, 0x632372, ..) has the titles of the CL9.3 dialogs; those not from this phase stop in
// their drawing.
constexpr std::int32_t ChaosMixModeDialog = 0x8e;

// The jewel mix (CL9 milestone 6, phase 4a): the menu (0xa1, 0x62e92e..0x62ed05), the jewel and the bundle (0xa2,
// 0x62ed0a..0x62f36c), unmix (0xa4, 0x62f371..0x62fae6), the BC wait (0xa5, 0x62faeb..0x62fc1a) and the result (0xa6,
// 0x62fc1f..0x62febe). All with [EBP − 0x20] at 0x78: the frame from y = 120, like 0x8e.
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
// The buttons' colors: hovered, white; otherwise 0.7 (0.8 in 0xa1's cancel and in 0xa6's OK); the chosen jewel,
// (1; 1; 0.2), and unchosen, 0.5.
constexpr Color JewelIdle = MixModeIdle;
constexpr Color JewelLightIdle{0.8f, 0.8f, 0.8f, 1.0f};
constexpr Color JewelChosen{1.0f, 1.0f, 0.2f, 1.0f};
constexpr Color JewelDisabled{0.5f, 0.5f, 0.5f, 1.0f};
constexpr std::uint32_t JewelButtonTextColor = MixModeTitleColor;
constexpr int JewelCancelCenterX = 0x13f;
constexpr std::size_t JewelCancelText = 1002;
constexpr float JewelButtonHeight = 25.0f;
// 0x62ea12..0x62ebd1: 0xa1's two buttons, 150 × 35 at x = 245, from y = 190 every 65; 1801 (mix) and 1800 (unmix) at
// y + 12; and before the first, 1802 and 1803 at x = 0xf8.
constexpr std::size_t MixText = 1801;
constexpr std::size_t UnmixText = 1800;
constexpr float JewelMenuButtonTop = 190.0f;
constexpr float JewelMenuButtonStep = 65.0f;
constexpr float JewelMenuTextDrop = 12.0f;
constexpr int JewelMenuNotesX = 0xf8;
constexpr std::array<MixModeNote, 2> JewelMenuNotes{{{0x96, 1802}, {0xa0, 1803}}};
// 0x62ebd7..0x62ecf2: 0xa1's cancel, 81 × 25 at (279, 320), its text at y = 0x148.
constexpr int JewelCancelLeft = 279;
constexpr int JewelCancelRight = 360;
constexpr float JewelCancelWidth = 81.0f;
constexpr int JewelMenuCancelTop = 320;
constexpr int JewelMenuCancelTextY = 0x148;
// 0x62edee..0x62f08f: 0xa2's two jewels (1806 and 1807), 80 × 25 at x = 235 + ftol(90 · k), y = 185, with the text
// centered at ftol(x + 275) and at y + 8; before them, 1804 and 1805 at x = 0xee. The step chooses from 245 + 80 · k
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
// 0x62f0bd..0x62f271: the three bundles, 170 × 35 at x = 235 every 45, with 1808 written with 10 · i jewels and
// 500000 · i zen into a 0x800-byte buffer, centered at y + 12.
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
// 0x62f27e..0x62f36c: 0xa2's cancel at (279, 365), its text at y = 0x175.
constexpr int JewelChoiceCancelTop = 365;
constexpr int JewelChoiceCancelTextY = 0x175;
// 0x62f4ae..0x62f689: 0xa4's two jewels, 80 × 25 from x = 235 every 80, at y = 160, with the text at ftol(x + 40) and
// y = 0xa8.
constexpr float JewelUnmixKindLeft = 235.0f;
constexpr int JewelUnmixKindTop = 160;
constexpr float JewelUnmixKindStep = 80.0f;
constexpr float JewelUnmixKindCenter = 40.0f;
constexpr int JewelUnmixKindTextY = 0xa8;
// 0x62f9c6..0x62fae6: without a jewel, the 80 × 25 cancel at (279.5, 200), its text at y = 0xd0.
constexpr float JewelUnmixCancelLeft = 279.5f;
constexpr float JewelUnmixCancelRight = 359.5f;
constexpr int JewelUnmixCancelTop = 200;
constexpr int JewelUnmixCancelTextY = 0xd0;
// 0x62f69c..0x62f715: with the jewel, 1821 with the free cells (0x402de0) in a 300-byte buffer and 1822, at x = 0xe4
// with 0xc8c8c8c8, in the font the last jewel left (07 Q-C950).
constexpr std::size_t FreeCellsText = 1821;
constexpr std::size_t FreeCellsRoom = 300;
constexpr std::size_t UnmixListText = 1822;
constexpr int UnmixNotesX = 0xe4;
constexpr int FreeCellsY = 0xc5;
constexpr int UnmixListTextY = 0xd4;
constexpr std::uint32_t UnmixNotesColor = 0xc8c8c8c8u;
// 0x62f71a..0x62f9b7: 0x6e5660's list with its bottom corner at (0xe4, height + 0xf0); 10 lower, the OK (188,
// centered at x = 0x112) and the cancel (1002, at 0x16c), 80 × 25 from x = 234.5 and 324.5, with the text 8 lower.
constexpr std::int32_t UnmixListX = 0xe4;
constexpr std::int32_t UnmixListBottom = 0xf0;
constexpr float UnmixButtonsDrop = 10.0f;
constexpr float UnmixOkLeft = 234.5f;
constexpr float UnmixCancelLeft = 324.5f;
constexpr float UnmixCancelRight = 404.5f;
constexpr float UnmixButtonTextDrop = 8.0f;
constexpr std::size_t UnmixOkText = 188;
constexpr int UnmixOkCenterX = 0x112;
constexpr int UnmixCancelCenterX = 0x16c;
constexpr std::uint16_t EmptyCell = 0xffff;
// 0x62fbc9..0x62fc12: 0xa5's 471, at y = 0xa5.
constexpr std::size_t JewelWaitText = 471;
constexpr int JewelWaitTextY = 0xa5;
// 0x62fd09..0x62febe: 0xa6. "%s%s %s" (0x6d62c8 and 0x6d62d0, equal) with 1801 or 1800 by 0x6e5794, 1816 and 858 in
// a 256-byte buffer, in bold at y = 0x91; the 65 × 25 OK at (287, 170) with 228 at y = 0xb2.
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

// 0x6304da..0x6304ed and 0x471ae0 (CL9 milestone 7): the confirmation box 0x9c. The frame, 0xfc of 213 × 5 at
// (213, 120), 0xfb of 213 × (h + 40) at (213, 125) and 0xfc again at (213, h + 40 + 125) (0x6b9bf0), with
// h = (letter step + 2) · lines and the boxes' UVs. The buttons, 52 × 16 with the OK's UVs at y = h + 140 (0x6b86c8
// and 0x6b86d0; 0x6b8ca8 for the unhovered yes with one button), hovered from y = h + 140 to h + 156 (0x6b885c),
// compared on the x87 with the edges: with two, the yes at x 255 (0xff to 0x133) and the no at 330 (0x14a to 0x17e);
// with one, the yes at 295 (0x127 to 0x15b). The lines, centered (0x623690) from y = 0x87 every (step + 2).
constexpr std::int32_t ConfirmDialog = 0x9c;
constexpr std::int32_t ConfirmLineGap = 2;
constexpr float ConfirmEdgeTop = 120.0f;
constexpr float ConfirmRowsTop = 125.0f;
constexpr std::int32_t ConfirmRowsExtra = 0x28;
constexpr double ConfirmBoxTop = 120.0;
constexpr double ConfirmButtonTop = 20.0;
constexpr double ConfirmButtonBottom = 36.0;
constexpr float ConfirmButtonWidth = 52.0f;
constexpr float ConfirmButtonHeight = 16.0f;
constexpr float ConfirmYesX = 255.0f;
constexpr float ConfirmNoX = 330.0f;
constexpr float ConfirmOkX = 295.0f;
constexpr int ConfirmYesLeft = 0xff;
constexpr int ConfirmYesRight = 0x133;
constexpr int ConfirmNoLeft = 0x14a;
constexpr int ConfirmNoRight = 0x17e;
constexpr int ConfirmOkLeft = 0x127;
constexpr int ConfirmOkRight = 0x15b;
constexpr std::int32_t ConfirmTwoButtons = 2;
constexpr std::int32_t ConfirmOneButton = 1;
constexpr int ConfirmTextTop = 0x87;

// 0x63140b..0x631d66: the Text.bmd texts of the generic OK dialogs that reach CL8, one or two lines, and the guild
// creation notices (CL9 milestone 8, from 0x654f80 and 0x655c10): the filtered name (0x72, 0x6314a3: 392 and 393),
// short (0x79, 0x63143f: 390), with a bad byte (0x7a, 0x631471: 391) and the empty mark (0x7e, 0x631f05: 426). Their
// frame and OK are the generic's (0x630818 and 0x63093e). 0x87 and 0x88 are opened by the warehouse password box
// (0x5e7d53, 0x5e7e83; CL9 milestone 6): the table 0x632bf0 takes them to 0x632290 and 0x6322c1, one line each. The
// zen notices (0x74, no zen, 0x631dc5; 0x75, the trade cap, 0x631df7) and the warehouse state ones of 83 (0x85, 0x86
// and 0x89: 0x63222e, 0x63225f and 0x6322f2) too, with one line: 0x631d66 and 0x631520 are the same x. 0x90 (99 with
// 1, 0x632441) draws like 0x8f, but its line is `fixed`, the .data string 0x6d6320.
struct GenericText
{
    std::int32_t id = 0;
    std::size_t first = 0;
    std::optional<std::size_t> second{};
    // Without Text.bmd: the line's bytes, from the .data of main.exe c32bbd5f.
    std::string_view fixed{};
};
// 0x6d6320: "서버 이전시 오류 발생 : 문의 바람" (a server migration error), in Korean (EUC-KR), as its bytes.
constexpr std::string_view ServerMoveErrorText =
    "\xbc\xad\xb9\xf6 \xc0\xcc\xc0\xfc\xbd\xc3 \xbf\xc0\xb7\xf9 \xb9\xdf\xbb\xfd : "
    "\xb9\xae\xc0\xc7 \xb9\xd9\xb6\xf7";
constexpr auto GenericTexts = std::to_array<GenericText>({
    {Ui::DisconnectDialog, 402, std::nullopt},
    {0x6f, 405, 406},
    {0x1a, 405, 406},
    {0x1c, 409, std::nullopt},
    {0x87, 442, std::nullopt},
    {0x88, 445, std::nullopt},
    {0x8f, 401, std::nullopt},
    {0x72, 392, 393},
    {0x79, 390, std::nullopt},
    {0x7a, 391, std::nullopt},
    {0x7e, 426, std::nullopt},
    {0x74, 423, std::nullopt},
    {0x75, 424, std::nullopt},
    {0x85, 440, std::nullopt},
    {0x86, 441, std::nullopt},
    {0x89, 401, std::nullopt},
    {0x90, 0, std::nullopt, ServerMoveErrorText},
});

// 0x6309d4..0x630aeb: the yes and the no of 0x7f, 0x76, 0x77 and 0x78, 70 × 21 at y 98 with the OK's UVs: the yes,
// 0xf1 (0xf2 with the mouse in [0xea, 0x130) × [0x62, 0x77)) at x 234, and the no, 0xf3 (0xf4 with the mouse in
// [0x14e, 0x194)) at x 334.
constexpr std::uint16_t NoSlot = 0xf3;
constexpr std::uint16_t NoHoverSlot = 0xf4;
constexpr float YesX = 234.0f;
constexpr float NoX = 334.0f;
constexpr int YesLeft = 0xea;
constexpr int YesRight = 0x12f;
constexpr int NoLeft = 0x14e;
constexpr int NoRight = 0x193;
constexpr std::int32_t PartyInviteDialog = 0x77;
constexpr std::int32_t TradeInviteDialog = 0x78;
// The trade's (0x5ece58) and the warehouse's (0x5ea15f) zen: the generic OK's frame and the EDIT of [0x5763504].
constexpr std::int32_t MoneyDialog = 0x73;
// 0x63086c..0x6308c3: 0x73's EDIT, +0x4c at 9 (digits only, 1, and the draw's black background, 8), the cap of 8 (the
// vtable + 0x3c), 0x46 × 0xe (+ 8) at (0x11b, 0x5f) (0x45a9d0).
constexpr std::int32_t MoneyEditFilter = 9;
constexpr std::size_t MoneyEditLimit = 8;
constexpr std::int32_t MoneyEditWidth = 0x46;
constexpr std::int32_t MoneyEditHeight = 0xe;
constexpr std::int32_t MoneyEditX = 0x11b;
constexpr std::int32_t MoneyEditY = 0x5f;
// 0x631c91..0x631d27: 0x73's text by 0x78abacc: 420 with 0, 421 with 1 and 422 with another, at y = 60 + 0xf.
constexpr std::size_t MoneyZeroText = 420;
constexpr std::size_t MoneyOneText = 421;
constexpr std::size_t MoneyOtherText = 422;
// 0x6304f2..0x630813 (CL9 milestone 7): the personal shop's title or price. The frame, 0xfc of 213 × 5 at (213, 100),
// two rows of 0xfb of 213 × 40 at y = 105 and 145 and 0xfc again at (213, 185), with the boxes' UVs; the OK, 0xf1
// (0xf2 with the mouse in [0xff, 0x133] × [0xa0, 0xb0], signed) of 52 × 16 at (255, 160), and the cancel, 0xf3 (0xf4
// with the mouse in [0x14a, 0x17e] × [0xa0, 0xb0]) at (330, 160), with the OK's UVs. Their texts (0x631d2f) go at
// y = 100 + 0xf: 1128 with the title and 1129 with the price (0x78aba60); with another mode, none.
constexpr std::int32_t ShopDialog = 0x91;
constexpr float ShopBoxTop = 100.0f;
constexpr int ShopFirstRowY = 105;
constexpr int ShopRows = 2;
constexpr float ShopButtonY = 160.0f;
constexpr float ShopButtonWidth = 52.0f;
constexpr float ShopButtonHeight = 16.0f;
constexpr float ShopOkX = 255.0f;
constexpr float ShopCancelX = 330.0f;
constexpr int ShopOkLeft = 0xff;
constexpr int ShopOkRight = 0x133;
constexpr int ShopCancelLeft = 0x14a;
constexpr int ShopCancelRight = 0x17e;
constexpr int ShopButtonTop = 0xa0;
constexpr int ShopButtonBottom = 0xb0;
constexpr int ShopTextY = 100 + 0xf;
constexpr std::int32_t ShopTitleMode = 1;
constexpr std::int32_t ShopPriceMode = 2;
constexpr std::size_t ShopTitleText = 1128;
constexpr std::size_t ShopPriceText = 1129;
// 0x631e54 and 0x631bbb: the name's format (0x76dbbb4); 0x631ec0 and 0x631c49, 0x77's question (0x76dc3e8) and 0x78's
// (0x76dbce0). Each line comes from sprintf.
constexpr std::size_t InviterText = 418;
constexpr std::size_t TradeQuestionText = 419;
constexpr std::size_t PartyQuestionText = 425;
// 0x631418: the lines go from [EBP − 0x20] + 0xf (75): 0x77 at 75 − 6 and 75 + 6; 0x78 at 75 − 11 and 75 + 11.
constexpr int InvitationRowY = GenericTextTop + 0xf;
constexpr int PartyRowStep = 6;
constexpr int TradeRowStep = 0xb;
// 0x63212a..0x6321f8: the war request (0x7f, CL9 milestone 8): 430 (0x76dc9c4) with the rival at 75 − 6 and 431
// (0x76dcaf0) without the war type (0x576796d at 0) or 432 (0x76dcc1c) with it, at 75 + 6.
constexpr std::int32_t WarRequestDialog = Ui::IgnoredOverChaosCastle;
constexpr std::size_t WarRequestText = 430;
constexpr std::size_t WarQuestionText = 431;
constexpr std::size_t TypedWarQuestionText = 432;
constexpr int WarRowStep = 6;
// 0x5110d0: the 400 it walks; with none, 400.
constexpr std::size_t KeySearched = 400;

// 0x630af0..0x630cb8 (0x7d, CL9 milestone 8; also 0x71 and 0x95, from other waves): the number EDIT ([0x5763508])
// shown (+4(0)) with +0x4c at 9 (0x468477's black and digits only), the background at 0 (+0x44(0, 0, 0, 0)), cap 7
// (+0x3c), 0x46 × 0xe (+8) at (0xf3, 0x62) (0x45a9d0) and with the focus (+0x30(0)); the yes, 0xf1 (0xf2 with the
// mouse in [0x143, 0x16b) × [0x62, 0x77)) at x 323, and the no, 0xf3 (0xf4 in [0x175, 0x19d)) at x 373, 40 × 21 with
// the OK's UVs.
constexpr std::int32_t NumberDialog = 0x7d;
constexpr std::int32_t WarehouseNumberDialog = 0x95;
constexpr std::size_t WarehouseNumberFirstText = 443;
constexpr std::size_t WarehouseNumberSecondText = 444;
constexpr std::int32_t ShownField = 0;
constexpr std::int32_t NumberFieldFilter = 9;
constexpr std::size_t NumberFieldLimit = 7;
constexpr std::int32_t NumberFieldWidth = 0x46;
constexpr std::int32_t NumberFieldHeight = 0xe;
constexpr std::int32_t NumberFieldX = 0xf3;
constexpr std::int32_t NumberFieldY = 0x62;
constexpr float NarrowYesX = 323.0f;
constexpr float NarrowNoX = 373.0f;
constexpr float NarrowWidth = 40.0f;
constexpr int NarrowYesLeft = 0x143;
constexpr int NarrowYesRight = 0x16a;
constexpr int NarrowNoLeft = 0x175;
constexpr int NarrowNoRight = 0x19c;
// 0x631f36..0x631fbd: the text in red (0x6c8e5c = 0xff0000ff, which stays so) and 427 and 428 centered at 0x4b − 6
// and 0x4b + 6.
constexpr std::uint32_t NumberTextColor = 0xff0000ffu;
constexpr std::size_t NumberFirstText = 427;
constexpr std::size_t NumberSecondText = 428;
constexpr int NumberRowStep = 6;

struct Frame
{
    Ui::GameHudContext& context;
    Kernel::WorldRuntime& runtime;
};

bool MouseIn(const Frame& frame, int left, int top, int right, int bottom)
{
    const Ui::Point mouse = frame.context.mouse;
    return left <= mouse.x && mouse.x <= right && top <= mouse.y && mouse.y <= bottom;
}

// 0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0): scaled in size and position, without alpha.
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

// 0x459ec0
void WhiteWithTexture(Frame& frame)
{
    frame.context.sprites.SetColor(White);
    frame.context.sprites.EnableTexture();
}

Core::Expected<std::string_view, std::string> TextOf(const Frame& frame, std::size_t index)
{
    const std::optional<std::string_view> text = Ui::TextAt(frame.context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x62db60: sin el texto {} de Text.bmd", index));
    return *text;
}

// 0x554180(x, y, text, 0, 0, 0): with the DC's font and the colors of 0x6c8e5c and 0x6c8e64; an empty text is not
// drawn, but it is measured anyway and its size in 640 × 480 stays in 0x77f31b8 and 0x77f31bc.
void DrawText(Frame& frame, int x, int y, std::string_view text)
{
    Kernel::HudState& hud = frame.context.hud;
    if (!text.empty())
        frame.context.text.Draw(x, y, text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    const Render::Text::TextExtent extent = frame.context.text.Measure(hud.dcFont, text);
    const Core::PixelSize screen = frame.context.sprites.Screen();
    hud.arrowsTextWidth = Render::Text::DesignX(extent.width, screen);
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, screen);
}

// GetTextExtentPointA and 0x554180 at x = center − (cx · 640 / W) / 2.
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

// A sprintf into a stack buffer of 0x62db60: what does not fit overwrites the stack, so it stops.
Core::Expected<std::string, std::string> Fits(std::string text, std::size_t room, std::string_view where)
{
    if (text.size() >= room)
        return Core::Unexpected(std::format("{}: el sprintf escribe {} bytes en un buffer de {} y pisa la pila", where,
                                            text.size() + 1, room));
    return text;
}

// A menu button or an options row, and with the mouse over it, again in additive and with color.
void DrawMenuButton(Frame& frame, int y, int height)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto draw = [&]
    {
        Quad(frame, MessageBoxSlot, MenuButtonLeft, static_cast<float>(y), MenuButtonWidth, static_cast<float>(height),
             MessageBoxDu, 1.0f);
    };
    draw();
    const Ui::Point mouse = frame.context.mouse;
    if (MenuHoverLeft < mouse.x && mouse.x < MenuHoverRight && y <= mouse.y && mouse.y < y + height)
    {
        sprites.SetColor(HoverColor);
        sprites.SetBlend(BlendMode::Additive);
        draw();
        sprites.SetColor(White);
        sprites.SetBlend(BlendMode::Opaque);
    }
}

// 0x62dbcf..0x62df1f
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

// 0x69fd9b(buffer, "%s  On" or "%s  Off", text)
Core::Expected<void, std::string> DrawTheSwitch(Frame& frame, int y, std::size_t index, bool on)
{
    auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    DrawCentered(frame, y, std::format("{}{}", text.Value(), on ? OnSuffix : OffSuffix));
    return {};
}

// 0x62e1ba..0x62e2a8. The bar's corner is kept for the step (0x62570a).
Core::Expected<void, std::string> DrawTheVolume(Frame& frame)
{
    auto text = TextOf(frame, VolumeText);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Render::SpriteBatch& sprites = frame.context.sprites;
    const Core::PixelSize screen = sprites.Screen();
    const Render::Text::TextExtent extent = frame.context.text.Measure(frame.context.hud.dcFont, text.Value());
    // cx · 640 / W in unsigned 32 bits, like 0x62e1df; without a screen, the width as is.
    std::int32_t width = extent.width;
    if (screen.width > 0)
        width = static_cast<std::int32_t>(static_cast<std::uint32_t>(extent.width) * DesignWidth /
                                          static_cast<std::uint32_t>(screen.width));
    const std::int32_t x = -((width + VolumeTextSpace) / 2);
    DrawText(frame, x + CenterX, VolumeY, text.Value());
    const std::int32_t barX = x + VolumeBarOffset + width;
    const std::int32_t barY = VolumeBarTop;
    Ui::GameUiState& ui = frame.runtime.ui;
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
    // fild, fmul by the float at 0x6b8bf0 on the x87 and fstp to float.
    const float fill = static_cast<float>(static_cast<double>(ui.volume + 1) * static_cast<double>(VolumeStepWidth));
    sprites.ColorQuad(static_cast<float>(barX + 1), static_cast<float>(barY + 1), fill, VolumeFillHeight);
    WhiteWithTexture(frame);
    return {};
}

// 0x62e2ce..0x62e45a: the glow frame, its boxes (those up to 0x6c9dcc on) and its digits.
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
        static_cast<void>(Ui::DrawNumber(sprites, static_cast<float>(GlowDigitFirstX + box * GlowStep), GlowDigitTop,
                                         GlowFirstDigit + box * GlowDigitStep, GlowDigitSize, GlowDigitSize));
    }
    WhiteWithTexture(frame);
}

// 0x62df3f..0x62e4bb
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
    // 0x62e2c4: 0x69fd9b(buffer, text 1840), the text as the format. MuRet copies it as is: a '%' would come out
    // different.
    auto glow = TextOf(frame, GlowText);
    if (!glow.HasValue())
        return Core::Unexpected(std::move(glow).Error());
    DrawTheGlow(frame);
    DrawCentered(frame, GlowTextY, glow.Value());
    frame.context.sprites.SetColor(White);
    return DrawCenteredText(frame, CloseY, CloseText);
}

// An arrow of 0x8c: gray at its limit; hovered, 0.7, or 0.5 with the click, which it spends (Q-C553).
void DrawTheArrow(Frame& frame, bool atTheEnd, int left, int right, float x, float degrees)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const Ui::Point mouse = frame.context.mouse;
    sprites.SetColor(White);
    bool& click = frame.runtime.mouse.pendingClick;
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

// 0x630e14..0x6313ff: the boxes' frame, their buttons and 0x8c's arrows.
int BoxRowsOf(const Ui::DialogBoxState& box, std::int32_t id)
{
    if (id == Ui::MessageBoxDialog || id == Ui::PagedDialog || id == 0x9b)
        return ShortBoxRows;
    if (id != Ui::AnswersDialog)
        return TallBoxRows;
    const int rows = ((box.lineCount + box.answerCount) * Ui::DialogLineStep + AnswersBoxRowsRound) / BoxRowStep;
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
    for (const Ui::DialogButton& button : frame.runtime.dialogBox.buttons)
    {
        if (button.state <= 0)
            continue;
        const float width = static_cast<float>(button.width);
        const float height = static_cast<float>(button.height);
        Quad(frame, static_cast<std::uint16_t>(MessageBoxSlot + button.state), static_cast<float>(button.x) + BoxLeft,
             static_cast<float>(button.y) + BoxTop, width, height - 1.0f, width * ButtonDu, height * ButtonDv);
    }
    if (id != Ui::PagedDialog)
        return;
    const Ui::DialogBoxState& box = frame.runtime.dialogBox;
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    DrawTheArrow(frame, box.page == 0, LeftArrowLeft, LeftArrowRight, LeftArrowX, LeftArrowDegrees);
    DrawTheArrow(frame, box.page == box.lastPage, RightArrowLeft, RightArrowRight, RightArrowX, RightArrowDegrees);
    frame.context.sprites.SetColor(White);
}

// 0x632472..0x632636: the lines of 0x8a, 0x8c and 0x8d, centered from DialogTextTop; in 0x8d, below, each answer with
// its color and, if its first byte is not 0, its line (the y goes down only with those drawn). At the end, white.
Core::Expected<void, std::string> DrawTheBoxText(Frame& frame, std::int32_t id)
{
    const Ui::DialogBoxState& box = frame.runtime.dialogBox;
    std::int32_t y = Ui::DialogTextTop(box, id);
    for (std::int32_t row = 0; row < box.lineCount; ++row, y += Ui::DialogLineStep)
        DrawCentered(frame, y, Ui::DialogRow(box, static_cast<std::size_t>(row)));
    if (id == Ui::AnswersDialog)
    {
        if (box.answerCount > static_cast<std::int32_t>(Ui::DialogAnswerRows))
            return Core::Unexpected(
                std::format("0x632563: {} respuestas pasan los 10 renglones de 0x7d3691c", box.answerCount));
        // 0x632515..0x632535: the answer under the mouse, with division toward zero, or −1 above the first.
        const Ui::Point mouse = frame.context.mouse;
        const std::int32_t offset = mouse.y - y;
        const std::int32_t hovered = offset < 0 ? -1 : offset / Ui::DialogLineStep;
        const bool inside = std::abs(Ui::AnswersCenterX - mouse.x) <= Ui::AnswersHalfWidth;
        for (std::int32_t answer = 0; answer < box.answerCount; ++answer)
        {
            frame.context.sprites.SetColor(answer == hovered && inside ? HoveredAnswerColor : AnswerColor);
            const std::size_t first = static_cast<std::size_t>(answer) * Ui::DialogRowBytes;
            if (box.answers[first] == '\0')
                continue;
            DrawCentered(frame, y, Ui::DialogAnswer(box, static_cast<std::size_t>(answer)));
            y += Ui::DialogLineStep;
        }
    }
    frame.context.sprites.SetColor(White);
    return {};
}

// The frame of the chaos box, jewel mix and 0x91 dialogs: 0xfc of 213 × 5 at (213, top), the rows of 0xfb of 213 × 40
// from top + 5 and 0xfc again, with the UVs (0, 0, 0.83203125, 0.625).
void DrawTheFrame(Frame& frame, int top, int rows)
{
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(top), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    int y = top + static_cast<int>(BoxEdgeHeight);
    for (int row = 0; row < rows; ++row, y += BoxRowStep)
        Quad(frame, BoxRowSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxRowHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
}

// 0x62e65c..0x62e929: the chaos box mode, without the tail's title.
Core::Expected<void, std::string> DrawTheMixModes(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Kernel::HudState& hud = frame.context.hud;
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

// 0x632323..0x63236d (0x8e's 734), 0x632372..0x6323bc (0xa1's and 0xa2's 1801) and 0x6323c1..0x63240b (0xa4's 1800):
// the title in bold with 0xff0080ff, centered at y = 0x78 + 0xf, which leaves the bold and its color.
Core::Expected<void, std::string> DrawTheHeadTitle(Frame& frame, std::size_t index)
{
    Kernel::HudState& hud = frame.context.hud;
    hud.dcFont = Render::Text::FontKind::Bold;
    hud.textColors.text = MixModeHeadColor;
    return DrawCenteredText(frame, MixModeTitleY, index);
}

// x in [left, right] and y in [top, bottom], edges included: the FCOMs of the floats with the integer mouse.
bool MouseWithin(const Frame& frame, float left, float top, float right, float bottom)
{
    const auto x = static_cast<float>(frame.context.mouse.x);
    const auto y = static_cast<float>(frame.context.mouse.y);
    return left <= x && x <= right && top <= y && y <= bottom;
}

// A jewel mix button: the glColor3f and 0xf0 with the UVs (0, 0, 0.83203125, 1).
void DrawTheJewelButton(Frame& frame, Color color, float x, float y, float width, float height)
{
    frame.context.sprites.SetColor(color);
    Quad(frame, MessageBoxSlot, x, y, width, height, MessageBoxDu, 1.0f);
}

// The Text.bmd texts in the current font, at a fixed x.
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

// The text of a jewel mix cancel or OK: in bold with 0xfffff0c8, centered on `centerX`, and the normal font after (the
// color stays).
Core::Expected<void, std::string> DrawTheJewelButtonText(Frame& frame, int centerX, int y, std::size_t index)
{
    Kernel::HudState& hud = frame.context.hud;
    auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    hud.dcFont = Render::Text::FontKind::Bold;
    hud.textColors.text = JewelButtonTextColor;
    DrawCenteredAt(frame, centerX, y, text.Value());
    hud.dcFont = Render::Text::FontKind::Normal;
    return {};
}

// 0x62e92e..0x62ed05: the jewel mix menu (0xa1). It leaves the text at 0xfffff0c8 and white.
Core::Expected<void, std::string> DrawTheJewelMenu(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Kernel::HudState& hud = frame.context.hud;
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

// 0x62ee33..0x62f089 and 0x62f4bc..0x62f689: a jewel of 0xa2 or 0xa4. The chosen one (0x6c62e0 equal to its own)
// goes in bold, in (1; 1; 0.2) and with 0xfffff0c8; otherwise, hovered in white and with 0xfffff0c8, and otherwise in
// 0.5. Neither the font nor the text color come back between the two (Q-C950).
void PaintTheJewelKind(Frame& frame, int kind, bool hovered)
{
    Kernel::HudState& hud = frame.context.hud;
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

// 0x62ed0a..0x62f36c: 0xa2's jewel and bundle. It leaves the text at 0xfffff0c8 and white.
Core::Expected<void, std::string> DrawTheJewelChoice(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Kernel::HudState& hud = frame.context.hud;
    const std::uint8_t chosen = frame.runtime.npcWindows.jewels.kind;
    sprites.SetColor(White);
    DrawTheFrame(frame, MixModeTop, JewelChoiceRows);
    float y = JewelChoiceTop;
    if (auto drawn = DrawTheNotes(frame, JewelChoiceNotesX, JewelChoiceNotes); !drawn.HasValue())
        return drawn;
    for (int kind = 0; kind < 2; ++kind)
    {
        // 0x62ee33..0x62ee41: ftol(k · 90), integer, and back to float for the x.
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
        // 0x62f0bd..0x62f15c: without a jewel, only the gray; with one, the mouse and the bold with 0xfffff0c8.
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
        auto written = Fits(Crt::Sprintf(format.Value(), bundle * JewelsPerBundle, bundle * ZenPerBundle),
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

// 0x402de0: the empty cells (type −1) of the inventory's 64.
std::int32_t FreeCellsOf(const Kernel::WorldState& world)
{
    return static_cast<std::int32_t>(std::ranges::count_if(world.Inventory().cells, [](const Kernel::ItemState& cell)
                                                           { return cell.id == EmptyCell; }));
}

// 0x62f371..0x62fae6: unmixing a bundle (0xa4). Without a jewel, the two jewels and the cancel, and the text in white
// at the end; with one, the jewels, 1821 and 1822, 0x6e5660's list (ChaosMix/JewelBundleList), the OK and the cancel.
Core::Expected<void, std::string> DrawTheJewelUnmix(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Kernel::HudState& hud = frame.context.hud;
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
        // 0x62e914
        sprites.SetColor(White);
        return {};
    }
    auto format = TextOf(frame, FreeCellsText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    auto written = Fits(Crt::Sprintf(format.Value(), FreeCellsOf(frame.context.world)), FreeCellsRoom, "0x62f6bd");
    if (!written.HasValue())
        return Core::Unexpected(std::move(written).Error());
    hud.textColors.text = UnmixNotesColor;
    sprites.SetColor(White);
    DrawText(frame, UnmixNotesX, FreeCellsY, written.Value());
    auto list = TextOf(frame, UnmixListText);
    if (!list.HasValue())
        return Core::Unexpected(std::move(list).Error());
    DrawText(frame, UnmixNotesX, UnmixListTextY, list.Value());
    // 0x62f71a..0x62f739: the list's corner (0x45a9d0) and its drawing (0x402b20 → 0x4832e0).
    Npc::JewelBundleList& bundles = frame.runtime.npcWindows.jewels.bundles;
    if (auto drawn = ChaosMix::DrawTheBundleList(frame.context, UnmixListX, bundles.height + UnmixListBottom);
        !drawn.HasValue())
        return drawn;
    // 0x62f73e..0x62f785: the OK, with the color and the blend the list left.
    const float top = static_cast<float>(bundles.height + UnmixListBottom) + UnmixButtonsDrop;
    Quad(frame, MessageBoxSlot, UnmixOkLeft, top, JewelKindWidth, JewelButtonHeight, MessageBoxDu, 1.0f);
    // 0x62f78a..0x62f807: with a chosen one (the vtable + 0x64), the text at 0xfffff0c8 and (1; 1; 0.2); without it,
    // 0.5 and the text color there was.
    if (ChaosMix::ChosenBundle(bundles))
    {
        hud.textColors.text = JewelButtonTextColor;
        sprites.SetColor(JewelChosen);
    }
    else
        sprites.SetColor(JewelDisabled);
    auto ok = TextOf(frame, UnmixOkText);
    if (!ok.HasValue())
        return Core::Unexpected(std::move(ok).Error());
    hud.dcFont = Render::Text::FontKind::Bold;
    const int textY = static_cast<int>(top + UnmixButtonTextDrop);
    DrawCenteredAt(frame, UnmixOkCenterX, textY, ok.Value());
    // 0x62f875..0x62f9b7: the cancel, white with the mouse in [324.5, 404.5] × [y, y + 25] and 0.7 otherwise, with the
    // text in bold and 0xfffff0c8; then the normal font and the text in white.
    const bool hovered = MouseWithin(frame, UnmixCancelLeft, top, UnmixCancelRight, top + JewelButtonHeight);
    DrawTheJewelButton(frame, hovered ? White : JewelIdle, UnmixCancelLeft, top, JewelKindWidth, JewelButtonHeight);
    auto cancel = TextOf(frame, JewelCancelText);
    if (!cancel.HasValue())
        return Core::Unexpected(std::move(cancel).Error());
    hud.dcFont = Render::Text::FontKind::Bold;
    hud.textColors.text = JewelButtonTextColor;
    DrawCenteredAt(frame, UnmixCancelCenterX, textY, cancel.Value());
    hud.dcFont = Render::Text::FontKind::Normal;
    hud.textColors.text = DialogTextColors.text;
    // 0x62e914
    sprites.SetColor(White);
    return {};
}

// 0x62faeb..0x62fc1a: the BC wait (0xa5), 471 centered at x = 0x13f.
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

// 0x62fc1f..0x62febe and 0x6313fa: the result (0xa6). It leaves the text at 0xfffff0c8 and white.
Core::Expected<void, std::string> DrawTheJewelResult(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Kernel::HudState& hud = frame.context.hud;
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
        Fits(Crt::Sprintf("%s%s %s", title.Value(), result.Value(), tail.Value()), JewelResultRoom, "0x62fd43");
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

// 0x630818..0x63084c and 0x63093e..0x6309cf: the generic OK's frame and its button.
void DrawTheGenericBox(Frame& frame)
{
    Quad(frame, MessageBoxSlot, BoxLeft, BoxTop, BoxWidth, GenericBoxHeight, MessageBoxDu, 1.0f);
    const std::uint16_t slot = MouseIn(frame, OkLeft, OkTop, OkRight, OkBottom) ? OkHoverSlot : OkSlot;
    Quad(frame, slot, OkX, OkY, OkWidth, OkHeight, OkDu, OkDv);
}

// The yes/no frame's y ([EBP − 0x20], 0x630cc4..0x630cd8).
int YesNoTopOf(std::int32_t id)
{
    return id == Ui::ServerTimeDialog ? ServerTimeBoxTop : YesNoBoxTop;
}

// 0x630e1f..0x630f50 and 0x630fdb..0x6310d0: a 0xa8 button, its text and the color and blend coming back.
void DrawTheLuckButton(Frame& frame, const Ui::DialogButton& button, int top, std::string_view text)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    Kernel::HudState& hud = frame.context.hud;
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

// 0x630cc4..0x6310d5: the yes/no dialogs' frame (0x94, 0x96, 0x98, 0x99, 0x9a, 0xa3, 0xa8, 0xa9 and 0xaa) and their
// buttons with state 1 to 4; 0xa8's with their text, and then its third.
Core::Expected<void, std::string> DrawTheWarningBox(Frame& frame, std::int32_t id)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const int top = YesNoTopOf(id);
    const int rows = id == Ui::ChaosCastleDialog ? ChaosCastleBoxRows : YesNoBoxRows;
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(top), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    int y = top + static_cast<int>(BoxEdgeHeight);
    for (int row = 0; row < rows; ++row, y += BoxRowStep)
        Quad(frame, BoxRowSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxRowHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    const Ui::DialogBoxState& box = frame.runtime.dialogBox;
    for (std::size_t index = 0; index < box.buttons.size(); ++index)
    {
        if (id == Ui::ChaosCastleDialog)
        {
            const bool noItem = frame.runtime.dialogItem.scalar1Low == NoDialogItem && index == 0;
            sprites.SetColor(noItem ? NoItemYesColor : White);
        }
        const Ui::DialogButton& button = box.buttons[index];
        if (button.state < 1 || button.state > YesNoLastButtonState)
            continue;
        if (id == Ui::ItemNameWithLuckDialog)
        {
            auto text = TextOf(frame, LuckButtonTexts[index]);
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            auto written = Fits(Crt::Sprintf("%s", text.Value()), LuckButtonRoom, "0x630ed3");
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
    if (id != Ui::ItemNameWithLuckDialog)
        return {};
    auto third = TextOf(frame, LuckThirdButtonText);
    if (!third.HasValue())
        return Core::Unexpected(std::move(third).Error());
    DrawTheLuckButton(frame, box.thirdButton, top, third.Value());
    return {};
}

// 0x632683..0x632877: the mix title, by kind (0x7d2dc98; ECX is the kind, or 0 if negative, and 7 with 0xb). Each text
// is a sprintf's format; 0x1c with a cape (0x7d2dc84) other than 3 or 4 leaves the buffer as it was on the stack, so
// it stops.
Core::Expected<std::string, std::string> TheMixTitle(const Frame& frame)
{
    const Npc::NpcWindowState& npc = frame.runtime.npcWindows;
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
        return Crt::Sprintf(creative.Value(), kind - 10);
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
        // 0x6327bb..0x63280f: "%s(%s)" (0x6d6344 and 0x6d634c) with 1923 and 1924 (cape 3) or 1925 (cape 4).
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
        return Crt::Sprintf("%s(%s)", first.Value(), cape.Value());
    }
    case 0x18:
        index = 1172;
        break;
    default:
    {
        // 0x6326b9..0x6326cd and 0x63282c..0x632837: 601 + ECX up to 4 and 607 + ECX after (0xb, with 7, gives 614).
        const std::int32_t shown = kind == 0xb ? 7 : std::max(kind, 0);
        index = static_cast<std::size_t>(shown) + (shown <= 4 ? 601u : 607u);
        break;
    }
    }
    auto text = format(index);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    return Crt::Sprintf(text.Value());
}

// 0x632637..0x632a2a: the lines of the yes/no dialogs that go to 0x632637, each centered on its x. It does not return
// to white or the normal font. With 0x78abb14 at 2, except in 0x96 and 0x9a, the mix title goes first.
Core::Expected<void, std::string> DrawTheWarningRows(Frame& frame, std::int32_t id)
{
    const std::int32_t choice = frame.runtime.social.trade.warningKind;
    const int top = YesNoTopOf(id) + YesNoTextDrop;
    int x = YesNoTextX;
    int y = top + YesNoTextDrop;
    Render::SpriteBatch& sprites = frame.context.sprites;
    if (id == Ui::ChaosCastleDialog)
        x = CenterX;
    else if (id != Ui::ItemNameDialog)
    {
        if (id == Ui::ServerTimeDialog)
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
    const Ui::DialogBoxState& box = frame.runtime.dialogBox;
    for (std::int32_t row = 0; row < box.lineCount; ++row, y += Ui::DialogLineStep)
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
        DrawCenteredAt(frame, x, y, Ui::DialogRow(box, static_cast<std::size_t>(row)));
    }
    return {};
}

// 0x632a45..0x632aeb: 0xa9's lines, and at the end the normal font (without returning to white).
void DrawTheBlessRows(Frame& frame)
{
    const Ui::DialogBoxState& box = frame.runtime.dialogBox;
    frame.context.hud.dcFont = Render::Text::FontKind::Bold;
    frame.context.sprites.SetColor(BlessRowColor);
    int y = LowerYesNoTextY;
    for (std::int32_t row = 0; row < box.lineCount; ++row, y += Ui::DialogLineStep)
        DrawCentered(frame, y, Ui::DialogRow(box, static_cast<std::size_t>(row)));
    frame.context.hud.dcFont = Render::Text::FontKind::Normal;
}

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

// The vtable + 0x30 with 0 (0x4682a0, the Windows focus) and + 0x14 (0x468320, which draws it with the focus) of the
// EDIT of [0x5763504] (ChatInputState::dialogEdit), after what `setup` changes in it this frame, with its id in
// 0x5762ec0. An intermediate pass works on a copy and does not move the focus.
template <typename Setup> void DrawTheDialogEdit(Frame& frame, Setup setup)
{
    Chat::ChatInputState& input = frame.runtime.chat.input;
    Ui::OldEdit copy = input.dialogEdit;
    Ui::OldEdit& edit = frame.context.projecting ? copy : input.dialogEdit;
    setup(edit);
    // 0x4682f1..0x4682f7: the focus also leaves the EDIT's id (7) in 0x5762ec0, the list that takes the arrows.
    if (!frame.context.projecting)
    {
        input.focus = Chat::ChatFocus::Dialog;
        frame.runtime.ui.focusedList = edit.base.id;
    }
    const Ui::OldWindowDrawContext drawing{.sprites = frame.context.sprites,
                                           .text = frame.context.text,
                                           .dcFont = frame.context.hud.dcFont,
                                           .textColors = frame.context.hud.textColors,
                                           .arrowHeld = frame.context.arrowHeld};
    Ui::OldDraw::DrawTheOldEdit(drawing, edit, true);
}

// 0x630818..0x6308c6 (0x73): the generic OK's frame, without the button, and with 0x6c7b8c at 1 (its .data value;
// with 0, 0x553ae0 at 0x6308cb, which is not reached) the EDIT of [0x5763504] rebuilt every frame, like the serial
// number's (0x5f696d) but without touching its colors: the vtable + 4 with 0 (shown), +0x4c at 9, + 0x3c with 8, + 8
// with (0x46, 0xe), 0x45a9d0(0x11b, 0x5f), + 0x30 with 0 and + 0x14.
void DrawTheMoneyBox(Frame& frame)
{
    Quad(frame, MessageBoxSlot, BoxLeft, BoxTop, BoxWidth, GenericBoxHeight, MessageBoxDu, 1.0f);
    DrawTheDialogEdit(frame,
                      [&frame](Ui::OldEdit& edit)
                      {
                          edit.base.state = Ui::ShownWindow;
                          edit.filter = MoneyEditFilter;
                          Ui::LimitTheEdit(edit, MoneyEditLimit, frame.context.arrowHeld);
                          edit.base.width = MoneyEditWidth;
                          edit.base.height = MoneyEditHeight;
                          edit.base.x = MoneyEditX;
                          edit.base.y = MoneyEditY;
                      });
}

// 0x6304f2..0x630813: 0x91. 0x5eba70 builds the EDIT when opening it; here it only takes the focus and is drawn, and
// then 0x620980 leaves the blend off.
void DrawTheShopBox(Frame& frame)
{
    Quad(frame, BoxEdgeSlot, BoxLeft, ShopBoxTop, BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    int y = ShopFirstRowY;
    for (int row = 0; row < ShopRows; ++row, y += BoxRowStep)
        Quad(frame, BoxRowSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxRowHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, static_cast<float>(y), BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    DrawTheDialogEdit(frame, [](Ui::OldEdit&) {});
    frame.context.sprites.SetBlend(BlendMode::Opaque);
    const bool ok = MouseIn(frame, ShopOkLeft, ShopButtonTop, ShopOkRight, ShopButtonBottom);
    Quad(frame, ok ? OkHoverSlot : OkSlot, ShopOkX, ShopButtonY, ShopButtonWidth, ShopButtonHeight, OkDu, OkDv);
    const bool cancel = MouseIn(frame, ShopCancelLeft, ShopButtonTop, ShopCancelRight, ShopButtonBottom);
    Quad(frame, cancel ? NoHoverSlot : NoSlot, ShopCancelX, ShopButtonY, ShopButtonWidth, ShopButtonHeight, OkDu, OkDv);
}

// 0x631c91 (0x73) and 0x631d2f (0x91): their centered texts.
Core::Expected<void, std::string> DrawTheMoneyText(Frame& frame)
{
    const std::int32_t mark = frame.runtime.social.trade.field78abacc;
    const std::size_t index = mark == 0 ? MoneyZeroText : mark == 1 ? MoneyOneText : MoneyOtherText;
    return DrawCenteredText(frame, GenericTextY, index);
}

Core::Expected<void, std::string> DrawTheShopText(Frame& frame)
{
    const std::int32_t mode = frame.runtime.social.personalShop.dialogMode;
    if (mode == ShopTitleMode)
        return DrawCenteredText(frame, ShopTextY, ShopTitleText);
    if (mode == ShopPriceMode)
        return DrawCenteredText(frame, ShopTextY, ShopPriceText);
    return {};
}

// 0x630818..0x63084c and 0x6309d4..0x630aeb: the generic OK's frame (0xf0 of 213 × 64 at (213, 60)) and the yes and
// the no.
void DrawTheYesNoBox(Frame& frame)
{
    Quad(frame, MessageBoxSlot, BoxLeft, BoxTop, BoxWidth, GenericBoxHeight, MessageBoxDu, 1.0f);
    const std::uint16_t yes = MouseIn(frame, YesLeft, OkTop, YesRight, OkBottom) ? OkHoverSlot : OkSlot;
    Quad(frame, yes, YesX, OkY, OkWidth, OkHeight, OkDu, OkDv);
    const std::uint16_t no = MouseIn(frame, NoLeft, OkTop, NoRight, OkBottom) ? NoHoverSlot : NoSlot;
    Quad(frame, no, NoX, OkY, OkWidth, OkHeight, OkDu, OkDv);
}

// 0x630818..0x63084c and 0x630af0..0x630cb8: the generic OK's frame, the number EDIT with the normal blend
// (0x620a00(1) and 0x620980 around its drawing, 0x468320) and the narrow yes and no. What the original sets on the
// EDIT every frame is also written in an intermediate drawing (they are the same values); its drawings (+0x80, the
// cursor blink) count only the original's, and 0x5762ec0 keeps its id (0x4682a0).
void DrawTheNumberBox(Frame& frame)
{
    Quad(frame, MessageBoxSlot, BoxLeft, BoxTop, BoxWidth, GenericBoxHeight, MessageBoxDu, 1.0f);
    Chat::ChatInputState& input = frame.runtime.chat.input;
    Ui::OldEdit& field = input.numberField;
    Chat::ShowTheNumberField(input, ShownField);
    field.filter = NumberFieldFilter;
    field.backgroundColor = 0;
    Ui::LimitTheEdit(field, NumberFieldLimit, frame.context.arrowHeld);
    field.base.width = NumberFieldWidth;
    field.base.height = NumberFieldHeight;
    field.base.x = NumberFieldX;
    field.base.y = NumberFieldY;
    Chat::FocusTheNumberField(input);
    if (input.focus == Chat::ChatFocus::NumberField)
        frame.runtime.ui.focusedList = field.base.id;
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    Ui::OldEdit projected = field;
    Ui::OldDraw::DrawTheOldEdit(Ui::OldWindowDrawContext{.sprites = sprites,
                                                         .text = frame.context.text,
                                                         .dcFont = frame.context.hud.dcFont,
                                                         .textColors = frame.context.hud.textColors,
                                                         .arrowHeld = frame.context.arrowHeld},
                                frame.context.projecting ? projected : field,
                                input.focus == Chat::ChatFocus::NumberField);
    sprites.SetBlend(BlendMode::Opaque);
    const std::uint16_t yes = MouseIn(frame, NarrowYesLeft, OkTop, NarrowYesRight, OkBottom) ? OkHoverSlot : OkSlot;
    Quad(frame, yes, NarrowYesX, OkY, NarrowWidth, OkHeight, OkDu, OkDv);
    const std::uint16_t no = MouseIn(frame, NarrowNoLeft, OkTop, NarrowNoRight, OkBottom) ? NoHoverSlot : NoSlot;
    Quad(frame, no, NarrowNoX, OkY, NarrowWidth, OkHeight, OkDu, OkDv);
}

// 0x471ae0 over 0x575d9d0 (0x471e80): sets 0x7d3a10c back to 0x9c, the frame, the buttons by kind (+4; with other
// than 1 or 2, none) and the lines (+8), each one copied with strcpy into a 0x80-byte stack buffer (0x4716e0 cuts
// them at 24). All without blend: 0x63140b sets the normal one later, and its switch has no 0x9c.
void DrawTheConfirmBox(Frame& frame)
{
    frame.runtime.dialogs.current = ConfirmDialog;
    const Social::ConfirmBoxState& box = frame.runtime.social.confirmBox;
    const std::int32_t step = Render::Text::FontStepFor(frame.context.sprites.Screen().width) + ConfirmLineGap;
    const std::int32_t height = step * static_cast<std::int32_t>(box.lines.size());
    const auto rows = static_cast<float>(height + ConfirmRowsExtra);
    Quad(frame, BoxEdgeSlot, BoxLeft, ConfirmEdgeTop, BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    Quad(frame, BoxRowSlot, BoxLeft, ConfirmRowsTop, BoxWidth, rows, MessageBoxDu, BoxDv);
    Quad(frame, BoxEdgeSlot, BoxLeft, rows + ConfirmRowsTop, BoxWidth, BoxEdgeHeight, MessageBoxDu, BoxDv);
    // 0x471bea..0x471c2b: h + 120 stays in a float (FST) and the bottom edge comes from it; the top one is h + 140.
    const double rowTop = static_cast<double>(height) + ConfirmBoxTop + ConfirmButtonTop;
    const double rowBottom =
        static_cast<double>(static_cast<float>(static_cast<double>(height) + ConfirmBoxTop)) + ConfirmButtonBottom;
    const auto mouseY = static_cast<double>(frame.context.mouse.y);
    const bool inRow = rowTop <= mouseY && mouseY <= rowBottom;
    const auto button = [&](std::uint16_t slot, std::uint16_t hoverSlot, float x, int left, int right)
    {
        const int mouseX = frame.context.mouse.x;
        const bool hovered = mouseX >= left && inRow && mouseX <= right;
        Quad(frame, hovered ? hoverSlot : slot, x, static_cast<float>(rowTop), ConfirmButtonWidth, ConfirmButtonHeight,
             OkDu, OkDv);
    };
    if (box.type == ConfirmTwoButtons)
    {
        button(OkSlot, OkHoverSlot, ConfirmYesX, ConfirmYesLeft, ConfirmYesRight);
        button(NoSlot, NoHoverSlot, ConfirmNoX, ConfirmNoLeft, ConfirmNoRight);
    }
    else if (box.type == ConfirmOneButton)
        button(OkSlot, OkHoverSlot, ConfirmOkX, ConfirmOkLeft, ConfirmOkRight);
    // 0x471dfa..0x471e74: 0x623690(0x140, (step + 2) · i + 0x87, line).
    int y = ConfirmTextTop;
    for (const std::string& line : box.lines)
    {
        DrawCentered(frame, y, line);
        y += step;
    }
}

// 0x5110d0([0x78aba4c]): the first live one of the 400 whose +0x1f4 (MOVSX) is the stored key, which /party sets with
// MOVSX and the 40 unsigned; with none, 400.
std::size_t InviterOf(const Kernel::WorldState& world, std::int32_t key)
{
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < KeySearched; ++index)
    {
        const Kernel::ActorState& actor = actors[index];
        if (actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key)
            return index;
    }
    return KeySearched;
}

// The trade partner's name (0x78aba1c, with its terminator), up to the first 0.
std::string_view TradeNameOf(const Social::TradeState& trade)
{
    const auto end = std::ranges::find(trade.otherName, std::byte{0});
    return {reinterpret_cast<const char*>(trade.otherName.data()),
            static_cast<std::size_t>(end - trade.otherName.begin())};
}

// 0x631e29..0x631f00 (0x77): sprintf(418, the inviter's +0x1d9 name) in whatever font is set and sprintf(425).
// 0x631bb0..0x631c8c (0x78): sprintf(418, 0x78aba1c) in the big bold font and sprintf(419) in the normal one. Centered.
Core::Expected<void, std::string> DrawTheInvitation(Frame& frame, std::int32_t id)
{
    auto format = TextOf(frame, InviterText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    if (id == PartyInviteDialog)
    {
        const Kernel::WorldState& world = frame.context.world;
        const Kernel::ActorState& inviter = world.Actors()[InviterOf(world, frame.runtime.social.party.requestKey)];
        DrawCentered(frame, InvitationRowY - PartyRowStep, Crt::Sprintf(format.Value(), Actors::ChatNameOf(inviter)));
        auto question = TextOf(frame, PartyQuestionText);
        if (!question.HasValue())
            return Core::Unexpected(std::move(question).Error());
        DrawCentered(frame, InvitationRowY + PartyRowStep, Crt::Sprintf(question.Value()));
        return {};
    }
    frame.context.hud.dcFont = Render::Text::FontKind::BigBold;
    DrawCentered(frame, InvitationRowY - TradeRowStep,
                 Crt::Sprintf(format.Value(), TradeNameOf(frame.runtime.social.trade)));
    frame.context.hud.dcFont = Render::Text::FontKind::Normal;
    auto question = TextOf(frame, TradeQuestionText);
    if (!question.HasValue())
        return Core::Unexpected(std::move(question).Error());
    DrawCentered(frame, InvitationRowY + TradeRowStep, Crt::Sprintf(question.Value()));
    return {};
}

// 0x63212a..0x6321f8 (0x7f): sprintf(430, the rival of 0x5767804, up to its zero) and sprintf(431 or 432), in
// whatever font is set and centered.
Core::Expected<void, std::string> DrawTheWarRequest(Frame& frame)
{
    auto format = TextOf(frame, WarRequestText);
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    const Guild::GuildState& guild = frame.runtime.guild;
    const auto end = std::ranges::find(guild.rival, '\0');
    const std::string_view rival(guild.rival.data(), static_cast<std::size_t>(end - guild.rival.begin()));
    DrawCentered(frame, InvitationRowY - WarRowStep, Crt::Sprintf(format.Value(), rival));
    auto question = TextOf(frame, guild.warType == 0 ? WarQuestionText : TypedWarQuestionText);
    if (!question.HasValue())
        return Core::Unexpected(std::move(question).Error());
    DrawCentered(frame, InvitationRowY + WarRowStep, Crt::Sprintf(question.Value()));
    return {};
}

// The stop of a dialog that is not from CL8, with the wave of its step.
Core::Unexpected<std::string> CutTheDialog(std::int32_t id)
{
    const DialogRoute route = DialogRouteOf(id);
    std::string_view wave = "CL9";
    if (route.step == DialogStep::Cut)
        wave = route.wave;
    return Core::Unexpected(std::format("0x62db60: el dibujo del diálogo {:#x} es de la {}", id, wave));
}
} // namespace

Core::Expected<void, std::string> DrawTheGameDialogs(Ui::GameHudContext& context)
{
    Frame frame{.context = context, .runtime = context.world.Runtime()};
    Render::SpriteBatch& sprites = context.sprites;
    // 0x62db60..0x62dba1: 0x6c8e70 at 0x100 (read by 0x553ae0, which does not run here), no blend, white, the normal
    // font in the DC and the text colors.
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
    const bool box = id == Ui::MessageBoxDialog || id == Ui::PagedDialog || id == Ui::AnswersDialog || id == 0x8b ||
                     id == 0x97 || id == 0x9b;
    // 0x630477..0x6304d4: the yes/no dialogs go to 0x630cc4.
    const bool yesNo = std::ranges::find(YesNoDialogs, id) != YesNoDialogs.end();
    if (id == Ui::EscapeMenuDialog)
    {
        if (auto drawn = DrawTheEscapeMenu(frame); !drawn.HasValue())
            return drawn;
    }
    else if (id == Ui::OptionsDialog)
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
    else if (box)
        DrawTheBox(frame, id);
    else if (yesNo)
    {
        if (auto drawn = DrawTheWarningBox(frame, id); !drawn.HasValue())
            return drawn;
    }
    else if (generic)
        DrawTheGenericBox(frame);
    else if (id == PartyInviteDialog || id == TradeInviteDialog || id == WarRequestDialog)
        DrawTheYesNoBox(frame);
    else if (id == NumberDialog || id == WarehouseNumberDialog)
        DrawTheNumberBox(frame);
    else if (id == ConfirmDialog)
        DrawTheConfirmBox(frame);
    else if (id == MoneyDialog)
        DrawTheMoneyBox(frame);
    else if (id == ShopDialog)
        DrawTheShopBox(frame);
    else if (id != 0)
        return CutTheDialog(id);
    // 0x63140b: the normal blend and the tail's texts.
    sprites.SetBlend(BlendMode::Alpha);
    // 0x631432: the table 0x632bf0 takes 0x8e to 0x632323, 0x8a, 0x8c, 0x8d and 0xaa to 0x632472, and the other boxes
    // to 0x632a2b.
    if (id == ChaosMixModeDialog)
        return DrawTheHeadTitle(frame, MixModeTitle);
    // 0x632372 and 0x6323c1; 0xa5 and 0xa6 go to 0x632aec, with nothing.
    if (id == JewelMenuDialog || id == JewelChoiceDialog)
        return DrawTheHeadTitle(frame, MixText);
    if (id == JewelUnmixDialog)
        return DrawTheHeadTitle(frame, UnmixText);
    if (id == Ui::MessageBoxDialog || id == Ui::PagedDialog || id == Ui::AnswersDialog ||
        id == Ui::NoticeQuestionDialog)
        return DrawTheBoxText(frame, id);
    // 0x632637 and 0x632a45: the yes/no dialogs' lines.
    if (yesNo && id == BlessDialog)
    {
        DrawTheBlessRows(frame);
        return {};
    }
    if (yesNo)
        return DrawTheWarningRows(frame, id);
    if (box)
    {
        // 0x632a2b: [0x723c60] + 0x10, the event object's box text (0x40bdd0, 0x40c440 or 0x405220), by its kind (CL9
        // milestone 11, Events/EventDrawing). Without the object, nothing.
        if (frame.runtime.eventObject)
            return Events::DrawTheEventBoxText(frame.context, *frame.runtime.eventObject);
    }
    else if (id == PartyInviteDialog || id == TradeInviteDialog)
        return DrawTheInvitation(frame, id);
    else if (id == WarRequestDialog)
        return DrawTheWarRequest(frame);
    else if (id == NumberDialog || id == WarehouseNumberDialog)
    {
        context.hud.textColors.text = NumberTextColor;
        if (auto drawn = DrawCenteredText(frame, InvitationRowY - NumberRowStep,
                                          id == WarehouseNumberDialog ? WarehouseNumberFirstText : NumberFirstText);
            !drawn.HasValue())
            return drawn;
        return DrawCenteredText(frame, InvitationRowY + NumberRowStep,
                                id == WarehouseNumberDialog ? WarehouseNumberSecondText : NumberSecondText);
    }
    else if (id == MoneyDialog)
        return DrawTheMoneyText(frame);
    else if (id == ShopDialog)
        return DrawTheShopText(frame);
    else if (generic)
    {
        if (!generic->fixed.empty())
        {
            DrawCentered(frame, GenericTextY, generic->fixed);
            return {};
        }
        if (!generic->second)
            return DrawCenteredText(frame, GenericTextY, generic->first);
        if (auto drawn = DrawCenteredText(frame, GenericFirstRowY, generic->first); !drawn.HasValue())
            return drawn;
        return DrawCenteredText(frame, GenericSecondRowY, *generic->second);
    }
    return {};
}
} // namespace Client::Hud::Dialogs
