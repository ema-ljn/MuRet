#pragma once

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <array>
#include <cstdint>

// The textures each scene uploads on entering, as data: the path as the original asks for it, the slot of its table
// and the loader's third and fourth arguments, in call order (Client/docs/research/06-render-escenas-y-ui.md, "La
// carga" and "El login, al entrar"). The scenes load them with Data::Textures::LoadTextureSet.
namespace Client::Assets
{
namespace TextureSetsDetail
{
using Data::Textures::Filter;
using Data::Textures::Sampling;
using Data::Textures::TextureSetEntry;
using Data::Textures::Wrap;

// 0x2600 and 0x2900; 0x2601 and 0x2900; 0x2601 and 0x2901; 0x2600 and 0x2901.
inline constexpr Sampling NearestClamp{Filter::Nearest, Wrap::Clamp};
inline constexpr Sampling LinearClamp{Filter::Linear, Wrap::Clamp};
inline constexpr Sampling LinearRepeat{Filter::Linear, Wrap::Repeat};
inline constexpr Sampling NearestRepeat{Filter::Nearest, Wrap::Repeat};
} // namespace TextureSetsDetail

// 0x61bd9f..0x61bdb4: FontTest at slot 1 with NEAREST/CLAMP, before the common load. It is the atlas 0x621710 uses for
// the digits and "miss" of 0x548efd, not the dynamic font of slot 0.
inline constexpr std::array<Data::Textures::TextureSetEntry, 1> DamageTextureSet{{
    {"Interface\\FontTest.tga", 0x01, TextureSetsDetail::NearestClamp},
}};

// The game HUD textures that 0x61ccb0 uploads (0x61cce0..0x61e5f0), in order and with their sampling: the game cursors
// (0x63bd00 with GL_LINEAR and GL_CLAMP), the bottom bar (GL_NEAREST and GL_CLAMP), the AG, the commands button, those
// of the old UI that 0x4a5120 draws, the chat input box (0xef, 0xf8 and 0xf9, drawn by 0x5aa9a0), the dialogs 0x62db60
// draws (0xf0 to 0xf4 and 0xfb), the chat log, the identify cursor, the siege attack cursor and the status icons; and
// those of the skills, which 0x61ae60 uploads from 0x61ccb0. GameModels uploads them with the models, and they stay. 2
// and 3, Cursor and CursorPush, do not: they belong to WindowTextureSet, which each scene uploads and releases, the
// game too. The log's track, nis_back at 0x640, is overwritten by Skill\SwordEff.jpg with GL_LINEAR later in the same
// function (0x61ccb0, Q-C463): the skill's stays.
//
// Frame windows: win_check at 0x64a (0x61d1f0, with 0x63bd00), the commands checkbox; CursorID_Wnd at 0x65b (0x61d3ab),
// the selected character's label. The old windows (UI/OldWindowDraw.h): win_titlebar, win_button, win_size, win_resize
// and win_scrollbar at 0x645 to 0x649 (0x61d151..0x61d1db, no filter and no repeat). Letters and chats: win_mail and
// win_mark at 0x64b and 0x64c (0x61d20c, 0x61d228; drawn by 0x4a0a30, 0x4641f0 and 0x4a5750) and win_question at 0x651
// (0x61d29e; drawn by the portrait, 0x497c10), with Local\Webzenlogo.jpg at 0x653 (0x61d2ba, with 0x63b870: the empty
// portrait). The inventory: setitem, btn_pshop and btn_pshop2 at 0x652, 0x657 and 0x658 (0x61d319..0x61d354);
// text_button at 0x659 (0x61d370), the own personal shop's buttons that 0x5f3030 draws. The duel: DuelWindow at 0x650
// (0x61d2d6), drawn by 0x5f37f0. And 0x104 to 0x120, which 0x61ae60 uploads after command.jpg (0x61aec8..0x61b1f2): the
// window background, the equipment slots, the money, the grid, the trade line and the buttons, among them close
// (0x118). Nobody uploads 0x106.
//
// The pet window: Level_box at 0xf5 (0x61cf8f, which the character window also uses), win_button at 0x646 (0x61d17a,
// with 0x63bd00; also the old UI's button) and pet1 and pet2 at 0x65d and 0x65e (0x61d4b0 and 0x61d4cc), the pet
// button. win_scrollbar (0x649) is also the bar of the jewel bundle list (Scenes/JewelBundleList). The warehouse
// password box and its button: bar.jpg and back1 to back3 at 0xfd to 0x100 (0x61d3e3..0x61d43a, after menu01_new2) and
// lock_01 to lock_04 at 0x126 to 0x129 (0x61b2c3..0x61b31d, after level_02, accept_box and mix_button at 0x121 to
// 0x125, which belong to the chaos box).
inline constexpr std::array<Data::Textures::TextureSetEntry, 129> HudTextureSet{{
    {"Interface\\CursorAttack.tga", 0x04, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorGet.tga", 0x05, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorTalk.tga", 0x06, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorRepair.tga", 0x07, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorLeanAgainst.tga", 0x08, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorSitDown.tga", 0x09, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorDontMove.tga", 0x0a, TextureSetsDetail::LinearClamp},
    {"Interface\\Menu01_new.jpg", 0xe6, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu02.jpg", 0xe7, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu03_new.jpg", 0xe8, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu04.tga", 0xe9, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu_Red.jpg", 0xea, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu_Blue.jpg", 0xeb, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu_Party.jpg", 0xec, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu_Character.jpg", 0xed, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu_Inventory.jpg", 0xee, TextureSetsDetail::NearestClamp},
    {"Interface\\dialogue2_1.jpg", 0xef, TextureSetsDetail::NearestClamp},
    {"Interface\\Message_box.jpg", 0xf0, TextureSetsDetail::NearestClamp},
    {"Interface\\ok.jpg", 0xf1, TextureSetsDetail::NearestClamp},
    {"Interface\\ok2.jpg", 0xf2, TextureSetsDetail::NearestClamp},
    {"Interface\\cancel.jpg", 0xf3, TextureSetsDetail::NearestClamp},
    {"Interface\\cancel2.jpg", 0xf4, TextureSetsDetail::NearestClamp},
    {"Interface\\Level_box.jpg", 0xf5, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu_Green.jpg", 0xf7, TextureSetsDetail::NearestClamp},
    {"Interface\\dialogue1.jpg", 0xf8, TextureSetsDetail::NearestClamp},
    {"Interface\\dialogue2.jpg", 0xf9, TextureSetsDetail::NearestClamp},
    {"Interface\\Guild.jpg", 0xfa, TextureSetsDetail::NearestClamp},
    {"Interface\\Message_box1.jpg", 0xfb, TextureSetsDetail::NearestClamp},
    {"Interface\\Message_box2.jpg", 0xfc, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_rsframe.tga", 0x63d, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_vframe.jpg", 0x63e, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_bar.jpg", 0x63f, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_btnarrow.jpg", 0x641, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_btnchat.jpg", 0x642, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_btnsize.jpg", 0x643, TextureSetsDetail::NearestClamp},
    {"Interface\\nis_btnalpha.jpg", 0x644, TextureSetsDetail::NearestClamp},
    {"Interface\\win_titlebar.jpg", 0x645, TextureSetsDetail::NearestClamp},
    {"Interface\\win_button.tga", 0x646, TextureSetsDetail::NearestClamp},
    {"Interface\\win_size.jpg", 0x647, TextureSetsDetail::NearestClamp},
    {"Interface\\win_resize.tga", 0x648, TextureSetsDetail::NearestClamp},
    {"Interface\\win_scrollbar.jpg", 0x649, TextureSetsDetail::NearestClamp},
    {"Interface\\win_check.tga", 0x64a, TextureSetsDetail::NearestClamp},
    {"Interface\\win_mail.tga", 0x64b, TextureSetsDetail::NearestClamp},
    {"Interface\\win_mark.tga", 0x64c, TextureSetsDetail::NearestClamp},
    {"Interface\\win_letter.jpg", 0x64d, TextureSetsDetail::NearestClamp},
    {"Interface\\win_man.jpg", 0x64e, TextureSetsDetail::NearestClamp},
    {"Interface\\win_push.jpg", 0x64f, TextureSetsDetail::NearestClamp},
    {"Interface\\win_question.tga", 0x651, TextureSetsDetail::NearestClamp},
    {"Local\\Webzenlogo.jpg", 0x653, TextureSetsDetail::NearestClamp},
    {"Interface\\DuelWindow.jpg", 0x650, TextureSetsDetail::NearestClamp},
    {"Interface\\setitem.jpg", 0x652, TextureSetsDetail::NearestClamp},
    {"Interface\\btn_pshop.jpg", 0x657, TextureSetsDetail::NearestClamp},
    {"Interface\\btn_pshop2.jpg", 0x658, TextureSetsDetail::NearestClamp},
    {"Interface\\text_button.jpg", 0x659, TextureSetsDetail::NearestClamp},
    {"Interface\\CursorID.tga", 0x65a, TextureSetsDetail::NearestClamp},
    {"Interface\\CursorID_Wnd.jpg", 0x65b, TextureSetsDetail::NearestClamp},
    {"Interface\\menu01_new2.jpg", 0x65c, TextureSetsDetail::NearestClamp},
    {"Interface\\bar.jpg", 0xfd, TextureSetsDetail::NearestClamp},
    {"Interface\\back1.jpg", 0xfe, TextureSetsDetail::NearestClamp},
    {"Interface\\back2.jpg", 0xff, TextureSetsDetail::NearestClamp},
    {"Interface\\back3.jpg", 0x100, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu03_new_AG.jpg", 0x101, TextureSetsDetail::NearestClamp},
    {"Interface\\pet1.jpg", 0x65d, TextureSetsDetail::NearestClamp},
    {"Interface\\pet2.jpg", 0x65e, TextureSetsDetail::NearestClamp},
    {"Skill\\SwordEff.jpg", 0x640, TextureSetsDetail::LinearClamp},
    // 0x61e385..0x61e46e: siege controls, NEAREST/CLAMP.
    {"Interface\\BattleSkill.tga", 0x65f, TextureSetsDetail::NearestClamp},
    {"Interface\\senatusmap.jpg", 0x660, TextureSetsDetail::NearestClamp},
    {"Interface\\gate_button2.jpg", 0x661, TextureSetsDetail::NearestClamp},
    {"Interface\\gate_button1.jpg", 0x662, TextureSetsDetail::NearestClamp},
    {"Interface\\suho_button2.jpg", 0x663, TextureSetsDetail::NearestClamp},
    {"Interface\\suho_button1.jpg", 0x664, TextureSetsDetail::NearestClamp},
    {"Interface\\DoorCL.jpg", 0x665, TextureSetsDetail::NearestClamp},
    {"Interface\\DoorOP.jpg", 0x666, TextureSetsDetail::NearestClamp},
    {"Interface\\CursorAttack2.tga", 0x68d, TextureSetsDetail::NearestClamp},
    {"Interface\\StatusIcon.jpg", 0x68e, TextureSetsDetail::NearestClamp},
    // 0x61e678..0x61e77d: battle orders and states, before 0x61ae60.
    // 0x61e605 and 0x61e621: the siege command's progress, NEAREST/CLAMP.
    {"Interface\\Progress_Back.jpg", 0x667, TextureSetsDetail::NearestClamp},
    {"Interface\\Progress.jpg", 0x668, TextureSetsDetail::NearestClamp},
    {"Interface\\frame.tga", 0x69f, TextureSetsDetail::NearestClamp},
    {"Interface\\i_attack.tga", 0x6a0, TextureSetsDetail::NearestClamp},
    {"Interface\\i_defense.tga", 0x6a1, TextureSetsDetail::NearestClamp},
    {"Interface\\i_wait.tga", 0x6a2, TextureSetsDetail::NearestClamp},
    {"Interface\\b_command01.jpg", 0x6a4, TextureSetsDetail::NearestClamp},
    {"Interface\\b_command02.jpg", 0x6a5, TextureSetsDetail::NearestClamp},
    {"Interface\\b_group02.jpg", 0x6a6, TextureSetsDetail::NearestClamp},
    {"Interface\\b_zoomout01.jpg", 0x6a7, TextureSetsDetail::NearestClamp},
    {"Interface\\hourglass.tga", 0x6a3, TextureSetsDetail::NearestClamp},
    {"Interface\\dot.tga", 0x669, TextureSetsDetail::NearestClamp},
    {"Interface\\Skill.jpg", 0x12a, TextureSetsDetail::NearestClamp},
    {"Interface\\Skill2.jpg", 0x12b, TextureSetsDetail::NearestClamp},
    {"Interface\\command.jpg", 0x12c, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Back01.jpg", 0x104, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Back02.jpg", 0x105, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Cap.jpg", 0x107, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Upper.jpg", 0x108, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Lower.jpg", 0x109, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Weapon01.jpg", 0x10a, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Gloves.jpg", 0x10b, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Boots.jpg", 0x10c, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_NeckLace.jpg", 0x10d, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Ring.jpg", 0x10e, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Money.jpg", 0x10f, TextureSetsDetail::NearestClamp},
    {"Interface\\Money_01.jpg", 0x110, TextureSetsDetail::NearestClamp},
    {"Interface\\Money_02.jpg", 0x111, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Wing.jpg", 0x112, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Fairy.jpg", 0x113, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_Weapon02.jpg", 0x114, TextureSetsDetail::NearestClamp},
    {"Interface\\Item_box.jpg", 0x115, TextureSetsDetail::NearestClamp},
    {"Interface\\InventoryBox2.jpg", 0x116, TextureSetsDetail::NearestClamp},
    {"Interface\\Trading_line.jpg", 0x117, TextureSetsDetail::NearestClamp},
    {"Interface\\exit_01.jpg", 0x118, TextureSetsDetail::NearestClamp},
    {"Interface\\exit_02.jpg", 0x119, TextureSetsDetail::NearestClamp},
    {"Interface\\buy_01.jpg", 0x11a, TextureSetsDetail::NearestClamp},
    {"Interface\\buy_02.jpg", 0x11b, TextureSetsDetail::NearestClamp},
    {"Interface\\sell_01.jpg", 0x11c, TextureSetsDetail::NearestClamp},
    {"Interface\\sell_02.jpg", 0x11d, TextureSetsDetail::NearestClamp},
    {"Interface\\repair_01.jpg", 0x11e, TextureSetsDetail::NearestClamp},
    {"Interface\\repair_02.jpg", 0x11f, TextureSetsDetail::NearestClamp},
    {"Interface\\level_01.jpg", 0x120, TextureSetsDetail::NearestClamp},
    {"Interface\\level_02.jpg", 0x121, TextureSetsDetail::NearestClamp},
    // 0x61ae60: the trade's checked OK (0x5f3b40). 0x122 (accept_box01) is uploaded by the terrain (AcceptBoxTexture).
    {"Interface\\accept_box02.jpg", 0x123, TextureSetsDetail::NearestClamp},
    {"Interface\\mix_button1.jpg", 0x124, TextureSetsDetail::NearestClamp},
    {"Interface\\mix_button2.jpg", 0x125, TextureSetsDetail::NearestClamp},
    {"Interface\\lock_01.jpg", 0x126, TextureSetsDetail::NearestClamp},
    {"Interface\\lock_02.jpg", 0x127, TextureSetsDetail::NearestClamp},
    {"Interface\\lock_03.jpg", 0x128, TextureSetsDetail::NearestClamp},
    {"Interface\\lock_04.jpg", 0x129, TextureSetsDetail::NearestClamp},
    // Slots of the common load of 0x61ccb0 that are still alive on entering the world.
    {"Interface\\level01.jpg", 0xf6, TextureSetsDetail::NearestClamp}, // 0x61cfb5
    {"Interface\\Quest1.jpg", 0x102, TextureSetsDetail::NearestClamp}, // 0x61d47f
    {"Interface\\Quest2.jpg", 0x103, TextureSetsDetail::NearestClamp}, // 0x61d49b
}};

// The Crywolf interface textures that 0x61ccb0 uploads (0x61de5f..0x61e373), 0x6b6 to 0x6e2 in order, with GL_LINEAR
// and GL_CLAMP: 0x63bd00 the .tga files and 0x63b870 in_bar2.jpg. 0x4365c0 draws them (Scenes/CrywolfInterfaceDrawing):
// the state, the banner, the rank and the results. GameModels uploads them with the HUD's, and they stay.
inline constexpr std::array<Data::Textures::TextureSetEntry, 45> CrywolfTextureSet{{
    {"Interface\\in_bar.tga", 0x6b6, TextureSetsDetail::LinearClamp},
    {"Interface\\in_bar2.jpg", 0x6b7, TextureSetsDetail::LinearClamp},
    {"Interface\\in_deco.tga", 0x6b8, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main.tga", 0x6b9, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_icon_bal1.tga", 0x6ba, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_icon_dl1.tga", 0x6bb, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_icon_dl2.tga", 0x6bc, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_number1.tga", 0x6bd, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_number2.tga", 0x6be, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main2.tga", 0x6bf, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_failure.tga", 0x6c0, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_success.tga", 0x6c1, TextureSetsDetail::LinearClamp},
    {"Interface\\t_main.tga", 0x6c2, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_no1.tga", 0x6c3, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_no2.tga", 0x6c4, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_no3.tga", 0x6c5, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_ok1.tga", 0x6c6, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_ok2.tga", 0x6c7, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_ok3.tga", 0x6c8, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_yes1.tga", 0x6c9, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_yes2.tga", 0x6ca, TextureSetsDetail::LinearClamp},
    {"Interface\\m_b_yes3.tga", 0x6cb, TextureSetsDetail::LinearClamp},
    {"Interface\\m_main.tga", 0x6cc, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_number1_1.tga", 0x6cd, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_number2_1.tga", 0x6ce, TextureSetsDetail::LinearClamp},
    {"Interface\\in_main_number0_2.tga", 0x6cf, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_table.tga", 0x6d0, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_rank.tga", 0x6d1, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_D.tga", 0x6d2, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_C.tga", 0x6d3, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_B.tga", 0x6d4, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_A.tga", 0x6d5, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_S.tga", 0x6d6, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_0.tga", 0x6d7, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_1.tga", 0x6d8, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_2.tga", 0x6d9, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_3.tga", 0x6da, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_4.tga", 0x6db, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_5.tga", 0x6dc, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_6.tga", 0x6dd, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_7.tga", 0x6de, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_8.tga", 0x6df, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_9.tga", 0x6e0, TextureSetsDetail::LinearClamp},
    {"Interface\\icon_Rank_exp.tga", 0x6e1, TextureSetsDetail::LinearClamp},
    {"Interface\\m_main_rank.tga", 0x6e2, TextureSetsDetail::LinearClamp},
}};

// The glow pass textures, which 0x61ccb0 uploads in its step 0 and nobody releases: Shiny01 for 0x8, Chrome01 for 0x4
// and Chrome02 for 0x200, 0x800 and 0x1000 (06 § Los pases de brillo). They belong to no scene: GameModels uploads them
// with the models, and they stay.
inline constexpr std::array<Data::Textures::TextureSetEntry, 3> GlowTextureSet{{
    {"Effect\\Shiny01.jpg", 0x5fc, TextureSetsDetail::LinearClamp},
    {"Effect\\Chrome01.jpg", 0x5c0, TextureSetsDetail::LinearRepeat},
    {"Effect\\Chrome02.jpg", 0x620, TextureSetsDetail::NearestClamp},
}};

// The login sprites' and particles' textures that 0x61ccb0 uploads (0x61d6a5, 0x61d925, 0x61d737 and 0x61d753, 0x63b870
// with GL_LINEAR and GL_CLAMP) and nobody releases: Effect\flare01.jpg at 0x5ac (the cape's flashes and the sceptre's
// glow), Effect\Shiny02.jpg at 0x5fd (the sceptre), Effect\Spark03.jpg at 0x5c6 (the raven's sparks) and
// Effect\smoke01.jpg at 0x5f2 (the horse's breath). 0x61d768 uploads Effect\smoke02.tga to 0x5f3 with 0x63bd00, the
// dust of the horse's gallop, with the same sampling. GameModels uploads them with the models, and they stay. 0x61d801
// uploads clouds.jpg to 0x622 with GL_LINEAR and GL_CLAMP: the dark clouds of map 55. 0x61d6e0 and 0x61d6fc upload
// Magic_Ground2 and Magic_Circle1 with the same sampling: the decal and the two rings of the arrival effect 0x61f (06 §
// Después de los objetos).
//
// 0x61d4db uploads Fire01 to 0x5d9, GL_LINEAR/GL_CLAMP, for the objects' flames. Noria adds Spark02/0x5c5 (0x61d711),
// lightning2/0x5d5 (0x61da3c), Spark01/0x617 (0x61da77) and flareBlue/0x62b (0x61dc9d), all GL_LINEAR/GL_CLAMP; the
// last two have their own physical slots. What hangs from a player (0x509340 and 0x50ba30) also asks for Shiny03 at
// 0x5fe (0x61d937) and Object9's Impack03 at 0x5ad (0x61e799), from the same branchless loop of 0x61ccb0 and with the
// same sampling.
inline constexpr std::array<Data::Textures::TextureSetEntry, 81> EffectTextureSet{{
    {"Effect\\Fire01.jpg", 0x5d9, TextureSetsDetail::LinearClamp},
    // 0x61d523: Fire03 for particle 0x5db/0 of Crywolf's effect 0x147/1.
    {"Effect\\Fire03.jpg", 0x5db, TextureSetsDetail::LinearClamp},
    // 0x61d663: ShockWave at 0x6eb, the wave of the horse's gallop in Icarus (0x44431b).
    {"Effect\\ShockWave.jpg", 0x6eb, TextureSetsDetail::LinearClamp},
    // 0x61d67f: Flame01 at 0x5de, for flame 0x5de/7 of object 0x43 on map 54.
    {"Effect\\Flame01.jpg", 0x5de, TextureSetsDetail::LinearClamp},
    {"Effect\\flare01.jpg", 0x5ac, TextureSetsDetail::LinearClamp},
    {"Effect\\Magic_Ground2.jpg", 0x61e, TextureSetsDetail::LinearClamp},
    {"Effect\\Magic_Circle1.jpg", 0x61f, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark02.jpg", 0x5c5, TextureSetsDetail::LinearClamp},
    {"Effect\\Shiny02.jpg", 0x5fd, TextureSetsDetail::LinearClamp},
    {"Effect\\Shiny03.jpg", 0x5fe, TextureSetsDetail::LinearClamp},
    {"Object9\\Impack03.jpg", 0x5ad, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark03.jpg", 0x5c6, TextureSetsDetail::LinearClamp},
    {"Effect\\Thunder01.jpg", 0x5ca, TextureSetsDetail::LinearRepeat}, // 0x61da58/0x61da62
    {"Effect\\smoke01.jpg", 0x5f2, TextureSetsDetail::LinearClamp},
    {"Effect\\smoke02.tga", 0x5f3, TextureSetsDetail::LinearClamp},
    {"Effect\\smoke05.tga", 0x5f6, TextureSetsDetail::LinearClamp},
    // 0x61d7e5 (0x63b870, GL_LINEAR and GL_CLAMP): Explotion01 at 0x5ed, the explosion 0x5ed/0 of earthquake 0xfa.
    {"Effect\\Explotion01.jpg", 0x5ed, TextureSetsDetail::LinearClamp},
    // 0x61d7aa (0x63bd00, GL_LINEAR and GL_CLAMP): blood01.tga at 0x5e3, the two ground marks left by one who finishes
    // dying (0x52e810), which 0x549130 draws with this slot's components.
    {"Effect\\blood01.tga", 0x5e3, TextureSetsDetail::LinearClamp},
    // 0x61d7c9 (0x63bd00, GL_LINEAR and GL_CLAMP): blood.tga at 0x5e4, the blood a damaging hit sheds (0x50419c).
    {"Effect\\blood.tga", 0x5e4, TextureSetsDetail::LinearClamp},
    {"Effect\\clouds.jpg", 0x622, TextureSetsDetail::LinearClamp},
    {"Effect\\lightning2.jpg", 0x5d5, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark01.jpg", 0x617, TextureSetsDetail::LinearClamp},
    {"Effect\\flareBlue.jpg", 0x62b, TextureSetsDetail::LinearClamp},
    // 0x61d5bf and 0x61dde7..0x61de5a: Kalima's native families, LINEAR/CLAMP, arguments 0/1.
    {"Effect\\fantaB.jpg", 0x6ed, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL1.jpg", 0x6f0, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL2.jpg", 0x6f1, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL3.jpg", 0x6f2, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL4.jpg", 0x6f3, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL5.jpg", 0x6f4, TextureSetsDetail::LinearClamp},
    // 0x61d55e, 0x61d57a (0x63bd00) and 0x61dbd3: the spark and flame of the Blood Castle torch and orbit 0x60f, also
    // the sprite of its 0xd; LINEAR/CLAMP.
    {"Effect\\fi01.jpg", 0x6ab, TextureSetsDetail::LinearClamp},
    {"Effect\\fi02.tga", 0x6ac, TextureSetsDetail::LinearClamp},
    {"Effect\\PoundingBall.jpg", 0x6ad, TextureSetsDetail::LinearClamp},
    // 0x61dbb4: the trail of fireworks 0x60e.
    {"Effect\\Fire04.jpg", 0x60d, TextureSetsDetail::LinearClamp},
    {"Effect\\Flare.jpg", 0x60f, TextureSetsDetail::LinearClamp},
    // 0x61dc0b: Chrome03 for the Crywolf trail and the monsters' glows, NEAREST/CLAMP.
    {"Effect\\Chrome03.jpg", 0x6b1, TextureSetsDetail::NearestClamp},
    // 0x61d5a0 and 0x61d893, LINEAR/CLAMP: 0x6ec/5 of Crywolf's 0x39 and 0x47 and the bubble 0x698/0 of its water.
    {"Effect\\fantaF.jpg", 0x6ec, TextureSetsDetail::LinearClamp},
    {"Effect\\coll.jpg", 0x698, TextureSetsDetail::LinearClamp},
    // 0x61e7bd (0x63b870, GL_LINEAR and GL_CLAMP): cra_04 at 0x5ae, the texture (+0x8) of 0x6f1/3 of the full set's
    // boots (0x544095).
    {"Effect\\cra_04.jpg", 0x5ae, TextureSetsDetail::LinearClamp},
    // 0x61e55c..0x61e5f0 (0x63b870, GL_NEAREST and GL_CLAMP): eff_lighting at 0x68f, pass 0x46 of mesh 1 of the Fenrir
    // with action 4 (0x60282a), and eff_lightinga01..05 at 0x690..0x694, the frames of decal 0x14e (0x52d906).
    {"Effect\\eff_lighting.jpg", 0x68f, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga01.jpg", 0x690, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga02.jpg", 0x691, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga03.jpg", 0x692, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga04.jpg", 0x693, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga05.jpg", 0x694, TextureSetsDetail::NearestClamp},
    // 0x61e4eb..0x61e542: the crater and the marks of map 30, NEAREST/CLAMP.
    {"Effect\\Cratered.tga", 0x636, TextureSetsDetail::NearestClamp},
    {"Effect\\FormationMark.tga", 0x637, TextureSetsDetail::NearestClamp},
    {"Effect\\Plus.tga", 0x638, TextureSetsDetail::NearestClamp},
    // Persistent materials of 0x61ccb0, checked against the Lorencia slots.
    {"Effect\\Piercing.jpg", 0x6e8, TextureSetsDetail::LinearRepeat},     // 0x61d5f7
    {"Effect\\Magic_b.jpg", 0x6e9, TextureSetsDetail::NearestClamp},      // 0x61d632
    {"Effect\\Magic_Ground1.jpg", 0x61d, TextureSetsDetail::LinearClamp}, // 0x61d6c4
    {"Effect\\BowE.jpg", 0x699, TextureSetsDetail::LinearClamp},          // 0x61d8af
    {"Effect\\Shockwave.jpg", 0x69a, TextureSetsDetail::LinearClamp},     // 0x61d8ce
    {"Effect\\DinoE.jpg", 0x5ee, TextureSetsDetail::LinearClamp},         // 0x61d8ea
    {"Effect\\eye01.jpg", 0x5ff, TextureSetsDetail::LinearClamp},         // 0x61d95d
    {"Effect\\ring.jpg", 0x600, TextureSetsDetail::LinearClamp},          // 0x61d97c
    {"Effect\\bab2.jpg", 0x5c1, TextureSetsDetail::LinearRepeat},         // 0x61d9d3
    {"Effect\\JointLaser02.jpg", 0x61b, TextureSetsDetail::LinearRepeat}, // 0x61db10
    {"Monster\\iui03.jpg", 0x624, TextureSetsDetail::LinearRepeat},       // 0x61db2f
    {"Monster\\magic_H.tga", 0x629, TextureSetsDetail::LinearRepeat},     // 0x61db4b
    {"Item\\t_lower_14m.tga", 0x62a, TextureSetsDetail::LinearRepeat},    // 0x61db67
    {"Skill\\Skull.jpg", 0x628, TextureSetsDetail::NearestClamp},         // 0x61db86
    {"Effect\\Chrome06.jpg", 0x6b2, TextureSetsDetail::NearestClamp},     // 0x61dc34
    {"Effect\\Chrome07.jpg", 0x6b3, TextureSetsDetail::NearestClamp},     // 0x61dc50
    {"Effect\\energy01.jpg", 0x6b4, TextureSetsDetail::NearestClamp},     // 0x61dc6c
    {"Item\\nfm03.jpg", 0x6b5, TextureSetsDetail::NearestRepeat},         // 0x61dc8b
    {"Monster\\King11.jpg", 0x62e, TextureSetsDetail::LinearClamp},       // 0x61dcfe
    {"Monster\\Kni000.jpg", 0x62f, TextureSetsDetail::LinearClamp},       // 0x61dd1a
    {"Monster\\Kni011.jpg", 0x630, TextureSetsDetail::LinearClamp},       // 0x61dd39
    {"Monster\\Kni022.jpg", 0x631, TextureSetsDetail::LinearClamp},       // 0x61dd55
    {"Skill\\pinkWave.jpg", 0x6ef, TextureSetsDetail::LinearClamp},       // 0x61dd71
    {"Effect\\flareRed.jpg", 0x62d, TextureSetsDetail::LinearClamp},      // 0x61dd90
    {"Effect\\Fire05.jpg", 0x5dc, TextureSetsDetail::LinearClamp},        // 0x61ddac
    {"Effect\\Hole.jpg", 0x6ae, TextureSetsDetail::LinearRepeat},         // 0x61ddc8
    {"Effect\\Flashing.jpg", 0x632, TextureSetsDetail::LinearClamp},      // 0x61e3ae
    {"Effect\\Inferno.jpg", 0x633, TextureSetsDetail::LinearClamp},       // 0x61e647
    {"Effect\\Lava.jpg", 0x634, TextureSetsDetail::LinearClamp},          // 0x61e666
    {"Effect\\FireSnuff.jpg", 0x635, TextureSetsDetail::LinearClamp},     // 0x61d877/0x61e494
    // Fixed slots of 0x615700, outside its mesh name table.
    {"Skill\\dkthreebody_r.jpg", 0x6f7, TextureSetsDetail::LinearRepeat}, // 0x616043
    {"Skill\\flower1.tga", 0x608, TextureSetsDetail::NearestClamp},       // 0x616e6f
    {"Skill\\flower2.tga", 0x609, TextureSetsDetail::NearestClamp},       // 0x616e8c
    {"Skill\\flower3.tga", 0x60a, TextureSetsDetail::NearestClamp},       // 0x616ea9
}};

// 0x61d83c: the common load uploads the slot and the selection frees it on leaving.
inline constexpr std::uint16_t StartupSelectionTextureSlot = 0x716;
inline constexpr std::array<Data::Textures::TextureSetEntry, 1> StartupSelectionTextureSet{{
    {"Effect\\Impack03.jpg", StartupSelectionTextureSlot, TextureSetsDetail::LinearClamp},
}};

// Resources of the joints the fish and birds already create: 0x61d858/0x61dab9/0x61dad8/0x61dce2. 0x61d5db uploads
// JointSpirit02 to 0x6ee (LINEAR/CLAMP): the meteor 0x6ee/9 of Chaos Castle (0x4054ad). Spark01 (0x617) is already in
// EffectTextureSet. Ribbons without a ported creator are not loaded here. 0x61daef uploads JointEnergy01 to 0x619
// (LINEAR/CLAMP): the 0x619/9 that the full set's 0x5ac/3 sheds.
inline constexpr std::array<Data::Textures::TextureSetEntry, 6> FaunaJointTextureSet{{
    {"Effect\\JointSpirit02.jpg", 0x6ee, TextureSetsDetail::LinearClamp},
    {"Effect\\ScolTail.jpg", 0x697, TextureSetsDetail::LinearClamp},
    {"Effect\\JointSpirit01.jpg", 0x613, TextureSetsDetail::LinearClamp},
    {"Effect\\JointLaser01.jpg", 0x618, TextureSetsDetail::LinearClamp},
    {"Effect\\Flare02.jpg", 0x610, TextureSetsDetail::LinearRepeat},
    {"Effect\\JointEnergy01.jpg", 0x619, TextureSetsDetail::LinearClamp},
}};

// Tail P-C55: 0x61d511/0x61d9b4/0x61d9ef/0x61da0b/0x61da2a/0x61da9d/0x61dba2/0x61dcc3. 0x615 has no upload of its own:
// 0x5319ea remaps its texture to 0x614. 0x61e4b3 (0x63bd00, GL_LINEAR and GL_CLAMP): Object31\Flag.tga at 0x69c, that
// of mesh 1 of 0xc on map 30 (0x42cc89).
inline constexpr std::array<Data::Textures::TextureSetEntry, 9> GameTailTextureSet{{
    {"Effect\\Fire02.jpg", 0x5da, TextureSetsDetail::LinearClamp},
    {"Effect\\blur01.jpg", 0x5bb, TextureSetsDetail::NearestClamp},
    {"Effect\\motion_blur.jpg", 0x5bc, TextureSetsDetail::NearestClamp},
    {"Effect\\motion_blur_r.jpg", 0x5bd, TextureSetsDetail::NearestClamp},
    {"Effect\\blur02.jpg", 0x639, TextureSetsDetail::NearestClamp},
    {"Effect\\JointThunder01.jpg", 0x614, TextureSetsDetail::LinearRepeat},
    {"Effect\\motion_blur_r2.jpg", 0x61c, TextureSetsDetail::LinearRepeat},
    {"Effect\\NSkill.jpg", 0x62c, TextureSetsDetail::LinearClamp},
    {"Object31\\Flag.tga", 0x69c, TextureSetsDetail::LinearClamp},
}};

// The two cloth layers that 0x61ccb0 uploads (0x61d616 and 0x61d64e) and nobody releases: Monster\iui06.jpg at 0x256,
// for the Dark Lord's two front cloths (0x63b870 with GL_NEAREST and GL_CLAMP), and Player\kaa.tga at 0x6ea, for its
// fourth (0x63bd00 with GL_LINEAR and GL_CLAMP). The other layers (Robe01 to 03 and DarklordRobe) belong to the
// character's (PlayerFixedTextures).
inline constexpr std::array<Data::Textures::TextureSetEntry, 2> ClothTextureSet{{
    {"Monster\\iui06.jpg", 0x256, TextureSetsDetail::NearestClamp},
    {"Player\\kaa.tga", 0x6ea, TextureSetsDetail::LinearClamp},
}};

// The 18 of 0x61c7c0, the login scene. Model 0xa2 (Logo\Logo03.bmd), which 0x61c7c0 also loads, is not a texture: it
// comes with the models.
inline constexpr std::array<Data::Textures::TextureSetEntry, 18> LoginTextureSet{{
    {"Logo\\Login_Back01.jpg", 0x15, TextureSetsDetail::NearestClamp},
    {"Logo\\Login_Back02.jpg", 0x16, TextureSetsDetail::NearestClamp},
    {"Object9\\sand01.jpg", 0x5c2, TextureSetsDetail::LinearRepeat},
    {"Object9\\sand02.jpg", 0x5c3, TextureSetsDetail::LinearRepeat},
    {"Effect\\FireSnuff.jpg", 0x635, TextureSetsDetail::LinearClamp},
    {"Interface\\cha_bt.tga", 0x0c, TextureSetsDetail::NearestClamp},
    {"Interface\\server_b2_all.tga", 0x0d, TextureSetsDetail::NearestClamp},
    {"Interface\\server_b2_loding.jpg", 0x0e, TextureSetsDetail::NearestClamp},
    {"Interface\\server_deco_all.tga", 0x0f, TextureSetsDetail::NearestClamp},
    {"Interface\\server_menu_b_all.tga", 0x10, TextureSetsDetail::NearestClamp},
    {"Interface\\server_credit_b_all.tga", 0x11, TextureSetsDetail::NearestClamp},
    {"Interface\\deco.tga", 0x12, TextureSetsDetail::NearestClamp},
    {"Interface\\login_back.tga", 0x13, TextureSetsDetail::NearestClamp},
    {"Interface\\login_me.tga", 0x14, TextureSetsDetail::NearestClamp},
    {"Interface\\server_ex03.tga", 0x17, TextureSetsDetail::NearestRepeat},
    {"Interface\\server_ex01.tga", 0x18, TextureSetsDetail::NearestClamp},
    {"Interface\\server_ex02.jpg", 0x19, TextureSetsDetail::NearestRepeat},
    {"Interface\\cr_mu_lo.tga", 0x1a, TextureSetsDetail::LinearClamp},
}};

// The textures of 0x61ccb0 that the login, the selection and the game use: the two cursors (0x61ccf9 and 0x61cd12, with
// filter, UI/GameCursor.h), and the windows': the OK and cancel of the account and of the notice, the notice box and
// the delete code box, without filter and without repeat (0x2600 and 0x2900). The original uploads them with the game
// data, at load, and does not release them; here each scene uploads them with its own and releases them on leaving. The
// other cursors, 4 to 0xa, are the HUD's (HudTextureSet). Also close (0x726), which the credits use
// (UI/CreditsWindow.h); the six of the Esc window (UI/EscapeWindow.h), the frame of 0x66a1c0 (the stone, op1_back3 and
// op1_back4 repeated) and the buttons; and the five of the options (UI/OptionsWindow.h), the top border, the checkboxes
// and the slider, with the fill repeated.
inline constexpr std::array<Data::Textures::TextureSetEntry, 18> WindowTextureSet{{
    {"Interface\\Cursor.tga", 0x02, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorPush.tga", 0x03, TextureSetsDetail::LinearClamp},
    {"Interface\\message_ok_b_all.tga", 0x724, TextureSetsDetail::NearestClamp},
    {"Interface\\loding_cancel_b_all.tga", 0x725, TextureSetsDetail::NearestClamp},
    {"Interface\\message_close_b_all.tga", 0x726, TextureSetsDetail::NearestClamp},
    {"Interface\\message_back.tga", 0x727, TextureSetsDetail::NearestClamp},
    {"Interface\\delete_secret_number.tga", 0x728, TextureSetsDetail::NearestClamp},
    {"Interface\\op1_stone.jpg", 0x72a, TextureSetsDetail::NearestRepeat},
    {"Interface\\op1_back1.tga", 0x72b, TextureSetsDetail::NearestClamp},
    {"Interface\\op1_back2.tga", 0x72c, TextureSetsDetail::NearestClamp},
    {"Interface\\op1_back3.jpg", 0x72d, TextureSetsDetail::NearestRepeat},
    {"Interface\\op1_back4.jpg", 0x72e, TextureSetsDetail::NearestRepeat},
    {"Interface\\op1_b_all.tga", 0x72f, TextureSetsDetail::NearestClamp},
    {"Interface\\op2_back1.tga", 0x730, TextureSetsDetail::NearestClamp},
    {"Interface\\op2_ch.tga", 0x731, TextureSetsDetail::NearestClamp},
    {"Interface\\op2_volume3.tga", 0x732, TextureSetsDetail::NearestClamp},
    {"Interface\\op2_volume2.jpg", 0x733, TextureSetsDetail::NearestRepeat},
    {"Interface\\op2_volume1.tga", 0x734, TextureSetsDetail::NearestClamp},
}};

// The credits illustrations (0x63fc50; UI/CreditsWindow.h): the +0x5c8 pair at 0x6f8 and 0x6f9, with filter and without
// repeat (0x63b870 with 0x2601 and 0x2900), from the names of +0x5cc. The original uploads them when the credits start
// or move to the next pair, over the ones there were; MuRet's login releases the previous pair and uploads the new one.
inline constexpr std::array<std::array<Data::Textures::TextureSetEntry, 2>, 8> CreditIllustrationSets{
    {{{{"Interface\\im1_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im1_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im2_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im2_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im3_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im3_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im4_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im4_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im5_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im5_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im6_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im6_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im7_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im7_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}},
     {{{"Interface\\im8_1.jpg", 0x6f8, TextureSetsDetail::LinearClamp},
       {"Interface\\im8_2.jpg", 0x6f9, TextureSetsDetail::LinearClamp}}}}};

// The Tarkan sands and the Crywolf and siege smoke that the game HUD draws (0x5a7520): 0x61c7c0, the login, uploads
// them with GL_LINEAR and GL_REPEAT and does not release them, and the game uses them as they are. MuRet releases them
// with the login (LoginTextureSet): the game uploads and releases its own.
inline constexpr std::array<Data::Textures::TextureSetEntry, 2> SandTextureSet{{
    {"Object9\\sand01.jpg", 0x5c2, TextureSetsDetail::LinearRepeat},
    {"Object9\\sand02.jpg", 0x5c3, TextureSetsDetail::LinearRepeat},
}};

// The six of 0x622e40, the loading scene: with filter, and only the bar (0x723) repeated. It releases them with
// 0x63c150 before moving to the login.
inline constexpr std::array<Data::Textures::TextureSetEntry, 6> LoadingTextureSet{{
    {"Interface\\lo_back_01.jpg", 0x71e, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_back_02.jpg", 0x71f, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_mu_logo.tga", 0x720, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_121518.tga", 0x721, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_webzen_logo.tga", 0x722, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_lo.jpg", 0x723, TextureSetsDetail::LinearRepeat},
}};

// The twelve of 0x61ca30, the selection: those of Interface\ at 0xc to 0x13, the two Logo\ backgrounds, Impack03 and
// chasellight, without filter and without repeat (0x2600, 0x2900). 0x61cc50 releases them on entering the world.
inline constexpr std::array<Data::Textures::TextureSetEntry, 12> SelectionTextureSet{{
    {"Interface\\cha_id.tga", 0xc, TextureSetsDetail::NearestClamp},
    {"Interface\\cha_bt.tga", 0xd, TextureSetsDetail::NearestClamp},
    {"Interface\\deco.tga", 0xe, TextureSetsDetail::NearestClamp},
    {"Interface\\b_create.tga", 0xf, TextureSetsDetail::NearestClamp},
    {"Interface\\server_menu_b_all.tga", 0x10, TextureSetsDetail::NearestClamp},
    {"Interface\\b_connect.tga", 0x11, TextureSetsDetail::NearestClamp},
    {"Interface\\b_delete.tga", 0x12, TextureSetsDetail::NearestClamp},
    {"Interface\\character_ex.tga", 0x13, TextureSetsDetail::NearestClamp},
    {"Logo\\character_back01.jpg", 0x717, TextureSetsDetail::NearestClamp},
    {"Logo\\character_back02.jpg", 0x718, TextureSetsDetail::NearestClamp},
    {"Effect\\Impack03.jpg", 0x719, TextureSetsDetail::NearestClamp},
    {"Logo\\chasellight.jpg", 0x729, TextureSetsDetail::NearestClamp},
}};

// The four of 0x6356f0, the world loading: the LSBg01 to 04 mosaic, with filter and without repeat (0x2601, 0x2900).
// It releases them on moving to the game.
inline constexpr std::array<Data::Textures::TextureSetEntry, 4> WorldLoadingTextureSet{{
    {"Interface\\LSBg01.JPG", 0x71e, TextureSetsDetail::LinearClamp},
    {"Interface\\LSBg02.JPG", 0x71f, TextureSetsDetail::LinearClamp},
    {"Interface\\LSBg03.JPG", 0x720, TextureSetsDetail::LinearClamp},
    {"Interface\\LSBg04.JPG", 0x721, TextureSetsDetail::LinearClamp},
}};
} // namespace Client::Assets
