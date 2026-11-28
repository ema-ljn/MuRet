#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Tables/NpcNames.h"
#include "Tables/TextTable.h"
#include "Render/ModelBatch.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Assets/GameModels.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/LoginModels.h"
#include "Scenes/NpcNames.h"
#include "Scenes/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/SelectionState.h"
#include "Scenes/UiItemDrawing.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/Widget.h"
#include "World/IFishStepServices.h"
#include "World/Sprites.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El tooltip de cada ítem de las tiendas de NPC del servidor del laboratorio (Muserver 1.2i 2020, data/shop0.txt a
// shop17.txt, con sus columnas: tipo, índice, nivel, durabilidad, skill, suerte y opción), con el Data\ del repo. El 31
// los pone en la grilla de la tienda (PlaceInTheNpcGrid, 0x5bc150) y el mouse sobre su casilla los dibuja con la tienda
// abierta (DrawTheTooltips, 0x5f7bc0): desde la grilla, con el precio de compra (0x78aba27 en 1), y ya comprados, desde
// el inventario, con el de venta. Ninguno puede cortar: jugando, un corte cierra el cliente.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldEntryResources;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::size_t HeroSlot = 7;
constexpr std::uint8_t Knight = 1;
constexpr std::int32_t LorenciaTax = 3;

struct ShopItem
{
    std::uint8_t shop = 0;
    std::uint8_t group = 0;
    std::uint8_t index = 0;
    std::uint8_t level = 0;
    std::uint8_t durability = 0;
    std::uint8_t skill = 0;
    std::uint8_t luck = 0;
    std::uint8_t option = 0;
    std::string_view name;
};

