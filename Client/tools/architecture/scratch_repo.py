"""A throwaway git repository for the tests of the refactor tools."""
import contextlib
import io
import pathlib
import subprocess
import tempfile


class ScratchRepo:
    def __init__(self, test):
        temporary = tempfile.TemporaryDirectory()
        test.addCleanup(temporary.cleanup)
        self.path = pathlib.Path(temporary.name)
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.com')
        self.git('config', 'commit.gpgsign', 'false')

    def git(self, *args):
        return subprocess.run(['git', '-C', str(self.path), *args], capture_output=True, text=True,
                              check=True).stdout.strip()

    def write(self, relative, text):
        path = self.path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def read(self, relative):
        return (self.path / relative).read_text(encoding='utf-8')

    def exists(self, relative):
        return (self.path / relative).exists()

    def move(self, source, target):
        (self.path / target).parent.mkdir(parents=True, exist_ok=True)
        self.git('mv', source, target)

    def commit(self, subject):
        self.git('add', '-A')
        self.git('commit', '-q', '--allow-empty', '-m', subject)
        return self.git('rev-parse', 'HEAD')


def run_main(main, *args):
    """Runs a tool's main(argv) and returns its exit code and what it printed."""
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        code = main(list(args))
    return code, output.getvalue()
