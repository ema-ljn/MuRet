#pragma once

#include "Data/Text/TextTable.h"
#include "Scenes/TooltipLines.h"
#include "UI/SprintfFormat.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Lo que los cuerpos de los tooltips (06 § Los tooltips, CL8 hito 5) hacen con TooltipLines: sprintf en el renglón
// TooltipLines::next (0x78abb30), su color y su negrita, subir el renglón y los "\n" que suben también los medios
// (0x78abb34). El primer error (un texto que Text.bmd no tiene, o un renglón fuera de los 30, donde el original
// escribe sobre el ítem del cursor, 0x7879f80) queda guardado y lo que sigue no escribe nada: quien escribe lo mira
// antes de lo que tiene efectos fuera de los renglones (un acceso protegido, la caja).
namespace Client::Scenes
{
class TooltipWriter
{
public:
    TooltipWriter(TooltipLines& lines, const Data::Text::TextTable* texts);

    // La dirección de lo que se escribe, para el error.
    void At(std::string_view where);
    [[nodiscard]] bool Failed() const;
    [[nodiscard]] const std::string& Error() const;
    // Guarda el error si todavía no hay uno.
    void Fail(std::string error);

    // El texto `index` de Text.bmd; sin él, vacío y el error.
    [[nodiscard]] std::string_view Text(std::size_t index);

    // sprintf(renglón, formato, ...): el renglón que sigue, con el formato de UI::Sprintf (Q-C590).
    template <typename... Arguments> void Print(std::string_view format, Arguments... arguments)
    {
        if (std::string* line = Current(); line != nullptr)
            *line = UI::Sprintf(format, arguments...);
    }
    // Lo mismo con el texto `index` de Text.bmd como formato.
    template <typename... Arguments> void PrintText(std::size_t index, Arguments... arguments)
    {
        const std::string_view format = Text(index);
        Print(format, arguments...);
    }
    // El color y la negrita del renglón que sigue.
    void Color(std::int32_t color);
    void Bold(std::int32_t bold);
    // El renglón que sigue sube o baja uno.
    void Next();
    void Back();
    // Un medio renglón más (0x78abb34).
    void AddHalf();
    // "\n" en el renglón que sigue, que sube, y un medio más.
    void NewLine();
    // Lo más común: el texto `index` como formato, el color, la negrita en 0 y el renglón que sube.
    template <typename... Arguments> void Line(std::int32_t color, std::size_t index, Arguments... arguments)
    {
        PrintText(index, arguments...);
        Color(color);
        Bold(0);
        Next();
    }

    // El renglón que sigue, o nullptr con el error si está fuera de los 30: para quien escribe en él con su propio
    // sprintf (0x40d530).
    [[nodiscard]] std::string* Current();

    [[nodiscard]] std::int32_t Count() const;
    void SetCount(std::int32_t count);
    [[nodiscard]] std::int32_t Halves() const;
    [[nodiscard]] TooltipLines& Lines();
    [[nodiscard]] const Data::Text::TextTable* Texts() const;

private:
    [[nodiscard]] bool Inside();

    TooltipLines& m_lines;
    const Data::Text::TextTable* m_texts = nullptr;
    std::string m_where;
    std::string m_error;
};
} // namespace Client::Scenes
