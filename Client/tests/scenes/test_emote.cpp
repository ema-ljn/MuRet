#include "Scenes/Emote.h"

#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Movement.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "UI/Emotes.h"

#include "support/Bytes.h"
#include "support/PlayerActors.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Scenes::PlayTheEmote (0x576a60) y DoTheEmote (un caso de 0x576eb0): la compuerta, el +1 del elfo y los C1 05 18.
// Los valores salen del ASM (0x576a84..0x576e97 y los casos), no de una corrida.
namespace
{
using Client::Scenes::ActorState;
using Client::Scenes::EmoteContext;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::UI::EmoteCase;

constexpr std::uint8_t ElfClass = 2;
// Un tipo que no es jugador, como el de un transformado (0x45), con sus acciones.
constexpr std::uint16_t TransformedModel = 0x30;

// Player.bmd con 205 acciones y el modelo de un transformado, con las que diga el test; ninguno más.
class EmoteModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == Client::Data::Models::PlayerSkeletonModel)
            return &player;
        return model == TransformedModel ? &transformed : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton player = Client::Tests::PlayerActionsSkeleton();
    Client::Render::Models::Skeleton transformed;
};

class EmoteTest : public ::testing::Test
{
protected:
    EmoteContext Context()
    {
        return EmoteContext{
            .hero = *m_hero, .models = m_models, .send = [this](std::uint8_t direction, std::uint8_t action) {
                m_sent.push_back(Client::Protocol::Movement::WriteAction(direction, action));
            }};
    }

    // Los C1 18 mandados, todos iguales a C1 05 18 [direction] [action] en claro y con el XOR32.
    void ExpectSent(std::size_t count, std::uint8_t direction, std::uint8_t action)
    {
        ASSERT_EQ(m_sent.size(), count);
        for (const Client::Protocol::Outgoing& sent : m_sent)
        {
            EXPECT_EQ(sent.delivery, Client::Network::Delivery::Clear);
            EXPECT_TRUE(SameBytes(Client::Network::Crypto::Xor32Decode(sent.packet).Bytes(),
                                  Bytes({0xc1, 5, 0x18, direction, action})));
        }
    }

    std::unique_ptr<ActorState> m_hero = std::make_unique<ActorState>(Client::Tests::UnarmedPlayer());
    EmoteModels m_models;
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x576a84..0x576a94 y un caso: con la acción de 1 a 12 o 0x8a, la animación y dos C1 18 iguales (Q-C776), los dos
// con la dirección del ángulo del héroe.
TEST_F(EmoteTest, AStandingHeroAnimatesAndSendsTwoEqualActions)
{
    std::vector<std::uint8_t> standing;
    for (std::uint8_t action = 1; action <= 0xc; ++action)
        standing.push_back(action);
    standing.push_back(0x8a);
    for (const std::uint8_t action : standing)
    {
        SCOPED_TRACE(action);
        m_sent.clear();
        m_hero->animation.action = action;
        m_hero->angles.z = 100.0f;
        ASSERT_TRUE(Client::Scenes::DoTheEmote(EmoteCase{0x99, 0x83}, Context()).HasValue());
        EXPECT_EQ(m_hero->animation.action, 0x99);
        ExpectSent(2, 3, 0x83);
    }
}

// Caminando (0x0d..0x21), pegando o en cualquier otra acción, solo el C1 18 del caso, y el héroe no se anima.
TEST_F(EmoteTest, AHeroThatIsNotStandingSendsOnlyTheCase)
{
    std::vector<std::uint8_t> moving{0, 0x89, 0x8b, 0x99, 0xca};
    for (std::uint8_t action = 0xd; action <= 0x21; ++action)
        moving.push_back(action);
    for (const std::uint8_t action : moving)
    {
        SCOPED_TRACE(action);
        m_sent.clear();
        m_hero->animation.action = action;
        m_hero->angles.z = -30.0f;
        ASSERT_TRUE(Client::Scenes::DoTheEmote(EmoteCase{0xa9, 0x8b}, Context()).HasValue());
        EXPECT_EQ(m_hero->animation.action, action);
        ExpectSent(1, 0, 0x8b);
    }
}

// 0x576a9d..0x576abc: con el elfo (+0x1d4 & 7 == 2, también con los bits de arriba) la animación siguiente, salvo de
// 0xb7 a 0xc0; las otras clases, la misma.
TEST_F(EmoteTest, TheElfTakesTheNextAnimationOutsideTheSharedOnes)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint8_t animation;
        std::uint8_t played;
    };
    const std::vector<Case> cases = {
        {ElfClass, 0x99, 0x9a}, {ElfClass, 0xb5, 0xb6}, {ElfClass, 0xb6, 0xb7}, {ElfClass, 0xb7, 0xb7},
        {ElfClass, 0xbc, 0xbc}, {ElfClass, 0xc0, 0xc0}, {ElfClass, 0xc1, 0xc2}, {0x12, 0x9f, 0xa0},
        {0x0a, 0xad, 0xae},     {0x1a, 0xbe, 0xbe},     {0, 0x99, 0x99},        {1, 0xb5, 0xb5},
        {3, 0xa7, 0xa7},        {0x10, 0xb1, 0xb1},     {0x11, 0xaf, 0xaf},     {7, 0xa1, 0xa1},
    };
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(testing::Message() << "clase " << int{tested.characterClass} << ", animación "
                                        << int{tested.animation});
        m_sent.clear();
        m_hero->characterClass = tested.characterClass;
        m_hero->animation.action = 1;
        const auto played = Client::Scenes::PlayTheEmote(*m_hero, EmoteCase{tested.animation, 0x8d}, Context());
        ASSERT_TRUE(played.HasValue());
        EXPECT_TRUE(played.Value());
        EXPECT_EQ(m_hero->animation.action, tested.played);
        ExpectSent(1, 1, 0x8d);
    }
}

