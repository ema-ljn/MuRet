#include "Models/IModelLibrary.h"
#include "Scenes/WizardEffects.h"

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Sprites.h"
#include "World/WizardModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace
{
using Client::Scenes::ActorState;

// Los esqueletos del juego (0x5769ad8 + tipo · 0xe0): Player.bmd con 0xc3 acciones y un monstruo con 6; el resto no
// está (el registro sin acciones).
class PushedModels final : public Client::Render::Models::IModelLibrary
{
public:
    static constexpr std::uint16_t Monster = 0x10;
    PushedModels()
    {
        player.actions.resize(0xc3);
        monster.actions.resize(6);
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == Client::Data::Models::PlayerSkeletonModel)
            return &player;
        return model == Monster ? &monster : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton player;
    Client::Render::Models::Skeleton monster;
};

// 0x524a8d..0x524b21: el 0xc2/4 que toca el suelo recorre los 400 primeros personajes del pool y empuja a los vivos,
// vistos (+0x16c), que no son el héroe ([0x743a170]), sin morirse (+0x389) y a 200 o menos en x e y: el 0x20a con
// Fenrir (+0x318 en 0x1c37) con 0x4e9cd0 (0x68 con sólo la derecha), el 0x20a sin él con 0x4ea030(0xc2) y los demás
// con 0x4ea030(5), que no pone una acción que el modelo no tiene. Sigue con todos y no saca rand().
TEST(WizardEffectsTest, TheFallingFirePushesWhoIsClose)
{
    constexpr std::size_t Hero = 3;
    constexpr std::uint8_t Standing = 1;
    constexpr std::uint16_t Missing = 0x11;
    constexpr std::uint16_t Sword = 0;
    auto world = std::make_unique<Client::Scenes::WorldState>();
    world->Runtime().hero = Hero;
    const PushedModels models;
    Client::Crt::CrtRandom random(1);
    const Client::World::WizardEffectServices services =
        Client::Scenes::WizardEffectServicesOf(*world, random, Client::Scenes::WizardSceneParts{.models = &models});
    ASSERT_TRUE(services.pushAround);
    const auto place = [&world](std::size_t slot, std::uint16_t type, float x, float y) -> ActorState&
    {
        ActorState& actor = world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.dying = 0;
        actor.type = type;
        actor.position = {x, y, 0.0f};
        actor.animation.action = Standing;
        actor.animation.frame = 2.0f;
        return actor;
    };
    const std::uint16_t player = Client::Data::Models::PlayerSkeletonModel;
    ActorState& justThere = place(7, player, 1000.0f, 1200.0f); // a 200 justo
    ActorState& rider = place(8, player, 1120.0f, 1160.0f);     // la raíz de 120² + 160²
    rider.pet.model = 0x1c37;
    rider.hands[0].model = Sword;
    rider.hands[1].model = Client::Scenes::NoItemModel;
    ActorState& monster = place(9, PushedModels::Monster, 900.0f, 900.0f);
    ActorState& unknown = place(10, Missing, 1000.0f, 1000.0f);
    ActorState& far = place(11, player, 1000.0f, 1201.0f);
    ActorState& unseen = place(12, player, 1000.0f, 1000.0f);
    unseen.seen = false;
    ActorState& dying = place(13, player, 1000.0f, 1000.0f);
    dying.dying = 1;
    ActorState& dead = place(14, player, 1000.0f, 1000.0f);
    dead.alive = false;
    ActorState& hero = place(Hero, player, 1000.0f, 1000.0f);
    ActorState& outside = place(Client::Scenes::ActorPoolSize - 1, player, 1000.0f, 1000.0f);

    const auto pushed = services.pushAround(1000.0f, 1000.0f);
    ASSERT_TRUE(pushed.HasValue()) << pushed.Error();
    EXPECT_EQ(justThere.animation.action, 0xc2);
    EXPECT_EQ(justThere.animation.previousAction, Standing);
    EXPECT_EQ(justThere.animation.frame, 0.0f);
    EXPECT_EQ(rider.animation.action, 0x68);
    EXPECT_EQ(monster.animation.action, 5);
    for (const ActorState* untouched : {&unknown, &far, &unseen, &dying, &dead, &hero, &outside})
    {
        EXPECT_EQ(untouched->animation.action, Standing);
        EXPECT_EQ(untouched->animation.frame, 2.0f);
    }
    EXPECT_EQ(random.State(), Client::Crt::CrtRandom(1).State());

    // Sin los modelos del juego, el 0x20a que empuja no tiene Player.bmd: corta en 0x524afc.
    const Client::World::WizardEffectServices bare =
        Client::Scenes::WizardEffectServicesOf(*world, random, Client::Scenes::WizardSceneParts{});
    const auto cut = bare.pushAround(1000.0f, 1000.0f);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x524afc"), std::string::npos) << cut.Error();
}

// 0x524d38: el 0xf6/2 del meteoro que cae va por World::CreateBlastModelEffect a la tabla del mundo, con el meteoro de
// dueño (+0x108, que el paso lee en vivo) y la clave 30 en +0xec y en la escala (30 · 0,01f), vida 12 y sin rand().
// Un dueño que no es un lugar de la tabla no tiene cómo leerse: corta.
TEST(WizardEffectsTest, TheFallingMeteorDropsItsBurstIntoTheWorld)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Crt::CrtRandom random(5);
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

// 0x5231f6..0x5232f8 con los joints del mundo: el 0xcb/1 en su primer paso (vida 45 con +0x1bc en 0) suelta 36
// joints 613/6 y 36 613/7 a la tabla de 0x530860 (WorldState::DrawTail), con el efecto de dueño; cada rand() % 10 del
// abanico va antes del alta de su 613/6, que saca el suyo (la key, rand() % 5). Después el paso de los joints los
// avanza sin cortar.
TEST(WizardEffectsTest, TheHiddenCircleFansItsRaysIntoTheWorld)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    ActorState& caster = world->Actors()[5];
    caster.alive = true;
    caster.byte1bc = 0;
    Client::World::FrameSprites sprites;
    Client::Crt::CrtRandom random(21);
    const Client::World::WizardEffectServices services = Client::Scenes::WizardEffectServicesOf(
        *world, random,
        Client::Scenes::WizardSceneParts{.particles = &world->Particles(), .worldClock = 1000.0f, .sprites = &sprites});
    const Client::World::ModelEffectArguments arguments = Client::Scenes::WizardArguments(
        0xcb, {1000.0f, 2000.0f, 300.0f}, {0.0f, 0.0f, 90.0f}, glm::vec3(1.0f), 1, &caster);
    const auto created = Client::World::CreateWizardModelEffect(world->Map().effects, arguments, random, services);
    ASSERT_TRUE(created.HasValue() && created.Value().has_value()) << (created.HasValue() ? "" : created.Error());
    Client::World::ModelEffect& circle = world->Map().effects.Effects()[*created.Value()];
    ASSERT_EQ(circle.life, 45);

    auto twin = std::make_unique<std::array<Client::Scenes::JointState, 500>>();
    Client::Crt::CrtRandom oracle = random;
    static_cast<void>(oracle.Next()); // 0x5208c5
    glm::vec3 point{1000.0f, 2000.0f, 400.0f};
    for (int ray = 0; ray < 36; ++ray)
    {
        glm::vec3 angles{-10.0f, 0.0f, static_cast<float>(oracle.Next() % 10 + 10) * static_cast<float>(ray)};
        glm::vec3 second = point;
        for (const std::int32_t subtype : {6, 7})
        {
            if (subtype == 7)
                angles.z = 10.0f * static_cast<float>(ray);
            const Client::World::FishEffectRequest request{.type = 0x613,
                                                           .firstPosition = point,
                                                           .secondPosition = second,
                                                           .angles = angles,
                                                           .subtype = subtype,
                                                           .ownerContext = &circle,
                                                           .scale = 60.0f,
                                                           .key = 0,
                                                           .skill = 0,
                                                           .skillSerial = 0,
                                                           .target = -1};
            ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(*twin, request, oracle).HasValue());
        }
    }
    const auto stepped = Client::World::StepWizardModelEffect(circle, nullptr, random, services);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), oracle.State());
    const auto& joints = world->DrawTail().joints;
    for (std::size_t slot = 0; slot < joints.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        const Client::Scenes::JointState& joint = joints[slot];
        const Client::Scenes::JointState& expected = (*twin)[slot];
        ASSERT_EQ(joint.alive, expected.alive);
        if (!joint.alive)
            continue;
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, expected.subtype);
        EXPECT_EQ(joint.key, expected.key);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.angles, expected.angles);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.width, 60.0f);
        EXPECT_EQ(joint.ownerContext, &circle);
    }
    EXPECT_EQ(std::ranges::count_if(joints, [](const Client::Scenes::JointState& joint) { return joint.alive; }), 72);

    const auto advanced =
        Client::Scenes::StepJoints(world->DrawTail().joints, random, nullptr, &world->Particles(), 1000.0f, &sprites);
    ASSERT_TRUE(advanced.HasValue()) << advanced.Error();
}
} // namespace
