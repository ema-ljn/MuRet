from mubmd.core.checks.text_checks import check_text_table, printf_specifiers
from mubmd.core.documents.text_table import TEXT_RECORD_COUNT, TEXT_RECORD_SIZE, TextTable


def test_specifiers_skip_the_percent_sign() -> None:
    assert printf_specifiers("%s chance de sucesso : %d%%") == ("%s", "%d")


def test_a_space_flag_is_part_of_the_specifier() -> None:
    assert printf_specifiers("% d puntos de Exp ganados") == ("% d",)


def test_changing_a_specifier_is_a_warning() -> None:
    records = [bytes(TEXT_RECORD_SIZE)] * TEXT_RECORD_COUNT
    records[577] = b"Aumenta %d%% de Dano".ljust(TEXT_RECORD_SIZE, b"\0")
    original = TextTable(tuple(records), "cp1252")
    edited = original.with_text(577, "Aumenta %s%% de Dano")
    findings = check_text_table(edited, original)
    assert [(finding.code, finding.severity, finding.pointer) for finding in findings] == [
        ("PRINTF_CHANGED", "warning", "/texts/577")
    ]


def test_an_unchanged_table_has_no_findings() -> None:
    table = TextTable((bytes(TEXT_RECORD_SIZE),) * TEXT_RECORD_COUNT, "cp1252")
    assert check_text_table(table, table) == ()
