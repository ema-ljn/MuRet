// Punto de entrada del cliente.
//
// Ola CL0.3: el "hola mundo" gráfico. Abre una ventana SDL3 con un contexto OpenGL 3.3 core, la
// limpia de un color, imprime GL_VERSION y GL_RENDERER y cierra con la X. Todavía sin capas: la
// CL3.1 lleva la ventana y el contexto a Platform/ y la CL3.2 arma el bucle con tick fijo. Los
// argumentos están en App/CommandLine.h.

#include "App/CommandLine.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

// La resolución de referencia del original (D-C11 del roadmap del cliente).
constexpr int WindowWidth = 640;
constexpr int WindowHeight = 480;
constexpr const char* WindowTitle = "MuRet";

// OpenGL 3.3 core (D-C3 del roadmap).
constexpr GLint GlMajorVersion = 3;
constexpr GLint GlMinorVersion = 3;

// Un azul oscuro: cualquier color que no sea negro sirve para ver que el clear anduvo.
constexpr GLfloat ClearRed = 0.08f;
constexpr GLfloat ClearGreen = 0.10f;
constexpr GLfloat ClearBlue = 0.18f;

// Las funciones de OpenGL no se enlazan como las de una biblioteca: las da el driver, en tiempo
// de ejecución, y recién cuando hay un contexto. Esta es la tabla mínima del hola mundo, pedida a
// SDL_GL_GetProcAddress. El tipo de cada puntero es su typedef PFNGL…PROC, con la convención de
// llamada de GL: los de la 1.x los declara SDL_opengl.h con SDL_OPENGL_1_FUNCTION_TYPEDEFS
// (Client/CMakeLists.txt) y los de la 3.3 (shaders, buffers) SDL_opengl_glext.h, siempre.
struct GlFunctions
{
    PFNGLGETSTRINGPROC getString = nullptr;
    PFNGLGETINTEGERVPROC getIntegerv = nullptr;
    PFNGLVIEWPORTPROC viewport = nullptr;
    PFNGLCLEARCOLORPROC clearColor = nullptr;
    PFNGLCLEARPROC clear = nullptr;
};

// En Windows, algunos drivers devuelven 1, 2, 3 o -1 en vez de nulo para una función que no
// tienen, y SDL no los filtra. En GLX pasa lo contrario: cualquier nombre da un puntero no nulo,
// así que este chequeo no garantiza nada ahí; lo que garantiza es la versión del contexto.
bool IsValidProcAddress(SDL_FunctionPointer function)
{
    constexpr std::uintptr_t LastInvalidAddress = 3;
    const auto address = reinterpret_cast<std::uintptr_t>(function);
    return address > LastInvalidAddress && address != UINTPTR_MAX;
}

template <typename Function> bool LoadGlFunction(Function& function, const char* name)
{
    const SDL_FunctionPointer address = SDL_GL_GetProcAddress(name);
    if (!IsValidProcAddress(address))
    {
        std::fprintf(stderr, "MuRet: el driver no tiene %s\n", name);
        return false;
    }
    function = reinterpret_cast<Function>(address);
    return true;
}

std::optional<GlFunctions> LoadGlFunctions()
{
    GlFunctions gl;
    const bool loaded = LoadGlFunction(gl.getString, "glGetString") &&
                        LoadGlFunction(gl.getIntegerv, "glGetIntegerv") && LoadGlFunction(gl.viewport, "glViewport") &&
                        LoadGlFunction(gl.clearColor, "glClearColor") && LoadGlFunction(gl.clear, "glClear");
    if (!loaded)
    {
        return std::nullopt;
    }
    return gl;
}

// El driver puede dar un contexto más nuevo que el pedido (Mesa da 4.5 core), nunca más viejo.
bool HasRequiredGlVersion(const GlFunctions& gl)
{
    GLint major = 0;
    GLint minor = 0;
    gl.getIntegerv(GL_MAJOR_VERSION, &major);
    gl.getIntegerv(GL_MINOR_VERSION, &minor);
    if (major > GlMajorVersion || (major == GlMajorVersion && minor >= GlMinorVersion))
    {
        return true;
    }
    std::fprintf(stderr, "MuRet: se pidió OpenGL %d.%d y el driver dio %d.%d\n", GlMajorVersion, GlMinorVersion, major,
                 minor);
    return false;
}

