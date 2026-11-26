import pytest

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.patch import Change, PatchOp, apply_patch, diff_views, parse_patch


def view() -> JsonObject:
    return {"format": "text", "texts": ["a", "b", "c"]}


def test_test_then_replace_applies() -> None:
    ops = parse_patch(
        [{"op": "test", "path": "/texts/1", "value": "b"}, {"op": "replace", "path": "/texts/1", "value": "x"}]
    )
    assert apply_patch(view(), ops) == {"format": "text", "texts": ["a", "x", "c"]}


def test_a_failing_test_changes_nothing() -> None:
    original = view()
    ops = (PatchOp("replace", "/texts/0", "x"), PatchOp("test", "/texts/1", "z"))
    with pytest.raises(MubmdError) as caught:
        apply_patch(original, ops)
    assert caught.value.code is ErrorCode.TEST_FAILED
    assert caught.value.pointer == "/texts/1"
    assert original == view()


@pytest.mark.parametrize(("stored", "expected"), [(1, True), (0, False), (True, 1), ([1, 0], [True, 0])])
def test_a_test_tells_a_boolean_from_a_number(stored: JsonValue, expected: JsonValue) -> None:
    with pytest.raises(MubmdError) as caught:
        apply_patch({"value": stored}, (PatchOp("test", "/value", expected),))
    assert caught.value.code is ErrorCode.TEST_FAILED


def test_a_test_takes_a_whole_number_for_its_float() -> None:
    assert apply_patch({"speed": 60.0}, (PatchOp("test", "/speed", 60),)) == {"speed": 60.0}


def test_apply_never_mutates_its_input() -> None:
    original = view()
    apply_patch(original, (PatchOp("replace", "/texts/0", "x"),))
    assert original == view()


def test_only_test_and_replace_are_accepted() -> None:
    with pytest.raises(MubmdError) as caught:
        parse_patch([{"op": "add", "path": "/texts/1", "value": "x"}])
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_a_patch_must_be_a_list_of_objects() -> None:
    with pytest.raises(MubmdError) as caught:
        parse_patch({"op": "replace"})
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_an_operation_needs_a_value() -> None:
    with pytest.raises(MubmdError) as caught:
        parse_patch([{"op": "replace", "path": "/texts/1"}])
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_replace_of_a_missing_pointer_is_not_found() -> None:
    with pytest.raises(MubmdError) as caught:
        apply_patch(view(), (PatchOp("replace", "/texts/9", "x"),))
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND


def test_diff_reports_each_changed_leaf() -> None:
    assert diff_views({"t": ["a", "b"]}, {"t": ["a", "c"]}) == (Change("/t/1", "b", "c"),)


def test_diff_of_equal_views_is_empty() -> None:
    assert diff_views(view(), view()) == ()
