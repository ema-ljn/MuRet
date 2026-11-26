from typing import Final

from mubmd.core.formats.record_fields import RecordField
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

MONSTER_TYPE_LIMIT: Final = 0xFFFF

MONSTER_SKILL_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="monster_skill",
        count=None,
        record_size=0x1C,
        fields=(
            RecordField("type", 4, "int", lowest=0, highest=MONSTER_TYPE_LIMIT),
            RecordField("skills", 4, "int", count=5),
            RecordField("raw_0x18", 4, "bytes"),
        ),
        checksum_key=None,
        reference="Client/docs/research/05-formatos-de-datos.md#monsterskillbmd",
    )
)
