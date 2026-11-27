import asyncio
import os
import socket
import sys
from contextlib import suppress
from pathlib import Path
from typing import Final
from urllib.parse import urlencode

from nicegui import app, background_tasks, ui
from nicegui.server import Server
from starlette.middleware.trustedhost import TrustedHostMiddleware

from mubmd.adapters.web.api import add_api
from mubmd.adapters.web.models import Models
from mubmd.adapters.web.page import add_page
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.ports import FileStore

HOST: Final = "127.0.0.1"
ALLOWED_HOSTS: Final = ("127.0.0.1", "localhost")
TITLE: Final = "mubmd"
LISTENING_CHECK_SECONDS: Final = 0.01


def free_port(port: int) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        if os.name == "posix":
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind((HOST, port))
        except OSError as error:
            message = f"mubmd web cannot listen on {HOST}:{port}: {error.strerror or error}"
            raise MubmdError(ErrorCode.IO_ERROR, message, suggestion="mubmd web --port 0") from error
        return int(probe.getsockname()[1])


def guard() -> None:
    app.add_middleware(TrustedHostMiddleware, allowed_hosts=list(ALLOWED_HOSTS))


def build(store: FileStore, root: Path) -> None:
    models = Models(store, root)
    add_api(app, models)
    add_page(models)


async def _say_when_listening(address: str) -> None:
    while not Server.instance.started:
        await asyncio.sleep(LISTENING_CHECK_SECONDS)
    print(f"mubmd web: {address} (Ctrl+C stops it)", file=sys.stderr, flush=True)


def serve(store: FileStore, target: Path, *, port: int, open_browser: bool) -> str:
    root, first_page = (target, "/") if target.is_dir() else (target.parent, "/?" + urlencode({"file": target.name}))
    chosen = free_port(port)
    address = f"http://{HOST}:{chosen}"
    build(store, root)
    guard()
    # NiceGUI's startup runs before the port is open.
    app.on_startup(lambda: background_tasks.create(_say_when_listening(address), name="mubmd web says it listens"))
    # uvicorn shuts down on Ctrl+C and then raises the signal again.
    with suppress(KeyboardInterrupt):
        ui.run(
            host=HOST,
            port=chosen,
            title=TITLE,
            dark=True,
            show=first_page if open_browser else False,
            reload=False,
            show_welcome_message=False,
        )
    return address
