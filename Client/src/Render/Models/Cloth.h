#pragma once

#include "Render/Models/HalfLifeMath.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>
#include <vector>

// La tela del personaje: la clase de la vtable 0x6b8cd8 del original (la capa del Dark Lord y la de la
// clase 3), una grilla de partículas con resortes, la fila 0 anclada a un hueso, esferas que la
// empujan, viento y gravedad (Client/docs/research/06-render-escenas-y-ui.md, "La tela de la clase
// 4"). Cada cuenta sigue el orden de la x87 del original con la precisión en 53 bits: lo que el
// original guarda en un float se redondea a float acá, y lo que deja en la pila sigue en double. Los
// tests comparan cada paso, bit a bit, con el código real del main.exe emulado (tests/vectors/cloth).
namespace Client::Render::Models
{
// 0x69fd70, el rand() de la CRT del original: s = s · 0x343fd + 0x269ec3 y devuelve (s >> 16) & 0x7fff.
// Lo siembra una vez srand(time(NULL)) (0x4bad9a).
class CrtRandom
{
public:
    explicit CrtRandom(std::uint32_t seed = 1) noexcept : m_state(seed) {}
    [[nodiscard]] int Next() noexcept;
    [[nodiscard]] std::uint32_t State() const noexcept
    {
        return m_state;
    }

private:
    std::uint32_t m_state;
};

// Lo que la tela lee de los globales del original, y su rand(): el viento base (0x7245f0), el viento
// de este paso (0x7245f4..0x7245fc; la z no la escribe nadie y queda en 0) y el contador protegido de
// pasos de lógica (0x7d3a0f0).
struct ClothWorld
{
    CrtRandom random;
    float windBase = 0.0f;
    glm::vec3 wind{};
    std::int32_t logicSteps = 0;
    // Cuántos accesos al contador están abiertos (la tabla de 0x57630c8): con 0, cada paso de una tela
    // lo vuelve a cifrar y saca 4 rand() que no usa. En el dibujo no hay otro abierto.
    std::int32_t openCounterAccesses = 0;
    // DAT_07d2d360 está en .bss:0.637d63/637d7c avanza el ciclo32 después del viento,
    // antes del contador lógico. El owner sólo publica valores0..31 y sobrevive a mapas/escenas.
    std::uint32_t waterTextureFrame = 0;
};

// 0x456b10: el paseo del viento base, b = f(b + (rand() % 200 − 100) · 0,001), entre −0,2 y 1.
void WalkTheWind(ClothWorld& world);

// Lo que hace 0x637700 en cada paso de lógica de las escenas 2, 4 y 5 para la tela: el paseo del
// viento cinco veces (0x637cec) y el contador más 1 (0x637f8c), que al cerrar su acceso saca otros
// 4 rand(). Los demás rand() de 0x637700 son de otras cosas y MuRet no los saca de acá.
void StepTheClothWorld(ClothWorld& world);

// Una partícula (0x3c bytes, vtable 0x6b8cbc). La posición va en el mundo.
struct ClothParticle
{
    glm::vec3 force{};
    glm::vec3 velocity{};
    glm::vec3 position{};
    bool anchored = false;
    std::int32_t corrections = 0;
    glm::vec3 correction{};
};

// Un resorte (0x10 bytes, 0x455140): las dos partículas, el largo mínimo f(L · 0,8), el de reposo L
// y sus banderas: 1 relaja, 2 tira y 4 restringe.
struct ClothSpring
{
    std::int16_t a = 0;
    std::int16_t b = 0;
    float shortest = 0.0f;
    float rest = 0.0f;
    std::uint8_t flags = 0;
};

// Una esfera (0x24 bytes, vtable 0x6b8ccc): su lugar en el hueso, el radio y el centro en el mundo
// del último paso.
struct ClothSphere
{
    glm::vec3 local{};
    float radius = 0.0f;
    std::int32_t bone = 0;
    glm::vec3 world{};
};

// Los argumentos del armado (0x454a50(objeto, hueso, x, y, z, columnas, filas, ancho, alto, frente,
// atrás, banderas)).
struct ClothShape
{
    std::int32_t bone = 0;
    glm::vec3 anchor{};
    std::int32_t columns = 2;
    std::int32_t rows = 2;
    float width = 1.0f;
    float height = 1.0f;
    TextureSlot front{0};
    TextureSlot back{0};
    std::uint32_t flags = 0;
};

// El objeto del que cuelga: sus huesos (objeto + 0x120, como los posa PoseSkeleton: sin la escala ni
// la posición y con la raíz girada), sus ángulos (+0x1c) y la escala y la posición del modelo
// (+0x68 y +0x6c..+0x74, que 0x6001b0 copia del objeto).
struct ClothBody
{
    std::span<const BoneTransform> bones;
    glm::vec3 angles{};
    float scale = 1.0f;
    glm::vec3 position{};
};

// Las banderas de la tela que mira el dibujo (06 § La tela de la clase 4).
inline constexpr std::uint32_t ClothBlendMask = 0x3000;
inline constexpr std::uint32_t ClothAlphaBlend = 0x1000;
inline constexpr std::uint32_t ClothAdditiveBlend = 0x2000;
inline constexpr std::uint32_t ClothKeepsTheBlend = 0x3000;
inline constexpr std::uint32_t ClothGlows = 0x40000000;

// La mezcla con la que se dibujan las caras (0x455930): por `& 0x3000`, 0x620980 (opaca), 0x620a00(1)
// (la normal, con alpha test) o 0x620a90 (aditiva), y con 0x3000 la que estaba; con el brillo, además,
// la aditiva.
enum class ClothBlend : std::uint8_t
{
    Opaque,
    Alpha,
    Additive,
    Previous,
};
[[nodiscard]] ClothBlend FaceBlendOf(std::uint32_t flags);

// Un vértice de una cara, como lo manda 0x455c40: la partícula y sus UV.
struct ClothCorner
{
    std::int32_t particle = 0;
    glm::vec2 uv{};
};

// Uno de los sprites del brillo (0x5491e0(0x5ac, …)): Effect\flare01.jpg en la posición de una
// partícula, con su escala y su luz. Los dibuja más tarde el administrador de efectos (0x549440).
struct ClothFlare
{
    glm::vec3 position{};
    float scale = 0.0f;
    float light = 0.0f;
};

class Cloth
{
public:
    // 0x454a00: todo en 0, el ancho, el alto, dx y dy en 1 y los bytes del viento en 1.
    Cloth() = default;

