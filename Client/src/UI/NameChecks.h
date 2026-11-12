#pragma once

#include "UI/Notices.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

// Los chequeos del nombre de un personaje nuevo antes del F3 01 (0x63d990; Client/docs/research/06-
// render-escenas-y-ui.md, "La ventana de crear"). *Estático*, y *emulado* con los nombres del
// contrato.
namespace Client::UI
{
// Lo que mira 0x622c70: las palabras de FilterName.bmd y de Filter.bmd (Data::Text::LoadFilterWords)
// y los textos 457 y 458 de Text.bmd. Tienen que vivir lo que la consulta.
struct NameFilters
{
    std::span<const std::string> filterName;
    std::span<const std::string> filter;
    std::string_view operation; // el texto 457
    std::string_view server;    // el texto 458
};

// 0x576960(texto, palabra, 0): si `word` está en `text`, distinguiendo mayúsculas. Una palabra vacía
// está en cualquier texto. El original avanza de a un carácter de su página de códigos (_mbclen);
// MuRet, de a un byte, que es lo mismo con los nombres de su campo, en Latin-1: un byte por carácter,
// como la página de códigos de Occidente (CP1252).
[[nodiscard]] bool ContainsLikeTheOriginal(std::string_view text, std::string_view word);

// 0x622b40(texto, comandos): con `skipCommands` y el texto empezando con '/', nada; si no, alguna palabra de
// Filter.bmd (`filter`) en el texto sin espacios. El original arma la copia sin espacios en 0x800 bytes de la pila.
[[nodiscard]] bool ContainsAFilteredWord(std::string_view text, std::span<const std::string> filter, bool skipCommands);

// 0x622c70: alguna palabra de FilterName.bmd en el nombre sin espacios; alguna de Filter.bmd en el
// nombre sin espacios, salvo que empiece con '/' (0x622b40 con 1); o, en el nombre tal cual, el
// espacio, el punto, los de doble byte "\xa1\xa1", "\xa1\xa4" y "\xa1\xad", "Webzen", "WebZen",
// "webzen", "WEBZEN" o los textos 457 y 458. Como el espacio ya filtra, sacar los espacios antes
// no cambia el resultado.
[[nodiscard]] bool IsFilteredName(std::string_view name, const NameFilters& filters);

// 0x447ca0, con el parche de NOPs de este binario: mira los bytes 0, 2, 4… de `buffer` mientras el
// de dos más adelante no sea 0 (detrás del final, ceros), y uno es malo si es menor que 0x21, está de
// 0x7f a 0xaf o es mayor que 0xf7.
[[nodiscard]] bool HasForbiddenSymbol(std::string_view name);

// En el orden de 0x63d990: menos de 4 bytes, el aviso 0x79; filtrado, el 0x72; con un byte malo en
// `buffer` (0x7798bf8, donde está el nombre y lo que quedó detrás: Q-C118), el 0x7a; si no, nada: se
// manda.
[[nodiscard]] std::optional<NoticeId> NoticeOfNewName(std::string_view name, std::string_view buffer,
                                                      const NameFilters& filters);
// Lo mismo con el nombre solo, sin nada detrás.
[[nodiscard]] std::optional<NoticeId> NoticeOfNewName(std::string_view name, const NameFilters& filters);
} // namespace Client::UI
