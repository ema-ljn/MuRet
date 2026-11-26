from typing import Final

from mubmd.core.formats.record_fields import RecordField
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

FILTER_WORD_COUNT: Final = 1000
FILTER_WORD_SIZE: Final = 20


def _word_table(name: str, checksum_key: int) -> RecordTableFormat:
    return RecordTableFormat(
        RecordTableSchema(
            name=name,
            count=FILTER_WORD_COUNT,
            record_size=FILTER_WORD_SIZE,
            fields=(RecordField("word", FILTER_WORD_SIZE, "text"),),
            checksum_key=checksum_key,
            reference="Client/docs/research/05-formatos-de-datos.md#tablas-bmd",
        )
    )


FILTER_FORMAT: Final = _word_table("filter", 0x3E7D)
FILTER_NAME_FORMAT: Final = _word_table("filter_name", 0x2BC1)
