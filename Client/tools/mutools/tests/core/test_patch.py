from typing import Any

import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.patch import Change, PatchOp, apply_patch, diff_views, parse_patch


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
    with pytest.raises(MutoolsError) as caught:
        apply_patch(original, ops)
    assert caught.value.code is ErrorCode.TEST_FAILED
    assert caught.value.pointer == "/texts/1"
    assert original == view()


@pytest.mark.parametrize(("stored", "expected"), [(1, True), (0, False), (True, 1), ([1, 0], [True, 0])])
def test_a_test_tells_a_boolean_from_a_number(stored: JsonValue, expected: JsonValue) -> None:
    with pytest.raises(MutoolsError) as caught:
        apply_patch({"value": stored}, (PatchOp("test", "/value", expected),))
    assert caught.value.code is ErrorCode.TEST_FAILED


def test_a_test_takes_a_whole_number_for_its_float() -> None:
    assert apply_patch({"speed": 60.0}, (PatchOp("test", "/speed", 60),)) == {"speed": 60.0}


def test_apply_never_mutates_its_input() -> None:
    original = view()
    apply_patch(original, (PatchOp("replace", "/texts/0", "x"),))
    assert original == view()


def test_only_test_and_replace_are_accepted() -> None:
    with pytest.raises(MutoolsError) as caught:
        parse_patch([{"op": "add", "path": "/texts/1", "value": "x"}])
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_a_patch_must_be_a_list_of_objects() -> None:
    with pytest.raises(MutoolsError) as caught:
        parse_patch({"op": "replace"})
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_an_operation_needs_a_value() -> None:
    with pytest.raises(MutoolsError) as caught:
        parse_patch([{"op": "replace", "path": "/texts/1"}])
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_replace_of_a_missing_pointer_is_not_found() -> None:
    with pytest.raises(MutoolsError) as caught:
        apply_patch(view(), (PatchOp("replace", "/texts/9", "x"),))
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND


def test_diff_reports_each_changed_leaf() -> None:
    assert diff_views({"t": ["a", "b"]}, {"t": ["a", "c"]}) == (Change("/t/1", "b", "c"),)


def test_diff_of_equal_views_is_empty() -> None:
    assert diff_views(view(), view()) == ()


def test_apply_shares_what_it_does_not_touch() -> None:
    original: JsonObject = {"a": [{"x": 1}, {"x": 2}], "b": [3]}
    result = apply_patch(original, (PatchOp("replace", "/a/1/x", 5),))
    assert result == {"a": [{"x": 1}, {"x": 5}], "b": [3]}
    assert result["b"] is original["b"]
    first, second = original["a"], result["a"]
    assert isinstance(first, list)
    assert isinstance(second, list)
    assert second[0] is first[0]
    assert first[1] == {"x": 2}


def test_a_later_operation_sees_an_earlier_one() -> None:
    original: JsonObject = {"a": [{"x": 1}, {"x": 2}], "b": [3]}
    ops = (PatchOp("replace", "/a/1/x", 5), PatchOp("test", "/a/1/x", 5), PatchOp("replace", "/a/0/x", 7))
    assert apply_patch(original, ops) == {"a": [{"x": 7}, {"x": 5}], "b": [3]}


def test_the_result_does_not_share_the_patch_value() -> None:
    value: JsonValue = {"x": [1]}
    result = apply_patch({"a": [0]}, (PatchOp("replace", "/a/0", value),))
    assert isinstance(value, dict)
    value["x"] = [2]
    assert result == {"a": [{"x": [1]}]}


def test_a_diff_skips_the_parts_two_views_share() -> None:
    class Unequal:
        def __eq__(self, other: object) -> bool:
            raise AssertionError("a shared part was compared")

        __hash__ = None  # type: ignore[assignment]

    shared: Any = [Unequal()]
    assert diff_views({"same": shared, "x": 1}, {"same": shared, "x": 2}) == (Change("/x", 1, 2),)


def test_a_zero_of_the_other_sign_is_a_change() -> None:
    (change,) = diff_views({"x": [0.0, 1.0]}, {"x": [-0.0, 1.0]})
    assert (change.pointer, str(change.old), str(change.new)) == ("/x/0", "0.0", "-0.0")


def test_a_test_tells_a_negative_zero_from_zero() -> None:
    with pytest.raises(MutoolsError) as caught:
        apply_patch({"x": 0.0}, (PatchOp("test", "/x", -0.0),))
    assert caught.value.code is ErrorCode.TEST_FAILED
