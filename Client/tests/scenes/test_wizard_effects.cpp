#include "Scenes/WizardEffects.h"

#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/WizardModelEffects.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

namespace
{
using Client::Scenes::ActorState;

// 0x524a8d..0x524b21: el 0xc2/4 que toca el suelo mira los 400 primeros personajes del pool: vivos, vistos (+0x16c),
// que no sean el héroe ([0x743a170]), sin morirse (+0x389) y a 200 o menos en x e y.
TEST(WizardEffectsTest, TheFallingFireFindsWhoItWouldPush)
{
    constexpr std::size_t Hero = 3;
    constexpr std::size_t Close = 7;
    auto world = std::make_unique<Client::Scenes::WorldState>();
    world->Runtime().hero = Hero;
    Client::Render::Models::CrtRandom random(1);
    const Client::World::WizardEffectServices services =
        Client::Scenes::WizardEffectServicesOf(*world, random, Client::Scenes::WizardSceneParts{});
    ASSERT_TRUE(services.someoneToPush);
    const auto place = [&world](std::size_t slot, float x, float y) -> ActorState&
    {
        ActorState& actor = world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.dying = 0;
        actor.position = {x, y, 0.0f};
        return actor;
    };
    EXPECT_FALSE(services.someoneToPush(1000.0f, 1000.0f));

    // A 200 justo, sí; a 201, no.
    ActorState& close = place(Close, 1000.0f, 1200.0f);
    EXPECT_TRUE(services.someoneToPush(1000.0f, 1000.0f));
    close.position = {1000.0f, 1201.0f, 0.0f};
    EXPECT_FALSE(services.someoneToPush(1000.0f, 1000.0f));
    close.position = {1120.0f, 1160.0f, 0.0f};
    EXPECT_TRUE(services.someoneToPush(1000.0f, 1000.0f));

    // Sin ver, muriéndose o muerto, no.
    close.seen = false;
    EXPECT_FALSE(services.someoneToPush(1000.0f, 1000.0f));
    close.seen = true;
    close.dying = 1;
    EXPECT_FALSE(services.someoneToPush(1000.0f, 1000.0f));
    close.dying = 0;
    close.alive = false;
    EXPECT_FALSE(services.someoneToPush(1000.0f, 1000.0f));

    // El héroe y el lugar 400 (fuera de los 400 del bucle), no.
    place(Hero, 1000.0f, 1000.0f);
    place(Client::Scenes::ActorPoolSize - 1, 1000.0f, 1000.0f);
    EXPECT_FALSE(services.someoneToPush(1000.0f, 1000.0f));
}
} // namespace
