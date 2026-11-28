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
from starlette.datastructures import Headers
from starlette.middleware.trustedhost import TrustedHostMiddleware
from starlette.responses import PlainTextResponse
from starlette.types import ASGIApp, Receive, Scope, Send

from mutools.adapters.web.api import add_api
from mutools.adapters.web.models import Models
from mutools.adapters.web.page import add_page
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.ports import FileStore

HOST: Final = "127.0.0.1"
ALLOWED_HOSTS: Final = ("127.0.0.1", "localhost")
TITLE: Final = "mutools"
LISTENING_CHECK_SECONDS: Final = 0.01
FORBIDDEN: Final = 403
POLICY_VIOLATION: Final = 1008
GUARDED_SCOPES: Final = frozenset({"http", "websocket"})


def free_port(port: int) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        if os.name == "posix":
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind((HOST, port))
        except OSError as error:
            message = f"mutools web cannot listen on {HOST}:{port}: {error.strerror or error}"
            raise MutoolsError(ErrorCode.IO_ERROR, message, suggestion="mutools web --port 0") from error
        return int(probe.getsockname()[1])


class SameOrigin:
    """Refuses what a page of another site asks, because NiceGUI's socket takes any origin."""

    def __init__(self, inner: ASGIApp, origins: frozenset[str]) -> None:
        self._inner = inner
        self._origins = origins

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        origin = Headers(scope=scope).get("origin") if scope["type"] in GUARDED_SCOPES else None
        if origin is None or origin in self._origins:
            await self._inner(scope, receive, send)
        elif scope["type"] == "websocket":
            await send({"type": "websocket.close", "code": POLICY_VIOLATION})
        else:
            await PlainTextResponse(f"mutools web answers only its own pages, not {origin}", FORBIDDEN)(
                scope, receive, send
            )


def guard(port: int) -> None:
    app.add_middleware(SameOrigin, origins=frozenset(f"http://{host}:{port}" for host in ALLOWED_HOSTS))
    app.add_middleware(TrustedHostMiddleware, allowed_hosts=list(ALLOWED_HOSTS))


def build(store: FileStore, root: Path, *, edit: bool = False) -> None:
    models = Models(store, root, edit=edit)
    add_api(app, models)
    add_page(models)


async def _say_when_listening(address: str) -> None:
    while not Server.instance.started:
        await asyncio.sleep(LISTENING_CHECK_SECONDS)
    print(f"mutools web: {address} (Ctrl+C stops it)", file=sys.stderr, flush=True)


def serve(store: FileStore, target: Path, *, port: int, open_browser: bool, edit: bool = False) -> str:
    root, first_page = (target, "/") if target.is_dir() else (target.parent, "/?" + urlencode({"file": target.name}))
    chosen = free_port(port)
    address = f"http://{HOST}:{chosen}"
    build(store, root, edit=edit)
    guard(chosen)
    # NiceGUI's startup runs before the port is open.
    app.on_startup(lambda: background_tasks.create(_say_when_listening(address), name="mutools web says it listens"))
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
