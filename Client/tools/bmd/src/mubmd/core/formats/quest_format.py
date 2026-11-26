from typing import Final

from mubmd.core.formats.record_fields import RecordField, struct
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

QUEST_ENTRY_COUNT: Final = 16
QUEST_CONDITION_COUNT: Final = 16

QUEST_FORMAT: Final = RecordTableFormat(
    RecordTableSchema(
        name="quest",
        count=200,
        record_size=0x2A8,
        fields=(
            RecordField("entry_count", 2, "int", highest=QUEST_ENTRY_COUNT),
            RecordField("condition_count", 2, "int", highest=QUEST_CONDITION_COUNT),
            RecordField("npc_type", 1, "uint"),
            RecordField("name", 32, "text"),
            RecordField("raw_0x25", 1, "bytes"),
            struct(
                "entries",
                (
                    RecordField("raw_0x00", 1, "bytes"),
                    RecordField("kind", 1, "uint"),
                    RecordField("item_group", 1, "uint"),
                    RecordField("item_index", 1, "uint"),
                    RecordField("item_level", 1, "uint"),
                    RecordField("item_count", 1, "uint"),
                    RecordField("condition", 1, "uint"),
                    RecordField("classes", 1, "uint", count=5),
                    RecordField("dialogs", 2, "int", count=4),
                ),
                count=QUEST_ENTRY_COUNT,
            ),
            RecordField("raw_0x166", 2, "bytes"),
            struct(
                "conditions",
                (
                    RecordField("raw_0x00", 1, "bytes"),
                    RecordField("link", 1, "uint"),
                    RecordField("need_quest", 2, "int"),
                    RecordField("min_level", 2, "uint"),
                    RecordField("max_level", 2, "uint"),
                    RecordField("raw_0x08", 4, "bytes"),
                    RecordField("zen", 4, "uint"),
                    RecordField("fail_dialog", 2, "int"),
                    RecordField("raw_0x12", 2, "bytes"),
                ),
                count=QUEST_CONDITION_COUNT,
            ),
        ),
        checksum_key=None,
        reference="Client/src/game/Tables/QuestTable.h",
    )
)
