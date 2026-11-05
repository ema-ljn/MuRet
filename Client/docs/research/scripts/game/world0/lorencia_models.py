"""Los modelos con nombre de Lorencia: el tipo del .obj ← el archivo de Data\\Object1\\.

El mapa 0 no usa el lazo genérico Object<k+1>.bmd de los otros mapas: 0x61a560 carga los modelos de
Object1 por nombre en sus tipos (0x618e4f..0x619edf; barrido D, 1.1) y después las texturas de los 160
de Object1. Es la tabla que la CL7.L2 pasa a dato.
"""

# (primer tipo, nombre, cuántos): Tree01..Tree13 en 0x00..0x0c, y así.
RANGES = [
    (0x00, 'Tree', 13), (0x14, 'Grass', 8), (0x1e, 'Stone', 5), (0x28, 'StoneStatue', 3),
    (0x2b, 'SteelStatue', 1), (0x2c, 'Tomb', 3), (0x32, 'FireLight', 2), (0x34, 'Bonfire', 1),
    (0x37, 'DoungeonGate', 1), (0x38, 'MerchantAnimal', 2), (0x3a, 'TreasureDrum', 1),
    (0x3b, 'TreasureChest', 1), (0x3c, 'Ship', 1), (0x41, 'SteelWall', 3), (0x44, 'SteelDoor', 1),
    (0x45, 'StoneWall', 6), (0x4b, 'StoneMuWall', 4), (0x50, 'Bridge', 1), (0x51, 'Fence', 4),
    (0x55, 'BridgeStone', 1), (0x5a, 'StreetLight', 1), (0x5b, 'Cannon', 3), (0x5f, 'Curtain', 1),
    (0x60, 'Sign', 2), (0x62, 'Carriage', 4), (0x66, 'Straw', 2), (0x69, 'Waterspout', 1),
    (0x6a, 'Well', 4), (0x6e, 'Hanging', 1), (0x6f, 'Stair', 1), (0x73, 'House', 5), (0x78, 'Tent', 1),
    (0x79, 'HouseWall', 6), (0x7f, 'HouseEtc', 3), (0x82, 'Light', 3), (0x85, 'PoseBox', 1),
    (0x8c, 'Furniture', 7), (0x96, 'Candle', 1), (0x97, 'Beer', 3),
]

NAMES = {base + k: '%s%02d' % (name, k + 1) for base, name, count in RANGES for k in range(count)}


def bmd(model_type):
    """El .bmd de un tipo de Lorencia (Tree01.bmd), o None si el tipo no tiene nombre."""
    name = NAMES.get(model_type)
    return name + '.bmd' if name else None
