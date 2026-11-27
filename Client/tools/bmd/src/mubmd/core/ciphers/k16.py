from itertools import cycle
from typing import Final

K16_KEY: Final = bytes.fromhex("d17352f6d29acb273eaf593137b3e7a2")
FIRST_PREVIOUS: Final = 0x5E
CHAIN_STEP: Final = 0x3D
BYTE_MASK: Final = 0xFF


def decrypt_k16(data: bytes) -> bytes:
    plain = bytearray(len(data))
    previous = FIRST_PREVIOUS
    for index, (cipher, key) in enumerate(zip(data, cycle(K16_KEY), strict=False)):
        plain[index] = ((key ^ cipher) - previous) & BYTE_MASK
        previous = cipher + CHAIN_STEP
    return bytes(plain)


def encrypt_k16(data: bytes) -> bytes:
    cipher = bytearray(len(data))
    previous = FIRST_PREVIOUS
    for index, (plain, key) in enumerate(zip(data, cycle(K16_KEY), strict=False)):
        byte = ((plain + previous) & BYTE_MASK) ^ key
        cipher[index] = byte
        previous = byte + CHAIN_STEP
    return bytes(cipher)
