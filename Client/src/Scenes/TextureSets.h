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
} // namespace Client::Scenes