// Generada de los shopN.txt del servidor, en su orden.
constexpr auto ShopItems = std::to_array<ShopItem>({
    {0, 7, 5, 13, 255, 0, 1, 7, "Leather Helm"},
    {0, 7, 0, 13, 255, 0, 1, 7, "Bronze Helm"},
    {0, 7, 6, 13, 255, 0, 1, 7, "Scale Helm"},
    {0, 7, 8, 13, 255, 0, 1, 7, "Brass Helm"},
    {0, 8, 5, 13, 255, 0, 1, 7, "Leather Armor"},
    {0, 8, 0, 13, 255, 0, 1, 7, "Bronze Armor"},
    {0, 8, 6, 13, 255, 0, 1, 7, "Scale Armor"},
    {0, 8, 8, 13, 255, 0, 1, 7, "Brass Armor"},
    {0, 10, 0, 13, 255, 0, 1, 7, "Bronze Gloves"},
    {0, 10, 6, 13, 255, 0, 1, 7, "Scale Gloves"},
    {0, 10, 8, 13, 255, 0, 1, 7, "Brass Gloves"},
    {0, 10, 5, 13, 255, 0, 1, 7, "Leather Gloves"},
    {0, 9, 0, 13, 255, 0, 1, 7, "Bronze Pants"},
    {0, 9, 6, 13, 255, 0, 1, 7, "Scale Pants"},
    {0, 9, 8, 13, 255, 0, 1, 7, "Brass Pants"},
    {0, 9, 5, 13, 255, 0, 1, 7, "Leather Pants"},
    {0, 11, 0, 13, 255, 0, 1, 7, "Bronze Boots"},
    {0, 11, 6, 13, 255, 0, 1, 7, "Scale Boots"},
    {0, 11, 8, 13, 255, 0, 1, 7, "Brass Boots"},
    {0, 11, 5, 13, 255, 0, 1, 7, "Leather Boots"},
    {0, 12, 7, 0, 255, 0, 0, 0, "Twisting Splash"},
    {0, 12, 12, 0, 255, 0, 0, 0, "Rageful Blow"},
    {0, 12, 19, 0, 255, 0, 0, 0, "Death Stab"},
    {0, 12, 14, 0, 255, 0, 0, 0, "Greater Fortitude"},
    {0, 12, 13, 0, 255, 0, 0, 0, "Impale"},
    {1, 14, 3, 13, 255, 0, 0, 0, "Large Healing Potion"},
    {1, 14, 6, 13, 255, 0, 0, 0, "Large Mana Potion"},
    {1, 14, 20, 0, 255, 0, 0, 0, "Remedy of Love"},
    {1, 13, 0, 0, 255, 0, 0, 0, "Guardian Angel"},
    {1, 13, 1, 0, 255, 0, 0, 0, "Imp"},
    {1, 13, 2, 0, 255, 0, 0, 0, "Horn of Uniria"},
    {1, 13, 3, 0, 255, 0, 0, 0, "Horn of Dinorant"},
    {1, 13, 4, 0, 255, 0, 0, 0, "Dark Horse"},
    {1, 13, 5, 0, 255, 0, 0, 0, "Dark Raven"},
    {1, 12, 15, 0, 255, 0, 0, 0, "Jewel of Chaos"},
    {1, 14, 16, 0, 255, 0, 0, 0, "Jewel of Life"},
    {1, 14, 13, 0, 255, 0, 0, 0, "Jewel of Bless"},
    {1, 14, 14, 0, 255, 0, 0, 0, "Jewel of Soul"},
    {1, 13, 8, 13, 255, 0, 0, 0, "Ring of Ice"},
    {1, 13, 9, 13, 255, 0, 0, 0, "Ring of Poison"},
    {1, 13, 21, 13, 255, 0, 0, 0, "Ring of Fire"},
    {1, 13, 22, 13, 255, 0, 0, 0, "Ring of Earth"},
    {1, 13, 23, 13, 255, 0, 0, 0, "Ring of Wind"},
    {1, 13, 24, 13, 255, 0, 0, 0, "Ring of Magic"},
    {1, 13, 12, 13, 255, 0, 0, 0, "Pendant of Lightning"},
    {1, 13, 13, 13, 255, 0, 0, 0, "Pendant of Fire"},
    {1, 13, 25, 13, 255, 0, 0, 0, "Pendant of Ice"},
    {1, 13, 26, 13, 255, 0, 0, 0, "Pendant of Wind"},
    {1, 13, 27, 13, 255, 0, 0, 0, "Pendant of Water"},
    {1, 13, 28, 13, 255, 0, 0, 0, "Pendant of Ability"},
    {1, 13, 10, 0, 255, 0, 0, 0, "Budge Dragon Transformation Ring"},
    {1, 14, 23, 0, 255, 0, 0, 0, "Scroll of Emperor"},
    {1, 13, 10, 1, 255, 0, 0, 0, "Giant Transformation Ring"},
    {1, 13, 10, 2, 255, 0, 0, 0, "Skeleton Transformation Ring"},
    {1, 13, 10, 3, 255, 0, 0, 0, "Thunder Lich Transformation Ring"},
    {1, 13, 10, 4, 255, 0, 0, 0, "Poison Bull Transformation Ring"},
    {1, 13, 10, 5, 255, 0, 0, 0, "Death Cow Transformation Ring"},
    {1, 13, 20, 0, 255, 0, 0, 0, "Wizards Ring"},
    {1, 14, 25, 0, 255, 0, 0, 0, "Tear Of Elf"},
    {1, 14, 26, 0, 255, 0, 0, 0, "Soul of Wizard"},
    {1, 14, 24, 0, 255, 0, 0, 0, "Broken Sword"},
    {1, 14, 9, 13, 255, 0, 0, 0, "Alcohol"},
    {1, 13, 29, 0, 255, 0, 0, 0, "Armor of Guardman"},
    {1, 13, 18, 1, 255, 0, 0, 0, "Invisiblity Cloak +1"},
    {1, 13, 18, 2, 255, 0, 0, 0, "Invisiblity Cloak +2"},
    {1, 13, 18, 3, 255, 0, 0, 0, "Invisiblity Cloak +3"},
    {1, 13, 18, 4, 255, 0, 0, 0, "Invisiblity Cloak +4"},
    {1, 13, 18, 5, 255, 0, 0, 0, "Invisiblity Cloak +5"},
    {1, 13, 18, 6, 255, 0, 0, 0, "Invisiblity Cloak +6"},
    {1, 14, 19, 1, 255, 0, 0, 0, "Devil Invitation+1"},
    {1, 14, 19, 2, 255, 0, 0, 0, "Devil Invitation+2"},
    {1, 14, 19, 3, 255, 0, 0, 0, "Devil Invitation+3"},
    {1, 14, 19, 4, 255, 0, 0, 0, "Devil Invitation+4"},
    {1, 14, 19, 5, 255, 0, 0, 0, "Devil Invitation+5"},
    {1, 14, 19, 6, 255, 0, 0, 0, "Devil Invitation+6"},
    {1, 14, 8, 13, 255, 0, 0, 0, "Antidote"},
    {1, 14, 0, 13, 255, 0, 0, 0, "Apple"},
    {2, 15, 3, 0, 0, 0, 0, 0, "Fireball Spell"},
    {2, 15, 10, 0, 0, 0, 0, 0, "Power Wave Spell"},
    {2, 15, 2, 0, 0, 0, 0, 0, "Lightning Spell"},
    {2, 15, 5, 0, 0, 0, 0, 0, "Teleport Spell"},
    {2, 15, 1, 0, 0, 0, 0, 0, "Meteorite Spell"},
    {2, 15, 6, 0, 0, 0, 0, 0, "Ice Spell"},
    {2, 15, 0, 0, 0, 0, 0, 0, "Poison Spell"},
    {2, 15, 4, 0, 0, 0, 0, 0, "Flame Spell"},
    {2, 15, 7, 0, 0, 0, 0, 0, "Twister Spell"},
    {2, 15, 8, 0, 0, 0, 0, 0, "Evil Spirits Spell"},
    {2, 15, 9, 0, 0, 0, 0, 0, "Hell Fire Spell"},
    {2, 15, 11, 0, 0, 0, 0, 0, "Aqua Beam Spell"},
    {2, 15, 12, 0, 0, 0, 0, 0, "Blast Spell"},
    {2, 15, 13, 0, 0, 0, 0, 0, "Inferno Spell"},
    {2, 15, 14, 0, 0, 0, 0, 0, "Mass Teleport Spell"},
    {2, 15, 15, 0, 0, 0, 0, 0, "Mana Shield Spell"},
    {2, 7, 2, 13, 255, 0, 1, 7, "Pad Helm"},
    {2, 7, 4, 13, 255, 0, 1, 7, "Bone Helm"},
    {2, 7, 3, 13, 255, 0, 1, 7, "Legendary Helm"},
    {2, 7, 22, 13, 255, 0, 1, 7, "Dark Soul Helm"},
    {2, 8, 2, 13, 255, 0, 1, 7, "Pad Armor"},
    {2, 8, 4, 13, 255, 0, 1, 7, "Bone Armor"},
    {2, 8, 3, 13, 255, 0, 1, 7, "Legendary Armor"},
    {2, 8, 22, 13, 255, 0, 1, 7, "Dark Soul Armor"},
    {2, 9, 2, 13, 255, 0, 1, 7, "Pad Pants"},
    {2, 9, 4, 13, 255, 0, 1, 7, "Bone Pants"},
    {2, 9, 3, 13, 255, 0, 1, 7, "Legendary Pants"},
    {2, 9, 22, 13, 255, 0, 1, 7, "Dark Soul Pants"},
    {2, 10, 2, 13, 255, 0, 1, 7, "Pad Gloves"},
    {2, 10, 4, 13, 255, 0, 1, 7, "Bone Gloves"},
    {2, 10, 3, 13, 255, 0, 1, 7, "Legendary Gloves"},
    {2, 10, 22, 13, 255, 0, 1, 7, "Dark Soul Gloves"},
    {2, 11, 2, 13, 255, 0, 1, 7, "Pad Boots"},
    {2, 11, 4, 13, 255, 0, 1, 7, "Bone Boots"},
    {2, 11, 3, 13, 255, 0, 1, 7, "Legendary Boots"},
    {2, 11, 22, 13, 255, 0, 1, 7, "Dark Soul Boots"},
    {3, 0, 1, 13, 255, 1, 1, 7, "Kris"},
    {3, 0, 0, 13, 255, 1, 1, 7, "Short Sword"},
    {3, 0, 2, 13, 255, 1, 1, 7, "Rapier"},
    {3, 0, 4, 13, 255, 1, 1, 7, "Sword Of Assassin"},
    {3, 0, 3, 13, 255, 1, 1, 7, "Katana"},
    {3, 0, 6, 13, 255, 1, 1, 7, "Gladius"},
    {3, 0, 7, 13, 255, 1, 1, 7, "Falchion"},
    {3, 0, 8, 13, 255, 1, 1, 7, "Serpant Sword"},
    {3, 0, 5, 13, 255, 1, 1, 7, "Blade"},
    {3, 0, 13, 13, 255, 1, 1, 7, "Double Blade"},
    {3, 0, 14, 13, 255, 1, 1, 7, "Lightning Sword"},
    {3, 0, 16, 13, 255, 1, 1, 7, "Sword of Destruction"},
    {3, 0, 19, 13, 255, 1, 1, 7, "Divine Sword of Archangel"},
    {3, 0, 20, 13, 255, 1, 1, 7, "Knight Blade"},
    {3, 0, 17, 13, 255, 1, 1, 7, "Dark Breaker"},
    {3, 5, 0, 13, 255, 0, 1, 7, "Skull Staff"},
    {3, 5, 1, 13, 255, 0, 1, 7, "Angelic Staff"},
    {3, 5, 2, 13, 255, 0, 1, 7, "Serpent Staff"},
    {3, 5, 3, 13, 255, 0, 1, 7, "Lightning Staff"},
    {3, 5, 4, 13, 255, 0, 1, 7, "Gorgon Staff"},
    {3, 5, 5, 13, 255, 0, 1, 7, "Legendary Staff"},
    {3, 5, 6, 13, 255, 0, 1, 7, "Resurrection Staff"},
    {3, 5, 9, 13, 255, 0, 1, 7, "Dragon Soul Staff"},
    {3, 5, 10, 13, 255, 0, 1, 7, "Divine Archangel Staff"},
    {3, 5, 11, 13, 255, 0, 1, 7, "Kundun Staff"},
    {4, 6, 1, 13, 255, 1, 1, 7, "Horn Shield"},
    {4, 6, 2, 13, 255, 1, 1, 7, "Kite Shield"},
    {4, 6, 3, 13, 255, 1, 1, 7, "Elven Shield"},
    {4, 6, 4, 13, 255, 1, 1, 7, "Buckler"},
    {4, 6, 5, 13, 255, 1, 1, 7, "Dragon Slayer Shield"},
    {4, 6, 6, 13, 255, 1, 1, 7, "Skull Shield"},
    {4, 6, 7, 13, 255, 1, 1, 7, "Spike Shield"},
    {4, 6, 8, 13, 255, 1, 1, 7, "Tower Shield"},
    {4, 6, 9, 13, 255, 1, 1, 7, "Plate Shield"},
    {4, 6, 10, 13, 255, 1, 1, 7, "Large Round Shield"},
    {4, 6, 11, 13, 255, 1, 1, 7, "Serpent Shield"},
    {4, 6, 12, 13, 255, 1, 1, 7, "Bronze Shield"},
    {4, 6, 14, 13, 255, 1, 1, 7, "Legendary Shield"},
    {4, 6, 15, 13, 255, 1, 1, 7, "Grand Soul Shield"},
    {4, 6, 13, 13, 255, 1, 1, 7, "Chaos Dragon Shield"},
    {4, 6, 16, 13, 255, 1, 1, 7, "Elemental Shield"},
    {4, 12, 2, 13, 255, 0, 1, 7, "Satan Wings"},
    {4, 12, 0, 13, 255, 0, 1, 7, "Fairy Wings"},
    {4, 12, 1, 13, 255, 0, 1, 7, "Heaven Wings"},
    {4, 15, 18, 0, 0, 0, 0, 0, "Nova Spell"},
    {4, 15, 17, 0, 0, 0, 0, 0, "Ice Storm Spell"},
    {4, 15, 16, 0, 0, 0, 0, 0, "Decay Spell"},
    {5, 8, 15, 13, 255, 0, 1, 7, "Storm Crow Armor"},
    {5, 8, 20, 13, 255, 0, 1, 7, "Thunder Hawk Armor"},
    {5, 8, 23, 13, 255, 0, 1, 7, "Hurricane Armor"},
    {5, 0, 18, 13, 255, 1, 1, 7, "Thunder Blade"},
    {5, 10, 15, 13, 255, 0, 1, 7, "Storm Crow Gloves"},
    {5, 10, 20, 13, 255, 0, 1, 7, "Thunder Hawk Gloves"},
    {5, 10, 23, 13, 255, 0, 1, 7, "Hurricane Gloves"},
    {5, 0, 31, 13, 255, 1, 1, 7, "Rune Blade"},
    {5, 9, 15, 13, 255, 0, 1, 7, "Storm Crow Pants"},
    {5, 9, 20, 13, 255, 0, 1, 7, "Thunder Hawk Pants"},
    {5, 9, 23, 13, 255, 0, 1, 7, "Hurricane Pants"},
    {5, 11, 15, 13, 255, 0, 1, 7, "Storm Crow Boots"},
    {5, 11, 20, 13, 255, 0, 1, 7, "Thunder Hawk Boots"},
    {5, 11, 23, 13, 255, 0, 1, 7, "Hurricane Boots"},
    {5, 0, 21, 13, 255, 1, 1, 7, "Dark Reign Blade"},
    {5, 0, 23, 13, 255, 1, 1, 7, "Explosion Blade"},
    {5, 12, 6, 13, 255, 0, 1, 7, "Wing of Darkness"},
    {5, 12, 16, 0, 255, 0, 0, 0, "Power Slash"},
    {6, 7, 25, 13, 255, 0, 1, 7, "Light Plate Mask"},
    {6, 7, 26, 13, 255, 0, 1, 7, "Adamantine Mask"},
    {6, 7, 27, 13, 255, 0, 1, 7, "Dark Steel Mask"},
    {6, 7, 28, 13, 255, 0, 1, 7, "Dark Master Mask"},
    {6, 8, 25, 13, 255, 0, 1, 7, "Light Plate Armor"},
    {6, 8, 26, 13, 255, 0, 1, 7, "Adamantine Armor"},
    {6, 8, 27, 13, 255, 0, 1, 7, "Dark Steel Armor"},
    {6, 8, 28, 13, 255, 0, 1, 7, "Dark Master Armor"},
    {6, 10, 25, 13, 255, 0, 1, 7, "Light Plate Gloves"},
    {6, 10, 26, 13, 255, 0, 1, 7, "Adamantine Gloves"},
    {6, 10, 27, 13, 255, 0, 1, 7, "Dark Steel Gloves"},
    {6, 10, 28, 13, 255, 0, 1, 7, "Dark Master Gloves"},
    {6, 9, 25, 13, 255, 0, 1, 7, "Light Plate Pants"},
    {6, 9, 26, 13, 255, 0, 1, 7, "Adamantine Pants"},
    {6, 9, 27, 13, 255, 0, 1, 7, "Dark Steel Pants"},
    {6, 9, 28, 13, 255, 0, 1, 7, "Dark Master Pants"},
    {6, 11, 25, 13, 255, 0, 1, 7, "Light Plate Boots"},
    {6, 11, 26, 13, 255, 0, 1, 7, "Adamantine Boots"},
    {6, 11, 27, 13, 255, 0, 1, 7, "Dark Steel Boots"},
    {6, 11, 28, 13, 255, 0, 1, 7, "Dark Master Boots"},
    {6, 2, 8, 13, 255, 1, 1, 7, "Battle Scepter"},
    {6, 2, 10, 13, 255, 1, 1, 7, "Great Scepter"},
    {6, 2, 11, 13, 255, 1, 1, 7, "Lord Scepter"},
    {6, 2, 12, 13, 255, 1, 1, 7, "Great Lord Scepter"},
    {6, 12, 21, 0, 255, 0, 0, 0, "FireBrust Spell"},
    {6, 12, 23, 0, 255, 0, 0, 0, "Critical Damage Spell"},
    {6, 13, 30, 13, 255, 0, 1, 7, "Cape of Lord"},
    {6, 12, 24, 0, 255, 0, 0, 0, "Electric Spark Spell"},
    {6, 12, 22, 0, 255, 0, 0, 0, "Summon Spell"},
    {7, 7, 31, 13, 255, 0, 1, 7, "Sylpid Ray Helm"},
    {7, 7, 30, 13, 255, 0, 1, 7, "Venom Mist Helm"},
    {7, 7, 29, 13, 255, 0, 1, 7, "Dragon Knight Helm"},
    {7, 4, 21, 13, 255, 1, 1, 7, "Sylph Wind Bow"},
    {7, 8, 31, 13, 255, 0, 1, 7, "Sylpid Ray Armor"},
    {7, 8, 30, 13, 255, 0, 1, 7, "Venom Mist Armor"},
    {7, 8, 29, 13, 255, 0, 1, 7, "Dragon Knight Armor"},
    {7, 0, 22, 13, 255, 1, 1, 7, "Bone Blade"},
    {7, 5, 12, 13, 255, 0, 1, 7, "Grand Viper Staff"},
    {7, 10, 31, 13, 255, 0, 1, 7, "Sylpid Ray Gloves"},
    {7, 10, 30, 13, 255, 0, 1, 7, "Venom Mist Gloves"},
    {7, 10, 29, 13, 255, 0, 1, 7, "Dragon Knight Gloves"},
    {7, 9, 31, 13, 255, 0, 1, 7, "Sylpid Ray Pants"},
    {7, 9, 30, 13, 255, 0, 1, 7, "Venom Mist Pants"},
    {7, 9, 29, 13, 255, 0, 1, 7, "Dragon Knight Pants"},
    {7, 6, 13, 13, 255, 1, 1, 7, "Chaos Dragon Shield"},
    {7, 11, 31, 13, 255, 0, 1, 7, "Sylpid Ray Boots"},
    {7, 11, 30, 13, 255, 0, 1, 7, "Venom Mist Boots"},
    {7, 11, 29, 13, 255, 0, 1, 7, "Dragon Knight Boots"},
    {7, 6, 15, 13, 255, 0, 1, 7, "Grand Soul Shield"},
    {8, 7, 9, 13, 255, 0, 1, 7, "Plate Helm"},
    {8, 7, 1, 13, 255, 0, 1, 7, "Dragon Helm"},
    {8, 7, 16, 13, 255, 0, 1, 7, "Black Dragon Helm"},
    {8, 7, 17, 13, 255, 0, 1, 7, "Dark Phoenix Helm"},
    {8, 8, 9, 13, 255, 0, 1, 7, "Plate Armor"},
    {8, 8, 1, 13, 255, 0, 1, 7, "Dragon Armor"},
    {8, 8, 16, 13, 255, 0, 1, 7, "Black Dragon Armor"},
    {8, 8, 17, 13, 255, 0, 1, 7, "Dark Phoenix Armor"},
    {8, 10, 9, 13, 255, 0, 1, 7, "Plate Gloves"},
    {8, 10, 1, 13, 255, 0, 1, 7, "Dragon Gloves"},
    {8, 10, 16, 13, 255, 0, 1, 7, "Black Dragon Gloves"},
    {8, 10, 17, 13, 255, 0, 1, 7, "Dark Phoenix Gloves"},
    {8, 9, 9, 13, 255, 0, 1, 7, "Plate Pants"},
    {8, 9, 1, 13, 255, 0, 1, 7, "Dragon Pants"},
    {8, 9, 16, 13, 255, 0, 1, 7, "Black Dragon Pants"},
    {8, 9, 17, 13, 255, 0, 1, 7, "Dark Phoenix Pants"},
    {8, 11, 9, 13, 255, 0, 1, 7, "Plate Boots"},
    {8, 11, 1, 13, 255, 0, 1, 7, "Dragon Boots"},
    {8, 11, 16, 13, 255, 0, 1, 7, "Black Dragon Boots"},
    {8, 11, 17, 13, 255, 0, 1, 7, "Dark Phoenix Boots"},
    {9, 14, 3, 13, 255, 0, 0, 0, "Large Healing Potion"},
    {9, 14, 6, 13, 255, 0, 0, 0, "Large Mana Potion"},
    {9, 14, 8, 13, 255, 0, 0, 0, "Antidote"},
    {9, 13, 0, 0, 255, 0, 0, 0, "Guardian Angel"},
    {9, 13, 1, 0, 255, 0, 0, 0, "Imp"},
    {9, 13, 2, 0, 255, 0, 0, 0, "Horn of Uniria"},
    {9, 13, 3, 0, 255, 0, 0, 0, "Horn of Dinorant"},
    {9, 14, 20, 0, 255, 0, 0, 0, "Remedy of Love"},
    {9, 7, 10, 13, 255, 0, 1, 7, "Vine Helm"},
    {9, 7, 11, 13, 255, 0, 1, 7, "Silk Helm"},
    {9, 4, 0, 13, 255, 1, 1, 7, "Short Bow"},
    {9, 4, 1, 13, 255, 1, 1, 7, "Bow"},
    {9, 8, 10, 13, 255, 0, 1, 7, "Vine Armor"},
    {9, 8, 11, 13, 255, 0, 1, 7, "Silk Armor"},
    {9, 4, 2, 13, 255, 1, 1, 7, "Elven Bow"},
    {9, 4, 3, 13, 255, 1, 1, 7, "Battle Bow"},
    {9, 10, 10, 13, 255, 0, 1, 7, "Vine Gloves"},
    {9, 10, 11, 13, 255, 0, 1, 7, "Silk Gloves"},
    {9, 9, 10, 13, 255, 0, 1, 7, "Vine Pants"},
    {9, 9, 11, 13, 255, 0, 1, 7, "Silk Pants"},
    {9, 4, 4, 13, 255, 1, 1, 7, "Tiger Bow"},
    {9, 4, 5, 13, 255, 1, 1, 7, "Silver Bow"},
    {9, 11, 10, 13, 255, 0, 1, 7, "Vine Boots"},
    {9, 11, 11, 13, 255, 0, 1, 7, "Silk Boots"},
    {9, 4, 6, 13, 255, 1, 1, 7, "Chaos Nature Bow"},
    {9, 4, 17, 13, 255, 1, 1, 7, "Celestial Bow"},
    {9, 4, 18, 13, 255, 1, 1, 7, "Divine Crossbow of Archangel"},
    {9, 4, 16, 13, 255, 1, 1, 7, "Saint Crossbow"},
    {9, 4, 7, 13, 255, 0, 0, 0, "Bolts"},
    {9, 4, 15, 13, 255, 0, 0, 0, "Arrows"},
    {10, 14, 3, 13, 255, 0, 0, 0, "Large Healing Potion"},
    {10, 14, 6, 13, 255, 0, 0, 0, "Large Mana Potion"},
    {10, 14, 8, 13, 255, 0, 0, 0, "Antidote"},
    {10, 14, 13, 0, 0, 0, 0, 0, "Jewel of Bless"},
    {10, 14, 14, 0, 0, 0, 0, 0, "Jewel of Soul"},
    {10, 14, 16, 0, 0, 0, 0, 0, "Jewel of Life"},
    {10, 4, 7, 13, 255, 1, 1, 4, "Bolts"},
    {10, 4, 15, 13, 255, 1, 1, 4, "Arrows"},
    {10, 12, 18, 0, 0, 0, 1, 0, "Ice Arrow Orb"},
    {10, 12, 11, 0, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 12, 11, 1, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 12, 11, 2, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 12, 11, 3, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 12, 11, 4, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 12, 11, 5, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 12, 11, 6, 0, 0, 1, 0, "Goblin Summon Orb"},
    {10, 13, 8, 13, 255, 0, 0, 4, "Ring of Ice"},
    {10, 13, 9, 13, 255, 0, 0, 4, "Ring of Poison"},
    {10, 13, 12, 13, 255, 0, 0, 4, "Pendant of Lightning"},
    {10, 13, 13, 13, 255, 0, 0, 4, "Pendant of Fire"},
    {10, 7, 12, 13, 255, 0, 1, 7, "Wind Helm"},
    {10, 7, 13, 13, 255, 0, 1, 7, "Spirit Helm"},
    {10, 7, 14, 13, 255, 0, 1, 7, "Guardian Helm"},
    {10, 7, 19, 13, 255, 0, 1, 7, "Divine Helm"},
    {10, 8, 12, 13, 255, 0, 1, 7, "Wind Armor"},
    {10, 8, 13, 13, 255, 0, 1, 7, "Spirit Armor"},
    {10, 8, 14, 13, 255, 0, 1, 7, "Guardian Armor"},
    {10, 8, 19, 13, 255, 0, 1, 7, "Divine Armor"},
    {10, 9, 12, 13, 255, 0, 1, 7, "Wind Pants"},
    {10, 9, 13, 13, 255, 0, 1, 7, "Spirit Pants"},
    {10, 9, 14, 13, 255, 0, 1, 7, "Guardian Pants"},
    {10, 9, 19, 13, 255, 0, 1, 7, "Divine Pants"},
    {10, 10, 12, 13, 255, 0, 1, 7, "Wind Gloves"},
    {10, 10, 13, 13, 255, 0, 1, 7, "Spirit Gloves"},
    {10, 10, 14, 13, 255, 0, 1, 7, "Guardian Gloves"},
    {10, 10, 19, 13, 255, 0, 1, 7, "Divine Gloves"},
    {10, 11, 12, 13, 255, 0, 1, 7, "Wind Boots"},
    {10, 11, 13, 13, 255, 0, 1, 7, "Spirit Boots"},
    {10, 11, 14, 13, 255, 0, 1, 7, "Guardian Boots"},
    {10, 11, 19, 13, 255, 0, 1, 7, "Divine Boots"},
    {10, 14, 19, 1, 0, 0, 0, 0, "Devil Invitation+1"},
    {10, 14, 19, 2, 0, 0, 0, 0, "Devil Invitation+2"},
    {10, 14, 19, 3, 0, 0, 0, 0, "Devil Invitation+3"},
    {10, 14, 19, 4, 0, 0, 0, 0, "Devil Invitation+4"},
    {10, 14, 11, 8, 255, 0, 0, 0, "Box of Kundun+1"},
    {10, 14, 11, 9, 255, 0, 0, 0, "Box of Kundun+2"},
    {10, 14, 11, 10, 255, 0, 0, 0, "Box of Kundun+3"},
    {10, 14, 11, 11, 255, 0, 0, 0, "Box of Kundun+4"},
    {10, 14, 11, 12, 255, 0, 0, 0, "Box of Kundun+5"},
    {13, 14, 0, 0, 20, 0, 0, 0, "Apple"},
    {13, 14, 1, 0, 20, 0, 0, 0, "Small Healing Potion"},
    {13, 14, 2, 0, 30, 0, 0, 0, "Healing Potion"},
    {13, 14, 3, 0, 50, 0, 0, 0, "Large Healing Potion"},
    {13, 14, 4, 0, 20, 0, 0, 0, "Small Mana Potion"},
    {13, 14, 5, 0, 30, 0, 0, 0, "Mana Potion"},
    {13, 14, 6, 0, 50, 0, 0, 0, "Large Mana Potion"},
    {13, 14, 8, 0, 0, 0, 0, 0, "Antidote"},
    {13, 14, 9, 0, 0, 0, 0, 0, "Pisco Peru"},
    {14, 14, 0, 0, 20, 0, 0, 0, "Apple"},
    {14, 14, 1, 0, 20, 0, 0, 0, "Small Healing Potion"},
    {14, 14, 2, 0, 30, 0, 0, 0, "Healing Potion"},
    {14, 14, 3, 0, 50, 0, 0, 0, "Large Healing Potion"},
    {14, 14, 4, 0, 20, 0, 0, 0, "Small Mana Potion"},
    {14, 14, 5, 0, 30, 0, 0, 0, "Mana Potion"},
    {14, 14, 6, 0, 50, 0, 0, 0, "Large Mana Potion"},
    {14, 14, 8, 0, 0, 0, 0, 0, "Antidote"},
    {14, 14, 9, 0, 0, 0, 0, 0, "Pisco Peru"},
    {14, 14, 10, 0, 0, 0, 0, 0, "Town Portal Scroll"},
    {15, 14, 0, 0, 20, 0, 0, 0, "Apple"},
    {15, 14, 1, 0, 20, 0, 0, 0, "Small Healing Potion"},
    {15, 14, 2, 0, 30, 0, 0, 0, "Healing Potion"},
    {15, 14, 3, 0, 50, 0, 0, 0, "Large Healing Potion"},
    {15, 14, 4, 0, 20, 0, 0, 0, "Small Mana Potion"},
    {15, 14, 5, 0, 30, 0, 0, 0, "Mana Potion"},
    {15, 14, 6, 0, 50, 0, 0, 0, "Large Mana Potion"},
    {15, 14, 8, 0, 0, 0, 0, 0, "Antidote"},
    {15, 14, 9, 0, 0, 0, 0, 0, "Pisco Peru"},
    {15, 14, 10, 0, 0, 0, 0, 0, "Town Portal Scroll"},
    {16, 14, 0, 0, 20, 0, 0, 0, "Apple"},
    {16, 14, 1, 0, 20, 0, 0, 0, "Small Healing Potion"},
    {16, 14, 2, 0, 30, 0, 0, 0, "Healing Potion"},
    {16, 14, 3, 0, 50, 0, 0, 0, "Large Healing Potion"},
    {16, 14, 4, 0, 20, 0, 0, 0, "Small Mana Potion"},
    {16, 14, 5, 0, 30, 0, 0, 0, "Mana Potion"},
    {16, 14, 6, 0, 50, 0, 0, 0, "Large Mana Potion"},
    {16, 14, 8, 0, 0, 0, 0, 0, "Antidote"},
    {16, 14, 9, 0, 0, 0, 0, 0, "Pisco Peru"},
    {16, 14, 10, 0, 0, 0, 0, 0, "Town Portal Scroll"},
    {17, 14, 0, 0, 20, 0, 0, 0, "Apple"},
    {17, 14, 1, 0, 20, 0, 0, 0, "Small Healing Potion"},
    {17, 14, 2, 0, 30, 0, 0, 0, "Healing Potion"},
    {17, 14, 3, 0, 50, 0, 0, 0, "Large Healing Potion"},
    {17, 14, 4, 0, 20, 0, 0, 0, "Small Mana Potion"},
    {17, 14, 5, 0, 30, 0, 0, 0, "Mana Potion"},
    {17, 14, 6, 0, 50, 0, 0, 0, "Large Mana Potion"},
    {17, 14, 8, 0, 0, 0, 0, 0, "Antidote"},
    {17, 14, 9, 0, 0, 0, 0, 0, "Pisco Peru"},
    {17, 14, 10, 0, 0, 0, 0, 0, "Town Portal Scroll"},
    {17, 0, 0, 0, 255, 1, 1, 0, ""},
    {17, 0, 1, 0, 255, 1, 1, 0, ""},
    {17, 0, 2, 0, 255, 1, 1, 0, ""},
    {17, 0, 3, 0, 255, 1, 1, 0, ""},
    {17, 0, 4, 0, 255, 1, 1, 0, ""},
    {17, 0, 5, 0, 255, 1, 1, 0, ""},
    {17, 0, 6, 0, 255, 1, 1, 0, ""},
    {17, 0, 7, 0, 0, 1, 1, 0, ""},
    {17, 0, 8, 0, 255, 1, 1, 0, ""},
    {17, 0, 9, 0, 255, 1, 1, 0, ""},
    {17, 0, 10, 0, 255, 1, 1, 0, ""},
    {17, 0, 11, 0, 255, 1, 1, 0, ""},
    {17, 0, 12, 0, 255, 1, 1, 0, ""},
    {17, 0, 13, 0, 255, 1, 1, 0, ""},
    {17, 0, 14, 0, 255, 1, 1, 0, ""},
    {17, 0, 15, 0, 255, 1, 1, 0, ""},
    {17, 0, 16, 0, 255, 1, 1, 0, ""},
    {17, 0, 17, 0, 255, 1, 1, 0, ""},
    {17, 0, 18, 0, 255, 1, 1, 0, ""},
    {17, 12, 7, 0, 255, 0, 0, 0, "Twisting slash (DK/MG)"},
    {17, 12, 12, 0, 255, 0, 0, 0, "Earth Break (BK)"},
    {17, 12, 13, 0, 255, 0, 0, 0, "Horn Attack (BK/DK/MG)"},
    {17, 12, 14, 0, 255, 0, 0, 0, "Mass Healing (BK)"},
    {17, 12, 16, 0, 255, 0, 0, 0, "Flame Slash (MG)"},
    {17, 12, 17, 0, 255, 0, 0, 0, "Penetration (M-Elf/Elf)"},
    {17, 12, 18, 0, 255, 0, 0, 0, "Ice Age (M-Elf)"},
    {17, 12, 19, 0, 255, 0, 0, 0, "Death Stab (DK)"},
});

