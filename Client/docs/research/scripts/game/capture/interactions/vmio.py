"""Session VMware helpers, with machine-specific settings taken from the environment.

vmguest obtains the guest password from the desktop keyring. This module neither
accepts passwords nor stores them. VMware calls remain sequential and explicit.
"""

from pathlib import Path
import os
import subprocess


def root() -> Path:
    return Path(os.environ["MU_VM_OUTPUT_DIR"]).expanduser().resolve(strict=True)


def guest(operation: str, *args: str, timeout: int = 40) -> str:
    result = subprocess.run(
        ["vmguest", operation, os.environ["MU_VM_VMX"], *args],
        env=os.environ.copy(), capture_output=True, text=True, timeout=timeout,
    )
    if result.returncode:
        raise RuntimeError(operation + ": " + result.stdout + " " + result.stderr)
    return result.stdout


def push(source: Path, destination: str) -> str:
    return guest("CopyFileFromHostToGuest", str(source), destination)


def pull(source: str, name: str) -> Path:
    if Path(name).name != name:
        raise ValueError("name must be a plain output filename")
    output = root() / name
    if output.exists():
        raise FileExistsError(output)
    guest("CopyFileFromGuestToHost", source, str(output))
    return output


def guest_path(name: str) -> str:
    return os.environ["MU_VM_GUEST_DIR"].rstrip("\\") + "\\" + name


def vbs(name: str, text: str, wait: bool = True) -> str:
    if Path(name).name != name:
        raise ValueError("name must be a plain script basename")
    script = root() / (name + ".vbs")
    with script.open("xb") as output:
        output.write(text.replace("\n", "\r\n").encode("ascii"))
    script.chmod(0o600)
    destination = guest_path(script.name)
    push(script, destination)
    flags = () if wait else ("-noWait",)
    return guest("runProgramInGuest", *flags, r"C:\WINDOWS\system32\wscript.exe",
                 "//B", "//Nologo", destination)


def control(mode: str, pipe: str, command_file: str, output_name: str) -> str:
    if mode not in {"meta", "exec", "interrupt", "status"}:
        raise ValueError("unknown debugger operation")
    if Path(output_name).name != output_name:
        raise ValueError("output_name must be a plain output filename")
    output = guest_path(output_name)
    guest("runProgramInGuest", os.environ["MU_VM_CONTROL_EXE"],
          mode, pipe, command_file, output)
    return pull(output, output_name).read_text(errors="replace")
