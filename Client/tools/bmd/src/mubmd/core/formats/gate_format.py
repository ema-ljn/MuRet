from typing import Final

from mubmd.core.formats.record_fields import RecordField
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

GATE_FIELD_NAMES: Final = ("flag", "map", "x1", "y1", "x2", "y2", "target", "direction", "level")
GATE_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="gate",
        count=256,
        record_size=len(GATE_FIELD_NAMES),
        fields=tuple(RecordField(name, 1, "uint") for name in GATE_FIELD_NAMES),
        checksum_key=None,
        reference="Client/docs/research/05-formatos-de-datos.md#gatebmd",
    )
)
