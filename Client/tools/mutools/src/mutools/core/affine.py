import math
from collections.abc import Iterable
from dataclasses import dataclass
from typing import Final

type Vector = tuple[float, float, float]
type Quaternion = tuple[float, float, float, float]
type Rows = tuple[Vector, Vector, Vector]

AXES: Final = range(3)
ORIGIN: Final[Vector] = (0.0, 0.0, 0.0)
SAME_SIZE: Final[Vector] = (1.0, 1.0, 1.0)
UNTURNED: Final[Quaternion] = (0.0, 0.0, 0.0, 1.0)
UNCHANGED: Final[Rows] = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
MATRIX_SIDE: Final = 4
MATRIX_SIZE: Final = MATRIX_SIDE * MATRIX_SIDE
PLACE_COLUMN: Final = 3


def vector(numbers: Iterable[float]) -> Vector:
    x, y, z = numbers
    return x, y, z


def _rows(rows: Iterable[Iterable[float]]) -> Rows:
    x, y, z = (vector(row) for row in rows)
    return x, y, z


def normalized(quaternion: Iterable[float]) -> Quaternion:
    x, y, z, w = quaternion
    size = math.sqrt(x * x + y * y + z * z + w * w)
    return UNTURNED if size == 0 else (x / size, y / size, z / size, w / size)


def both_turns(first: Quaternion, second: Quaternion) -> Quaternion:
    """The turn second and then first."""
    ax, ay, az, aw = first
    bx, by, bz, bw = second
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def _turn_rows(turn: Quaternion) -> Rows:
    x, y, z, w = normalized(turn)
    return (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )


def _rows_turn(rows: Rows) -> Quaternion:
    (m00, m01, m02), (m10, m11, m12), (m20, m21, m22) = rows
    trace = m00 + m11 + m22
    if trace > 0:
        s = math.sqrt(trace + 1) * 2
        return normalized(((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, s / 4))
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1 + m00 - m11 - m22) * 2
        return normalized((s / 4, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s))
    if m11 > m22:
        s = math.sqrt(1 + m11 - m00 - m22) * 2
        return normalized(((m01 + m10) / s, s / 4, (m12 + m21) / s, (m02 - m20) / s))
    s = math.sqrt(1 + m22 - m00 - m11) * 2
    return normalized(((m02 + m20) / s, (m12 + m21) / s, s / 4, (m10 - m01) / s))


def _column(rows: Rows, axis: int) -> Vector:
    return rows[0][axis], rows[1][axis], rows[2][axis]


def _length(numbers: Vector) -> float:
    return math.sqrt(sum(number * number for number in numbers))


@dataclass(frozen=True, slots=True)
class Affine:
    """A turn, a scale and a place: the point p goes to rows · p + place."""

    rows: Rows = UNCHANGED
    place: Vector = ORIGIN

    @classmethod
    def of(cls, place: Vector = ORIGIN, turn: Quaternion = UNTURNED, scale: Vector = SAME_SIZE) -> "Affine":
        return cls(_rows((row[axis] * scale[axis] for axis in AXES) for row in _turn_rows(turn)), place)

    @classmethod
    def from_columns(cls, numbers: tuple[float, ...]) -> "Affine":
        """A 4 x 4 matrix of 16 numbers column after column, as glTF stores one."""
        rows = _rows((numbers[column * MATRIX_SIDE + row] for column in AXES) for row in AXES)
        return cls(rows, vector(numbers[PLACE_COLUMN * MATRIX_SIDE + row] for row in AXES))

    def columns(self) -> tuple[float, ...]:
        numbers = [0.0] * MATRIX_SIZE
        for row in AXES:
            for column in AXES:
                numbers[column * MATRIX_SIDE + row] = self.rows[row][column]
            numbers[PLACE_COLUMN * MATRIX_SIDE + row] = self.place[row]
        numbers[-1] = 1.0
        return tuple(numbers)

    def turned(self, numbers: Vector) -> Vector:
        return vector(sum(row[axis] * numbers[axis] for axis in AXES) for row in self.rows)

    def applied(self, point: Vector) -> Vector:
        return vector(moved + place for moved, place in zip(self.turned(point), self.place, strict=True))

    def __matmul__(self, other: "Affine") -> "Affine":
        columns = [self.turned(_column(other.rows, axis)) for axis in AXES]
        return Affine(_rows((column[row] for column in columns) for row in AXES), self.applied(other.place))

    def inverse(self) -> "Affine":
        (a, b, c), (d, e, f), (g, h, i) = self.rows
        cofactors = (
            (e * i - f * h, c * h - b * i, b * f - c * e),
            (f * g - d * i, a * i - c * g, c * d - a * f),
            (d * h - e * g, b * g - a * h, a * e - b * d),
        )
        determinant = a * cofactors[0][0] + b * cofactors[1][0] + c * cofactors[2][0]
        undone = Affine(_rows((number / determinant for number in row) for row in cofactors))
        return Affine(undone.rows, vector(-number for number in undone.turned(self.place)))

    def scale(self) -> Vector:
        return vector(_length(_column(self.rows, axis)) for axis in AXES)

    def turn(self) -> Quaternion:
        """The turn alone: each column is taken to length 1 before it is read."""
        lengths = [length or 1.0 for length in self.scale()]
        return _rows_turn(_rows((row[axis] / lengths[axis] for axis in AXES) for row in self.rows))
