import math

import pytest

from mutools.core.affine import Affine, both_turns, normalized

ROOT_HALF = math.sqrt(0.5)
QUARTER_ABOUT_Z = (0.0, 0.0, ROOT_HALF, ROOT_HALF)
QUARTER_ABOUT_X = (ROOT_HALF, 0.0, 0.0, ROOT_HALF)
CLOSE = 1e-12


def test_a_turn_and_a_place_move_a_point() -> None:
    moved = Affine.of((10.0, 0.0, 0.0), QUARTER_ABOUT_Z)

    assert moved.applied((1.0, 0.0, 0.0)) == pytest.approx((10.0, 1.0, 0.0), abs=CLOSE)


def test_the_product_applies_the_right_one_first() -> None:
    turn, move = Affine.of(turn=QUARTER_ABOUT_Z), Affine.of((1.0, 0.0, 0.0))

    assert (turn @ move).applied((0.0, 0.0, 0.0)) == pytest.approx((0.0, 1.0, 0.0), abs=CLOSE)


def test_the_inverse_undoes_a_turn_a_scale_and_a_place() -> None:
    moved = Affine.of((1.0, 2.0, 3.0), normalized((0.1, 0.2, 0.3, 0.9)), (2.0, 0.5, 1.5))

    assert (moved.inverse() @ moved).applied((4.0, 5.0, 6.0)) == pytest.approx((4.0, 5.0, 6.0), abs=CLOSE)


def test_the_turn_is_read_back_without_the_scale() -> None:
    turn = normalized((0.1, -0.7, 0.3, 0.6))

    found = Affine.of((5.0, 0.0, 0.0), turn, (3.0, 3.0, 3.0)).turn()

    assert abs(sum(a * b for a, b in zip(found, turn, strict=True))) == pytest.approx(1.0, abs=CLOSE)


def test_the_scale_is_the_length_of_each_column() -> None:
    assert Affine.of(turn=QUARTER_ABOUT_X, scale=(2.0, 3.0, 4.0)).scale() == pytest.approx((2.0, 3.0, 4.0))


def test_the_columns_come_in_gltf_order() -> None:
    numbers = Affine.of((7.0, 8.0, 9.0)).columns()

    assert numbers[12:] == (7.0, 8.0, 9.0, 1.0)
    assert Affine.from_columns(numbers) == Affine.of((7.0, 8.0, 9.0))


def test_both_turns_turn_by_the_second_and_then_the_first() -> None:
    both = both_turns(QUARTER_ABOUT_X, QUARTER_ABOUT_Z)

    expected = (Affine.of(turn=QUARTER_ABOUT_X) @ Affine.of(turn=QUARTER_ABOUT_Z)).applied((1.0, 0.0, 0.0))
    assert Affine.of(turn=both).applied((1.0, 0.0, 0.0)) == pytest.approx(expected, abs=CLOSE)
