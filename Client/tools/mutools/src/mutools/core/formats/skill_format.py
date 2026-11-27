from typing import Final

from mutools.core.formats.record_fields import RecordField
from mutools.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

SKILL_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="skill",
        count=256,
        record_size=0x3C,
        fields=(
            RecordField("name", 32, "text"),
            RecordField("level", 1, "uint"),
            RecordField("damage", 1, "uint"),
            RecordField("mana", 2, "uint"),
            RecordField("stamina", 2, "uint"),
            RecordField("distance", 1, "uint"),
            RecordField("raw_0x27", 1, "bytes"),
            RecordField("delay", 4, "int"),
            RecordField("energy", 1, "uint"),
            RecordField("raw_0x2d", 1, "bytes"),
            RecordField("leadership", 2, "uint"),
            RecordField("element", 1, "uint"),
            RecordField("kind", 1, "uint"),
            RecordField("linked_skill", 1, "uint"),
            RecordField("parameter", 1, "uint"),
            RecordField("evolutions", 1, "uint", count=3),
            RecordField("classes", 1, "uint", count=5),
        ),
        checksum_key=0x5A18,
        xor_odd_words=False,
        reference="Client/docs/research/05-formatos-de-datos.md#tablas-bmd",
    )
)