// Los siete bytes del ítem como los arma el GameServer, lo que 0x54b3a0 deshace: el tipo (grupo · 512 + índice) en el
// byte 0, su bit 8 en el 0x80 del byte 3 y los bits 9 a 12 arriba del byte 5; el nivel, la skill, la suerte y los dos
// bits de abajo de la opción en el byte 1, y su bit 2 en el 0x40 del byte 3.
std::array<std::byte, Client::Scenes::InventoryItemBytes::extent> ItemBytes(const ShopItem& entry)
{
    const auto type = static_cast<std::uint16_t>(entry.group * 512 + entry.index);
    const auto byte1 = static_cast<std::uint8_t>((entry.level << 3) | (entry.skill != 0 ? 0x80 : 0) |
                                                 (entry.luck != 0 ? 0x04 : 0) | (entry.option & 3));
    const auto byte3 = static_cast<std::uint8_t>(((type >> 1) & 0x80) | ((entry.option & 4) != 0 ? 0x40 : 0));
    return {static_cast<std::byte>(type & 0xff),
            std::byte{byte1},
            std::byte{entry.durability},
            std::byte{byte3},
            std::byte{0},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
}

// Los cortes que MuRet ya conoce en estos ítems, por su dirección, en la tienda y en el inventario. Cuando uno se
// porte, el test falla hasta sacarlo de acá.
std::optional<std::string_view> KnownCut(const ShopItem& entry)
{
    // Q-C415 y Q-C1044: el caballo (0x1a04) con la 0x96 en la primera opción; el lazo de 0x423ae9 agrega otra en cada
    // vuelta y escribe fuera del ítem hasta que la cuenta da la vuelta.
    if (entry.group == 13 && entry.index == 4)
        return "0x423b07";
    // Q-C735: desde el nivel 3, el precio de las flechas y las saetas (0x807, 0x80f) multiplica por la dirección del
    // ítem.
    if (entry.group == 4 && (entry.index == 7 || entry.index == 15) && entry.level >= 3)
        return "0x54d283";
    return std::nullopt;
}

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class GoldenShopTooltipsTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        const auto read = [this](std::string_view path) -> std::vector<char>
        {
            const auto file = DataFile(path);
            if (!file)
                return {};
            auto bytes = Client::Tests::ReadFileBytes(*file);
            return bytes ? std::move(*bytes) : std::vector<char>{};
        };
        const std::vector<char> itemFile = read("Local\\Item.bmd");
        const std::vector<char> textFile = read("Local\\Text.bmd");
        const std::vector<char> skillFile = read("Local\\Skill.bmd");
        const std::vector<char> setTypeFile = read("Local\\ItemSetType.bmd");
        const std::vector<char> setOptionFile = read("Local\\ItemSetOption.bmd");
        auto items = Client::Tables::LoadItemTable(std::as_bytes(std::span(itemFile)));
        ASSERT_TRUE(items.HasValue());
        m_items.emplace(std::move(items).Value().table);
        auto texts = Client::Tables::LoadTextTable(std::as_bytes(std::span(textFile)));
        ASSERT_TRUE(texts.HasValue());
        m_texts.emplace(std::move(texts).Value());
        auto skills = Client::Tables::LoadSkillTable(std::as_bytes(std::span(skillFile)));
        ASSERT_TRUE(skills.HasValue());
        m_skills.emplace(std::move(skills).Value());
        auto types = Client::Tables::LoadItemSetTypeTable(std::as_bytes(std::span(setTypeFile)));
        ASSERT_TRUE(types.HasValue());
        auto options = Client::Tables::LoadItemSetOptionTable(std::as_bytes(std::span(setOptionFile)));
        ASSERT_TRUE(options.HasValue());
        m_sets.emplace(Client::Tables::ItemSetTables{std::move(types).Value(), std::move(options).Value()});
        // 0x7681eb8: los monstruos de NpcName(Eng).txt, que nombran los anillos de transformación (0x1a0a).
        const std::vector<char> npcFile = read("Local\\NpcName(Eng).txt");
        auto npcs = Client::Tables::LoadNpcNames(std::as_bytes(std::span(npcFile)));
        ASSERT_TRUE(npcs.HasValue());
        m_world.Runtime().npcNames = Client::Scenes::NpcNameTableOf(npcs.Value());

        m_models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        m_world.OwnCharacter().ClearEquipmentForEntry();
        m_world.Inventory().ClearForInventoryReply();
        m_world.Runtime().hero = HeroSlot;
        m_world.Actors()[HeroSlot] = Client::Tests::UnarmedPlayer();
        m_world.Actors()[HeroSlot].alive = true;
        m_world.Actors()[HeroSlot].characterClass = Knight;
        Client::Kernel::OwnCharacterRecord& own = m_world.OwnCharacter();
        own.level = 150;
        own.strength = 200;
        own.agility = 100;
        m_world.Runtime().npcWindows.taxRate = LorenciaTax;
        m_sprites.Begin(Screen640);
    }

    WorldEntryResources Resources()
    {
        WorldEntryResources resources{.world = m_world,
                                      .selection = m_selection,
                                      .dataRoot = std::filesystem::path{},
                                      .cloth = m_cloth,
                                      .camera = m_camera,
                                      .cameraInput = m_cameraInput,
                                      .models = m_models,
                                      .items = &*m_items,
                                      .itemSetTables = &*m_sets,
                                      .log = m_log};
        resources.textMeasure = &m_measure;
        resources.texts = &*m_texts;
        return resources;
    }

    GameHudContext Context()
    {
        Client::Kernel::WorldRuntime& runtime = m_world.Runtime();
        Client::Kernel::ActorState& hero = m_world.Hero();
        return GameHudContext{.world = m_world,
                              .random = m_random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = m_sound,
                              .projecting = m_projecting,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = &*m_texts,
                              .items = &*m_items,
                              .skills = &*m_skills,
                              .setOptions = &m_sets->options,
                              .setTypes = &m_sets->types,
                              .mouse = m_mouse};
    }

    // El ítem en la casilla 0 de la grilla vacía de la tienda, como lo deja el 31.
    Client::Core::Expected<ItemState, std::string> Placed(const ShopItem& entry)
    {
        auto& grid = m_world.Runtime().npcWindows.grid;
        for (ItemState& cell : grid)
        {
            cell.id = Client::Kernel::EmptyOwnItemId;
            cell.occupied = 0;
        }
        const auto bytes = ItemBytes(entry);
        auto placed =
            Client::Scenes::PlaceInTheNpcGrid(grid, 0, Client::Scenes::InventoryItemBytes(bytes), Resources());
        if (!placed.HasValue())
            return Client::Core::Unexpected(std::move(placed).Error());
        return grid[0];
    }

    // El mouse sobre `place`/`index` con la tienda abierta, dos frames: 0x5f7bc0 con el registro propio abierto
    // alrededor. El caballo y el cuervo piden la mascota en el primero (0x4229d4) y se dibujan en el segundo.
    Client::Core::Expected<void, std::string> Hover(Client::UI::HoveredItem::Place place, std::uint8_t fromTheShop)
    {
        Client::Kernel::WorldRuntime& runtime = m_world.Runtime();
        runtime.panels.npcShop = 1;
        Client::UI::GameUiState& ui = runtime.ui;
        for (int frame = 0; frame < 2; ++frame)
        {
            ui.hoveredItem = Client::UI::HoveredItem{.place = place, .index = 0};
            ui.field78aba27 = fromTheShop;
            ui.skillTooltipX = 320;
            ui.skillTooltipY = 300;
            GameHudContext context = Context();
            if (auto drawn = Client::Scenes::DrawTheTooltips(context); !drawn.HasValue())
                return drawn;
        }
        return {};
    }

    Client::Kernel::WorldState m_world;
    Client::Scenes::SelectionState m_selection{m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_cameraInput;
    Client::Models::ClothWorld m_cloth{.random = CrtRandom(0x1357)};
    Client::Scenes::SingleModelLibrary m_models;
    std::optional<Client::Tables::ItemTable> m_items;
    std::optional<Client::Tables::TextTable> m_texts;
    std::optional<Client::Tables::SkillTable> m_skills;
    std::optional<Client::Tables::ItemSetTables> m_sets;
    LetterMeasure m_measure;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    CrtRandom m_random;
    std::optional<Client::Audio::SoundId> m_sound;
    bool m_projecting = false;
    Client::Render::SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text{m_sprites};
    Client::UI::Point m_mouse{};
};

