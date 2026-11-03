#include "Core/IntervalTimer.h"

#include "Core/IClock.h"

#include <gtest/gtest.h>

#include <chrono>

// Client::Core::IntervalTimer: algo que pasa cada tanto de reloj, sin deriva y sin ponerse al día
// (el plan de la CL4, CL4.5, paso 2). Los instantes son del reloj del bucle, en Duration.
namespace
{
using Client::Core::Duration;
using Client::Core::IntervalTimer;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr Duration Period = seconds{20};

// Empezado en 0: vence a los 20 s y no antes, una vez, y el siguiente a los 40 s, aunque el de los
// 20 s se haya visto 16 ms tarde. Sin deriva: los 16 ms no se suman.
TEST(IntervalTimerTest, FiresEveryPeriod)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_FALSE(timer.Due(milliseconds{19999}));
    EXPECT_TRUE(timer.Due(milliseconds{20016}));
    EXPECT_FALSE(timer.Due(milliseconds{20016}));
    EXPECT_FALSE(timer.Due(milliseconds{39999}));
    EXPECT_TRUE(timer.Due(seconds{40}));
    EXPECT_FALSE(timer.Due(seconds{40}));
}

// Mirado a los 65 s, con dos periodos de atraso: vence una vez, no tres, y el siguiente queda a un
// periodo de los 65 s, no en los 80 s que tocaban.
TEST(IntervalTimerTest, ALongPauseFiresOnce)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_TRUE(timer.Due(seconds{65}));
    EXPECT_FALSE(timer.Due(seconds{65}));
    EXPECT_FALSE(timer.Due(seconds{80}));
    EXPECT_FALSE(timer.Due(milliseconds{84999}));
    EXPECT_TRUE(timer.Due(seconds{85}));
}

// Mirado justo un periodo tarde, a los 40 s: vence una vez, y el siguiente es a los 60 s.
TEST(IntervalTimerTest, ExactlyAPeriodLateFiresOnce)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_TRUE(timer.Due(seconds{40}));
    EXPECT_FALSE(timer.Due(seconds{40}));
    EXPECT_FALSE(timer.Due(milliseconds{59999}));
    EXPECT_TRUE(timer.Due(seconds{60}));
}

// Con menos de un periodo de atraso, el siguiente sigue en la grilla: mirado a los 39,999 s, el
// siguiente es a los 40 s y no a los 59,999 s.
TEST(IntervalTimerTest, LessThanAPeriodLateKeepsTheGrid)
{
    IntervalTimer timer(Period);
    timer.Start(Duration::zero());

    EXPECT_TRUE(timer.Due(milliseconds{39999}));
    EXPECT_TRUE(timer.Due(seconds{40}));
}

// El reloj va para atrás: no vence, y el vencimiento no se mueve.
TEST(IntervalTimerTest, AClockThatGoesBackDoesNotFire)
{
    IntervalTimer timer(Period);
    timer.Start(seconds{10});

    EXPECT_FALSE(timer.Due(seconds{5}));
    EXPECT_FALSE(timer.Due(milliseconds{29999}));
    EXPECT_TRUE(timer.Due(seconds{30}));
}

// Sin Start no corre, y detenido no vence. Start vuelve a empezar desde el instante que recibe.
TEST(IntervalTimerTest, StoppedItDoesNotFire)
{
    IntervalTimer timer(Period);
    EXPECT_FALSE(timer.Running());
    EXPECT_FALSE(timer.Due(seconds{100}));

    timer.Start(Duration::zero());
    EXPECT_TRUE(timer.Running());
    timer.Stop();
    EXPECT_FALSE(timer.Running());
    EXPECT_FALSE(timer.Due(seconds{100}));

    timer.Start(seconds{100});
    EXPECT_FALSE(timer.Due(milliseconds{119999}));
    EXPECT_TRUE(timer.Due(seconds{120}));
    EXPECT_TRUE(timer.Running());
}
} // namespace
