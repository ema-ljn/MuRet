#pragma once

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <string>

// Una textura de GL hecha con una de la CL1 (Data::Textures::Texture, de un OZT o un OZJ), subida
// como la sube el original: el buffer entero de lados potencia de 2, con el formato del archivo
// (GL_RGBA el OZT, 0x63bd00; GL_RGB el OZJ, 0x63b870), sin dar vuelta las filas y sin mipmaps
// (05 § Texturas). Se mueve y no se copia, como su GlName, y necesita el contexto vivo.
namespace Client::Render::Gl
{
class GlTexture
{
public:
    // Sube `texture` con el filtro y el wrap de `sampling`. GL_UNPACK_ALIGNMENT va en 1 mientras
    // sube, porque una fila RGB de 1 o 2 píxeles mide 3 o 6 bytes y con el 4 de GL la siguiente se
    // leería corrida, y después vuelve a lo que estaba. Como en el original, la textura nueva queda
    // atada a GL_TEXTURE_2D: quien recuerde cuál está atada (lo que hace 0x620780, Q-C64) tiene
    // que saberlo. Si los píxeles no son el buffer entero (textureWidth × textureHeight en su
    // formato) o un lado es 0, no llama a GL: da el error.
    //
    // Los errores de GL (AUD-C03 de la auditoría del 2026-11-03): si GL ya tenía uno pendiente
    // antes de subir, no sube y da un error que lo nombra como de otra llamada, porque glGetError
    // no dice cuál lo dejó. Si glTexImage2D no acepta la textura (un lado mayor que
    // GL_MAX_TEXTURE_SIZE, o GL_OUT_OF_MEMORY), da el error con su nombre y borra el nombre nuevo.
    // En los dos casos saca los errores que leyó, así la llamada siguiente empieza sin ninguno, y
    // el alignment vuelve a lo que estaba. Después de GL_OUT_OF_MEMORY, GL no promete nada del
    // contexto: Upload no intenta arreglarlo.
    [[nodiscard]] static Core::Expected<GlTexture, std::string>
    Upload(const Platform::GlFunctions& gl, const Data::Textures::Texture& texture, Data::Textures::Sampling sampling);

    [[nodiscard]] GLuint Name() const;
    // El tamaño del buffer, textureWidth × textureHeight: el de la textura de GL, no el de la
    // imagen que tiene en la esquina.
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;

private:
    GlTexture(GlName name, int width, int height);

    GlName m_name;
    int m_width;
    int m_height;
};
} // namespace Client::Render::Gl
