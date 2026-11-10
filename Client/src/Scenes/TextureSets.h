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
// (las chispas del cuervo) y Effect\smoke01.jpg en 0x5f2 (el aliento del caballo). Las sube GameModels
// con los modelos, y quedan. 0x61d801 sube clouds.jpg a 0x622 con GL_LINEAR y GL_CLAMP: las nubes
// oscuras del mapa 55. 0x61d6e0 y 0x61d6fc suben Magic_Ground2 y Magic_Circle1 con el mismo sampling:
// el calco y los dos anillos del efecto de llegada 0x61f (06 § Después de los objetos).
// 0x61d4db sube Fire01 a 0x5d9, GL_LINEAR/GL_CLAMP, para las llamas de los objetos.
// Noria agrega Spark02/5c5 (61d711), lightning2/5d5 (61da3c), Spark01/617 (61da77) y
// flareBlue/62b (61dc9d), todos GL_LINEAR/GL_CLAMP; los dos últimos tienen slots físicos propios.
inline constexpr std::array<Data::Textures::TextureSetEntry, 12> EffectTextureSet{{
    {"Effect\\Fire01.jpg", 0x5d9, TextureSetsDetail::LinearClamp},
    {"Effect\\flare01.jpg", 0x5ac, TextureSetsDetail::LinearClamp},
    {"Effect\\Magic_Ground2.jpg", 0x61e, TextureSetsDetail::LinearClamp},
    {"Effect\\Magic_Circle1.jpg", 0x61f, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark02.jpg", 0x5c5, TextureSetsDetail::LinearClamp},
    {"Effect\\Shiny02.jpg", 0x5fd, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark03.jpg", 0x5c6, TextureSetsDetail::LinearClamp},
    {"Effect\\smoke01.jpg", 0x5f2, TextureSetsDetail::LinearClamp},
    {"Effect\\clouds.jpg", 0x622, TextureSetsDetail::LinearClamp},
    {"Effect\\lightning2.jpg", 0x5d5, TextureSetsDetail::LinearClamp},
    {"Effect\\Spark01.jpg", 0x617, TextureSetsDetail::LinearClamp},
    {"Effect\\flareBlue.jpg", 0x62b, TextureSetsDetail::LinearClamp},
}};

// Recursos de los joints que ya crean los peces y pájaros: 61d858/61dab9/61dad8/61dce2.
// Spark01 (0x617) ya está en EffectTextureSet. Las cintas sin creador portado no se cargan acá.
inline constexpr std::array<Data::Textures::TextureSetEntry, 4> FaunaJointTextureSet{{
    {"Effect\\ScolTail.jpg", 0x697, TextureSetsDetail::LinearClamp},
    {"Effect\\JointSpirit01.jpg", 0x613, TextureSetsDetail::LinearClamp},
    {"Effect\\JointLaser01.jpg", 0x618, TextureSetsDetail::LinearClamp},
    {"Effect\\Flare02.jpg", 0x610, TextureSetsDetail::LinearRepeat},
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

// Las seis de 0x61ccb0 que usan el login y la selección: los dos cursores (0x61ccf9 y 0x61cd12, con
// filtro, UI/GameCursor.h), y lo de las ventanas (CL5.5 y CL6.5): el OK y el cancelar de la cuenta y
// del aviso, la caja del aviso y la del código del borrado, sin filtro y sin repetir (0x2600 y
// 0x2900). El original las sube con los datos del juego, en la carga, y no las suelta; acá las sube
// cada escena con las suyas y las suelta al salir. Las demás de 0x61ccb0 (los otros cursores, de 4 a
// 0xa, y el cerrar, 0x726) no las usa ninguna de las dos.
inline constexpr std::array<Data::Textures::TextureSetEntry, 6> WindowTextureSet{{
    {"Interface\\Cursor.tga", 0x02, TextureSetsDetail::LinearClamp},
    {"Interface\\CursorPush.tga", 0x03, TextureSetsDetail::LinearClamp},
    {"Interface\\message_ok_b_all.tga", 0x724, TextureSetsDetail::NearestClamp},
    {"Interface\\loding_cancel_b_all.tga", 0x725, TextureSetsDetail::NearestClamp},
    {"Interface\\message_back.tga", 0x727, TextureSetsDetail::NearestClamp},
    {"Interface\\delete_secret_number.tga", 0x728, TextureSetsDetail::NearestClamp},
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
