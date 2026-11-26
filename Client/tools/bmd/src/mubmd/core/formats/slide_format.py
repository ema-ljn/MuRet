from typing import Final

from mubmd.core.formats.record_fields import RecordField, struct
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

SLIDE_GROUP_COUNT: Final = 5
SLIDE_GROUP_TEXTS: Final = 32

SLIDE_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="slide",
        count=1,
        record_size=0xA030,
        fields=(
            RecordField("period", 4, "int"),
            RecordField("speed", 4, "f32"),
            struct(
                "groups",
                (
                    RecordField("max_level", 4, "int"),
                    RecordField("count", 4, "int", highest=SLIDE_GROUP_TEXTS),
                    RecordField("texts", 0x100, "text", count=SLIDE_GROUP_TEXTS),
                ),
                count=SLIDE_GROUP_COUNT,
            ),
        ),
        checksum_key=None,
        reference="Client/docs/research/05-formatos-de-datos.md#slidebmd",
    )
)
