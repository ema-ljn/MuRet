#include "Scenes/CameraTour.h"

#include "Scenes/EventGlobals.h"
#include "Scenes/WorldRuntime.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// CL9 hito 11, fase 3 (06 § El recorrido de la cámara): el arranque de CCameraMove contra el ASM de c32bbd5f
// (0x403590, 0x4033f0, 0x4033d0, 0x404c90, 0x403300 y 0x403d60). Lo empieza solo el BD 00 con la etapa 2; el guion
// (0x4035f0) corta donde empieza. El mapa no cuenta: Lorencia, Devias y Crywolf dan lo mismo.
namespace
{
using Client::Scenes::CameraTour;
using Client::Scenes::WorldRuntime;

// Con la etapa 2, 0x404c90(5000) arranca su reloj en el primer paso y vence a los 5.000 ms (con signo, <=): la espera
// se apaga, 0x4033d0 da verdadero y el guion corta en 0x4035da.
TEST(CameraTourTest, TheSecondStageStartsTheTourFiveSecondsLater)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        WorldRuntime runtime;
        runtime.map = map;
        CameraTour& tour = runtime.events.cameraTour;
        tour.stage = 2;
        ASSERT_TRUE(Client::Scenes::StepTheCameraTour(runtime, 1000).HasValue());
        EXPECT_EQ(tour.timerStart, 1000u);
        EXPECT_TRUE(tour.waiting);
        ASSERT_TRUE(Client::Scenes::StepTheCameraTour(runtime, 5999).HasValue());
        EXPECT_TRUE(tour.waiting);
        EXPECT_FALSE(Client::Scenes::CameraTourRunning(tour));
        const auto started = Client::Scenes::StepTheCameraTour(runtime, 6000);
        ASSERT_FALSE(started.HasValue());
        EXPECT_NE(started.Error().find("0x4035da"), std::string::npos) << started.Error();
        EXPECT_FALSE(tour.waiting);
        EXPECT_EQ(tour.timerCount, 1);
        EXPECT_TRUE(tour.timerRestart);
        EXPECT_TRUE(Client::Scenes::CameraTourRunning(tour));
        // Esc (0x623edc) lo da por cortado: 0x4033d0 vuelve a falso y el paso no hace nada.
        tour.skipped = true;
        EXPECT_TRUE(Client::Scenes::StepTheCameraTour(runtime, 7000).HasValue());
    }
}

// Otras etapas no lo empiezan: el reloj no corre y 0x4033d0 pide la 2. La 3 lo deshace con 0x403d60 y 0x403300, que
// vuelve todo a sus valores (+0x14c en 10000, la espera y el reloj en 1); con personajes del guion, 0x403d60 corta.
TEST(CameraTourTest, OtherStagesDoNotStartItAndTheThirdUndoesIt)
{
    WorldRuntime runtime;
    CameraTour& tour = runtime.events.cameraTour;
    for (const std::int32_t stage : {0, 1, 4, 5, 6})
    {
        SCOPED_TRACE(stage);
        tour = CameraTour{};
        tour.stage = stage;
        for (const std::uint32_t now : {1000u, 10000u, 100000u})
            ASSERT_TRUE(Client::Scenes::StepTheCameraTour(runtime, now).HasValue());
        EXPECT_TRUE(tour.waiting);
        EXPECT_TRUE(tour.timerRestart);
        EXPECT_EQ(tour.timerCount, 0);
    }
    tour = CameraTour{
        .stage = 3, .waiting = false, .skipped = true, .timerRestart = false, .timerStart = 5, .timerCount = 2};
    ASSERT_TRUE(Client::Scenes::StepTheCameraTour(runtime, 1000).HasValue());
    EXPECT_EQ(tour.stage, 0);
    EXPECT_TRUE(tour.waiting);
    EXPECT_FALSE(tour.skipped);
    EXPECT_TRUE(tour.timerRestart);
    EXPECT_EQ(tour.timerStart, 0u);
    EXPECT_EQ(tour.timerCount, 0);
    tour.stage = 3;
    tour.nextActorKey = 10002;
    const auto cut = Client::Scenes::StepTheCameraTour(runtime, 2000);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x403d60"), std::string::npos) << cut.Error();
}
} // namespace
