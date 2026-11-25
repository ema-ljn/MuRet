from typing import Final

XOR3_KEY: Final = bytes((0xFC, 0xCF, 0xAB))


def xor3(data: bytes) -> bytes:
    size = len(data)
    key = (XOR3_KEY * (size // 3 + 1))[:size]
    return (int.from_bytes(data, "little") ^ int.from_bytes(key, "little")).to_bytes(size, "little")
