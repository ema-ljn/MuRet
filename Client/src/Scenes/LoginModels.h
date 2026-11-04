#pragma once

#include "Core/IClock.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Textures/Sampling.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <random>

// Lo que el login dibuja en su pase 3D (CL6.4): el logo, un objeto del mundo (0x606660), y el
// personaje de la escena 2 (0x517270(2, 4, …)), con lo que les cambia la lógica en cada paso
// (0x634c60). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El login, al entrar", "El
// logo en el binario" y "La luz del terreno en un punto", y el plan de la CL6 (CL6.4).
namespace Client::Scenes
{
// Un objeto suelto del mundo, como lo arma 0x606660: el modelo, dónde va, lo que le pasa a
// RenderBody (+0x64, +0x68, +0x6c, +0x70 y +0x58), su luz (+0xf4..+0xfc, la que se suma a la del
// terreno) y cuánto avanza su animación en cada paso (+0xd8, con 0x4ec200 en 0x604ab0).
struct ModelObject
{
    std::uint16_t model = 0;
    Render::Models::ObjectTransform transform;
    Render::Models::ObjectState state;
    glm::vec3 light{};
    float speed = 0.0f;
};

// El logo: Logo03.bmd en el 0xa2 (0x61c7c0: 0x60da30(0xa2, "Data\\Logo\\", "Logo", 3)), con sus
// texturas de Logo\ por 0x60d5f0(0xa2, "Logo\\", 0x2901, 0x2601, 1): repitiendo y con GL_LINEAR.
inline constexpr std::uint16_t LoginLogoModel = 0xa2;
inline constexpr Data::Models::ModelFile LoginLogoFile{LoginLogoModel, "Logo\\", "Logo", 3};
inline constexpr Data::Textures::Sampling LoginLogoSampling{Data::Textures::Filter::Linear,
                                                            Data::Textures::Wrap::Repeat};
// Sin un 0x60d530 antes: sus texturas van al próximo slot que dejó el mundo 55, 0x41c (0x61a0e8) más
// las 57 nuevas de Object56\ (Q-C92; *inferido* hasta la captura de P-C15).
inline constexpr Render::TextureSlot LoginLogoFirstSlot{0x455};

// 0x606660(0xa2, (10.680; 15.550; 1.710), (10, 0, 36), 1) y su caso en las escenas 2 y 4
// (0x6068d5): la escala 2,2, el blendMesh en 1 (la malla de la textura 1 va aditiva) y sin luz por
// vértice. La luz del blendMesh en 1 y la del objeto en 0 hasta el primer paso, y el avance de 0,16
// (0x3e23d70a) por paso.
[[nodiscard]] ModelObject LoginLogo();

// La cuenta (0x6d5e30) y la fase (0x7d3a0cc) de las rutas de la cámara, que lleva 0x633920 en cada
// paso: la cuenta arranca en −1 (0x6349a0 al entrar y la .data).
struct LoginRoutes
{
    int count = -1;
    int phase = 0;
};

// 0x633920: la cuenta sube 1; con la fase en 0 y la cuenta en 40, la cuenta vuelve a 0 y la fase
// pasa a 1, y con la fase en 1, cada 128 la cuenta vuelve a 0 (y la cámara elige otra ruta, que
// 0x635ae0 pisa: SceneCameras.h). El paso a la fase 2 es del fundido, que no dibuja (Q-C66).
void AdvanceRoutes(LoginRoutes& routes);

// 0x604ab0 con el 0xa2: con la fase en 0, la cuenta · 0,02 (0x6b8b90); con otra, 1,5 (0x6b8b94).
// Va a la luz del objeto y a la del blendMesh (Q-C105). Corre antes que AdvanceRoutes en el mismo
// paso: el primero da −0,02, y el 41, 0,78.
[[nodiscard]] float LoginLogoLight(const LoginRoutes& routes);

// 0x5ffce0 con un objeto sin luz por vértice: el color del modelo es la luz del terreno en su lugar
// por 0,1 (0x6b87b8) más la del objeto.
[[nodiscard]] glm::vec3 UnlitObjectColor(const glm::vec3& terrainLight, const glm::vec3& objectLight);

// El personaje de la escena 2 (0x517270(2, 4, 0, 0, 0, 0)): la clase 4, el casco, la armadura, los
// pantalones, los guantes y las botas del juego 29 (0x102e a 0x182e) en +7, la maza 0x620 en +13 en
// la derecha, el cuervo 0x1c17 en la izquierda, la capa 0x1c30 y el caballo oscuro 0x1c16 de
// mascota, sin cabeza (0x512930 la deja en −1).
[[nodiscard]] GameLogic::CharacterAppearance LoginCharacter();

// 0x6349a0 después de 0x517270: (10.603; 14.112; 580), girado 188° en z y con la escala 1,6.
[[nodiscard]] Render::Models::ObjectTransform LoginCharacterTransform();

// La luz que 0x517270 le pone (+0xf4..+0xfc, 0x3e99999a): 0x50ba30, fuera de la escena 4, la suma a
// la del terreno en su lugar.
inline constexpr float LoginCharacterLight = 0.3f;

// La acción de parado que le da 0x4f2250 con el caballo oscuro de mascota (+0x318 en 0x1c16).
inline constexpr std::uint8_t LoginCharacterStandingAction = 0x4a;

// 0x634c60 en cada paso: con el reloj del frame (DAT_05767b88, en ms) de 0 a 100 ms en cada 2.300
// (ftol(T % 2300 · 0,01) en 0: el producto va en la x87, y 100 · 0,01 da 0,99999998, Q-C110),
// rand() % 6 en 1 pone la 0x54 y en 2 la 0x55 (0x4ea030); con otro valor, o fuera de esa ventana,
// ninguna, y fuera de la ventana no se tira. `random` hace de rand(): de 0 a 32.767.
[[nodiscard]] std::optional<std::uint8_t> LoginCharacterActionAt(Core::Duration frameClock, std::mt19937& random);
} // namespace Client::Scenes