TEST_F(GoldenShopTooltipsTest, EveryShopItemDrawsItsTooltipFromTheGridAndFromTheInventory)
{
    std::ostringstream failures;
    int failed = 0;
    const auto check = [&](const std::string& which, std::string_view where,
                           const Client::Core::Expected<void, std::string>& drawn,
                           const std::optional<std::string_view>& cut)
    {
        if (cut && (drawn.HasValue() || !drawn.Error().starts_with(*cut)))
        {
            failures << which << " " << where << ": se esperaba el corte " << *cut << " y "
                     << (drawn.HasValue() ? std::string("se dibujó") : drawn.Error()) << "\n";
            ++failed;
        }
        else if (!cut && !drawn.HasValue())
        {
            failures << which << " " << where << ": " << drawn.Error() << "\n";
            ++failed;
        }
    };
    for (const ShopItem& entry : ShopItems)
    {
        std::ostringstream which;
        which << "shop" << int{entry.shop} << " " << int{entry.group} << " " << int{entry.index} << " +"
              << int{entry.level} << " (" << entry.name << ")";
        auto placed = Placed(entry);
        if (!placed.HasValue())
        {
            failures << which.str() << " al ponerlo: " << placed.Error() << "\n";
            ++failed;
            continue;
        }
        const std::optional<std::string_view> cut = KnownCut(entry);
        check(which.str(), "en la tienda", Hover(Client::UI::HoveredItem::Place::NpcGridCell, 1), cut);
        m_world.Inventory().cells[0] = placed.Value();
        check(which.str(), "en el inventario", Hover(Client::UI::HoveredItem::Place::InventoryCell, 0), cut);
    }
    EXPECT_EQ(failed, 0) << failures.str();
    EXPECT_EQ(m_world.Runtime().ownRecordDepth, 0);

    // 0x5b41d0..0x5b421a: el anillo +1 (13 10 1 de shop1) es el monstruo 7 de 0x6c9338, el Giant de NpcName(Eng).txt,
    // con el 103; desde la grilla de la tienda va después del precio.
    const auto ring = std::ranges::find_if(ShopItems, [](const ShopItem& item)
                                           { return item.group == 13 && item.index == 10 && item.level == 1; });
    ASSERT_NE(ring, ShopItems.end());
    ASSERT_TRUE(Placed(*ring).HasValue());
    ASSERT_TRUE(Hover(Client::UI::HoveredItem::Place::NpcGridCell, 1).HasValue());
    const auto& text = m_world.Runtime().hud.tooltip.text;
    EXPECT_NE(std::ranges::find(text, std::string("Giant Anillo de transformacion")), text.end());
}
// El mismo barrido con los modelos del repo: cada ítem en la grilla de la tienda abierta, dibujado como lo dibuja el
// pase 3D de la UI (0x5ac120 → 0x5f7610, la tienda desde 0x5f769b, cada ítem con 0x5e1480 → 0x5e0720 → 0x60b3d0). Las
// alas de 12,6 (el modelo 0x1a18, en la tienda de la Potion Girl) cuelgan sus uniones del objeto local (Q-C1821).
// Ninguno puede cortar.
TEST_F(GoldenShopTooltipsTest, EveryShopItemDrawsItsModelInTheShopGrid)
{
    Client::Tests::FakeTextureStore textures;
    Client::Assets::GameModels models;
    const auto loaded = models.Load(m_dataRoot, textures, m_log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    Client::Render::ModelBatch batch;
    Client::Scenes::FaunaDrawCarry poseCarry;
    Client::World::FrameSprites sprites;
    Client::Actors::Drawing::ItemSpecialDrawContext special;
    special.random = &m_random;
    special.worldClock = 4321.0f;
    special.sprites = &sprites;
    Client::Actors::Drawing::ItemDrawContext itemContext;
    itemContext.special = &special;
    itemContext.selections = &m_world.Runtime().itemRender.selections;
    itemContext.originalFrame = true;
    itemContext.poseBones = [&poseCarry]() { return std::span<const Client::Math::BoneTransform>(poseCarry.bones); };
    std::size_t joints = 0;
    itemContext.createJoint = [&joints](const Client::World::FishEffectRequest& request)
    {
        EXPECT_EQ(request.ownerContext, Client::Scenes::UiItemLocalObject());
        ++joints;
        return Client::Core::Expected<void, std::string>{};
    };
    m_world.Runtime().panels.npcShop = 1;
    std::ostringstream failures;
    int failed = 0;
    for (const ShopItem& entry : ShopItems)
    {
        std::ostringstream which;
        which << "shop" << int{entry.shop} << " " << int{entry.group} << " " << int{entry.index} << " +"
              << int{entry.level} << " (" << entry.name << ")";
        auto placed = Placed(entry);
        if (!placed.HasValue())
        {
            failures << which.str() << " al ponerlo: " << placed.Error() << "\n";
            ++failed;
            continue;
        }
        auto pass = Client::Scenes::MakeUiItemPass(Screen640);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        batch.Begin(Client::Render::Pass3D{});
        sprites.Clear();
        Client::Scenes::UiItemResources resources{
            .batch = batch, .models = models, .poseCarry = poseCarry, .itemContext = itemContext, .pass = pass.Value()};
        GameHudContext context = Context();
        context.uiItems = &resources;
        Client::Scenes::GamePanels panels(m_world, m_random);
        const auto drawn = Client::Scenes::DrawTheGridItems(context, panels.Registry());
        Client::Scenes::CloseUiItemPass(resources);
        if (!drawn.HasValue())
        {
            failures << which.str() << " en la grilla: " << drawn.Error() << "\n";
            ++failed;
        }
    }
    EXPECT_EQ(failed, 0) << failures.str();
    // Las alas de 12,6 de la Potion Girl (shop5): 20 uniones cada vez que se dibujan.
    EXPECT_GT(joints, 0u);
    EXPECT_EQ(joints % 20, 0u);
    EXPECT_EQ(m_world.Runtime().ownRecordDepth, 0);
}
} // namespace
