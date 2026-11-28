#include "FrontEnd/LoginModels.h"

#include "Crt/CrtRandom.h"

#include "Pets/DarkSpirit.h"

#include "Data/Textures/Sampling.h"
#include "Kernel/CharacterAppearance.h"
#include "Models/ObjectState.h"
#include "Models/Pose.h"
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

// Client::FrontEnd::LoginModels: the logo (0x606660 and 0x604ab0) and the login character (0x517270, 0x4f2250 and
// 0x634c60), as data and as each step changes them (06 § El logo en el binario).
namespace
{
using Client::FrontEnd::AdvanceRoutes;
using Client::FrontEnd::LoginCharacter;
using Client::FrontEnd::LoginCharacterActionAt;
using Client::FrontEnd::LoginCharacterLight;
using Client::FrontEnd::LoginCharacterStandingAction;
using Client::FrontEnd::LoginCharacterTransform;
using Client::FrontEnd::LoginLogo;
using Client::FrontEnd::LoginLogoFile;
using Client::FrontEnd::LoginLogoFirstSlot;
using Client::FrontEnd::LoginLogoLight;
using Client::FrontEnd::LoginLogoModel;
using Client::FrontEnd::LoginLogoSampling;
using Client::FrontEnd::LoginRoutes;
using Client::FrontEnd::ModelObject;
using Client::FrontEnd::UnlitObjectColor;
using Client::Kernel::BodyPart;
using Client::Kernel::CharacterAppearance;
using Client::Models::NoHiddenMesh;
using Client::Tests::FixedSeed;

// 0x6349a0's 0xa2 in its place, with Logo03.bmd from Data\Logo\ and its textures from 0x455.
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

// 0x6068d5 overwrites scale 1 with 2.2 and sets blendMesh to 1; the rest, from 0x606660: no hidden mesh, the
// blendMesh light at 1 and the object's at 0 until 0x604ab0 writes them, and 0.16 per step.
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

// Q-C92: the logo textures go from the next one world 55 left, with GL_LINEAR and repeating.
TEST(LoginModelsTest, TheLogoTexturesStartWhereTheWorldLeft)
{
    EXPECT_EQ(LoginLogoFirstSlot.value, 0x455);
    EXPECT_EQ(LoginLogoSampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(LoginLogoSampling.wrap, Client::Data::Textures::Wrap::Repeat);
}

// The count starts at −1 and 0x633920's first pass sets it to 0; at 40, it returns to 0 and the phase moves to 1;
// from there, it returns to 0 every 128.
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

// Q-C105: the light goes by count · 0.02 with the phase at 0, from −0.02 with the count at −1 (on the first step,
// which does not write it: LoginSceneModelsTest) to 0.78 at 41, and jumps to 1.5 at 42, when the phase moves to 1;
// then it stays there.
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

// 0x5ffce0 without per-vertex light: 0.1 of the terrain's, channel by channel, plus the object's.
TEST(LoginModelsTest, AnUnlitObjectTakesATenthOfTheTerrainLight)
{
    const glm::vec3 color = UnlitObjectColor({0.5f, 0.8f, 1.0f}, glm::vec3(0.3f));
    EXPECT_FLOAT_EQ(color.x, (0.5f * 0.1f) + 0.3f);
    EXPECT_FLOAT_EQ(color.y, (0.8f * 0.1f) + 0.3f);
    EXPECT_FLOAT_EQ(color.z, (1.0f * 0.1f) + 0.3f);
}

// 0x517270 in scene 2: class 4 with set 29 at +7, the mace at +13, the raven, the cape and the horse, without a head.
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
    EXPECT_EQ(character.wings, std::optional<std::uint16_t>{std::uint16_t{0x1c30}});
    EXPECT_EQ(character.pet, std::optional<std::uint16_t>{std::uint16_t{0x1c16}});
    EXPECT_EQ(character.head, std::nullopt);
}

// 0x6349a0 after 0x517270: the place, the turn and the scale; 0x517270's light and the standing action 0x4f2250 gives
// it with the horse.
TEST(LoginModelsTest, TheCharacterStandsAtItsPlace)
{
    const Client::Models::ObjectTransform transform = LoginCharacterTransform();
    EXPECT_EQ(transform.position, (glm::vec3{10603.0f, 14112.0f, 580.0f}));
    EXPECT_EQ(transform.angles, (glm::vec3{0.0f, 0.0f, 188.0f}));
    EXPECT_FLOAT_EQ(transform.scale, 1.6f);
    EXPECT_FLOAT_EQ(transform.lockedRootHeight, 0.0f);
    EXPECT_FLOAT_EQ(LoginCharacterLight, 0.3f);
    EXPECT_EQ(LoginCharacterStandingAction, 0x4a);
}

// Outside the window of every 2300 ms, never, and without a rand(); inside, with the fixed seed, both come out within
// 100 steps, and on others it does not change. The window goes from 0 to 100 ms: 100 · 0.01 on the x87 gives
// 0.99999998, and ftol truncates it to 0 (Q-C110).
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
    EXPECT_EQ(seen, (std::set<std::optional<std::uint8_t>>{std::nullopt, std::uint8_t{0x54}, std::uint8_t{0x55}}));
    EXPECT_FALSE(random == untouched);
}

