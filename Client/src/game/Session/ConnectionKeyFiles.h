#pragma once

#include "Core/Expected.h"
#include "Network/Connection.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Las claves de la conexión con el GameServer, de la carpeta de datos: Enc1.dat para lo que sale y
// Dec2.dat para lo que llega, como las carga WinMain (0x4ba094 y 0x4ba0a3).
// Contrato: el plan de la CL4 (CL4.4), fila "las claves"; 03-red-y-cifrado.md, "C3/C4", y Q-C41.
//
// Las lee App y no Data, porque los cargadores de la capa de datos reciben los bytes y no abren
// archivos. El original sigue sin la clave, y el primer C3 dividiría por cero (Q-C41): acá, con un
// servidor, una clave que no carga corta antes de abrir la ventana.
namespace Client::App
{
inline constexpr std::string_view EncryptionKeyFileName = "Enc1.dat";
inline constexpr std::string_view DecryptionKeyFileName = "Dec2.dat";

// Las dos claves de `dataFolder`, buscadas como las pide el original ("Data\Enc1.dat"), sin
// distinguir mayúsculas (Data::ResolveDataPath). Sin carpeta, o si alguna no está, no es un
// archivo, no se lee o no carga, el error nombra los dos archivos y dice qué pasó con cada uno,
// sin un byte de la clave.
[[nodiscard]] Core::Expected<Network::ConnectionKeys, std::string>
LoadConnectionKeys(const std::optional<std::filesystem::path>& dataFolder);
} // namespace Client::App
