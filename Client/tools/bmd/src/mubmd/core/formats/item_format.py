from typing import Final

from mubmd.core.ciphers.table_checksum import CHECKSUM_WORD
from mubmd.core.documents.item_table import ITEM_CHECKSUM_KEY, ITEM_COUNT, ITEM_NAME_SIZE, ITEM_RECORD_SIZE
from mubmd.core.formats.record_fields import RecordField
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

ITEM_TABLE_SIZE: Final = ITEM_COUNT * ITEM_RECORD_SIZE + CHECKSUM_WORD.size
ITEM_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="item",
        count=ITEM_COUNT,
        record_size=ITEM_RECORD_SIZE,
        fields=(
            RecordField("name", ITEM_NAME_SIZE, "text"),
            RecordField("two_handed", 1, "uint"),
            RecordField("raw_0x1f", 1, "bytes"),
            RecordField("level", 2, "uint"),
            RecordField("raw_0x22", 2, "bytes"),
            RecordField("width", 1, "uint"),
            RecordField("height", 1, "uint"),
            RecordField("raw_0x26", ITEM_RECORD_SIZE - 0x26, "bytes"),
        ),
        checksum_key=ITEM_CHECKSUM_KEY,
        reference="Client/docs/research/05-formatos-de-datos.md#tablas-bmd",
    )
)
