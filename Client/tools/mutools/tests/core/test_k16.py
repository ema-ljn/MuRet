from mutools.core.ciphers.k16 import decrypt_k16, encrypt_k16


def test_k16_decrypts_the_clients_vectors() -> None:
    assert decrypt_k16(bytes(2)) == bytes.fromhex("7336")
    assert decrypt_k16(bytes(17))[16] == 0x94
    assert decrypt_k16(bytes.fromhex("8be52492")) == bytes.fromhex("fcce5403")


def test_k16_encrypts_what_it_decrypts() -> None:
    data = bytes(range(256)) * 3
    assert decrypt_k16(encrypt_k16(data)) == data
    assert encrypt_k16(decrypt_k16(data)) == data
