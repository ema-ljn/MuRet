import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.pointer import format_pointer, parse_pointer, resolve


def test_the_empty_pointer_is_the_whole_document() -> None:
    assert parse_pointer("") == ()


def test_a_pointer_splits_on_slashes() -> None:
    assert parse_pointer("/texts/500") == ("texts", "500")


def test_escapes_are_undone() -> None:
    assert parse_pointer("/a~1b/c~0d") == ("a/b", "c~d")


def test_format_escapes_and_joins() -> None:
    assert format_pointer(["a/b", 3]) == "/a~1b/3"


def test_a_pointer_must_start_with_a_slash() -> None:
    with pytest.raises(MutoolsError) as caught:
        parse_pointer("texts")
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND


def test_resolve_walks_objects_and_lists() -> None:
    assert resolve({"texts": ["a", "b"]}, "/texts/1") == "b"


@pytest.mark.parametrize("index", ["-", "007", "-1", "2"])
def test_resolve_rejects_bad_list_indices(index: str) -> None:
    with pytest.raises(MutoolsError) as caught:
        resolve({"texts": ["a", "b"]}, f"/texts/{index}")
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND
    assert caught.value.pointer == f"/texts/{index}"


def test_resolve_rejects_a_missing_key() -> None:
    with pytest.raises(MutoolsError) as caught:
        resolve({"texts": []}, "/names")
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND
