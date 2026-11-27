from dataclasses import asdict
from typing import Final

from fastapi import FastAPI, Request, Response
from fastapi.responses import JSONResponse

from mutools.adapters.web.models import Models
from mutools.app.model_scene import action_poses, model_scene, rest_bounds
from mutools.app.textures import mesh_texture
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.texture_images import RgbaImage, png_of

OWN: Final = "own"
DONOR: Final = "donor"
NO_STORE: Final = {"Cache-Control": "no-store"}
NOT_FOUND: Final = frozenset({ErrorCode.FILE_NOT_FOUND, ErrorCode.POINTER_NOT_FOUND, ErrorCode.UNKNOWN_FORMAT})
NOT_FOUND_STATUS: Final = 404
REFUSED_STATUS: Final = 400


def _skeleton_file(file: str, on: str, donor: str | None) -> str:
    if on == OWN:
        return file
    if on != DONOR:
        raise MutoolsError(ErrorCode.INVALID_VALUE, f"on is {OWN!r} or {DONOR!r}, not {on!r}")
    if donor is None:
        raise MutoolsError(ErrorCode.INVALID_VALUE, f"{file} has no skeleton to borrow")
    return donor


def add_api(app: FastAPI, models: Models) -> None:
    @app.exception_handler(MutoolsError)
    def refused(_: Request, error: MutoolsError) -> JSONResponse:
        status = NOT_FOUND_STATUS if error.code in NOT_FOUND else REFUSED_STATUS
        return JSONResponse({"code": error.code, "message": error.message}, status, NO_STORE)

    @app.get("/api/scene")
    def scene(file: str = "", on: str = OWN) -> JSONResponse:
        view = models.session(file).view
        donor = models.donor(file)
        skeleton = _skeleton_file(file, on, donor)
        posed = None if skeleton == file else models.session(skeleton).view
        bounds = rest_bounds(view, posed)
        answer = {
            "file": file,
            "skeleton": skeleton,
            "donor": donor,
            **asdict(model_scene(view, posed)),
            "bounds": None if bounds is None else asdict(bounds),
        }
        return JSONResponse(answer, headers=NO_STORE)

    @app.get("/api/poses")
    def poses(file: str = "", action: int = 0) -> JSONResponse:
        return JSONResponse(asdict(action_poses(models.session(file).view, action)), headers=NO_STORE)

    @app.get("/api/texture")
    def texture(file: str = "", mesh: int = 0) -> Response:
        session = models.session(file)
        content = mesh_texture(models.store, session.path, session.view, mesh).content
        if isinstance(content, RgbaImage):
            size = {"X-Width": str(content.width), "X-Height": str(content.height)}
            return Response(content.pixels, media_type="application/octet-stream", headers=NO_STORE | size)
        return Response(content, media_type="image/jpeg", headers=NO_STORE)

    @app.get("/api/swatch")
    def swatch(file: str = "", mesh: int = 0) -> Response:
        session = models.session(file)
        content = mesh_texture(models.store, session.path, session.view, mesh).content
        if isinstance(content, RgbaImage):
            return Response(png_of(content), media_type="image/png", headers=NO_STORE)
        return Response(content, media_type="image/jpeg", headers=NO_STORE)
