#include "Scenes/LoginModels.h"

#include "Data/Textures/Sampling.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Pose.h"
#include "support/FixedRandom.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <string_view>
#include <vector>

// Client::Scenes::LoginModels: el logo (0x606660 y 0x604ab0) y el personaje del login (0x517270,
// 0x4f2250 y 0x634c60), como datos y como lo cambia cada paso (06 § El logo en el binario).
namespace
{
using Client::GameLogic::BodyPart;
using Client::GameLogic::CharacterAppearance;
using Client::Render::Models::NoHiddenMesh;
using Client::Scenes::AdvanceRoutes;
using Client::Scenes::LoginCharacter;
using Client::Scenes::LoginCharacterActionAt;
using Client::Scenes::LoginCharacterLight;
using Client::Scenes::LoginCharacterStandingAction;
using Client::Scenes::LoginCharacterTransform;
using Client::Scenes::LoginLogo;
using Client::Scenes::LoginLogoFile;
using Client::Scenes::LoginLogoFirstSlot;
using Client::Scenes::LoginLogoLight;
using Client::Scenes::LoginLogoModel;
using Client::Scenes::LoginLogoSampling;
using Client::Scenes::LoginRoutes;
using Client::Scenes::ModelObject;
using Client::Scenes::UnlitObjectColor;
using Client::Tests::FixedSeed;

// El 0xa2 de 0x6349a0 en su lugar, con Logo03.bmd de Data\Logo\ y sus texturas desde el 0x455.
TEST(LoginModelsTest, TheLogoIsModel0xA2AtItsPlace)
{
    const ModelObject logo = LoginLogo();
    EXPECT_EQ(logo.model, 0xa2);
    EXPECT_EQ(LoginLogoModel, 0xa2);
    EXPECT_EQ(logo.transform.position, (glm::vec3{10680.0f, 15550.0f, 1710.0f}));
    EXPECT_EQ(logo.transform.angles, (glm::vec3{10.0f, 0.0f, 36.0f}));
    EXPECT_EQ(LoginLogoFile.model, 0xa2);
    EXPECT_EQ(LoginLogoFile.folder, std::string_view{"Logo\\"});
    EXPECT_EQ(LoginLogoFile.name, std::string_view{"Logo"});
    EXPECT_EQ(LoginLogoFile.number, 3);
}

// 0x6068d5 pisa la escala 1 con 2,2 y pone el blendMesh en 1; el resto, de 0x606660: sin malla oculta,
// la luz del blendMesh en 1 y la del objeto en 0 hasta el primer paso, y 0,16 por paso.
TEST(LoginModelsTest, TheLogoIsScaled2_2WithBlendMesh1)
{
    const ModelObject logo = LoginLogo();
    EXPECT_FLOAT_EQ(logo.transform.scale, 2.2f);
    EXPECT_EQ(logo.state.blendMesh, 1);
    EXPECT_EQ(logo.state.hiddenMesh, NoHiddenMesh);
    EXPECT_FLOAT_EQ(logo.state.light, 1.0f);
    EXPECT_EQ(logo.state.uv, glm::vec2(0.0f));
    EXPECT_EQ(logo.light, glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(logo.speed, 0.16f);
}

// Q-C92: las texturas del logo van desde el próximo que dejó el mundo 55, con GL_LINEAR y repitiendo.
TEST(LoginModelsTest, TheLogoTexturesStartWhereTheWorldLeft)
{
    EXPECT_EQ(LoginLogoFirstSlot.value, 0x455);
    EXPECT_EQ(LoginLogoSampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(LoginLogoSampling.wrap, Client::Data::Textures::Wrap::Repeat);
}

// La cuenta arranca en −1 y la primera vuelta de 0x633920 la pone en 0; a los 40, vuelve a 0 y la fase
// pasa a 1; desde ahí, vuelve a 0 cada 128.
TEST(LoginModelsTest, TheRoutesCountLikeTheCamera)
{
    LoginRoutes routes;
    EXPECT_EQ(routes.count, -1);
    EXPECT_EQ(routes.phase, 0);
    AdvanceRoutes(routes);
    EXPECT_EQ(routes.count, 0);
    for (int step = 1; step < 40; ++step)
    {
        AdvanceRoutes(routes);
    }
    EXPECT_EQ(routes.count, 39);
    EXPECT_EQ(routes.phase, 0);
    AdvanceRoutes(routes);
    EXPECT_EQ(routes.count, 0);
    EXPECT_EQ(routes.phase, 1);
    for (int step = 0; step < 127; ++step)
    {
        AdvanceRoutes(routes);
    }
    EXPECT_EQ(routes.count, 127);
    AdvanceRoutes(routes);
    EXPECT_EQ(routes.count, 0);
    EXPECT_EQ(routes.phase, 1);
}

// Q-C105: la luz va por la cuenta · 0,02 con la fase en 0, de −0,02 en el primer paso a 0,78 en el 41,
// y salta a 1,5 en el 42, cuando la fase pasa a 1; después queda ahí.
TEST(LoginModelsTest, TheLogoLightJumpsWhenThePhaseChanges)
{
    LoginRoutes routes;
    std::vector<float> lights;
    for (int step = 0; step < 200; ++step)
    {
        lights.push_back(LoginLogoLight(routes));
        AdvanceRoutes(routes);
    }
    EXPECT_FLOAT_EQ(lights[0], -0.02f);
    EXPECT_FLOAT_EQ(lights[1], 0.0f);
    EXPECT_FLOAT_EQ(lights[11], 10.0f * 0.02f);
    EXPECT_FLOAT_EQ(lights[40], 39.0f * 0.02f);
    EXPECT_FLOAT_EQ(lights[41], 1.5f);
    EXPECT_FLOAT_EQ(lights[199], 1.5f);
    EXPECT_FLOAT_EQ(LoginLogoLight(LoginRoutes{.count = 5, .phase = 2}), 1.5f);
}

// 0x5ffce0 sin luz por vértice: 0,1 de la del terreno, canal por canal, más la del objeto.
TEST(LoginModelsTest, AnUnlitObjectTakesATenthOfTheTerrainLight)
{
    const glm::vec3 color = UnlitObjectColor({0.5f, 0.8f, 1.0f}, glm::vec3(0.3f));
    EXPECT_FLOAT_EQ(color.x, (0.5f * 0.1f) + 0.3f);
    EXPECT_FLOAT_EQ(color.y, (0.8f * 0.1f) + 0.3f);
    EXPECT_FLOAT_EQ(color.z, (1.0f * 0.1f) + 0.3f);
}

// 0x517270 en la escena 2: la clase 4 con el juego 29 en +7, la maza en +13, el cuervo, la capa y el
// caballo, sin cabeza.
TEST(LoginModelsTest, TheCharacterIsClassFourWithItsSet)
{
    const CharacterAppearance character = LoginCharacter();
    EXPECT_EQ(character.characterClass, 4);
    EXPECT_EQ(character.skin, 0);
    const auto partAt = [&character](BodyPart part) { return character.bodyParts[static_cast<std::size_t>(part)]; };
    EXPECT_EQ(partAt(BodyPart::Helm).model, 0x102e);
    EXPECT_EQ(partAt(BodyPart::Armor).model, 0x122e);
    EXPECT_EQ(partAt(BodyPart::Pants).model, 0x142e);
    EXPECT_EQ(partAt(BodyPart::Gloves).model, 0x162e);
    EXPECT_EQ(partAt(BodyPart::Boots).model, 0x182e);
    for (const auto& part : character.bodyParts)
    {
        EXPECT_EQ(part.level, 7);
        EXPECT_FALSE(part.excellent);
        EXPECT_FALSE(part.ancient);
    }
    ASSERT_TRUE(character.rightHand.has_value());
    EXPECT_EQ(character.rightHand->model, 0x620);
    EXPECT_EQ(character.rightHand->level, 13);
    ASSERT_TRUE(character.leftHand.has_value());
    EXPECT_EQ(character.leftHand->model, 0x1c17);
    EXPECT_EQ(character.leftHand->level, 0);
    EXPECT_EQ(character.wings, std::optional<std::uint16_t>{0x1c30});
    EXPECT_EQ(character.pet, std::optional<std::uint16_t>{0x1c16});
    EXPECT_EQ(character.head, std::nullopt);
}

// 0x6349a0 después de 0x517270: el lugar, el giro y la escala; la luz de 0x517270 y la acción de
// parado que 0x4f2250 le da con el caballo.
TEST(LoginModelsTest, TheCharacterStandsAtItsPlace)
{
    const Client::Render::Models::ObjectTransform transform = LoginCharacterTransform();
    EXPECT_EQ(transform.position, (glm::vec3{10603.0f, 14112.0f, 580.0f}));
    EXPECT_EQ(transform.angles, (glm::vec3{0.0f, 0.0f, 188.0f}));
    EXPECT_FLOAT_EQ(transform.scale, 1.6f);
    EXPECT_FLOAT_EQ(transform.lockedRootHeight, 0.0f);
    EXPECT_FLOAT_EQ(LoginCharacterLight, 0.3f);
    EXPECT_EQ(LoginCharacterStandingAction, 0x4a);
}

// Fuera de la ventana de cada 2.300 ms, nunca, y sin tirar; adentro, con la semilla fija, las dos salen
// en 100 pasos, y en otros no cambia. La ventana va de 0 a 100 ms: 100 · 0,01 en la x87 da 0,99999998,
// y ftol lo trunca a 0 (Q-C110).
TEST(LoginModelsTest, TheActionOnlyChangesInItsWindow)
{
    using std::chrono::milliseconds;
    std::mt19937 random(FixedSeed);
    const std::mt19937 untouched = random;
    for (const milliseconds clock : {milliseconds{101}, milliseconds{2299}, milliseconds{2300 + 101},
                                     milliseconds{(2 * 2300) + 101}, milliseconds{1150}, milliseconds{230000 + 150}})
    {
        EXPECT_EQ(LoginCharacterActionAt(clock, random), std::nullopt) << clock.count();
    }
    EXPECT_TRUE(random == untouched);
    for (const milliseconds clock : {milliseconds{0}, milliseconds{100}, milliseconds{2300 + 100}})
    {
        std::mt19937 inside(FixedSeed);
        static_cast<void>(LoginCharacterActionAt(clock, inside));
        EXPECT_FALSE(inside == untouched) << clock.count();
    }

    std::set<std::optional<std::uint8_t>> seen;
    for (int step = 0; step < 100; ++step)
    {
        const milliseconds clock{(2300 * step) + (step % 101)};
        const std::optional<std::uint8_t> action = LoginCharacterActionAt(clock, random);
        EXPECT_TRUE(!action || *action == 0x54 || *action == 0x55);
        seen.insert(action);
    }
    EXPECT_EQ(seen, (std::set<std::optional<std::uint8_t>>{std::nullopt, 0x54, 0x55}));
    EXPECT_FALSE(random == untouched);
}

// rand() % 6 en 1 es la 0x54 y en 2 la 0x55, con rand() de 0 a 32.767: con 40 semillas salen los seis
// restos, y cada uno da lo suyo.
TEST(LoginModelsTest, TheActionComesFromRandModSix)
{
    std::set<int> remainders;
    for (std::mt19937::result_type seed = 0; seed < 40; ++seed)
    {
        std::mt19937 random(seed);
        std::mt19937 copy = random;
        const int remainder = static_cast<int>(copy() % 32768) % 6;
        remainders.insert(remainder);
        const std::optional<std::uint8_t> expected =
            remainder == 1 ? std::optional<std::uint8_t>{0x54}
                           : (remainder == 2 ? std::optional<std::uint8_t>{0x55} : std::nullopt);
        EXPECT_EQ(LoginCharacterActionAt(std::chrono::milliseconds{2300}, random), expected) << seed;
        EXPECT_TRUE(random == copy) << seed;
    }
    EXPECT_EQ(remainders.size(), 6U);
}
} // namespace
