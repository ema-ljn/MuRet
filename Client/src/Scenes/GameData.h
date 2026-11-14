#pragma once

#include "Core/Expected.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/Credits.h"
#include "Data/Text/MoveRequirements.h"
#include "Data/Text/SlideTable.h"
#include "Data/Text/TextTable.h"
#include "UI/MacroFile.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Lo que se lee una vez, antes de la primera escena, y usan las que siguen (CL5.4). Vive en
// main.cpp, como el SceneManager.
namespace Client::Scenes
{
// Text.bmd como lo pide el original ("Data\Local\Text.bmd", 0x6d4370).
inline constexpr std::string_view TextTableFile = "Local\\Text.bmd";
// Los dos filtros del nombre, como los pide 0x61ccb0 (0x6d2a7c y 0x6d2ab8).
inline constexpr std::string_view FilterNameFile = "Local\\FilterName.bmd";
inline constexpr std::string_view FilterFile = "Local\\Filter.bmd";
// La tabla de 0x54a360, pedida por 0x61ccb0 en la carga (05 § Gate.bmd).
inline constexpr std::string_view GateTableFile = "Gate.bmd";
// Metadata de ítems que usa 0x54bf40, cargada una vez por 0x54b9e0.
inline constexpr std::string_view ItemTableFile = "Local\\Item.bmd";
// 0x54acf0: 256 registros de 0x3c y checksum obligatorio (05 § Tablas .bmd).
inline constexpr std::string_view SkillTableFile = "Local\\Skill.bmd";
// Las tablas de los conjuntos, como las pide 0x40c600 desde 0x61ccb0 (0x6c63e8 y 0x6c63c8).
inline constexpr std::string_view ItemSetTypeFile = "Local\\ItemSetType.bmd";
inline constexpr std::string_view ItemSetOptionFile = "Local\\ItemSetOption.bmd";
// Los créditos, como los pide 0x63ff30 ("Data\Local\credit.bmd", 0x6d66e0).
inline constexpr std::string_view CreditsFile = "Local\\credit.bmd";
// Las macros, como las pide 0x61ee30 (0x6c649c), detrás de Text.bmd.
inline constexpr std::string_view MacroFile = "Macro.txt";
// Los textos del cartel, como los pide 0x61ccb0 (0x61ed18, con 0x6d29c8).
inline constexpr std::string_view SlideTableFile = "Local\\Slide.bmd";
// Los destinos de la ventana /move, como los pide 0x61ccb0 (0x61ec8f, con 0x6d2a18).
inline constexpr std::string_view MoveRequirementsFile = "Local\\movereq.bmd";

struct GameData
{
    // Text.bmd (CL1): los textos se piden por índice. La cara de la fuente es el texto 0.
    std::optional<Data::Text::TextTable> text;
    // FilterName.bmd y Filter.bmd (CL6.5): las palabras que no puede tener el nombre de un personaje
    // nuevo (UI::NameFilters).
    std::vector<std::string> filterName;
    std::vector<std::string> filter;
    std::optional<Data::Tables::GateTable> gates;
    // Q-C01: checksumMatches queda disponible para el aviso del integrador.
    std::optional<Data::Tables::LoadedItemTable> items;
    // ItemSetType.bmd e ItemSetOption.bmd, las tablas de [0x6eed3c] (05, "Las tablas de los conjuntos").
    std::optional<Data::Tables::ItemSetTables> itemSets;
    // Skill.bmd: la tabla en claro, sin el contenedor guardia del original (Q-C08).
    std::optional<Data::Tables::SkillTable> skills;
    // credit.bmd (CL8, hito 8): los 400 registros de los créditos (UI::CreditsWindow), o ninguno si no
    // está. El original lo lee cada vez que arma la UI del login (0x63ff30) y, si no está, avisa con un
    // MessageBox y cierra el juego (WM_DESTROY); MuRet lo lee una vez, y sin él los créditos se cierran al
    // abrirlos (P-C78).
    std::vector<Data::Text::CreditEntry> credits;
    // Data\Macro.txt (0x61c1c0): main.cpp las copia a ChatInputState::macros cuando arma el mundo.
    UI::Macros macros{};
    // Slide.bmd (CL8 hito 3): el período, la velocidad y los textos del cartel de [0x5763528]. Vacío si no está.
    std::optional<Data::Text::SlideTable> slides;
    // movereq.bmd (CL8 hito 6): los destinos de la ventana /move, que main.cpp copia a WorldRuntime::moves (la lista
    // de 0x723c68). Vacío si no está, como cuando fopen falla en 0x448440.
    std::vector<Data::Text::MoveRequirement> moves;
};

// Lo de WinMain antes de la escena 1: Text.bmd en 0x4b9b9f (0x61ee30 y 0x5495e0), antes de las
// fuentes (la cara es su texto 0) y de la carga. Lo busca en `dataRoot` sin mirar mayúsculas
// (ResolveDataPath). Si falta, no se lee o no carga, un error que lo nombra, sin un texto. El
// original lee igual un archivo corto y deja lo que había en la memoria (05, Tablas .bmd). Después,
// los dos filtros, que 0x61ccb0 lee en la carga: uno que no está queda vacío (el original no
// arranca sin él), y uno que no se lee o cuya suma no da es un error que lo nombra. Gate.bmd es
// obligatorio: se carga con sus 256 registros; un error de ruta, lectura o formato lo nombra.
// Item.bmd también es obligatorio; lee registros y checksum de un prefijo fijo, permite cola y
// conserva el checksum no fatal (Q-C01) en items->checksumMatches. Las dos tablas de los conjuntos
// también: sin ellas, o con un checksum que no da, el original muestra su aviso y cierra la ventana
// (0x40c660 y 0x40c850); un error de ruta, lectura o formato las nombra. Skill.bmd también es
// obligatorio: lee registros y checksum de un prefijo fijo, permite cola y rechaza un checksum
// distinto (0x54acf0). La tabla queda en claro, sin el contenedor guardia del original (Q-C08). Las macros van
// detrás de Text.bmd, como en 0x61ee30: si Macro.txt no está o no se lee, quedan vacías, como cuando fopen falla en
// 0x61c1c0; una palabra que desborda su lugar (Q-C531) es el error. Al final, Slide.bmd (0x46b3b0): uno que no está
// deja `slides` vacío y el cartel con lo de su constructor (el original avisa "File not exist." y se cierra, Q-C517);
// uno que no se lee o es corto es un error que lo nombra. credit.bmd puede faltar (los créditos quedan vacíos); uno
// que no se lee o es corto es un error que lo nombra. movereq.bmd, antes de las tablas de los conjuntos como en
// 0x61ccb0, también: sin él, la ventana /move queda sin destinos (0x448440 devuelve 0 y nadie lo mira);
// uno que no se lee, corto o con un nombre sin terminar es un error que lo nombra.
[[nodiscard]] Core::Expected<GameData, std::string> LoadGameData(const std::filesystem::path& dataRoot);

// 0x61c170: fopen("wt") de Data\Macro.txt, que lo crea o lo vacía, y `content` (UI::WrittenMacros). Lo que guardan
// las macros no va al log. La salida del juego (GameSceneUi) y el cambio de servidor (0x412b6d) lo comparten.
[[nodiscard]] Core::Expected<void, std::string> WriteMacroFile(const std::filesystem::path& dataRoot,
                                                               std::string_view content);
} // namespace Client::Scenes
