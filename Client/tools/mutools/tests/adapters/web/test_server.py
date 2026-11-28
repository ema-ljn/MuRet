import re
import socket
from collections.abc import Iterator, MutableMapping
from pathlib import Path
from typing import Any

import httpx2
import pytest
from nicegui import app, ui
from nicegui.testing.general import nicegui_reset_globals

from mutools.adapters.storage import LocalFileStore
from mutools.adapters.web.server import HOST, SameOrigin, free_port, serve
from mutools.core.errors import ErrorCode, MutoolsError


@pytest.fixture
def run_asked(monkeypatch: pytest.MonkeyPatch) -> Iterator[dict[str, object]]:
    """What `ui.run` was asked, with a `ui.run` that ends at once as on Ctrl+C, on an app that has not started."""
    asked: dict[str, object] = {}

    def stopped_with_ctrl_c(**arguments: object) -> None:
        asked.update(arguments)
        raise KeyboardInterrupt

    monkeypatch.setattr(ui, "run", stopped_with_ctrl_c)
    with nicegui_reset_globals():
        yield asked


def test_a_taken_port_is_an_error_with_a_way_out() -> None:
    with socket.socket() as taken:
        taken.bind((HOST, 0))
        taken.listen()

        with pytest.raises(MutoolsError) as error:
            free_port(taken.getsockname()[1])

    assert (error.value.code, error.value.suggestion) == (ErrorCode.IO_ERROR, "mutools web --port 0")


def test_port_zero_takes_a_free_one() -> None:
    port = free_port(0)

    assert port > 0
    with socket.socket() as probe:
        probe.bind((HOST, port))


def test_a_free_port_is_the_one_asked_for() -> None:
    port = free_port(0)

    assert free_port(port) == port


@pytest.mark.usefixtures("run_asked")
def test_ctrl_c_ends_the_server_with_its_address(web_copy: Path) -> None:
    address = serve(LocalFileStore(), web_copy, port=0, open_browser=False)

    assert re.fullmatch(r"http://127\.0\.0\.1:\d+", address)


def test_the_server_listens_on_this_machine_only(web_copy: Path, run_asked: dict[str, object]) -> None:
    serve(LocalFileStore(), web_copy, port=0, open_browser=False)

    assert run_asked["host"] == "127.0.0.1"


@pytest.mark.usefixtures("run_asked")
async def test_the_server_refuses_another_host(web_copy: Path) -> None:
    serve(LocalFileStore(), web_copy, port=0, open_browser=False)

    async with httpx2.AsyncClient(transport=httpx2.ASGITransport(app), base_url="http://test") as client:
        answers = [
            (await client.get("/api/scene", params={"file": "Item/Sword01.bmd"}, headers={"Host": host})).status_code
            for host in ("evil.example", "127.0.0.1:8765", "localhost:8765")
        ]

    assert answers == [400, 200, 200]


@pytest.mark.usefixtures("run_asked")
async def test_the_server_refuses_a_page_of_another_site(web_copy: Path) -> None:
    address = serve(LocalFileStore(), web_copy, port=0, open_browser=False)

    async with httpx2.AsyncClient(transport=httpx2.ASGITransport(app), base_url=address) as client:
        answers = [
            (await client.get("/api/scene", params={"file": "Item/Sword01.bmd"}, headers=origin)).status_code
            for origin in ({"Origin": "http://evil.example"}, {"Origin": "null"}, {"Origin": address}, {})
        ]

    assert answers == [403, 403, 200, 200]


async def test_a_socket_from_another_site_is_closed_before_it_opens() -> None:
    sent: list[MutableMapping[str, Any]] = []
    reached: list[bool] = []

    async def inner(scope: Any, receive: Any, send: Any) -> None:
        reached.append(True)

    async def send(message: MutableMapping[str, Any]) -> None:
        sent.append(message)

    async def receive() -> MutableMapping[str, Any]:
        return {"type": "websocket.connect"}

    guarded = SameOrigin(inner, frozenset({"http://127.0.0.1:8765"}))
    scope = {"type": "websocket", "headers": [(b"origin", b"http://evil.example")]}

    await guarded(scope, receive, send)

    assert (sent, reached) == ([{"type": "websocket.close", "code": 1008}], [])
