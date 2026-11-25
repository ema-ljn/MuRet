import pytest

from mubmd.core.ciphers.table_checksum import table_checksum
from mubmd.core.documents.item_table import ITEM_CHECKSUM_KEY


@pytest.mark.parametrize(
    ("key", "xor_odd_words", "expected"),
    [(ITEM_CHECKSUM_KEY, True, 0xE36F5FC9), (0x5A18, False, 0xA9B5ED92)],
)
def test_checksum_matches_the_client_research_vectors(key: int, xor_odd_words: bool, expected: int) -> None:
    data = bytes((index * 37 + 11) % 256 for index in range(64))
    assert table_checksum(data, key, xor_odd_words=xor_odd_words) == expected