// With the CRT rand() (the cloth's, ClothWorld, which is the original's), the same: outside the window it does not
// touch it, and inside it takes a single one, with its % 6.
TEST(LoginModelsTest, TheActionTakesOneRandOfTheCrtInsideTheWindow)
{
    using std::chrono::milliseconds;
    Client::Crt::CrtRandom random(77);
    const std::uint32_t untouched = random.State();
    EXPECT_EQ(LoginCharacterActionAt(milliseconds{101}, random), std::nullopt);
    EXPECT_EQ(random.State(), untouched);

    std::set<int> remainders;
    for (int k = 0; k < 60; ++k)
    {
        Client::Crt::CrtRandom copy = random;
        const int remainder = copy.Next() % 6;
        remainders.insert(remainder);
        const std::optional<std::uint8_t> expected =
            remainder == 1 ? std::optional<std::uint8_t>{std::uint8_t{0x54}}
                           : (remainder == 2 ? std::optional<std::uint8_t>{std::uint8_t{0x55}} : std::nullopt);
        EXPECT_EQ(LoginCharacterActionAt(milliseconds{2300 * k}, random), expected) << k;
        EXPECT_EQ(random.State(), copy.State()) << "un solo rand()";
    }
    EXPECT_EQ(remainders.size(), 6u);
}

// rand() % 6 at 1 is 0x54 and at 2 is 0x55, with rand() from 0 to 32767: with 40 seeds all six remainders come out,
// and each gives its own.
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
            remainder == 1 ? std::optional<std::uint8_t>{std::uint8_t{0x54}}
                           : (remainder == 2 ? std::optional<std::uint8_t>{std::uint8_t{0x55}} : std::nullopt);
        EXPECT_EQ(LoginCharacterActionAt(std::chrono::milliseconds{2300}, random), expected) << seed;
        EXPECT_TRUE(random == copy) << seed;
    }
    EXPECT_EQ(remainders.size(), 6U);
}

// The raven's owner at login: the character at its place, without flags (+0x78 at 0: neither 0x1000, which stops it,
// nor 0x4000, which removes its sparks).
TEST(LoginModelsTest, TheRavensOwnerIsTheRiderWithoutFlags)
{
    const Client::Pets::DarkSpiritOwner owner = Client::FrontEnd::LoginRavenOwner();
    EXPECT_EQ(owner.position, Client::FrontEnd::LoginCharacterTransform().position);
    EXPECT_EQ(owner.flags, 0u);
}
} // namespace
