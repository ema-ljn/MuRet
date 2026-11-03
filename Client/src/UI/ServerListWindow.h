#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/SceneTextures.h"
#include "UI/Button.h"
#include "UI/ServerListModel.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// La ventana de la lista de servidores (CL5.5; 06 § La lista de servidores): la arma 0x6451d0, la
// ubica 0x645520 con 0x6456c0 y 0x6457c0, le pone los rótulos y la descripción 0x645860, atiende
// los clics 0x645e10 y la dibuja 0x6463b0. 21 botones de grupo, en dos columnas de 10 y uno abajo
// al medio, y en el medio los del grupo elegido: uno por servidor, o uno solo sin número si el
// grupo tiene uno. El original tiene 16 de servidor; MuRet tiene 20, uno por número (Q-C63). Hasta
// que se elige un grupo no hay ninguno (+0x3138 empieza en -1). Alrededor, el marco de
// server_deco_all y, con un grupo elegido, la caja de abajo con su descripción en dos renglones; y
// con el grupo 20, tres textos arriba a la izquierda (Q-C83). *Estático*.
//
// Un clic en un grupo no vuelve a pedir la lista: el original manda otro F4 02 (Q-C82). Y todo se
// atiende en el paso (Q-C81).
namespace Client::UI
{
// El ancho de la ventana (0x6451d0): dos columnas de botones de grupo, una de servidor y 56.
inline constexpr int ServerListWidth = 465;
// El de la caja de la descripción (0x66a1c0): el de server_ex01, el borde de arriba.
inline constexpr int DescriptionWidth = 512;

// La descripción del botón de grupo k es el texto 1717 + k (0x645860), en dos renglones de 83
// bytes (0x632c90).
inline constexpr std::size_t FirstDescriptionText = 1717;
inline constexpr std::size_t DescriptionLines = 2;
inline constexpr std::size_t DescriptionLineBytes = 83;
// Con el grupo 20 elegido, los textos 565, 566 y 567 (0x6463b0).
inline constexpr std::size_t FirstGroupTwentyText = 565;
inline constexpr std::size_t GroupTwentyTextCount = 3;

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
    // La caja de la descripción, centrada abajo de la ventana (0x645520).
    Rect description;
};

// Las cuatro piezas de server_deco_all que van alrededor de los botones de grupo (0x6451d0,
// 0x645520, 0x6457c0, 0x645c70 y 0x645ca0), en píxeles y ya corridas por su punto: por cada
// columna, la de arriba y la marca del elegido, en el orden en que las dibuja 0x6463b0.
struct ServerListFrame
{
    // La de la columna de la izquierda, siempre, y la de la derecha, con más de 10 botones.
    std::array<std::optional<Render::UiImage>, 2> corners{};
    // La marca del elegido, en la columna de él: del 0 al 9 a la izquierda, del 10 al 19 a la
    // derecha, y ninguna con el 20.
    std::array<std::optional<Render::UiImage>, 2> markers{};
};

// El alto de la caja de la descripción (+0x3064, 0x6451d0): 10 líneas del alto del buffer de la
// textura del slot 0x19 (server_ex02), más 6 arriba y 6 abajo.
[[nodiscard]] int DescriptionHeight(Scenes::TextureBufferSize lineTexture);

// 0x6451d0, 0x645520 y 0x6456c0 con la ventana donde la pone 0x65f300: ((W − 465) / 2,
// (H − alto) / 2) con la división de C, y el alto, 16 · 26 + 26 + la caja + 10. Las filas de los
// grupos empiezan en y + alto − 11 · 26 − la caja − 10; el 20 va al medio, en y + alto − la caja −
// 26 − 5. Los de servidor van en x + 136 desde la primera fila; con 10 o más, el último termina en
// la fila 10 y los de antes suben, aunque salgan de la ventana. La caja, de 512 de ancho, va en
// (x − (512 − 465) / 2, y + alto − la caja).
[[nodiscard]] ServerListLayout LayoutServerList(App::PixelSize screen, int descriptionHeight, std::size_t serverCount);

// El marco para `groupButtons` botones de grupo y el elegido, desde 0. Cada pieza pone su punto
// en una esquina de un botón: (8, 19) arriba a la izquierda del 0, (60, 19) arriba a la derecha
// del 10, (1, 2) arriba a la derecha del elegido de la izquierda y (23, 2) arriba a la izquierda
// del de la derecha.
[[nodiscard]] ServerListFrame LayoutServerListFrame(const ServerListLayout& layout, std::size_t groupButtons,
                                                    std::optional<std::size_t> selected, const TextureSizes& sizes);

// Las cinco imágenes de la caja de 0x66a1c0 en `box`, en el orden de 0x66a960: el fondo de
// server_ex03 en (3, 3), repetido; el borde de arriba y el de abajo de server_ex01; y los lados de
// server_ex02, el de la izquierda repetido y el de la derecha estirado (06 § La lista de
// servidores).
[[nodiscard]] std::array<Render::UiImage, 5> DescriptionBoxImages(Rect box, const TextureSizes& sizes);

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
    // Los renglones de la descripción del grupo elegido, vacío sin él.
    [[nodiscard]] const std::vector<std::string>& Description() const;

    // 0x6463b0 con la mezcla normal (el lote llega con la aditiva de la arena, Q-C80): la caja,
    // las piezas del marco con los renglones de la descripción, los botones con sus rótulos, las
    // barras y, con el grupo 20, sus tres textos. Ubica todo para `screen` antes, por si cambió.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    void Select(std::size_t group);
    void Place(App::PixelSize screen);
    [[nodiscard]] std::size_t ShownServers() const;
    [[nodiscard]] const ServerGroup* SelectedGroup() const;
    // Si el nombre del grupo elegido es el texto del grupo 20 (el strcmp de 0x6463b0).
    [[nodiscard]] bool ShowsGroupTwentyTexts() const;

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    bool m_open = false;
    ServerGroups m_groups;
    std::optional<std::size_t> m_selected;
    std::vector<Button> m_groupButtons;
    std::vector<Button> m_serverButtons;
    std::vector<std::string> m_description;
    ServerListLayout m_layout;
};
} // namespace Client::UI
