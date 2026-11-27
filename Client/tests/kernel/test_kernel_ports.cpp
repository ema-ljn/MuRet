#include "Kernel/IOnScreenMessages.h"
#include "Kernel/IPacketSender.h"
#include "Kernel/ISoundRequests.h"

#include "Protocol/Movement/Movement.h"
#include "Audio/SoundId.h"

#include "support/FakeKernelPorts.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace
{
using Client::Tests::FakeOnScreenMessages;
using Client::Tests::FakePacketSender;
using Client::Tests::FakeSoundRequests;
using Client::Tests::OnScreenMessage;
using Client::Tests::SoundRequest;

TEST(KernelPortsTest, AFakePacketSenderKeepsEachPacketInOrder)
{
    FakePacketSender fake;
    const Client::Kernel::IPacketSender& send = fake;

    send.Send(Client::Protocol::Movement::WriteAction(3, 0x7a));
    send.Send(Client::Protocol::Movement::WriteAction(5, 0x80));

    ASSERT_EQ(fake.Sent().size(), std::size_t{2});
    EXPECT_TRUE(Client::Tests::SameBytes(fake.Sent()[0].packet.Bytes(),
                                         Client::Protocol::Movement::WriteAction(3, 0x7a).packet.Bytes()));
    EXPECT_TRUE(Client::Tests::SameBytes(fake.Sent()[1].packet.Bytes(),
                                         Client::Protocol::Movement::WriteAction(5, 0x80).packet.Bytes()));
}

TEST(KernelPortsTest, AFakeSoundRequestsKeepsTheIdTheOriginAndTheLoop)
{
    FakeSoundRequests fake;
    Client::Kernel::ISoundRequests& sounds = fake;

    sounds.Request(Client::Audio::SoundId{0x19}, std::nullopt, false);
    sounds.Request(Client::Audio::SoundId{0x8b}, glm::vec3(1.0f, 2.0f, 3.0f), true);

    EXPECT_EQ(fake.Requests(),
              (std::vector<SoundRequest>{
                  {.id = Client::Audio::SoundId{0x19}, .origin = std::nullopt, .loop = false},
                  {.id = Client::Audio::SoundId{0x8b}, .origin = glm::vec3(1.0f, 2.0f, 3.0f), .loop = true}}));
}

TEST(KernelPortsTest, AFakeOnScreenMessagesKeepsEachMessageAndAnswersWithItsFailure)
{
    FakeOnScreenMessages fake;
    Client::Kernel::IOnScreenMessages& messages = fake;

    const auto added = messages.Add("Elfa", "hola", 1);
    fake.FailWith("sin el medidor");
    const auto failed = messages.Add("", "aviso", 3);

    EXPECT_TRUE(added.HasValue());
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(failed.Error(), "sin el medidor");
    EXPECT_EQ(fake.Messages(), (std::vector<OnScreenMessage>{{.name = "Elfa", .text = "hola", .type = 1},
                                                             {.name = "", .text = "aviso", .type = 3}}));
}
} // namespace