    // 0x454a50: las partículas en una grilla provisoria (con los ángulos del objeto y la traslación del
    // hueso), los resortes medidos en ella y la fila 0 anclada en esa misma matriz. Las banderas se
    // suman con OR a las que ya tenía, y las esferas quedan: así lo arma 0x50ba30 otra vez sobre la
    // cuarta tela.
    void Build(const ClothShape& shape, const ClothBody& body);
    // 0x455cd0.
    void AddSphere(const glm::vec3& local, float radius, std::int32_t bone);
    // Los bytes +0x49 y +0x48: el mínimo y el rango del viento de las capas con 0x200.
    void SetWindBytes(std::uint8_t minimum, std::uint8_t range);
    // 0x454f80: suelta las esferas, los resortes y las partículas, y vuelve a 0x454a00.
    void Release();

    // 0x455190(dt, steps): los pasos de 0x4551d0 hasta el primero que da false (la tela se rompió).
    [[nodiscard]] bool Simulate(float dt, int steps, const ClothBody& body, ClothWorld& world, std::uint8_t map);
    // 0x4551d0: el viento, las fuerzas, los resortes que tiran, el anclaje, la integración, las
    // esferas, la relajación y la restricción. false si un resorte pasó de 20 veces su largo.
    [[nodiscard]] bool Step(float dt, const ClothBody& body, ClothWorld& world, std::uint8_t map);

    // El dibujo (0x455930): las cuatro esquinas de cada cuadrado, de adelante (i, j), (i+1, j),
    // (i+1, j+1), (i, j+1) y de atrás al revés, con u = f(i / (columnas − 1)) y v = j / (filas − 1)
    // hasta 0,99.
    [[nodiscard]] std::vector<ClothCorner> Corners(bool front) const;
    // El brillo de 0x40000000 con el reloj T en ms (DAT_05767b88): t = f(sin(T · 0,001) · 0,1 + 0,4).
    [[nodiscard]] static float GlowAt(float worldTime);
    // Los 30 sprites del brillo: k recorre las partículas por filas, y la escala y la luz cambian
    // cada `filas` sprites, como el original.
    [[nodiscard]] std::vector<ClothFlare> Flares(float glow) const;
    // Si dibuja la cara de atrás: no con la mezcla aditiva y el brillo juntos.
    [[nodiscard]] bool DrawsTheBack() const;

    [[nodiscard]] std::span<const ClothParticle> Particles() const
    {
        return m_particles;
    }
    [[nodiscard]] std::span<const ClothSpring> Springs() const
    {
        return m_springs;
    }
    [[nodiscard]] std::span<const ClothSphere> Spheres() const
    {
        return m_spheres;
    }
    [[nodiscard]] std::uint32_t Flags() const
    {
        return m_flags;
    }
    [[nodiscard]] TextureSlot Front() const
    {
        return m_front;
    }
    [[nodiscard]] TextureSlot Back() const
    {
        return m_back;
    }
    [[nodiscard]] float Wind() const
    {
        return m_wind;
    }
    [[nodiscard]] std::int32_t Columns() const
    {
        return m_columns;
    }
    // El hueso de la fila anclada, el `bone` de su ClothShape.
    [[nodiscard]] std::int32_t Bone() const
    {
        return m_bone;
    }
    [[nodiscard]] std::int32_t Rows() const
    {
        return m_rows;
    }

private:
    void Reset();
    void AnchorTheTopRow(const BoneTransform& matrix, const ClothBody& body);
    void AddForces(ClothWorld& world);
    void PullTheSprings();
    [[nodiscard]] bool Constrain(const ClothBody& body);

    std::int32_t m_bone = 0;
    TextureSlot m_front{0};
    TextureSlot m_back{0};
    std::uint32_t m_flags = 0;
    glm::vec3 m_anchor{};
    float m_width = 1.0f;
    float m_height = 1.0f;
    std::int32_t m_columns = 0;
    std::int32_t m_rows = 0;
    std::vector<ClothParticle> m_particles;
    std::vector<ClothSpring> m_springs;
    float m_wind = 0.0f;
    std::uint8_t m_windRange = 1;
    std::uint8_t m_windMinimum = 1;
    float m_dx = 1.0f;
    float m_dy = 1.0f;
    std::vector<ClothSphere> m_spheres;
};
} // namespace Client::Render::Models
