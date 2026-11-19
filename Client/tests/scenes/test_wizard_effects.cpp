#include "Scenes/WizardEffects.h"

#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>

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

// 0x524d38: el 0xf6/2 del meteoro que cae va por World::CreateBlastModelEffect a la tabla del mundo, con el meteoro de
// dueño (+0x108, que el paso lee en vivo) y la clave 30 en +0xec y en la escala (30 · 0,01f), vida 12 y sin rand().
// Un dueño que no es un lugar de la tabla no tiene cómo leerse: corta.
TEST(WizardEffectsTest, TheFallingMeteorDropsItsBurstIntoTheWorld)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::CrtRandom random(5);
    const Client::World::WizardEffectServices services =
        Client::Scenes::WizardEffectServicesOf(*world, random, Client::Scenes::WizardSceneParts{});
    ASSERT_TRUE(services.otherEffect);
    Client::World::ModelEffect& meteor = world->Map().effects.Effects()[3];
    meteor.alive = true;
    meteor.type = Client::World::FireEffect;
    meteor.subtype = 7;
    const Client::World::ModelEffectArguments burst{.type = 0xf6,
                                                    .position = {100.0f, 200.0f, 300.0f},
                                                    .angles = {},
                                                    .light = {0.0f, 0.5f, 0.0f},
                                                    .subtype = 2,
                                                    .owner = &meteor,
                                                    .key = 30};
    const auto created = services.otherEffect(burst);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), 5u);
    const Client::World::ModelEffect& made = world->Map().effects.Effects()[0];
    ASSERT_TRUE(made.alive);
    EXPECT_EQ(made.type, 0xf6);
    EXPECT_EQ(made.subtype, 2);
    EXPECT_EQ(made.owner.effect, &meteor);
    EXPECT_EQ(made.ownerContext, &meteor);
    EXPECT_EQ(made.life, 12);
    EXPECT_EQ(made.flagEc, 30.0f);
    EXPECT_EQ(made.transform.scale, static_cast<float>(30.0 * static_cast<double>(0.01f)));
    EXPECT_EQ(made.transform.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(made.light, glm::vec3(0.0f, 0.5f, 0.0f));

    Client::World::ModelEffect loose = meteor;
    auto outside = burst;
    outside.owner = &loose;
    const auto cut = services.otherEffect(outside);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x518cf0"), std::string::npos) << cut.Error();
    EXPECT_FALSE(world->Map().effects.Effects()[1].alive);
}
} // namespace
