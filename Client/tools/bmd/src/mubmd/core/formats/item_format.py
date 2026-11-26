from typing import Final

from mubmd.core.documents.item_table import (
    ITEM_CHECKSUM_KEY,
    ITEM_COUNT,
    ITEM_NAME_SIZE,
    ITEM_RECORD_SIZE,
    ITEM_SECTION_SIZE,
)
from mubmd.core.formats.record_fields import RecordField
from mubmd.core.formats.record_table_format import RecordTableFormat, RecordTableSchema

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
            RecordField("equip_slot", 1, "uint"),
            RecordField("raw_0x23", 1, "bytes"),
            RecordField("width", 1, "uint"),
            RecordField("height", 1, "uint"),
            RecordField("damage_min", 1, "uint"),
            RecordField("damage_max", 1, "uint"),
            RecordField("defense_rate", 1, "uint"),
            RecordField("defense", 1, "uint"),
            RecordField("magic_defense", 1, "uint"),
            RecordField("attack_speed", 1, "uint"),
            RecordField("walk_speed", 1, "uint"),
            RecordField("durability", 1, "uint"),
            RecordField("magic_durability", 1, "uint"),
            RecordField("magic_power", 1, "uint"),
            RecordField("required_strength", 2, "uint"),
            RecordField("required_agility", 2, "uint"),
            RecordField("required_energy", 2, "uint"),
            RecordField("required_vitality", 2, "uint"),
            RecordField("required_leadership", 2, "uint"),
            RecordField("required_level", 2, "uint"),
            RecordField("value", 1, "uint"),
            RecordField("raw_0x3d", 3, "bytes"),
            RecordField("price", 4, "int"),
            RecordField("stat_kind", 1, "uint"),
            RecordField("classes", 1, "uint", count=5),
            RecordField("resistances", 1, "uint", count=7),
            RecordField("raw_0x51", 3, "bytes"),
        ),
        checksum_key=ITEM_CHECKSUM_KEY,
        reference="Client/docs/research/05-formatos-de-datos.md#tablas-bmd",
        section_size=ITEM_SECTION_SIZE,
    )
)
