from pathlib import Path

from tests.adapters.cli.conftest import Cli


def test_find_pages(cli: Cli, text: Path) -> None:
    everything = cli("find", text, "usuario", "--limit", "1000").json["data"]
    page = cli("find", text, "usuario", "--limit", "2", "--offset", "2").json["data"]
    assert page["items"] == everything["items"][2:4]
    assert (page["total"], page["next_offset"]) == (everything["total"], 4)


def test_fields_keep_only_the_named_keys(cli: Cli, text: Path) -> None:
    items = cli("find", text, "usuario", "--fields", "index,pointer").json["data"]["items"]
    assert items[0] == {"index": items[0]["index"], "pointer": items[0]["pointer"]}


def test_fields_of_a_single_object(cli: Cli, text: Path) -> None:
    assert cli("info", text, "--fields", "records,used_records").json["data"] == {"records": 2048, "used_records": 1645}


def test_an_unknown_field_is_usage(cli: Cli, text: Path) -> None:
    run = cli("find", text, "usuario", "--fields", "index,colour")
    assert run.code == 2
    assert run.error["code"] == "USAGE"


def test_get_says_when_it_cut_a_list(cli: Cli, text: Path) -> None:
    data = cli("get", text, "/texts", "--limit", "3").json["data"]
    assert (len(data["value"]), data["truncated"]) == (3, True)


def test_fields_work_when_nothing_matches(cli: Cli, text: Path) -> None:
    run = cli("find", text, "no text says this", "--fields", "pointer")
    assert run.code == 0
    assert run.json["data"] == {"items": [], "total": 0, "next_offset": None}


def test_an_unknown_field_names_the_real_ones(cli: Cli, text: Path) -> None:
    error = cli("find", text, "no text says this", "--fields", "colour").error
    assert error["suggestion"] == "--fields index,pointer,text"


def test_get_reads_a_slice_of_a_list(cli: Cli, text: Path) -> None:
    data = cli("get", text, "/texts", "--offset", "500", "--limit", "2").json["data"]
    assert data["value"] == ["El usuario salió del juego", "El usuario ya está en otra party"]
    assert data["truncated"] is True


def test_get_with_an_offset_needs_a_list(cli: Cli, text: Path) -> None:
    run = cli("get", text, "/texts/500", "--offset", "1")
    assert (run.code, run.error["code"]) == (2, "USAGE")
