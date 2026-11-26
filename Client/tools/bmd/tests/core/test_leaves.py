import pytest

from mubmd.core.json_types import JsonValue
from mubmd.core.leaves import is_blank, is_leaf, leaf_text, leaves

TAILED: JsonValue = {"text": "x", "tail": "01"}


def test_leaves_walks_lists_and_objects_down_to_values() -> None:
    view: JsonValue = {"a": [1, {"hex": "00"}], "b": TAILED}
    assert list(leaves(view)) == [(("a", 0), 1), (("a", 1), {"hex": "00"}), (("b",), TAILED)]


def test_leaves_starts_from_the_tokens_it_is_given() -> None:
    assert list(leaves(["x"], ("records", 3))) == [(("records", 3, 0), "x")]


@pytest.mark.parametrize("value", ["text", 3, 2.5, {"hex": "00"}, {"f32": "0000c07f"}, TAILED])
def test_a_value_the_codec_writes_whole_is_a_leaf(value: JsonValue) -> None:
    assert is_leaf(value)


@pytest.mark.parametrize("value", [[1], {"name": "x"}, {"text": "x"}, {}])
def test_a_container_is_not_a_leaf(value: JsonValue) -> None:
    assert not is_leaf(value)


def test_leaf_text_reads_strings_and_texts_with_a_tail() -> None:
    assert leaf_text("Kris") == "Kris"
    assert leaf_text(TAILED) == "x"


@pytest.mark.parametrize("value", [1, {"hex": "00"}, {"f32": "0000c07f"}, [1], {"name": "x"}])
def test_leaf_text_is_none_for_what_is_not_a_text(value: JsonValue) -> None:
    assert leaf_text(value) is None


@pytest.mark.parametrize("value", ["", 0, 0.0, {"hex": "0000"}, {"hex": ""}, [0, ""], {"a": 0, "b": [""]}])
def test_blank_values(value: JsonValue) -> None:
    assert is_blank(value)


NOT_BLANK: list[JsonValue] = ["a", 1, -1, 2.5, {"hex": "ff"}, {"text": "", "tail": "65"}, {"f32": "0000c07f"}]


@pytest.mark.parametrize("value", [*NOT_BLANK, [0, 1], {"a": 0, "b": "x"}])
def test_values_that_are_not_blank(value: JsonValue) -> None:
    assert not is_blank(value)
