import json
from pathlib import Path

import pytest

from mubmd.core.catalog import identify
from mubmd.core.json_text import LINE_WIDTH, view_text
from mubmd.core.json_types import JsonValue
from tests.models import view_of


def test_a_value_that_fits_stays_on_one_line() -> None:
    assert view_text({"position": [1.5, 2.0, -0.0]}) == '{"position": [1.5, 2.0, -0.0]}\n'


def test_a_value_that_does_not_fit_gets_a_line_per_member() -> None:
    first, second = "a" * 70, "b" * 70
    assert view_text({"texts": [first, second]}) == f'{{\n  "texts": [\n    "{first}",\n    "{second}"\n  ]\n}}\n'


def test_a_line_counts_its_key_its_indentation_and_its_comma() -> None:
    text = "a" * (LINE_WIDTH - len('  "k": [""]'))
    assert view_text({"k": [text], "l": [text]}) == f'{{\n  "k": [\n    "{text}"\n  ],\n  "l": ["{text}"]\n}}\n'


def text_view(reference_data: Path) -> JsonValue:
    path = reference_data / "Local" / "Text.bmd"
    identified = identify(path, b"")
    codec = identified.format.codec
    view: JsonValue = codec.to_view(codec.decode(path.read_bytes(), identified.file))
    return view


@pytest.mark.parametrize("view", [{}, {"a": [], "b": {}}, {"q": '"é\\'}, [], [[], {}], "é", 3, None, [1.5, True]])
def test_the_text_reads_back_as_the_view(view: JsonValue) -> None:
    text = view_text(view)
    assert json.loads(text) == view
    assert text.endswith("\n")
    assert "\\u" not in text


def test_a_models_text_reads_back_as_its_view(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    assert json.loads(view_text(view)) == view


def test_a_tables_text_reads_back_as_its_view(reference_data: Path) -> None:
    view = text_view(reference_data)
    text = view_text(view)
    assert json.loads(text) == view
    assert "salió" in text


def long_values(text: str) -> list[JsonValue]:
    lines = [line.strip().rstrip(",") for line in text.splitlines() if len(line) > LINE_WIDTH]
    return [json.loads(line.rpartition('": ')[2]) for line in lines]


def test_no_line_passes_the_width_but_a_single_value(reference_data: Path) -> None:
    values = long_values(view_text(text_view(reference_data)))
    assert values
    assert not [value for value in values if isinstance(value, list | dict)]


def test_a_models_short_parts_share_a_line(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    lines = view_text(view).splitlines()
    assert '        {"bone": 0, "position": [-1.417384, -1.0287832, -10.2511]},' in lines
    assert len(lines) < len(json.dumps(view, indent=2).splitlines()) // 4
    assert max(map(len, lines)) <= LINE_WIDTH
