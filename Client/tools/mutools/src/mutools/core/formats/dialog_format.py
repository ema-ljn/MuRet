from typing import Final

from mutools.core.formats.record_fields import RecordField
from mutools.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

DIALOG_ANSWER_COUNT: Final = 10

DIALOG_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="dialog",
        count=200,
        record_size=0x400,
        fields=(
            RecordField("text", 0x12C, "text"),
            RecordField("answer_count", 4, "int", highest=DIALOG_ANSWER_COUNT),
            RecordField("next", 4, "int", count=DIALOG_ANSWER_COUNT),
            RecordField("actions", 4, "int", count=DIALOG_ANSWER_COUNT),
            RecordField("answers", 0x40, "text", count=DIALOG_ANSWER_COUNT),
        ),
        checksum_key=None,
        reference="Client/src/game/Tables/DialogTable.h",
    )
)
