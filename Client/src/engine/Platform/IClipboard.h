#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Client::UI
{
// Ctrl+C y Ctrl+X entregan a WriteText la selección que decide el EDIT; Ctrl+V consulta ReadText.
// El EDIT conserva la decisión sobre selección y reemplazo. La conversión ANSI del original sigue
// abierta en Client/docs/research/11-edit-win32.md, «Dibujo: correcciones y dudas abiertas».
class IClipboard
{
public:
    virtual ~IClipboard() = default;

    // nullopt indica error o ausencia de texto; una cadena vacía indica texto disponible de largo cero.
    [[nodiscard]] virtual std::optional<std::string> ReadText() = 0;
    [[nodiscard]] virtual bool WriteText(std::string_view text) = 0;
};
} // namespace Client::UI
