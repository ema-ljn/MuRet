"""Tests for the commit waves and the blame of build_roadmap_matrix.py: a refactor commit does not change anyone's wave.

    python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_commit_waves.py' -v
"""
import os
import subprocess
import tempfile
import unittest

from build_roadmap_matrix import MuRet

GUILD = 'Client/src/Scenes/Guild.cpp'
MOVED_GUILD = 'Client/src/game/Guild/Guild.cpp'
MARK = 0x4D3200
IGNORE_REVS = '.git-blame-ignore-revs'


def guild_source(include, citation):
    body = ''.join(f'int guildValue{i} = {i};\n' for i in range(12))
    return f'#include "{include}"\n\n{body}// 0x4d3200: {citation}\nint guildMark = 0;\n'


class EveryAddressIsAFunction:
    def containing(self, address):
        return address


class ScratchRepo:
    def __init__(self, path):
        self.path = path
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.com')
        self.git('config', 'commit.gpgsign', 'false')

    def git(self, *args):
        return subprocess.run(['git', '-C', self.path, *args], capture_output=True, text=True,
                              check=True).stdout.strip()

    def write(self, relative, text):
        path = os.path.join(self.path, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8') as fh:
            fh.write(text)

    def move(self, source, target):
        os.makedirs(os.path.dirname(os.path.join(self.path, target)), exist_ok=True)
        self.git('mv', source, target)

    def commit(self, subject):
        self.git('add', '-A')
        self.git('commit', '-q', '--allow-empty', '-m', subject)
        return self.git('rev-parse', 'HEAD')

    def measure(self):
        return MuRet(self.path, 'HEAD', EveryAddressIsAFunction())


class CommitWavesTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.repo = ScratchRepo(folder.name)

    def test_ar_commit_does_not_inherit_wave(self):
        port = self.repo.commit('CL9 hito 2: the guild window')
        refactor = self.repo.commit('refactor(AR1.2): move the engine to Client/src/engine')
        fix = self.repo.commit('fix: a typo')
        waves = self.repo.measure().commit_wave
        self.assertEqual([waves[port], waves[refactor], waves[fix]], ['CL9', 'AR', 'CL9'])

    def test_ar_scope_wins_over_a_wave_token(self):
        refactor = self.repo.commit('refactor(AR4.2): move the CL9 guild code')
        self.assertEqual(self.repo.measure().commit_wave[refactor], 'AR')

    def test_angular_wave_scope(self):
        port = self.repo.commit('feat(CL11): the effect models')
        self.assertEqual(self.repo.measure().commit_wave[port], 'CL11')


class BlameFollowsMovesTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.repo = ScratchRepo(folder.name)
        self.repo.write(GUILD, guild_source('Scenes/Guild.h', 'the guild mark'))
        self.repo.commit('CL7 hito 3: the guild mark')
        self.repo.commit('CL9 hito 1: the trade window')
        self.repo.move(GUILD, MOVED_GUILD)
        self.repo.write(MOVED_GUILD, guild_source('Guild/Guild.h', 'guild mark'))
        self.move = self.repo.commit('refactor(AR1.2): move the guild to game/Guild')

    def test_blame_ignores_listed_move(self):
        self.repo.write(IGNORE_REVS, f'# AR1.2 move\n{self.move}\n')
        self.repo.commit('docs(AR1.2): record the move')
        measured = self.repo.measure()
        self.assertEqual(dict(measured.impl[MARK]), {'CL7': 1})
        self.assertEqual(measured.warnings, [])

    def test_unlisted_move_warns(self):
        measured = self.repo.measure()
        self.assertEqual(len(measured.warnings), 1)
        self.assertTrue(measured.warnings[0].startswith('citation-attributed-to-AR'))
        self.assertIn(MOVED_GUILD, measured.warnings[0])
        self.assertIn(self.move[:7], measured.warnings[0])

    def test_unlisted_move_keeps_the_last_port_wave(self):
        self.assertEqual(dict(self.repo.measure().impl[MARK]), {'CL9': 1})

    def test_a_bad_ignore_line_stops_the_matrix(self):
        self.repo.write(IGNORE_REVS, 'not-a-commit\n')
        self.repo.commit('docs(AR1.2): record the move by hand')
        with self.assertRaises(SystemExit):
            self.repo.measure()


if __name__ == '__main__':
    unittest.main()
