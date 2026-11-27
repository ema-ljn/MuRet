import socket
from pathlib import Path

import pytest
from nicegui.testing import User

from mubmd.adapters.storage import LocalFileStore
from mubmd.adapters.web.server import HOST, build, free_port, guard
from mubmd.core.errors import ErrorCode, MubmdError


def test_a_taken_port_is_an_error_with_a_way_out() -> None:
    with socket.socket() as taken:
        taken.bind((HOST, 0))
        taken.listen()

        with pytest.raises(MubmdError) as error:
            free_port(taken.getsockname()[1])

    assert (error.value.code, error.value.suggestion) == (ErrorCode.IO_ERROR, "mubmd web --port 0")


def test_port_zero_takes_a_free_one() -> None:
    port = free_port(0)

    assert port > 0
    with socket.socket() as probe:
        probe.bind((HOST, port))


def test_a_free_port_is_the_one_asked_for() -> None:
    port = free_port(0)

    assert free_port(port) == port


async def test_another_host_is_refused(user: User, web_copy: Path) -> None:
    build(LocalFileStore(), web_copy)
    guard()

    answers = [
        (
            await user.http_client.get("/api/scene", params={"file": "Item/Sword01.bmd"}, headers={"Host": host})
        ).status_code
        for host in ("evil.example", "127.0.0.1:8765", "localhost:8765")
    ]

    assert answers == [400, 200, 200]
