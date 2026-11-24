"""Tests for check_pure_move.py on a throwaway git repository.

    python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_pure_move.py' -v
"""
import unittest

import check_pure_move
from scratch_repo import ScratchRepo, run_main

STATE = 'Client/src/Scenes/GuildState.h'
MOVED_STATE = 'Client/src/game/Guild/GuildState.h'
GUILD_STATE = ('#pragma once\n\n#include "Scenes/GuildMark.h"\n\nnamespace Guild\n{\nusing Rank = int;\n'
               'int MemberCount();\nint MarkSize();\n}\n')


class CheckPureMoveTest(unittest.TestCase):
    def setUp(self):
        self.repo = ScratchRepo(self)
        self.repo.write(STATE, GUILD_STATE)
        self.repo.write('Client/src/Scenes/GuildWindow.cpp', '#include "Scenes/GuildState.h"\n\nint Draw();\n')
        self.repo.write('docs/notes.md', '# Notes\n\nThe guild state lives in Scenes.\nIt has a rank.\n')
        self.base = self.repo.commit('base')

    def check(self):
        return run_main(check_pure_move.main, '--repo', str(self.repo.path), '--base', self.base, '--head', 'HEAD')

    def move_state(self, text=GUILD_STATE):
        self.repo.move(STATE, MOVED_STATE)
        self.repo.write(MOVED_STATE, text)

    def test_pure_move_passes(self):
        self.move_state(GUILD_STATE.replace('"Scenes/GuildMark.h"', '"Guild/GuildMark.h"'))
        self.repo.write('Client/src/Scenes/GuildWindow.cpp', '#include "Guild/GuildState.h"\n\nint Draw();\n')
        self.repo.commit('refactor(AR3.1): move the guild')
        code, output = self.check()
        self.assertEqual(code, 0, output)

    def test_moved_and_edited_fails(self):
        self.move_state(GUILD_STATE.replace('int MemberCount();', 'long MemberCount();'))
        self.repo.commit('refactor(AR3.1): move the guild')
        code, output = self.check()
        self.assertEqual(code, 1)
        self.assertIn('long MemberCount();', output)
        self.assertIn(MOVED_STATE, output)

    def test_dropped_line_fails(self):
        self.move_state(GUILD_STATE.replace('using Rank = int;\n', ''))
        self.repo.commit('refactor(AR3.1): move the guild')
        code, output = self.check()
        self.assertEqual(code, 1)
        self.assertIn('using Rank = int;', output)

    def test_renamed_docs_may_change(self):
        self.repo.move('docs/notes.md', 'docs/guild/notes.md')
        self.repo.write('docs/guild/notes.md', '# Notes\n\nThe guild state lives in game/Guild.\nIt has a rank.\n')
        self.repo.commit('refactor(AR3.1): move the guild notes')
        code, output = self.check()
        self.assertEqual(code, 0, output)


if __name__ == '__main__':
    unittest.main()
