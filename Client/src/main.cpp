// Punto de entrada del cliente.
//
// Ola CL0.3: el "hola mundo" gráfico. Abre una ventana SDL3 con un contexto OpenGL 3.3 core, la
// limpia de un color, imprime GL_VERSION y GL_RENDERER y cierra con la X. Todavía sin capas: la
// CL3.1 lleva la ventana y el contexto a Platform/ y la CL3.2 arma el bucle con tick fijo.
//
//   Main              corre hasta que se cierre la ventana
//   Main --frames N   dibuja N frames y sale (para probarlo sin nadie que cierre la ventana)

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>

namespace
{

// La resolución de referencia del original (D-C11 del roadmap del cliente).
constexpr int WindowWidth = 640;
constexpr int WindowHeight = 480;
constexpr const char* WindowTitle = "MuRet";

// OpenGL 3.3 core (D-C3 del roadmap).
constexpr int GlMajorVersion = 3;
constexpr int GlMinorVersion = 3;

// Un azul oscuro: cualquier color que no sea negro sirve para ver que el clear anduvo.
constexpr GLfloat ClearRed = 0.08f;
constexpr GLfloat ClearGreen = 0.10f;
constexpr GLfloat ClearBlue = 0.18f;

// Las funciones de OpenGL no se enlazan como las de una biblioteca: las da el driver, en tiempo
// de ejecución, y recién cuando hay un contexto. Esta es la tabla mínima del hola mundo, pedida a
// SDL_GL_GetProcAddress. Las de GL 1.1 Windows también las exporta opengl32.dll, pero las de la
// 3.3 (shaders, buffers) solo existen así, así que se piden todas igual. El tipo de cada puntero
// sale de la declaración de SDL_opengl.h, con su convención de llamada.
struct GlFunctions
{
    decltype(&::glGetString) getString = nullptr;
    decltype(&::glViewport) viewport = nullptr;
    decltype(&::glClearColor) clearColor = nullptr;
    decltype(&::glClear) clear = nullptr;
};

template <typename Function> bool LoadGlFunction(Function& function, const char* name)
{
    function = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
    if (function == nullptr)
    {
        std::fprintf(stderr, "MuRet: el driver no tiene %s\n", name);
        return false;
    }
    return true;
}

std::optional<GlFunctions> LoadGlFunctions()
{
    GlFunctions gl;
    const bool loaded = LoadGlFunction(gl.getString, "glGetString") && LoadGlFunction(gl.viewport, "glViewport") &&
                        LoadGlFunction(gl.clearColor, "glClearColor") && LoadGlFunction(gl.clear, "glClear");
    if (!loaded)
    {
        return std::nullopt;
    }
    return gl;
}

const char* GlString(const GlFunctions& gl, GLenum name)
{
    const GLubyte* value = gl.getString(name);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "(sin dato)";
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

// "--frames N": cuántos frames dibujar antes de salir. Sin el argumento, std::nullopt (sin límite).
bool ParseArguments(int argc, char** argv, std::optional<std::uint64_t>& maxFrames)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--frames" && i + 1 < argc)
        {
            const std::string_view value = argv[++i];
            std::uint64_t frames = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frames);
            if (error != std::errc() || end != value.data() + value.size() || frames == 0)
            {
                std::fprintf(stderr, "MuRet: --frames necesita un número mayor que 0, no \"%s\"\n", argv[i]);
                return false;
            }
            maxFrames = frames;
        }
        else
        {
            std::fprintf(stderr, "MuRet: no conozco el argumento \"%s\" (uso: Main [--frames N])\n", argv[i]);
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::optional<std::uint64_t> maxFrames;
    if (!ParseArguments(argc, argv, maxFrames))
    {
        return EXIT_FAILURE;
    }

    const SdlVideo video;
    if (!video.Ready())
    {
        std::fprintf(stderr, "MuRet: SDL_Init falló: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    // Los atributos del contexto se piden antes de crear la ventana.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const WindowPtr window(
        SDL_CreateWindow(WindowTitle, WindowWidth, WindowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE));
    if (!window)
    {
        std::fprintf(stderr, "MuRet: no se pudo crear la ventana: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    const GlContextPtr context(SDL_GL_CreateContext(window.get()));
    if (!context)
    {
        std::fprintf(stderr, "MuRet: no se pudo crear el contexto OpenGL %d.%d core: %s\n", GlMajorVersion,
                     GlMinorVersion, SDL_GetError());
        return EXIT_FAILURE;
    }

    const std::optional<GlFunctions> gl = LoadGlFunctions();
    if (!gl)
    {
        return EXIT_FAILURE;
    }

    std::printf("MuRet Client (CL0.3)\n");
    std::printf("GL_VERSION:  %s\n", GlString(*gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", GlString(*gl, GL_RENDERER));
    std::fflush(stdout);

    // VSync. Si el driver no lo deja, el hola mundo igual anda, solo que más rápido.
    if (!SDL_GL_SetSwapInterval(1))
    {
        std::fprintf(stderr, "MuRet: sin VSync: %s\n", SDL_GetError());
    }

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(window.get(), &pixelWidth, &pixelHeight);
    gl->viewport(0, 0, pixelWidth, pixelHeight);

    std::uint64_t frames = 0;
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                running = false;
            }
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            {
                gl->viewport(0, 0, event.window.data1, event.window.data2);
            }
        }

        gl->clearColor(ClearRed, ClearGreen, ClearBlue, 1.0f);
        gl->clear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(window.get());

        ++frames;
        if (maxFrames && frames >= *maxFrames)
        {
            running = false;
        }
    }

    std::printf("frames dibujados: %llu\n", static_cast<unsigned long long>(frames));
    return EXIT_SUCCESS;
}
