from pathlib import Path

from nicegui import app

from mubmd.adapters.web.api import add_api
from mubmd.adapters.web.models import Models
from mubmd.adapters.web.page import add_page
from mubmd.core.ports import FileStore


def build(store: FileStore, root: Path) -> None:
    models = Models(store, root)
    add_api(app, models)
    add_page(models)
