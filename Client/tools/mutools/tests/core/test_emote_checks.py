from mutools.core.checks.emote_checks import original_triggers, taken_triggers
from mutools.core.documents.text_table import TEXT_RECORD_COUNT, TEXT_RECORD_SIZE
from mutools.core.formats.emote_table import AddedEmote


def records(**texts: bytes) -> tuple[bytes, ...]:
    filled = [b"unused".ljust(TEXT_RECORD_SIZE, b"\0")] * TEXT_RECORD_COUNT
    for name, text in texts.items():
        filled[int(name.removeprefix("text"))] = text.ljust(TEXT_RECORD_SIZE, b"\0")
    return tuple(filled)


def taken(*triggers: str, texts: tuple[bytes, ...] | None = None) -> list[tuple[str, str]]:
    emote = AddedEmote(205, 205, 0.32, "loop", triggers)
    findings = taken_triggers(emote, original_triggers(texts), "cp1252", "/emotes/0")
    return [(finding.code, finding.pointer) for finding in findings]


def test_the_original_has_95_triggers() -> None:
    assert len(original_triggers(records())) == 95


def test_a_trigger_that_holds_a_text_the_original_looks_for_is_taken() -> None:
    assert taken("wave", "Thanks", texts=records(text279=b"T")) == [("TRIGGER_TAKEN", "/emotes/0/triggers/1")]


def test_the_comparison_tells_capitals_apart() -> None:
    assert taken("thanks", texts=records(text279=b"T")) == []


def test_main_exes_triggers_are_looked_for_without_text_bmd() -> None:
    assert taken("a;b", "^^") == [("TRIGGER_TAKEN", "/emotes/0/triggers/0"), ("TRIGGER_TAKEN", "/emotes/0/triggers/1")]


def test_an_empty_text_takes_every_trigger() -> None:
    assert taken("wave", texts=records(text783=b"")) == [("TRIGGER_TAKEN", "/emotes/0/triggers/0")]


def test_a_text_outside_the_search_takes_none() -> None:
    assert taken("wave", texts=records(text269=b"wave")) == []
