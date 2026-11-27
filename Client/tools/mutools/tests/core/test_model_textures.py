import pytest

from mutools.core.formats.model_textures import TextureUse, texture_text, texture_use


@pytest.mark.parametrize(
    ("name", "file", "slot", "script"),
    [
        ("sword02.jpg", "sword02.OZJ", None, ""),
        ("lnn01.tga", "lnn01.OZT", None, ""),
        ("Hair_R.TGA", "Hair_R.OZT", None, "R"),
        ("hair_R.jpg", "hair_R.OZJ", "hair", "R"),
        ("hair_r.tga", "hair_r.OZT", "hair", ""),
        ("level_man02.jpg", "level_man02.OZJ", "skin", ""),
        ("skin_barbarian_01.jpg", "skin_barbarian_01.OZJ", "skin", ""),
        ("hide.jpg", "hide.OZJ", "hidden", ""),
        ("bossmap1_R.jpg.jpg", "bossmap1_R.OZJ", None, "R"),
        ("x.tga.jpg", "x.OZT", None, ""),
        ("x.jpg.tga", "x.OZJ", None, ""),
        ("ataque", "ataque.OZJ", None, ""),
        ("ataque.", "ataque.OZJ", None, ""),
        ("", ".OZJ", None, ""),
        ("x_NSHR.jpg", "x_NSHR.OZJ", None, "RHSN"),
        ("x_RRRRzz.jpg", "x_RRRRzz.OZJ", None, "R"),
        ("Monster81_R2.jpg", "Monster81_R2.OZJ", None, ""),
        ("Object01_H", "Object01_H.OZJ", None, "H"),
    ],
)
def test_a_texture_name_says_its_file_slot_and_script(name: str, file: str, slot: str | None, script: str) -> None:
    assert texture_use(name) == TextureUse(file, slot, script)


@pytest.mark.parametrize(
    ("name", "text"),
    [
        (b"sword02.jpg\0" + b"\xcd" * 20, "sword02.jpg"),
        (b"\0" * 32, ""),
        (b"a" * 32, None),
        (b"\xc7.jpg\0".ljust(32, b"\0"), None),
    ],
)
def test_a_texture_name_is_read_up_to_its_zero(name: bytes, text: str | None) -> None:
    assert texture_text(name, "cp949") == text
