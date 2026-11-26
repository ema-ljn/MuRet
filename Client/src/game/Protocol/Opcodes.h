#pragma once

#include "Network/Packet.h"

// The GameServer's head codes, each named once: the cases of 0x4e2b10 (coverage/dispatcher-cases.tsv) and the codes
// the client only writes. A head code's subcodes are in the namespace of its name. dispatcher_cases.py checks it.
namespace Client::Protocol
{
inline constexpr Network::Opcode ChatOpcode{0x00};
inline constexpr Network::Opcode KeyChatOpcode{0x01};
inline constexpr Network::Opcode WhisperOpcode{0x02};
inline constexpr Network::Opcode MainCheckOpcode{0x03};
inline constexpr Network::Opcode ActorStateOpcode{0x07};
inline constexpr Network::Opcode EventStateOpcode{0x0b};
inline constexpr Network::Opcode WhisperResultOpcode{0x0c};
inline constexpr Network::Opcode NoticeOpcode{0x0d};
inline constexpr Network::Opcode KeepaliveOpcode{0x0e}; // client only
inline constexpr Network::Opcode WeatherOpcode{0x0f};
inline constexpr Network::Opcode SkillHitsOpcode{0x10}; // client only
inline constexpr Network::Opcode ViewportPlayersOpcode{0x12};
inline constexpr Network::Opcode ViewportMonstersOpcode{0x13};
inline constexpr Network::Opcode ViewportLeaveOpcode{0x14};
inline constexpr Network::Opcode KillExperienceOpcode{0x16};
inline constexpr Network::Opcode DeathOpcode{0x17};
inline constexpr Network::Opcode ActionOpcode{0x18};
inline constexpr Network::Opcode MagicAttackOpcode{0x19};
inline constexpr Network::Opcode MagicAttackCountOpcode{0x1a};
inline constexpr Network::Opcode MagicCancelOpcode{0x1b};
inline constexpr Network::Opcode TeleportOpcode{0x1c};
inline constexpr Network::Opcode DurationMagicOpcode{0x1e};
inline constexpr Network::Opcode ViewportSummonsOpcode{0x1f};
inline constexpr Network::Opcode FloorItemCreateOpcode{0x20};
inline constexpr Network::Opcode FloorItemDeleteOpcode{0x21};
inline constexpr Network::Opcode PickUpOpcode{0x22};
inline constexpr Network::Opcode DropOpcode{0x23};
inline constexpr Network::Opcode ItemMoveOpcode{0x24};
inline constexpr Network::Opcode EquipmentShownOpcode{0x25};
inline constexpr Network::Opcode LifeOpcode{0x26};
inline constexpr Network::Opcode ManaOpcode{0x27};
inline constexpr Network::Opcode ItemDeleteOpcode{0x28};
inline constexpr Network::Opcode Report29Opcode{0x29}; // 0x4e3bb8: MuRet does not port it.
inline constexpr Network::Opcode DurabilityOpcode{0x2a};
inline constexpr Network::Opcode StatFruitOpcode{0x2c};
inline constexpr Network::Opcode TalkOpcode{0x30};
inline constexpr Network::Opcode NpcItemListOpcode{0x31};
inline constexpr Network::Opcode BuyOpcode{0x32};
inline constexpr Network::Opcode SellOpcode{0x33};
inline constexpr Network::Opcode RepairOpcode{0x34};
inline constexpr Network::Opcode TradeRequestOpcode{0x36};
inline constexpr Network::Opcode TradeResponseOpcode{0x37};
inline constexpr Network::Opcode TradeItemRemovedOpcode{0x38};
inline constexpr Network::Opcode TradeItemAddedOpcode{0x39};
inline constexpr Network::Opcode TradeMoneyOpcode{0x3a};
inline constexpr Network::Opcode TradeOtherMoneyOpcode{0x3b};
inline constexpr Network::Opcode TradeOkButtonOpcode{0x3c};
inline constexpr Network::Opcode TradeResultOpcode{0x3d};
inline constexpr Network::Opcode PersonalShopOpcode{0x3f};
namespace PersonalShop
{
inline constexpr Network::Subcode TitlesSubcode{0x00};
inline constexpr Network::Subcode SetPriceSubcode{0x01};
inline constexpr Network::Subcode OpenSubcode{0x02};
inline constexpr Network::Subcode CloseSubcode{0x03};
inline constexpr Network::Subcode ItemListSubcode{0x05}; // 0x406b81 writes it; 0x4db890 reads the list
inline constexpr Network::Subcode BuySubcode{0x06};
inline constexpr Network::Subcode ViewClosedSubcode{0x07}; // 0x5eb17f
inline constexpr Network::Subcode NoticeSubcode{0x08};
inline constexpr Network::Subcode TitleSubcode{0x10};
inline constexpr Network::Subcode OwnerLeftSubcode{0x12};
inline constexpr Network::Subcode RefreshSubcode{0x13};
} // namespace PersonalShop
inline constexpr Network::Opcode PartyRequestOpcode{0x40};
inline constexpr Network::Opcode PartyRequestResultOpcode{0x41};
inline constexpr Network::Opcode PartyListOpcode{0x42};
inline constexpr Network::Opcode PartyLeaveOpcode{0x43};
inline constexpr Network::Opcode PartyLifeOpcode{0x44};
inline constexpr Network::Opcode TransformedOpcode{0x45};
inline constexpr Network::Opcode MapAttributesOpcode{0x46};
inline constexpr Network::Opcode ItemAcquiredOpcode{0x47};
inline constexpr Network::Opcode ActorEffectOpcode{0x48};
inline constexpr Network::Opcode GuildJoinRequestOpcode{0x50};
inline constexpr Network::Opcode GuildJoinResultOpcode{0x51};
inline constexpr Network::Opcode GuildMemberListOpcode{0x52};
inline constexpr Network::Opcode GuildLeaveResultOpcode{0x53};
inline constexpr Network::Opcode GuildMasterQuestionOpcode{0x54};
inline constexpr Network::Opcode GuildCreateOpcode{0x55}; // client only
inline constexpr Network::Opcode GuildCreateResultOpcode{0x56};
inline constexpr Network::Opcode ViewportGuildDeleteOpcode{0x5d};
inline constexpr Network::Opcode GuildWarRequestResultOpcode{0x60};
inline constexpr Network::Opcode GuildWarRequestOpcode{0x61};
inline constexpr Network::Opcode GuildWarStartOpcode{0x62};
inline constexpr Network::Opcode GuildWarEndOpcode{0x63};
inline constexpr Network::Opcode GuildWarScoreOpcode{0x64};
inline constexpr Network::Opcode ViewportGuildOpcode{0x65};
inline constexpr Network::Opcode GuildInfoOpcode{0x66};
inline constexpr Network::Opcode ViewportUnionOpcode{0x67};
inline constexpr Network::Opcode ViewportObjectsOpcode{0x68};
inline constexpr Network::Opcode SkillTargetOpcode{0x69};
inline constexpr Network::Opcode PingOpcode{0x71}; // 0x4e465b: MuRet does not port it.
inline constexpr Network::Opcode Report73Opcode{0x73};
inline constexpr Network::Opcode WarehouseMoneyOpcode{0x81};
inline constexpr Network::Opcode WarehouseCloseOpcode{0x82};
inline constexpr Network::Opcode WarehouseStateOpcode{0x83};
inline constexpr Network::Opcode ChaosMixOpcode{0x86};
inline constexpr Network::Opcode ChaosBoxCloseOpcode{0x87};
inline constexpr Network::Opcode DevilSquareEntryOpcode{0x90};
inline constexpr Network::Opcode EventOpenTimeOpcode{0x91};
inline constexpr Network::Opcode EventCountdownOpcode{0x92};
inline constexpr Network::Opcode EventResultOpcode{0x93};
inline constexpr Network::Opcode EventChipInfoOpcode{0x94};
inline constexpr Network::Opcode EventChipCountOpcode{0x95};
inline constexpr Network::Opcode MutoNumbersOpcode{0x96};
inline constexpr Network::Opcode EventWindowLeaveOpcode{0x97};  // client only
inline constexpr Network::Opcode MutoNumberRequestOpcode{0x98}; // client only
inline constexpr Network::Opcode EventDialogOpcode{0x99};
inline constexpr Network::Opcode BloodCastleEntryOpcode{0x9a};
inline constexpr Network::Opcode EventPlayStateOpcode{0x9b};
inline constexpr Network::Opcode ExtendedKillExperienceOpcode{0x9c};
inline constexpr Network::Opcode SerialResultOpcode{0x9d};
inline constexpr Network::Opcode EventSoundOpcode{0x9e};
inline constexpr Network::Opcode QuestFlagOpcode{0x9f};
inline constexpr Network::Opcode QuestListOpcode{0xa0};
inline constexpr Network::Opcode QuestStateOpcode{0xa1};
inline constexpr Network::Opcode SetQuestOpcode{0xa2};
inline constexpr Network::Opcode QuestResultOpcode{0xa3};
inline constexpr Network::Opcode PetOrderOpcode{0xa7};
inline constexpr Network::Opcode PetAttackOpcode{0xa8};
inline constexpr Network::Opcode PetInfoOpcode{0xa9};
inline constexpr Network::Opcode DuelStartOpcode{0xaa};
inline constexpr Network::Opcode DuelEndOpcode{0xab};
inline constexpr Network::Opcode DuelRequestOpcode{0xac};
inline constexpr Network::Opcode DuelScoreOpcode{0xad};
inline constexpr Network::Opcode ChaosCastleOpcode{0xaf};
namespace ChaosCastle
{
inline constexpr Network::Subcode EntrySubcode{0x01}; // 0x4e5032: the dispatcher reads the AF only with this subcode
inline constexpr Network::Subcode RepositionSubcode{0x02}; // 0x4fbee8
} // namespace ChaosCastle
inline constexpr Network::Opcode TargetTeleportOpcode{0xb0}; // client only
inline constexpr Network::Opcode MapServerOpcode{0xb1};
namespace MapServer
{
inline constexpr Network::Subcode MoveSubcode{0x00}; // 0x4e506a: the dispatcher reads the B1 only with this subcode
inline constexpr Network::Subcode AuthSubcode{0x01}; // 0x412c5f
} // namespace MapServer
inline constexpr Network::Opcode CastleOpcode{0xb2};
// The B2 subcodes are the same both ways.
namespace Castle
{
inline constexpr Network::Subcode StateSubcode{0x00};        // 0x4e50bd
inline constexpr Network::Subcode RegisterSubcode{0x01};     // 0x4df080
inline constexpr Network::Subcode GiveUpSubcode{0x02};       // 0x4df1a0
inline constexpr Network::Subcode RegistrationSubcode{0x03}; // 0x4e5186
inline constexpr Network::Subcode RegisterMarkSubcode{0x04}; // 0x4e51fa
inline constexpr Network::Subcode NpcBuySubcode{0x05};       // 0x4df7d0
inline constexpr Network::Subcode NpcRepairSubcode{0x06};    // 0x4e52b7
inline constexpr Network::Subcode NpcUpgradeSubcode{0x07};   // 0x4df950
inline constexpr Network::Subcode TaxInfoSubcode{0x08};      // 0x4e537b
inline constexpr Network::Subcode TaxRateSubcode{0x09};      // 0x4e53e7
inline constexpr Network::Subcode MoneyOutSubcode{0x10};     // 0x4e548a
inline constexpr Network::Subcode SiegeFlagsSubcode{0x1a};   // 0x4e54f6
inline constexpr Network::Subcode CommandSubcode{0x1d};
inline constexpr Network::Subcode HuntZoneGateSubcode{0x1f}; // 0x4e5532
inline constexpr Network::Subcode SwitchHolderSubcode{0x20}; // 0x4e572d
} // namespace Castle
inline constexpr Network::Opcode CastleNpcListOpcode{0xb3};
inline constexpr Network::Opcode SiegeGuildListOpcode{0xb4};
inline constexpr Network::Opcode RegisteredGuildListOpcode{0xb5};
inline constexpr Network::Opcode SiegeMinimapOpcode{0xb6};
inline constexpr Network::Opcode SiegeWeaponOpcode{0xb7};
namespace SiegeWeapon
{
inline constexpr Network::Subcode UseSubcode{0x01};
} // namespace SiegeWeapon
inline constexpr Network::Opcode KillCountOpcode{0xb8};
namespace KillCount
{
inline constexpr Network::Subcode CountSubcode{0x01}; // 0x4e5894: the dispatcher reads the B8 only with this subcode
} // namespace KillCount
inline constexpr Network::Opcode CastleInfoOpcode{0xb9};
namespace CastleInfo
{
inline constexpr Network::Subcode NpcLevelSubcode{0x01};      // 0x4e0750
inline constexpr Network::Subcode OwnerMarkSubcode{0x02};     // 0x4e0790
inline constexpr Network::Subcode HuntZoneInfoSubcode{0x03};  // 0x4e07f0
inline constexpr Network::Subcode HuntZoneEntrySubcode{0x05}; // 0x4e0860
} // namespace CastleInfo
inline constexpr Network::Opcode ActorFieldOpcode{0xba};
inline constexpr Network::Opcode SiegeMinimapMembersOpcode{0xbb};
inline constexpr Network::Opcode JewelMixOpcode{0xbc};
namespace JewelMix
{
inline constexpr Network::Subcode MixSubcode{0x00};
inline constexpr Network::Subcode UnmixSubcode{0x01};
} // namespace JewelMix
inline constexpr Network::Opcode CrywolfOpcode{0xbd};
// 0x4e620c, the BD's table by pkt[3]: 01, 06, 0a and 0b go to the default (0x4e5b4a).
namespace Crywolf
{
inline constexpr Network::Subcode StateSubcode{0x00};
inline constexpr Network::Subcode AltarsSubcode{0x02};
inline constexpr Network::Subcode ContractSubcode{0x03};
inline constexpr Network::Subcode TimerSubcode{0x04};
inline constexpr Network::Subcode BossSubcode{0x05};
inline constexpr Network::Subcode RankSubcode{0x07};
inline constexpr Network::Subcode RankingSubcode{0x08};
inline constexpr Network::Subcode ChaosBoxSubcode{0x09}; // 0x65a986..0x65a9d5 writes it
inline constexpr Network::Subcode EffectSubcode{0x0c};
} // namespace Crywolf
inline constexpr Network::Opcode FriendListOpcode{0xc0};
inline constexpr Network::Opcode FriendAddOpcode{0xc1};
inline constexpr Network::Opcode FriendRequestOpcode{0xc2};
inline constexpr Network::Opcode FriendDeleteOpcode{0xc3};
inline constexpr Network::Opcode FriendStateOpcode{0xc4};
inline constexpr Network::Opcode LetterSendOpcode{0xc5};
inline constexpr Network::Opcode LetterListOpcode{0xc6};
inline constexpr Network::Opcode LetterReadOpcode{0xc7};
inline constexpr Network::Opcode LetterDeleteOpcode{0xc8};
inline constexpr Network::Opcode LetterListRequestOpcode{0xc9}; // client only
inline constexpr Network::Opcode RoomRequestOpcode{0xca};
inline constexpr Network::Opcode RoomInviteOpcode{0xcb};
inline constexpr Network::Opcode NoticeAnswerOpcode{0xd0}; // client only
namespace NoticeAnswer
{
inline constexpr Network::Subcode YesSubcode{0x00};
} // namespace NoticeAnswer
inline constexpr Network::Opcode WalkOpcode{0xd3};
inline constexpr Network::Opcode AttackOpcode{0xd7};
inline constexpr Network::Opcode PositionOpcode{0xdf};
inline constexpr Network::Opcode GuildAssignResultOpcode{0xe1};
inline constexpr Network::Opcode RelationshipRequestOpcode{0xe5};
inline constexpr Network::Opcode RelationshipResultOpcode{0xe6};
inline constexpr Network::Opcode UnionListOpcode{0xe9};
inline constexpr Network::Opcode UnionKickOpcode{0xeb}; // 0x650830 writes it; 0x4e4486 reads the answer
inline constexpr Network::Opcode JoinOpcode{0xf1};
namespace Join
{
inline constexpr Network::Subcode HelloSubcode{0x00};
inline constexpr Network::Subcode LoginSubcode{0x01};
inline constexpr Network::Subcode LogoutSubcode{0x02};
inline constexpr Network::Subcode DecryptErrorSubcode{0x03};
} // namespace Join
inline constexpr Network::Opcode CharacterOpcode{0xf3};
namespace Character
{
inline constexpr Network::Subcode ListSubcode{0x00}; // 0x4c20a0
inline constexpr Network::Subcode CreateSubcode{0x01};
inline constexpr Network::Subcode DeleteSubcode{0x02};
inline constexpr Network::Subcode SelectSubcode{0x03}; // 0x4c3940
inline constexpr Network::Subcode RegenSubcode{0x04};
inline constexpr Network::Subcode LevelUpSubcode{0x05};      // 0x4e30b9
inline constexpr Network::Subcode LevelUpPointSubcode{0x06}; // 0x5e468e
inline constexpr Network::Subcode PkLevelSubcode{0x08};      // 0x4d3b50
inline constexpr Network::Subcode InventoryListSubcode{0x10};
inline constexpr Network::Subcode SkillListSubcode{0x11};
inline constexpr Network::Subcode TeleportDoneSubcode{0x12};
inline constexpr Network::Subcode EquipmentChangeSubcode{0x13};
inline constexpr Network::Subcode ItemPutSubcode{0x14};
inline constexpr Network::Subcode SummonLifeSubcode{0x20};
inline constexpr Network::Subcode EventTimeSubcode{0x22};
inline constexpr Network::Subcode SoccerScoreSubcode{0x23};
inline constexpr Network::Subcode GuildWarOutcomeSubcode{0x24};
inline constexpr Network::Subcode OptionDataSubcode{0x30};
inline constexpr Network::Subcode ServerCommandSubcode{0x40};
} // namespace Character
} // namespace Client::Protocol
