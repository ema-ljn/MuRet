#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/SceneTextures.h"
#include "UI/Button.h"
#include "UI/ServerListModel.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// La ventana de la lista de servidores (CL5.5; 06 § La lista de servidores): la arma 0x6451d0, la
// ubica 0x645520 con 0x6456c0, le pone los rótulos 0x645860 y atiende los clics 0x645e10. 21
// botones de grupo, en dos columnas de 10 y uno abajo al medio, y en el medio los del grupo
// elegido: uno por servidor, o uno solo sin número si el grupo tiene uno. El original tiene 16 de
// servidor; MuRet tiene 20, uno por número (Q-C63). Hasta que se elige un grupo no hay ninguno
// (+0x3138 empieza en -1). *Estático*.
//
// Lo que no está: la caja de la descripción de abajo (0x645ca0 y 0x645ce0, el texto 1717 + el
// grupo partido en dos líneas por 0x632c90), las decoraciones del marco (0x645c70) y los textos
// 565 a 567 del grupo 20; quedan para P-C13. Un clic en un grupo no vuelve a pedir la lista: el
// original manda otro F4 02 (Q-C82). Y todo se atiende en el paso (Q-C81).
namespace Client::UI
{
// El ancho de la ventana (0x6451d0): dos columnas de botones de grupo, una de servidor y 56.
inline constexpr int ServerListWidth = 465;

// Lo que pasó con un clic en un servidor (0x645e10).
struct ServerClick
{
    enum class Kind : std::uint8_t
    {
        // Con carga de menos de 100: la lista se cierra y se elige (el F4 03).
        Chosen,
        // Con carga de 100 a 127: el aviso 0x63, y la lista queda. Con 128 o más no pasa nada.
        Full,
    };

    Kind kind = Kind::Chosen;
    std::uint16_t code = 0;

    friend bool operator==(const ServerClick&, const ServerClick&) = default;
};

// Dónde va cada cosa, en píxeles con la y desde arriba.
struct ServerListLayout
{
    Rect window;
    std::array<Rect, GroupButtonCount> groupButtons{};
    // Los 20 lugares de servidor del grupo, para `serverCount` servidores.
    std::array<Rect, ServersPerGroup> serverButtons{};
    // La barra de la carga de cada uno, de su ancho entero (160).
    std::array<Rect, ServersPerGroup> loadBars{};
};

// El alto de la caja de la descripción (+0x3064, 0x6451d0): 10 líneas del alto del buffer de la
// textura del slot 0x19 (server_ex02), más 6 arriba y 6 abajo.
[[nodiscard]] int DescriptionHeight(Scenes::TextureBufferSize lineTexture);

// 0x6451d0, 0x645520 y 0x6456c0 con la ventana donde la pone 0x65f300: ((W − 465) / 2,
// (H − alto) / 2) con la división de C, y el alto, 16 · 26 + 26 + la caja + 10. Las filas de los
// grupos empiezan en y + alto − 11 · 26 − la caja − 10; el 20 va al medio, en y + alto − la caja −
// 26 − 5. Los de servidor van en x + 136 desde la primera fila; con 10 o más, el último termina en
// la fila 10 y los de antes suben, aunque salgan de la ventana.
[[nodiscard]] ServerListLayout LayoutServerList(App::PixelSize screen, int descriptionHeight, std::size_t serverCount);

class ServerListWindow
{
public:
    // `texts` puede ser nullptr (sin Text.bmd: los botones salen sin rótulo) y tiene que vivir lo
    // que la ventana.
    ServerListWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // Abre con la lista ya agrupada (GroupServers), sin grupo elegido.
    void Open(ServerGroups groups);
    void Close();
    [[nodiscard]] bool IsOpen() const;
    // El botón de grupo elegido, desde 0.
    [[nodiscard]] std::optional<std::size_t> Selected() const;

    // 0x645e10 en el paso: un clic en un grupo lo elige; uno en un servidor dice qué pasó.
    std::optional<ServerClick> Step(const Input::InputState& input, App::PixelSize screen);
    // Pone la mezcla normal (el lote llega con la aditiva de la arena, Q-C80) y dibuja los botones
    // con sus rótulos y las barras. Ubica los botones para `screen` antes, por si cambió.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    void Select(std::size_t group);
    void Place(App::PixelSize screen);
    [[nodiscard]] std::size_t ShownServers() const;
    [[nodiscard]] const ServerGroup* SelectedGroup() const;

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    bool m_open = false;
    ServerGroups m_groups;
    std::optional<std::size_t> m_selected;
    std::vector<Button> m_groupButtons;
    std::vector<Button> m_serverButtons;
    ServerListLayout m_layout;
};
} // namespace Client::UI
