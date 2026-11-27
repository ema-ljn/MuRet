from struct import Struct
from typing import Final

CHECKSUM_WORD: Final = Struct("<I")
WORD_MASK: Final = (1 << (8 * CHECKSUM_WORD.size)) - 1
SEED_SHIFT: Final = 9
MIX_PERIOD: Final = 4
MIX_SHIFT_MASK: Final = 7


def table_checksum(data: bytes, key: int, *, xor_odd_words: bool) -> int:
    checksum = (key << SEED_SHIFT) & WORD_MASK
    word_bytes = len(data) - len(data) % CHECKSUM_WORD.size
    for index, (word,) in enumerate(CHECKSUM_WORD.iter_unpack(memoryview(data)[:word_bytes])):
        use_xor = bool(index % 2) == xor_odd_words
        checksum = checksum ^ word if use_xor else (checksum + word) & WORD_MASK
        if index % MIX_PERIOD == 0:
            shift = (index & MIX_SHIFT_MASK) + 1
            checksum ^= ((checksum + key) & WORD_MASK) >> shift
    return checksum
