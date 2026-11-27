import hashlib
import re
from pathlib import Path

import pytest

from mutools.adapters import web

WEB = Path(web.__file__).parent
VENDORED = WEB / "studio" / "vendor" / "three"
THREE_MODULE = WEB / "three" / "index.js"
PINNED = {
    "TransformControls.js": "151befe25bb0d68626f9a6b033625b7b0cf6848e39ba71623b7bed8e3df565d6",
    "LICENSE": "8b378ebe60e2fe500158cb0ac71cb5e8b7d92953c2abcc63a0eb90499653b5bc",
}
IMPORTED = re.compile(r"^import \{([^}]*)\} from 'three';", re.MULTILINE)
EXPORTED = re.compile(r"^export const \{([^}]*)\} = THREE;", re.MULTILINE)


def names(pattern: re.Pattern[str], path: Path) -> set[str]:
    (listed,) = pattern.findall(path.read_text(encoding="utf-8"))
    return {name.strip() for name in listed.split(",") if name.strip()}


@pytest.mark.parametrize(("name", "sha256"), PINNED.items())
def test_the_vendored_file_is_three_0_187_0s_byte_for_byte(name: str, sha256: str) -> None:
    assert hashlib.sha256((VENDORED / name).read_bytes()).hexdigest() == sha256


def test_the_three_module_gives_every_name_the_vendored_file_imports() -> None:
    imported = names(IMPORTED, VENDORED / "TransformControls.js")

    assert imported <= names(EXPORTED, THREE_MODULE)
    assert len(imported) == 20
