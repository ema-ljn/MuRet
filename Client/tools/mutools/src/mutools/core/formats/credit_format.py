from typing import Final

from mutools.core.formats.record_fields import RecordField
from mutools.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

CREDIT_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="credit",
        count=400,
        record_size=33,
        fields=(
            RecordField("kind", 1, "uint"),
            RecordField("text", 32, "text"),
        ),
        checksum_key=None,
        reference="Client/docs/research/05-formatos-de-datos.md#creditbmd",
    )
)