// 0x4ea030 no pone una animación que el modelo no tiene, pero 0x576a60 manda igual su C1 18.
TEST_F(EmoteTest, AnAnimationTheModelDoesNotHaveStillSends)
{
    m_models.player.actions.resize(0x99);
    m_hero->animation.action = 2;
    ASSERT_TRUE(Client::Scenes::DoTheEmote(EmoteCase{0x99, 0x83}, Context()).HasValue());
    EXPECT_EQ(m_hero->animation.action, 2);
    ExpectSent(2, 1, 0x83);
}

// La guerra de gremios (0x4d8473, hito 8) llama a 0x576a60 con otro personaje: la compuerta, la clase y la animación
// son suyas, pero la dirección del C1 18 sale siempre del ángulo del héroe ([0x743a170] + 0x24).
TEST_F(EmoteTest, AnotherActorTakesTheDirectionFromTheHero)
{
    auto other = std::make_unique<ActorState>(Client::Tests::UnarmedPlayer(ElfClass));
    other->animation.action = 0x8a;
    other->angles.z = 200.0f;
    m_hero->angles.z = 0.0f;
    m_hero->animation.action = 0x20;
    const auto played = Client::Scenes::PlayTheEmote(*other, EmoteCase{0xc0, 0x94}, Context());
    ASSERT_TRUE(played.HasValue());
    EXPECT_TRUE(played.Value());
    EXPECT_EQ(other->animation.action, 0xc0);
    EXPECT_EQ(m_hero->animation.action, 0x20);
    ExpectSent(1, 1, 0x94);

    m_sent.clear();
    other->animation.action = 0x0d;
    const auto walking = Client::Scenes::PlayTheEmote(*other, EmoteCase{0xad, 0x8d}, Context());
    ASSERT_TRUE(walking.HasValue());
    EXPECT_FALSE(walking.Value());
    EXPECT_EQ(other->animation.action, 0x0d);
    EXPECT_TRUE(m_sent.empty());
}

// Sin quien mande, el error con la dirección del envío; la animación ya está puesta, como en el original.
TEST_F(EmoteTest, WithoutASenderTheEmoteIsAnError)
{
    m_hero->animation.action = 1;
    const auto done = Client::Scenes::DoTheEmote(EmoteCase{0x99, 0x83},
                                                 EmoteContext{.hero = *m_hero, .models = m_models, .send = {}});
    ASSERT_FALSE(done.HasValue());
    EXPECT_NE(done.Error().find("0x576b3e"), std::string::npos);
    EXPECT_EQ(m_hero->animation.action, 0x99);
}

// 0x4ea036..0x4ea054: 0x4ea030 mira las acciones del modelo del tipo del personaje (+2). Un héroe transformado anima
// con las de su modelo, no con las de Player.bmd; sin el modelo, el error. Los C1 18 salen igual.
TEST_F(EmoteTest, TheAnimationUsesTheModelOfTheActorType)
{
    m_hero->type = TransformedModel;
    m_hero->animation.action = 1;
    m_models.transformed.actions.resize(0x99);
    ASSERT_TRUE(Client::Scenes::DoTheEmote(EmoteCase{0x99, 0x83}, Context()).HasValue());
    EXPECT_EQ(m_hero->animation.action, 1);
    ExpectSent(2, 1, 0x83);

    m_sent.clear();
    m_models.transformed.actions.resize(0x9a);
    ASSERT_TRUE(Client::Scenes::DoTheEmote(EmoteCase{0x99, 0x83}, Context()).HasValue());
    EXPECT_EQ(m_hero->animation.action, 0x99);
    ExpectSent(2, 1, 0x83);

    m_sent.clear();
    m_hero->type = 0x31;
    m_hero->animation.action = 1;
    const auto missing = Client::Scenes::DoTheEmote(EmoteCase{0x99, 0x83}, Context());
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x4ea030"), std::string::npos);
    EXPECT_TRUE(m_sent.empty());
}
} // namespace
