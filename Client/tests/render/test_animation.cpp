#include "Render/Models/Animation.h"

#include "App/FixedStep.h"
#include "Core/IClock.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Client::Render::Models: el avance de la animación (0x4ec200), el cambio de acción (0x4ea030), la
// mascota y las alas por tiempo (Q-C20) y la copia del render.
namespace
{
using Client::App::FixedStep;
using Client::Core::Duration;
using Client::Render::Models::ActionInfo;
using Client::Render::Models::AdvanceAnimation;
using Client::Render::Models::AdvanceByTime;
using Client::Render::Models::AnimationScene;
using Client::Render::Models::AnimationState;
using Client::Render::Models::Interpolated;
using Client::Render::Models::OriginalFrameTime;
using Client::Render::Models::PetKeysPerOriginalFrame;
using Client::Render::Models::SetAction;
using Client::Render::Models::Skeleton;
using Client::Render::Models::TimedAnimation;
using Client::Render::Models::WingKeysPerOriginalFrame;

using namespace std::chrono_literals;

constexpr AnimationScene Other = AnimationScene::Other;
constexpr float Tolerance = 1e-4f;

// Una acción que repite, de `keys` keys, como las de Player.bmd.
ActionInfo Repeating(std::int16_t keys, float speed = 0.0f)
{
    return ActionInfo{true, speed, keys, false};
}

// Avanza `steps` pasos con la velocidad de la acción.
void Steps(AnimationState& state, const ActionInfo& action, int steps, AnimationScene scene = Other)
{
    for (int k = 0; k < steps; ++k)
    {
        (void)AdvanceAnimation(state, action, action.speed, scene);
    }
}

// frame += velocidad, por paso.
TEST(AnimationTest, AdvancesBySpeedPerStep)
{
    AnimationState state{3, 1.0f, 3, 0};

    const bool ended = AdvanceAnimation(state, Repeating(7), 0.26f, Other);

    EXPECT_FALSE(ended);
    EXPECT_FLOAT_EQ(state.frame, 1.26f);
}

// Al pasar la última key vuelve con el módulo de las keys, con la parte fraccionaria, y termina.
TEST(AnimationTest, WrapsModuloTheKeys)
{
    AnimationState state{3, 6.9f, 3, 6};

    const bool ended = AdvanceAnimation(state, Repeating(7), 0.26f, Other);

    EXPECT_TRUE(ended);
    EXPECT_NEAR(state.frame, 0.16f, Tolerance);
}

// Con lock_positions, el módulo es de las keys menos 1: la última key es la misma que la 0.
TEST(AnimationTest, WrapsModuloKeysMinusOneWithLockPositions)
{
    AnimationState state{3, 5.9f, 3, 5};

    const bool ended = AdvanceAnimation(state, ActionInfo{true, 0.0f, 7, true}, 0.26f, Other);

    EXPECT_TRUE(ended);
    EXPECT_NEAR(state.frame, 0.16f, Tolerance);
}

// Una que no repite se queda en las keys menos 0,01 (0x6b88d4), y termina.
TEST(AnimationTest, HoldsJustBeforeTheLastKeyWithoutRepeat)
{
    AnimationState state{68, 6.9f, 68, 6};

    const bool ended = AdvanceAnimation(state, ActionInfo{false, 0.0f, 7, false}, 0.26f, Other);

    EXPECT_TRUE(ended);
    EXPECT_FLOAT_EQ(state.frame, 7.0f - 0.01f);
}

// Con el frame en 0 o menos, le suma las keys menos 1.
TEST(AnimationTest, ANegativeFrameAddsKeysMinusOne)
{
    AnimationState state{3, 0.1f, 3, 0};

    (void)AdvanceAnimation(state, Repeating(7), -0.3f, Other);

    EXPECT_NEAR(state.frame, 6.0f - 0.2f, Tolerance);
}

// Q-C87: una acción a la que el original no le pone velocidad avanza con 0. En el frame 0, la regla de
// arriba la pasa a la última key, y ahí se queda. La key entera no cambió al sumar, así que la
// anterior es la que había: con t en 0, la pose muestra esa.
TEST(AnimationTest, AnActionWithoutSpeedGoesToItsLastKeyAndStays)
{
    AnimationState state{40, 0.0f, 12, 3};

    Steps(state, Repeating(5), 10);

    EXPECT_FLOAT_EQ(state.frame, 4.0f);
    EXPECT_EQ(state.previousAction, 12);
    EXPECT_EQ(state.previousKey, 3);
}

// Con lock_positions (la acción 0 de Player.bmd, de tres keys, la única sin velocidad que la tiene),
// la última key es el módulo: vuelve a 0 en cada avance, y en cada uno termina.
TEST(AnimationTest, AnActionWithoutSpeedAndWithLockStaysAtZero)
{
    AnimationState state{0, 0.0f, 0, 0};

    for (int k = 0; k < 10; ++k)
    {
        EXPECT_TRUE(AdvanceAnimation(state, ActionInfo{true, 0.0f, 3, true}, 0.0f, Other));
        EXPECT_EQ(state.frame, 0.0f);
    }
}

// Con una velocidad negativa, la key entera se trunca hacia 0, como el __ftol del original: de 0,2 a
// −0,3 no cambia, y la anterior queda la que había.
TEST(AnimationTest, ANegativeSpeedTruncatesTowardZero)
{
    AnimationState state{3, 0.2f, 1, 4};

    (void)AdvanceAnimation(state, Repeating(7), -0.5f, Other);

    EXPECT_EQ(state.previousAction, 1);
    EXPECT_EQ(state.previousKey, 4);
    EXPECT_NEAR(state.frame, 6.0f - 0.3f, Tolerance);
}

// Un paso que cruza dos keys (con los multiplicadores de 0x4f9580 una velocidad llega a 2) guarda la
// key de antes, no la que cruzó en el medio.
TEST(AnimationTest, CrossingTwoKeysStoresTheOldOne)
{
    AnimationState state{3, 1.5f, 3, 0};

    (void)AdvanceAnimation(state, Repeating(7), 2.0f, Other);

    EXPECT_EQ(state.previousKey, 1);
    EXPECT_FLOAT_EQ(state.frame, 3.5f);
}

// Cayendo justo en la cantidad de keys ya pasó la última: la que repite vuelve a 0 y la que no se
// queda en las keys menos 0,01.
TEST(AnimationTest, LandingOnTheKeyCountIsPastTheLastKey)
{
    AnimationState repeating{3, 6.75f, 3, 6};
    AnimationState holding{68, 6.5f, 68, 6};

    EXPECT_TRUE(AdvanceAnimation(repeating, Repeating(7), 0.25f, Other));
    EXPECT_TRUE(AdvanceAnimation(holding, ActionInfo{false, 0.0f, 7, false}, 0.5f, Other));

    EXPECT_EQ(repeating.frame, 0.0f);
    EXPECT_FLOAT_EQ(holding.frame, 7.0f - 0.01f);
}

// El + 2 de la selección es solo de las acciones que repiten: una que no repite termina con el frame
// real.
TEST(AnimationTest, TheSelectionDoesNotLookAheadWithoutRepeat)
{
    AnimationState state{68, 4.9f, 68, 4};

    EXPECT_FALSE(AdvanceAnimation(state, ActionInfo{false, 0.0f, 7, false}, 0.26f, AnimationScene::CharacterSelect));
    EXPECT_NEAR(state.frame, 5.16f, Tolerance);
}

// Al cambiar la key entera, la vieja y su acción pasan a anteriores; sin cambiar, quedan las de antes.
TEST(AnimationTest, StoresThePreviousKeyWhenItCrossesOne)
{
    AnimationState state{3, 1.9f, 1, 4};

    (void)AdvanceAnimation(state, Repeating(7), 0.05f, Other);
    EXPECT_EQ(state.previousAction, 1);
    EXPECT_EQ(state.previousKey, 4);

    (void)AdvanceAnimation(state, Repeating(7), 0.26f, Other);
    EXPECT_EQ(state.previousAction, 3);
    EXPECT_EQ(state.previousKey, 1);
}

// Q-C21: en la selección, "terminó" sale con el frame + 2, dos keys antes, pero el frame sigue el
// real y vuelve recién al pasar la última.
TEST(AnimationTest, TheSelectionEndsTwoKeysEarlyButWrapsOnTheRealFrame)
{
    AnimationState selection{3, 4.9f, 3, 4};
    AnimationState other = selection;

    EXPECT_TRUE(AdvanceAnimation(selection, Repeating(7), 0.26f, AnimationScene::CharacterSelect));
    EXPECT_FALSE(AdvanceAnimation(other, Repeating(7), 0.26f, Other));
    EXPECT_FLOAT_EQ(selection.frame, other.frame);
    EXPECT_NEAR(selection.frame, 5.16f, Tolerance);
}

// Con una key o ninguna, ni avanza ni termina.
TEST(AnimationTest, AnActionOfOneKeyDoesNotMove)
{
    for (const std::int16_t keys : {std::int16_t{0}, std::int16_t{1}})
    {
        AnimationState state{3, 0.0f, 3, 0};

        EXPECT_FALSE(AdvanceAnimation(state, Repeating(keys), 0.26f, Other));
        EXPECT_EQ(state.frame, 0.0f);
    }
}

Skeleton ThreeActions()
{
    Skeleton skeleton;
    skeleton.actions = {Repeating(7), Repeating(7), Repeating(7)};
    return skeleton;
}

// Otra acción vuelve el frame a 0; la misma no toca nada.
TEST(AnimationTest, SetActionResetsOnlyForAnotherAction)
{
    AnimationState state{1, 3.5f, 0, 2};

    SetAction(state, ThreeActions(), 1);
    EXPECT_EQ(state.action, 1);
    EXPECT_EQ(state.frame, 3.5f);
    EXPECT_EQ(state.previousKey, 2);

    SetAction(state, ThreeActions(), 2);
    EXPECT_EQ(state.action, 2);
    EXPECT_EQ(state.frame, 0.0f);
}

// La de antes y su key pasan a anteriores: la pose va de ahí a la key 0 de la nueva.
TEST(AnimationTest, SetActionKeepsTheOldOneAsPrevious)
{
    AnimationState state{1, 3.5f, 0, 2};

    SetAction(state, ThreeActions(), 2);

    EXPECT_EQ(state.previousAction, 1);
    EXPECT_EQ(state.previousKey, 3);
}

// Una acción que el modelo no tiene no cambia nada.
TEST(AnimationTest, SetActionIgnoresAnActionTheModelDoesNotHave)
{
    AnimationState state{1, 3.5f, 0, 2};

    SetAction(state, ThreeActions(), 3);

    EXPECT_EQ(state.action, 1);
    EXPECT_EQ(state.frame, 3.5f);
    EXPECT_EQ(state.previousAction, 0);
}

// La traza de la selección (06): la acción 3, de siete keys, a 0,26 por paso. Los frames de la tabla,
// más 1 como los guarda el original, saltan 33, 33, 34 y 34 pasos.
TEST(AnimationTest, TheSelectionJumpsAreThoseOfTheTrace)
{
    constexpr std::array<float, 5> Frames = {1.740f, 3.320f, 4.900f, 6.740f, 1.580f};
    constexpr std::array<int, 4> StepsBetween = {33, 33, 34, 34};
    const ActionInfo action = Repeating(7, 0.26f);
    AnimationState state{3, Frames[0] + 1.0f, 3, 1};

    for (std::size_t k = 0; k < StepsBetween.size(); ++k)
    {
        Steps(state, action, StepsBetween[k]);
        EXPECT_NEAR(state.frame, std::fmod(Frames[k + 1] + 1.0f, 7.0f), 0.002f) << "swap " << k + 1;
    }
}

// La de Lorencia: la acción 1, de seis keys, a 0,28; el guardia salta 20, 19, 19 y 19 pasos.
TEST(AnimationTest, TheLorenciaJumpsAreThoseOfTheTrace)
{
    constexpr std::array<float, 5> Frames = {0.681f, 0.281f, 5.601f, 4.921f, 4.241f};
    constexpr std::array<int, 4> StepsBetween = {20, 19, 19, 19};
    const ActionInfo action = Repeating(6, 0.28f);
    AnimationState state{1, Frames[0] + 1.0f, 1, 0};

    for (std::size_t k = 0; k < StepsBetween.size(); ++k)
    {
        Steps(state, action, StepsBetween[k]);
        EXPECT_NEAR(state.frame, std::fmod(Frames[k + 1] + 1.0f, 6.0f), 0.002f) << "swap " << k + 1;
    }
}

// Tres segundos de frames de `frameTime`: el cuerpo, con el paso fijo de la CL3.
AnimationState BodyAfterThreeSeconds(Duration frameTime)
{
    const ActionInfo action = Repeating(7, 0.26f);
    AnimationState state{3, 0.0f, 3, 0};
    FixedStep step;
    for (Duration elapsed{}; elapsed < 3s; elapsed += frameTime)
    {
        Steps(state, action, step.Advance(frameTime));
    }
    return state;
}

// El cuerpo avanza por paso de 40 ms: las mismas keys por segundo a 21,33, 60 y 240 FPS.
TEST(AnimationTest, TheBodyRunsTheSameKeysPerSecondAtEveryFrameRate)
{
    const AnimationState original = BodyAfterThreeSeconds(OriginalFrameTime);

    for (const Duration frameTime : {Duration{16'666'667}, Duration{4'166'667}})
    {
        const AnimationState state = BodyAfterThreeSeconds(frameTime);
        EXPECT_FLOAT_EQ(state.frame, original.frame) << frameTime.count();
    }
    // 75 pasos de 0,26: 19,5 keys, módulo 7.
    EXPECT_NEAR(original.frame, 5.5f, 0.001f);
}

// Tres segundos de la mascota por tiempo, con frames de `frameTime`.
TimedAnimation PetAfterThreeSeconds(Duration frameTime)
{
    TimedAnimation pet;
    for (Duration elapsed{}; elapsed < 3s; elapsed += frameTime)
    {
        AdvanceByTime(pet, Repeating(7), PetKeysPerOriginalFrame, frameTime);
    }
    return pet;
}

// Q-C20 corregido: la mascota da 0,5 keys cada 46,875 ms, 10,67 por segundo, a cualquier FPS.
TEST(AnimationTest, ThePetRunsByTimeAtEveryFrameRate)
{
    for (const Duration frameTime : {OriginalFrameTime, Duration{16'666'667}, Duration{4'166'667}})
    {
        const TimedAnimation pet = PetAfterThreeSeconds(frameTime);
        // 64 frames del original: 32 keys, módulo 7.
        EXPECT_FLOAT_EQ(pet.state.frame, 4.0f) << frameTime.count();
    }
}

// Un tiempo negativo no hace retroceder nada.
TEST(AnimationTest, ANegativeTimeCountsZero)
{
    TimedAnimation pet;

    AdvanceByTime(pet, Repeating(7), PetKeysPerOriginalFrame, -OriginalFrameTime);
    AdvanceByTime(pet, Repeating(7), PetKeysPerOriginalFrame, OriginalFrameTime);

    EXPECT_FLOAT_EQ(pet.state.frame, 0.5f);
}

// Las alas, 0,25 por frame del original, y 1 con el personaje en la acción 0x1e o 0x1f (0x50ba30).
TEST(AnimationTest, TheWingsRunFasterInThirtyAndThirtyOne)
{
    EXPECT_EQ(WingKeysPerOriginalFrame(0x1e), 1.0f);
    EXPECT_EQ(WingKeysPerOriginalFrame(0x1f), 1.0f);
    EXPECT_EQ(WingKeysPerOriginalFrame(0x1d), 0.25f);
    EXPECT_EQ(WingKeysPerOriginalFrame(0x20), 0.25f);
    EXPECT_EQ(WingKeysPerOriginalFrame(0), 0.25f);
}

// La copia del render con alpha 1 es un paso, y guarda la key que cruza como el paso. El estado no
// se toca.
TEST(AnimationTest, TheRenderCopyCrossesKeysLikeTheStep)
{
    const AnimationState state{3, 1.9f, 3, 0};
    AnimationState stepped = state;
    (void)AdvanceAnimation(stepped, Repeating(7), 0.26f, Other);

    const AnimationState copy = Interpolated(state, Repeating(7), 0.26f, 1.0f);
    const AnimationState half = Interpolated(state, Repeating(7), 0.26f, 0.5f);

    EXPECT_FLOAT_EQ(copy.frame, stepped.frame);
    EXPECT_EQ(copy.previousKey, stepped.previousKey);
    EXPECT_EQ(copy.previousKey, 1);
    EXPECT_FLOAT_EQ(half.frame, 2.03f);
    EXPECT_EQ(state.frame, 1.9f);
}
} // namespace
