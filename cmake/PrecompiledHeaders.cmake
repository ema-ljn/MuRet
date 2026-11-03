# Los precompiled headers del cliente (decidido el 2026-11-03): el reemplazo moderno del
# `stdafx.h` de MSVC. CMake (target_precompile_headers) arma el header y lo fuerza en cada fuente
# del target (`-include` en GCC y Clang, `/FI` en MSVC), así que ningún archivo lo incluye a mano.
#
# Solo va la biblioteca estándar (y GoogleTest en los tests): headers que no cambian. Uno del
# proyecto haría recompilar todo cada vez que se toca. No es una forma de ahorrar includes: cada
# archivo sigue incluyendo lo que usa (docs/CODING_RULES.md § 7, Core Guidelines SF.10 y SF.11), y
# el job de Linux del CI compila con MURET_PRECOMPILED_HEADERS=OFF para que un include que falta
# siga fallando.
#
# Un target puede usar el header de otro (REUSE_FROM) solo si compila con las mismas opciones y
# definiciones, como pide CMake: con otras, el header precompilado puede no valer para ese target
# (GCC avisa con -Winvalid-pch, MSVC con C4651). Por eso hay dos para los tests: el de
# ClientTestSupport y el de ClientGlTestSupport, que además ve SDL_OPENGL_1_FUNCTION_TYPEDEFS de
# ClientPlatform.

option(MURET_PRECOMPILED_HEADERS "Precompilar los headers de la biblioteca estándar del cliente" ON)

# Los de la biblioteca estándar que más incluyen Client/src y Client/tests.
set(MURET_STD_PRECOMPILED_HEADERS
    <algorithm>
    <array>
    <bit>
    <chrono>
    <cstddef>
    <cstdint>
    <filesystem>
    <format>
    <functional>
    <memory>
    <optional>
    <span>
    <string>
    <string_view>
    <system_error>
    <utility>
    <variant>
    <vector>
)

# muret_precompile_headers(<target> [header ...])
#
# Le da a <target> su propio precompiled header: la biblioteca estándar de arriba y los headers que
# se pasen (por ejemplo <gtest/gtest.h>). Sin MURET_PRECOMPILED_HEADERS no hace nada.
function(muret_precompile_headers target)
    if(MURET_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} PRIVATE ${MURET_STD_PRECOMPILED_HEADERS} ${ARGN})
    endif()
endfunction()

# muret_reuse_precompiled_headers(<target> <from>)
#
# <target> usa el precompiled header de <from> en vez de armar uno: tienen que compilar con las
# mismas definiciones (ver arriba). Sin MURET_PRECOMPILED_HEADERS no hace nada.
function(muret_reuse_precompiled_headers target from)
    if(MURET_PRECOMPILED_HEADERS)
        target_precompile_headers(${target} REUSE_FROM ${from})
    endif()
endfunction()
