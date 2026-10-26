# Ola 0.1: cómo se arma un programa C++ y qué hace CMake

## El concepto en corto

Un `.cpp` no se convierte en programa de una. Pasa por tres etapas:

1. **Preprocesador:** reemplaza cada `#include` por el contenido del header y resuelve
   los `#define`/`#ifdef`. El resultado es una *unidad de traducción*.
2. **Compilador:** convierte cada unidad de traducción en un archivo objeto
   (`.o` en Linux, `.obj` en Windows), con código máquina y una lista de símbolos
   que define y que necesita.
3. **Linker:** junta todos los objetos y bibliotecas, resuelve los símbolos que faltan
   y produce el ejecutable. Un "undefined reference" o "unresolved external" es el
   linker diciendo que nadie definió algo que alguien usa.

El DataServer original se compilaba con un `.dsp` de Visual C++ 6: un archivo que solo
entiende VC6 en Windows. CMake describe lo mismo de forma portable: vos declarás
*targets* (un ejecutable, una biblioteca) y sus propiedades, y CMake genera el proyecto
para la herramienta de cada sistema (Ninja en Linux, Visual Studio en Windows).

## Qué quedó en el repo

| Archivo | Para qué |
| --- | --- |
| `CMakeLists.txt` | fija C++20 sin extensiones, carga los módulos de `cmake/` y agrega `Server/DataServer` |
| `cmake/CompilerWarnings.cmake` | `muret_set_warnings(target)`: los mismos warnings en MSVC, GCC y Clang |
| `CMakePresets.json` | las configuraciones con nombre: `linux-x64-debug`, `linux-x64-release`, `windows-x64` |
| `Server/DataServer/CMakeLists.txt` | el target `DataServer` |
| `Server/DataServer/src/main.cpp` | un `main` que imprime una línea y sale |

Dos detalles que conviene mirar:

- `target_compile_options(... PRIVATE ...)`: `PRIVATE` quiere decir que la opción vale
  para compilar *este* target y no se contagia a quien lo use. En la ola 1, cuando haya
  bibliotecas, vas a ver `PUBLIC` e `INTERFACE` también.
- En Linux el preset usa Ninja (un *single-config generator*: Debug o Release se elige
  al configurar). En Windows usa Visual Studio, que es *multi-config*: el mismo
  directorio de build tiene Debug y Release, y se elige al compilar. Por eso Windows
  tiene un solo configure preset y dos build presets.

## Te toca

1. Cloná el repo y compilá en Windows:
   `cmake --preset windows-x64` y `cmake --build --preset windows-x64-debug`.
   Corré el `.exe` que queda en `build\windows-x64\Server\DataServer\Debug\`.
2. Respondé con tus palabras (en el PR o en el chat):
   - ¿Qué etapa (preprocesador, compilador o linker) falla si borrás `#include <iostream>`
     de `main.cpp`? ¿Y si declarás una función `void f();`, la llamás y nunca la definís?
   - ¿Por qué `CMAKE_CXX_EXTENSIONS OFF`? Pista: compará `-std=c++20` con `-std=gnu++20`.
   - El mensaje de `main.cpp` no tiene tildes. ¿Qué podría pasar con "todavía" en la
     consola de Windows, y por qué?
3. Opcional: hacé que `main` acepte el puerto como argumento (`DataServer 55960`) con
   55960 por defecto, como el original (`dataserver.exe 55960`). Mirá `argc`/`argv`
   y `std::from_chars`. Pensá qué hacer si el argumento no es un número.
