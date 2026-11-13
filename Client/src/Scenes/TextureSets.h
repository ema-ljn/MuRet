#pragma once

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <array>

// Las texturas que sube cada escena al entrar, como datos: la ruta como la pide el original, el
// slot de su tabla y el tercer y cuarto argumento del cargador, en el orden de las llamadas
// (Client/docs/research/06-render-escenas-y-ui.md, "La carga" y "El login, al entrar"). Las escenas
// de la CL5.4 las cargan con Data::Textures::LoadTextureSet; un mundo tendrá la suya, con su
// carpeta (CL7).
namespace Client::Scenes
{
namespace TextureSetsDetail
{
using Data::Textures::Filter;
using Data::Textures::Sampling;
using Data::Textures::TextureSetEntry;
using Data::Textures::Wrap;

// 0x2600 y 0x2900; 0x2601 y 0x2900; 0x2601 y 0x2901; 0x2600 y 0x2901.
inline constexpr Sampling NearestClamp{Filter::Nearest, Wrap::Clamp};
inline constexpr Sampling LinearClamp{Filter::Linear, Wrap::Clamp};
inline constexpr Sampling LinearRepeat{Filter::Linear, Wrap::Repeat};
inline constexpr Sampling NearestRepeat{Filter::Nearest, Wrap::Repeat};
} // namespace TextureSetsDetail

// 0x61bd9f..0x61bdb4: FontTest en el slot1 con NEAREST/CLAMP, antes de la carga común.
// Es el atlas que 0x621710 usa para las cifras y "miss" de 0x548efd, no la fuente dinámica del slot0.
inline constexpr std::array<Data::Textures::TextureSetEntry, 1> DamageTextureSet{{
    {"Interface\\FontTest.tga", 0x01, TextureSetsDetail::NearestClamp},
}};

// Las del HUD del juego que sube 0x61ccb0 (0x61cce0..0x61e5f0, CL8 hito 2), en su orden y con su sampling: los
// cursores del juego (0x63bd00 con GL_LINEAR y GL_CLAMP), la barra de abajo (GL_NEAREST y GL_CLAMP), el AG, el
// botón de los comandos, los de la UI vieja que dibuja 0x4a5120, la caja de la entrada del chat (0xef, 0xf8 y 0xf9,
// que dibuja 0x5aa9a0), los diálogos que dibuja 0x62db60 (0xf0 a 0xf4 y 0xfb, hito 6), el registro del chat (hito 3),
// el cursor de identificar, el de atacar en el asedio y los íconos de estado; y las de las skills, que sube 0x61ae60
// desde 0x61ccb0. Las sube GameModels con los modelos, y quedan. El 2 y el 3, Cursor y CursorPush, no: son de
// WindowTextureSet, que cada escena sube y suelta, y el juego también. La pista del registro, nis_back en 0x640, la
// pisa Skill\SwordEff.jpg con GL_LINEAR más adelante en la misma función (0x61ccb0, Q-C463): queda la de la skill.
// Las de las ventanas del paso que son marco (hito 6): win_check en 0x64a (0x61d1f0, con 0x63bd00), la casilla de los
// comandos; CursorID_Wnd en 0x65b (0x61d3ab), el cartel del personaje elegido. Las del inventario (hito 4): setitem,
// btn_pshop y btn_pshop2 en 0x652, 0x657 y 0x658 (0x61d319..0x61d354). Y las de 0x104 a 0x120 que 0x61ae60 sube
// después de command.jpg (0x61aec8..0x61b1f2), de los hitos 4 y 6: el fondo de las ventanas, los casilleros del
// equipo, la plata, la grilla, la línea del comercio y los botones, entre ellos el de cerrar (0x118). La 0x106 no la
// sube nadie.
inline constexpr std::array<Data::Textures::TextureSetEntry, 80> HudTextureSet{{
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
    {"Interface\\win_check.tga", 0x64a, TextureSetsDetail::NearestClamp},
    {"Interface\\win_letter.jpg", 0x64d, TextureSetsDetail::NearestClamp},
    {"Interface\\win_man.jpg", 0x64e, TextureSetsDetail::NearestClamp},
    {"Interface\\win_push.jpg", 0x64f, TextureSetsDetail::NearestClamp},
    {"Interface\\setitem.jpg", 0x652, TextureSetsDetail::NearestClamp},
    {"Interface\\btn_pshop.jpg", 0x657, TextureSetsDetail::NearestClamp},
    {"Interface\\btn_pshop2.jpg", 0x658, TextureSetsDetail::NearestClamp},
    {"Interface\\CursorID.tga", 0x65a, TextureSetsDetail::NearestClamp},
    {"Interface\\CursorID_Wnd.jpg", 0x65b, TextureSetsDetail::NearestClamp},
    {"Interface\\menu01_new2.jpg", 0x65c, TextureSetsDetail::NearestClamp},
    {"Interface\\Menu03_new_AG.jpg", 0x101, TextureSetsDetail::NearestClamp},
    {"Skill\\SwordEff.jpg", 0x640, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorAttack2.tga", 0x68d, TextureSetsDetail::NearestClamp},
    {"Interface\\StatusIcon.jpg", 0x68e, TextureSetsDetail::NearestClamp},
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
}};

// Las de los pases de brillo, que 0x61ccb0 sube en su avance 0 y nadie suelta: Shiny01 para el 0x8,
// Chrome01 para el 0x4 y Chrome02 para el 0x200, el 0x800 y el 0x1000 (06 § Los pases de brillo).
// No son de una escena: las sube GameModels con los modelos, y quedan.
inline constexpr std::array<Data::Textures::TextureSetEntry, 3> GlowTextureSet{{
    {"Effect\\Shiny01.jpg", 0x5fc, TextureSetsDetail::LinearClamp},
    {"Effect\\Chrome01.jpg", 0x5c0, TextureSetsDetail::LinearRepeat},
    {"Effect\\Chrome02.jpg", 0x620, TextureSetsDetail::NearestClamp},
}};

// Las de los sprites y las partículas del login que sube 0x61ccb0 (0x61d6a5, 0x61d925, 0x61d737 y
// 0x61d753, 0x63b870 con GL_LINEAR y GL_CLAMP) y nadie suelta: Effect\flare01.jpg en 0x5ac (los destellos
// de la capa y el brillo del cetro), Effect\Shiny02.jpg en 0x5fd (el cetro), Effect\Spark03.jpg en 0x5c6
// (las chispas del cuervo) y Effect\smoke01.jpg en 0x5f2 (el aliento del caballo). 0x61d768 sube con 0x63bd00
// Effect\smoke02.tga a 0x5f3, el polvo del galope del caballo, con el mismo sampling. Las sube GameModels
// con los modelos, y quedan. 0x61d801 sube clouds.jpg a 0x622 con GL_LINEAR y GL_CLAMP: las nubes
// oscuras del mapa 55. 0x61d6e0 y 0x61d6fc suben Magic_Ground2 y Magic_Circle1 con el mismo sampling:
// el calco y los dos anillos del efecto de llegada 0x61f (06 § Después de los objetos).
// 0x61d4db sube Fire01 a 0x5d9, GL_LINEAR/GL_CLAMP, para las llamas de los objetos.
// Noria agrega Spark02/5c5 (61d711), lightning2/5d5 (61da3c), Spark01/617 (61da77) y
// flareBlue/62b (61dc9d), todos GL_LINEAR/GL_CLAMP; los dos últimos tienen slots físicos propios.
// Lo que cuelga de un jugador (0x509340 y 0x50ba30) pide además Shiny03 en 0x5fe (61d937) e Impack03 de
// Object9 en 0x5ad (61e799), del mismo lazo sin ramas de 0x61ccb0 y con el mismo sampling.
inline constexpr std::array<Data::Textures::TextureSetEntry, 38> EffectTextureSet{{
    {"Effect\\Fire01.jpg", 0x5d9, TextureSetsDetail::LinearClamp},
    // 0x61d663: ShockWave en 0x6eb, la onda del galope del caballo en Icarus (0x44431b).
    {"Effect\\ShockWave.jpg", 0x6eb, TextureSetsDetail::LinearClamp},
    // 0x61d67f: Flame01 en 0x5de, de la llama 5de/7 del 0x43 del 54.
    {"Effect\\Flame01.jpg", 0x5de, TextureSetsDetail::LinearClamp},
    {"Effect\\flare01.jpg", 0x5ac, TextureSetsDetail::LinearClamp},
    {"Effect\\Magic_Ground2.jpg", 0x61e, TextureSetsDetail::LinearClamp},
    {"Effect\\Magic_Circle1.jpg", 0x61f, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark02.jpg", 0x5c5, TextureSetsDetail::LinearClamp},
    {"Effect\\Shiny02.jpg", 0x5fd, TextureSetsDetail::LinearClamp},
    {"Effect\\Shiny03.jpg", 0x5fe, TextureSetsDetail::LinearClamp},
    {"Object9\\Impack03.jpg", 0x5ad, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark03.jpg", 0x5c6, TextureSetsDetail::LinearClamp},
    {"Effect\\Thunder01.jpg", 0x5ca, TextureSetsDetail::LinearRepeat}, // 61da58/61da62.
    {"Effect\\smoke01.jpg", 0x5f2, TextureSetsDetail::LinearClamp},
    {"Effect\\smoke02.tga", 0x5f3, TextureSetsDetail::LinearClamp},
    // 0x61d7e5 (0x63b870, GL_LINEAR y GL_CLAMP): Explotion01 en 0x5ed, la explosión 5ed/0 del terremoto 0xfa.
    {"Effect\\Explotion01.jpg", 0x5ed, TextureSetsDetail::LinearClamp},
    // 0x61d7c9 (0x63bd00, GL_LINEAR y GL_CLAMP): blood.tga en 0x5e4, la sangre que suelta el golpe con daño (0x50419c).
    {"Effect\\blood.tga", 0x5e4, TextureSetsDetail::LinearClamp},
    {"Effect\\clouds.jpg", 0x622, TextureSetsDetail::LinearClamp},
    {"Effect\\lightning2.jpg", 0x5d5, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark01.jpg", 0x617, TextureSetsDetail::LinearClamp},
    {"Effect\\flareBlue.jpg", 0x62b, TextureSetsDetail::LinearClamp},
    // 61d5bf y61dde7..61de5a: familias nativas de Kalima, LINEAR/CLAMP, argumentos0/1.
    {"Effect\\fantaB.jpg", 0x6ed, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL1.jpg", 0x6f0, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL2.jpg", 0x6f1, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL3.jpg", 0x6f2, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL4.jpg", 0x6f3, TextureSetsDetail::LinearClamp},
    {"Effect\\WATERFALL5.jpg", 0x6f4, TextureSetsDetail::LinearClamp},
    // 0x61d55e, 0x61d57a (0x63bd00) y 0x61dbd3: la chispa y la llama de la antorcha de Blood Castle y la órbita
    // 0x60f, también el sprite de su 0xd; LINEAR/CLAMP.
    {"Effect\\fi01.jpg", 0x6ab, TextureSetsDetail::LinearClamp},
    {"Effect\\fi02.tga", 0x6ac, TextureSetsDetail::LinearClamp},
    {"Effect\\Flare.jpg", 0x60f, TextureSetsDetail::LinearClamp},
    // 0x61d5a0 y 0x61d893, LINEAR/CLAMP: la 6ec/5 del 0x39 y el 0x47 de Crywolf y la burbuja 698/0 de su agua.
    {"Effect\\fantaF.jpg", 0x6ec, TextureSetsDetail::LinearClamp},
    {"Effect\\coll.jpg", 0x698, TextureSetsDetail::LinearClamp},
    // 0x61e7bd (0x63b870, GL_LINEAR y GL_CLAMP): cra_04 en 0x5ae, la textura (+0x8) de la 6f1/3 de las botas del
    // conjunto completo (0x544095).
    {"Effect\\cra_04.jpg", 0x5ae, TextureSetsDetail::LinearClamp},
    // 0x61e55c..0x61e5f0 (0x63b870, GL_NEAREST y GL_CLAMP): eff_lighting en 0x68f, el pase 0x46 de la malla 1 de los
    // Fenrir con la acción 4 (0x60282a), y eff_lightinga01..05 en 0x690..0x694, los cuadros del calco 0x14e (0x52d906).
    {"Effect\\eff_lighting.jpg", 0x68f, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga01.jpg", 0x690, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga02.jpg", 0x691, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga03.jpg", 0x692, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga04.jpg", 0x693, TextureSetsDetail::NearestClamp},
    {"Effect\\eff_lightinga05.jpg", 0x694, TextureSetsDetail::NearestClamp},
}};

// Recursos de los joints que ya crean los peces y pájaros: 61d858/61dab9/61dad8/61dce2. 61d5db sube
// JointSpirit02 a 0x6ee (LINEAR/CLAMP): el meteoro 6ee/9 de Chaos Castle (0x4054ad).
// Spark01 (0x617) ya está en EffectTextureSet. Las cintas sin creador portado no se cargan acá.
// 0x61daef sube JointEnergy01 a 0x619 (LINEAR/CLAMP): el 619/9 que suelta el 5ac/3 del conjunto completo.
inline constexpr std::array<Data::Textures::TextureSetEntry, 6> FaunaJointTextureSet{{
    {"Effect\\JointSpirit02.jpg", 0x6ee, TextureSetsDetail::LinearClamp},
    {"Effect\\ScolTail.jpg", 0x697, TextureSetsDetail::LinearClamp},
    {"Effect\\JointSpirit01.jpg", 0x613, TextureSetsDetail::LinearClamp},
    {"Effect\\JointLaser01.jpg", 0x618, TextureSetsDetail::LinearClamp},
    {"Effect\\Flare02.jpg", 0x610, TextureSetsDetail::LinearRepeat},
    {"Effect\\JointEnergy01.jpg", 0x619, TextureSetsDetail::LinearClamp},
}};

// Cola P-C55:61d511/61d9b4/61d9ef/61da0b/61da2a/61da9d/61dba2/61dcc3.
// 615 no tiene upload propio:5319ea remapea su textura a614.
// 0x61e4b3 (0x63bd00, GL_LINEAR y GL_CLAMP): Object31\Flag.tga en 0x69c, la de la malla 1 del 0xc del mapa
// 30 (0x42cc89).
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

// Las dos capas de la tela que sube 0x61ccb0 (0x61d616 y 0x61d64e) y nadie suelta: Monster\iui06.jpg en
// 0x256, de las dos telas de adelante del Dark Lord (0x63b870 con GL_NEAREST y GL_CLAMP), y
// Player\kaa.tga en 0x6ea, la de su cuarta (0x63bd00 con GL_LINEAR y GL_CLAMP). Las demás capas
// (Robe01 a 03 y DarklordRobe) son de las del personaje (PlayerFixedTextures).
inline constexpr std::array<Data::Textures::TextureSetEntry, 2> ClothTextureSet{{
    {"Monster\\iui06.jpg", 0x256, TextureSetsDetail::NearestClamp},
    {"Player\\kaa.tga", 0x6ea, TextureSetsDetail::LinearClamp},
}};

// Las 18 de 0x61c7c0, la escena de login. El modelo 0xa2 (Logo\Logo03.bmd), que 0x61c7c0 también
// carga, no es una textura: llega con los modelos (CL6).
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

// Las de 0x61ccb0 que usan el login, la selección y el juego: los dos cursores (0x61ccf9 y 0x61cd12,
// con filtro, UI/GameCursor.h), y lo de las ventanas (CL5.5 y CL6.5): el OK y el cancelar de la cuenta y
// del aviso, la caja del aviso y la del código del borrado, sin filtro y sin repetir (0x2600 y
// 0x2900). El original las sube con los datos del juego, en la carga, y no las suelta; acá las sube
// cada escena con las suyas y las suelta al salir. Los otros cursores, de 4 a 0xa, son del HUD
// (HudTextureSet). Y las del hito 8 de la CL8: el cerrar (0x726), que usan los créditos
// (UI/CreditsWindow.h); las seis de la ventana de Esc (UI/EscapeWindow.h), el marco de 0x66a1c0 (la
// piedra, op1_back3 y op1_back4 repetidas) y los botones; y las cinco de las opciones
// (UI/OptionsWindow.h), el borde de arriba, las casillas y el deslizador, con el relleno repetido.
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

// Las ilustraciones de los créditos (0x63fc50, CL8 hito 8; UI/CreditsWindow.h): el par +0x5c8 en 0x6f8
// y 0x6f9, con filtro y sin repetir (0x63b870 con 0x2601 y 0x2900), de los nombres de +0x5cc. El original
// las sube cuando los créditos empiezan o pasan al par siguiente, encima de las que había; el login de
// MuRet suelta el par anterior y sube el nuevo.
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

// Las arenas de Tarkan y el humo de Crywolf y del asedio que dibuja el HUD del juego (0x5a7520): 0x61c7c0, el
// login, las sube con GL_LINEAR y GL_REPEAT y no las suelta, y el juego las usa así. MuRet las suelta con el
// login (LoginTextureSet): el juego las sube y las suelta él.
inline constexpr std::array<Data::Textures::TextureSetEntry, 2> SandTextureSet{{
    {"Object9\\sand01.jpg", 0x5c2, TextureSetsDetail::LinearRepeat},
    {"Object9\\sand02.jpg", 0x5c3, TextureSetsDetail::LinearRepeat},
}};

// Las seis de 0x622e40, la escena de carga: con filtro, y repetida solo la barra (0x723). Las
// suelta con 0x63c150 antes de pasar al login.
inline constexpr std::array<Data::Textures::TextureSetEntry, 6> LoadingTextureSet{{
    {"Interface\\lo_back_01.jpg", 0x71e, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_back_02.jpg", 0x71f, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_mu_logo.tga", 0x720, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_121518.tga", 0x721, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_webzen_logo.tga", 0x722, TextureSetsDetail::LinearClamp},
    {"Interface\\lo_lo.jpg", 0x723, TextureSetsDetail::LinearRepeat},
}};

// Las doce de 0x61ca30, la selección: las de Interface\ en 0xc a 0x13, los dos fondos de Logo\,
// Impack03 y chasellight, sin filtro y sin repetir (0x2600, 0x2900). Las suelta 0x61cc50 al entrar
// al mundo.
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

// Las cuatro de 0x6356f0, la carga del mundo: el mosaico de LSBg01 a 04, con filtro y sin repetir
// (0x2601, 0x2900). Las suelta al pasar al juego.
inline constexpr std::array<Data::Textures::TextureSetEntry, 4> WorldLoadingTextureSet{{
    {"Interface\\LSBg01.JPG", 0x71e, TextureSetsDetail::LinearClamp},
    {"Interface\\LSBg02.JPG", 0x71f, TextureSetsDetail::LinearClamp},
    {"Interface\\LSBg03.JPG", 0x720, TextureSetsDetail::LinearClamp},
    {"Interface\\LSBg04.JPG", 0x721, TextureSetsDetail::LinearClamp},
}};
} // namespace Client::Scenes
