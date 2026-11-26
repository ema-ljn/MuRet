from typing import Final

from mubmd.core.documents.item_table import ITEM_COUNT, ITEM_SECTION_SIZE
from mubmd.core.formats.record_fields import RecordField
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

ITEM_SET_REFERENCE: Final = "Client/docs/research/05-formatos-de-datos.md#las-tablas-de-los-conjuntos"

ITEM_SET_TYPE_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="item_set_type",
        count=ITEM_COUNT,
        record_size=4,
        fields=(
            RecordField("sets", 1, "uint", count=3),
            RecordField("raw_0x03", 1, "bytes"),
        ),
        checksum_key=0xE5F1,
        reference=ITEM_SET_REFERENCE,
        section_size=ITEM_SECTION_SIZE,
    )
)

ITEM_SET_OPTION_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="item_set_option",
        count=64,
        record_size=0x4C,
        fields=(
            RecordField("name", 32, "text"),
            RecordField("options", 1, "uint", count=12),
            RecordField("values", 1, "uint", count=12),
            RecordField("extra_options", 1, "uint", count=2),
            RecordField("extra_values", 1, "uint", count=2),
            RecordField("full_set_pieces", 1, "uint"),
            RecordField("full_options", 1, "uint", count=5),
            RecordField("full_values", 1, "uint", count=5),
            RecordField("classes", 1, "uint", count=5),
        ),
        checksum_key=0xA2F1,
        reference=ITEM_SET_REFERENCE,
    )
)
