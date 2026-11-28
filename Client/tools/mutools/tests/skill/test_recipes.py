import json
import re
import shlex
import shutil
import subprocess
import sys
from collections.abc import Iterator
from functools import partial
from itertools import takewhile
from operator import ne
from pathlib import Path
from string import Template

import pytest

from mutools.adapters.cli.describe import BRIEF

REPO = Path(__file__).resolve().parents[5]
SKILL = REPO / ".claude" / "skills" / "muret-bmd" / "SKILL.md"
MUTOOLS = Path(sys.executable).parent / "mutools"
RECIPE = re.compile(r"<!-- recipe: (?P<name>[\w-]+) -->\n+```bash\n(?P<body>.*?)```", re.DOTALL)
EXPECTED_EXIT = re.compile(r"#\s*exit (\d+)\s*$")
HEREDOC = re.compile(r"\s*<<'(\w+)'\s*$")
PLAYER = Path("player") / "Player.bmd"


def recipes() -> dict[str, list[str]]:
    text = SKILL.read_text(encoding="utf-8")
    return {match["name"]: match["body"].strip().splitlines() for match in RECIPE.finditer(text)}


def commands(lines: list[str]) -> Iterator[tuple[str, str | None]]:
    rows = iter(lines)
    for line in rows:
        heredoc = HEREDOC.search(line)
        if heredoc is not None:
            yield line[: heredoc.start()], "\n".join(takewhile(partial(ne, heredoc[1]), rows))
        elif line.strip() and not line.lstrip().startswith("#"):
            yield line, None


def run_line(line: str, variables: dict[str, str], stdin: str | None) -> None:
    expected = EXPECTED_EXIT.search(line)
    command = [Template(word).substitute(variables) for word in shlex.split(line, comments=True)]
    assert command[0] == "mutools", line
    result = subprocess.run(
        [str(MUTOOLS), *command[1:]], input=stdin, capture_output=True, text=True, encoding="utf-8", check=False
    )
    assert result.returncode == (int(expected[1]) if expected else 0), f"{line}\n{result.stdout}\n{result.stderr}"
    envelope = json.loads(result.stdout)
    assert envelope["ok"] is True
    assert envelope["schema"] == 1


def test_the_skill_has_recipes() -> None:
    assert len(recipes()) >= 6


@pytest.mark.parametrize("name", sorted(recipes()) if SKILL.exists() else [])
def test_every_recipe_runs(name: str, model_copy: Path, reference_data: Path, tmp_path: Path) -> None:
    shutil.copyfile(reference_data / PLAYER, model_copy / PLAYER)
    variables = {"WORK": str(model_copy), "REF": str(reference_data), "TMP": str(tmp_path)}
    for line, stdin in commands(recipes()[name]):
        run_line(line, variables, stdin)


def test_brief_matches_the_skill() -> None:
    body = SKILL.read_text(encoding="utf-8").split("---", 2)[2]
    paragraphs = [block for block in body.strip().split("\n\n") if not block.startswith("#")]
    assert " ".join(paragraphs[0].split()) == BRIEF
