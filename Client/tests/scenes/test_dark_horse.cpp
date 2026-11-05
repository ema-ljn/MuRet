#include "Scenes/DarkHorse.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <ios>
#include <map>
#include <vector>

// Client::Scenes::DarkHorse: el caballo oscuro (0xbf) que crea 0x443d70 y mueve 0x444010 en cada paso
// (Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro").
namespace
{
using Client::Render::Models::ActionInfo;
using Client::Render::Models::BoneKey;
using Client::Render::Models::ObjectTransform;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Scenes::DarkHorse;
using Client::Scenes::DarkHorseGait;
using Client::Scenes::DarkHorseGaitOf;
using Client::Scenes::DarkHorseLoginScale;
using Client::Scenes::DarkHorseModel;
using Client::Scenes::DrawnDarkHorse;
using Client::Scenes::StepDarkHorse;

// Las siete acciones de DarkHorse.bmd, de 10 keys cada una, en un hueso.
Skeleton HorseSkeleton()
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.actions.assign(7, std::vector<BoneKey>(10, BoneKey{}));
    for (std::vector<BoneKey>& keys : bone.actions)
    {
        for (BoneKey& key : keys)
        {
            key.rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
        }
    }
    skeleton.bones = {bone};
    skeleton.actions.assign(7, ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false});
    return skeleton;
}

void ExpectGait(std::uint8_t rider, std::uint8_t action, float speed)
{
    const DarkHorseGait gait = DarkHorseGaitOf(rider);
    EXPECT_EQ(gait.action, action) << std::hex << static_cast<int>(rider);
    EXPECT_FLOAT_EQ(gait.speed, speed) << std::hex << static_cast<int>(rider);
}

// 0x444010 con el 0xbf: la acción del caballo por la del jinete (+0x111), con la velocidad que escribe
// en +0xd8.
TEST(DarkHorseTest, TheHorseFollowsItsRider)
{
    ExpectGait(0x53, 3, 0.34f);
    ExpectGait(0x4b, 1, 0.34f);
    ExpectGait(0x4e, 2, 0.34f);
    ExpectGait(0x52, 2, 0.34f);
    ExpectGait(0x54, 5, 1.0f);
    ExpectGait(0x55, 6, 1.0f);
    // La de parado montado del login (0x4a) y las vecinas de los rangos, la 0 a 0,3.
    ExpectGait(0x4a, 0, 0.3f);
    ExpectGait(0x4c, 0, 0.3f);
    ExpectGait(0x4d, 0, 0.3f);
    ExpectGait(0x56, 0, 0.3f);
    ExpectGait(0x00, 0, 0.3f);
}

// 0x443d70 lo crea invisible, con el alpha en 0 y la velocidad en 0,5; 0x4e9980 lo sube en cada paso
// un décimo de lo que le falta para 1: 0,1, 0,19, … y pasa 0,99 (desde ahí va opaco) en el paso 44.
TEST(DarkHorseTest, TheHorseFadesInByATenthOfWhatIsLeft)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    EXPECT_FLOAT_EQ(horse.alpha, 0.0f);
    EXPECT_FLOAT_EQ(horse.speed, 0.5f);

    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_FLOAT_EQ(horse.alpha, 0.1f);
    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_FLOAT_EQ(horse.alpha, 0.19f);
    for (int step = 3; step <= 43; ++step)
    {
        StepDarkHorse(horse, skeleton, 0x4a);
    }
    EXPECT_LT(horse.alpha, 0.99f);
    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_GE(horse.alpha, 0.99f);
}

// 0x4e9a0c..0x4e9a1e: la resta, el producto por 0,1f y la suma van en la x87, con un solo redondeo a float
// al final (fstp). Redondear en cada operación da otro bit desde el paso 5 (0x3ed1ab4c y no 0x3ed1ab4b).
TEST(DarkHorseTest, TheFadeRoundsOnceLikeTheBinary)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    const std::map<int, std::uint32_t> expected{{1, 0x3dcccccdU},  {2, 0x3e428f5cU},  {5, 0x3ed1ab4bU},
                                                {10, 0x3f26bd03U}, {44, 0x3f7d8474U}, {60, 0x3f7f8a3dU}};
    for (int step = 1; step <= 60; ++step)
    {
        StepDarkHorse(horse, skeleton, 0x4a);
        if (const auto bits = expected.find(step); bits != expected.end())
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(horse.alpha), bits->second) << "paso " << step;
        }
    }
}

// En cada paso, la acción que le da el jinete (0x4ea030: la otra pasa a anterior y el frame a 0) y el
// avance a su velocidad (0x4ec200).
TEST(DarkHorseTest, AStepTakesTheActionOfTheRiderAndAdvancesAtItsSpeed)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;

    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_EQ(horse.animation.action, 0u);
    EXPECT_FLOAT_EQ(horse.animation.frame, 0.3f);
    EXPECT_FLOAT_EQ(horse.speed, 0.3f);

    StepDarkHorse(horse, skeleton, 0x54);
    EXPECT_EQ(horse.animation.action, 5u);
    EXPECT_FLOAT_EQ(horse.animation.frame, 1.0f);
    EXPECT_FLOAT_EQ(horse.speed, 1.0f);

    StepDarkHorse(horse, skeleton, 0x54);
    EXPECT_FLOAT_EQ(horse.animation.frame, 2.0f);
}

// Lo que se dibuja: el 0xbf, en el lugar y con los ángulos del jinete y la escala de 0x445260, con su
// alpha, la luz por vértice y la animación de un poco más que el paso.
TEST(DarkHorseTest, TheHorseIsDrawnWhereItsRiderIs)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    StepDarkHorse(horse, skeleton, 0x4a);
    ObjectTransform rider;
    rider.position = {10603.0f, 14112.0f, 580.0f};
    rider.angles = {0.0f, 0.0f, 188.0f};
    rider.scale = 1.6f;

    const auto drawn = DrawnDarkHorse(horse, &skeleton, rider, DarkHorseLoginScale, 0.5f);

    EXPECT_EQ(drawn.model, DarkHorseModel);
    EXPECT_EQ(drawn.transform.position, rider.position);
    EXPECT_EQ(drawn.transform.angles, rider.angles);
    EXPECT_FLOAT_EQ(drawn.transform.scale, 1.6f);
    EXPECT_FLOAT_EQ(drawn.alpha, 0.1f);
    EXPECT_TRUE(drawn.vertexLight);
    EXPECT_FLOAT_EQ(drawn.animation.frame, 0.3f + 0.5f * 0.3f);
    // Sin el esqueleto, la del paso.
    EXPECT_FLOAT_EQ(DrawnDarkHorse(horse, nullptr, rider, 1.0f, 0.5f).animation.frame, 0.3f);
    EXPECT_FLOAT_EQ(DrawnDarkHorse(horse, nullptr, rider, 1.0f, 0.5f).transform.scale, 1.0f);
}
} // namespace
