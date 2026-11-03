#pragma once

#include <cstdint>

// Qué forma de los paquetes espera el servidor ([Server] Obfuscation; pregunta 8 del plan de la
// CL4): la elige la config porque depende del servidor (D-C6). En su propio header, como
// PacingMode.h, porque la lee la config y la usa quien arma la conexión.
namespace Client::App
{
enum class ObfuscationMode : std::uint8_t
{
    // La capa de FoxAnti del servidor del laboratorio (Network::FoxAntiObfuscation), el único
    // contra el que se prueba: es la de por defecto.
    FoxAnti,
    // Ninguna: un servidor que habla como main.exe (Network::IdentityObfuscation).
    None,
};
} // namespace Client::App
