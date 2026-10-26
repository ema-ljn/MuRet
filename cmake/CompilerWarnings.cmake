# muret_set_warnings(<target>)
#
# Aplica el mismo nivel de warnings a un target, sea MSVC, GCC o Clang.
# Se llama una vez por target desde el CMakeLists de cada componente.
function(muret_set_warnings target)
    if(MSVC)
        set(warnings
            /W4            # nivel alto de warnings
            /permissive-   # conformidad estricta con el estándar
            /w14242        # conversión con posible pérdida de datos
            /w14265        # clase con virtuales sin destructor virtual
        )
        if(MURET_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    else()
        set(warnings
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow       # una variable tapa a otra de un scope exterior
            -Wconversion   # conversiones implícitas que pierden datos
            -Wsign-conversion
            -Wold-style-cast
            -Wnon-virtual-dtor
        )
        if(MURET_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
