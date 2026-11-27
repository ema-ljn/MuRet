from typing import Final

from mutools.core.formats.record_fields import RecordField
from mutools.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

MOVE_REQUIREMENT_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="move_requirement",
        count=None,
        record_size=0x50,
        fields=(
            RecordField("number", 4, "int"),
            RecordField("name", 32, "text"),
            RecordField("search_name", 32, "text"),
            RecordField("level", 4, "int"),
            RecordField("zen", 4, "uint"),
            RecordField("gate", 4, "int"),
        ),
        checksum_key=None,
        reference="Client/docs/research/05-formatos-de-datos.md#movereqbmd",
    )
)