const char* GlString(const GlFunctions& gl, GLenum name)
{
    const GLubyte* value = gl.getString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "(sin dato)";
}

void PrintGlInfo(const GlFunctions& gl)
{
    std::printf("MuRet Client (CL0.3)\n");
    std::printf("GL_VERSION:  %s\n", GlString(gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", GlString(gl, GL_RENDERER));
    std::fflush(stdout);
}

// SDL_Init y SDL_Quit, con RAII: SDL_Quit corre aunque main salga por un error.
class SdlVideo
{
public:
    SdlVideo() : ready(SDL_Init(SDL_INIT_VIDEO)) {}
    ~SdlVideo()
    {
        SDL_Quit();
    }
    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;

    bool Ready() const
    {
        return ready;
    }

private:
    bool ready;
};

struct WindowDeleter
{
    void operator()(SDL_Window* window) const
    {
        SDL_DestroyWindow(window);
    }
};

struct GlContextDeleter
{
    void operator()(SDL_GLContextState* context) const
    {
        SDL_GL_DestroyContext(context);
    }
};

using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
using GlContextPtr = std::unique_ptr<SDL_GLContextState, GlContextDeleter>;

// El contexto se declara después de la ventana: los miembros se destruyen al revés, así que el
// contexto se libera antes que la ventana en la que vive.
struct GlWindow
{
    WindowPtr window;
    GlContextPtr context;
};

std::optional<GlWindow> CreateGlWindow()
{
    // Los atributos del contexto se piden antes de crear la ventana.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    WindowPtr window(
        SDL_CreateWindow(WindowTitle, WindowWidth, WindowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
    if (!window)
    {
        std::fprintf(stderr, "MuRet: no se pudo crear la ventana: %s\n", SDL_GetError());
        return std::nullopt;
    }
    // SDL_GL_CreateContext también lo deja como el contexto actual de este hilo.
    GlContextPtr context(SDL_GL_CreateContext(window.get()));
    if (!context)
    {
        std::fprintf(stderr, "MuRet: no se pudo crear el contexto OpenGL %d.%d core: %s\n", GlMajorVersion,
                     GlMinorVersion, SDL_GetError());
        return std::nullopt;
    }
    return GlWindow{std::move(window), std::move(context)};
}

// Atiende los eventos pendientes. Devuelve false cuando hay que cerrar.
bool HandleEvents(const GlFunctions& gl)
{
    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            running = false;
        }
        else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            gl.viewport(0, 0, event.window.data1, event.window.data2);
        }
    }
    return running;
}

// Dibuja hasta que se cierre la ventana o se llegue a `maxFrames`. Devuelve cuántos dibujó.
std::uint64_t RunFrames(SDL_Window* window, const GlFunctions& gl, std::optional<std::uint64_t> maxFrames)
{
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
    gl.viewport(0, 0, pixelWidth, pixelHeight);

    std::uint64_t frames = 0;
    while (HandleEvents(gl))
    {
        gl.clearColor(ClearRed, ClearGreen, ClearBlue, 1.0f);
        gl.clear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window);

        ++frames;
        if (maxFrames && frames >= *maxFrames)
        {
            break;
        }
    }
    return frames;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    std::string error;
    const std::optional<Client::App::CommandLine> commandLine = Client::App::ParseCommandLine(arguments, error);
    if (!commandLine)
    {
        std::fprintf(stderr, "MuRet: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    const SdlVideo video;
    if (!video.Ready())
    {
        std::fprintf(stderr, "MuRet: SDL_Init falló: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    const std::optional<GlWindow> glWindow = CreateGlWindow();
    if (!glWindow)
    {
        return EXIT_FAILURE;
    }
    const std::optional<GlFunctions> gl = LoadGlFunctions();
    if (!gl || !HasRequiredGlVersion(*gl))
    {
        return EXIT_FAILURE;
    }
    PrintGlInfo(*gl);

    // VSync. Si el driver no lo deja, el hola mundo igual anda, solo que más rápido.
    if (!SDL_GL_SetSwapInterval(1))
    {
        std::fprintf(stderr, "MuRet: sin VSync: %s\n", SDL_GetError());
    }

    const std::uint64_t frames = RunFrames(glWindow->window.get(), *gl, commandLine->maxFrames);
    std::printf("frames dibujados: %llu\n", static_cast<unsigned long long>(frames));
    return EXIT_SUCCESS;
}
