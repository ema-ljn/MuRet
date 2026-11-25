import random

from mubmd.core.ciphers.xor3 import xor3


def test_zeros_give_the_key() -> None:
    assert xor3(bytes(6)) == bytes.fromhex("fccfabfccfab")


def test_xor3_is_its_own_inverse() -> None:
    data = random.Random(7).randbytes(1000)
    assert xor3(xor3(data)) == data


def test_record_500_of_the_real_text_table(text_bytes: bytes) -> None:
    record = xor3(text_bytes[500 * 300 : 501 * 300])
    expected = "El usuario salió del juego".encode("cp1252") + b"\0"
    assert record.startswith(expected)
