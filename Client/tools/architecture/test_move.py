"""Tests for move.py on a throwaway git repository with the client's layout.

    python3 -I -m unittest discover -s Client/tools/architecture -p 'test_move.py' -v
"""
import unittest

import move
from scratch_repo import ScratchRepo, run_main

MAP = 'Client/docs/research/coverage/module-map.tsv'
BASELINE = 'Client/tools/architecture/layers-baseline.tsv'
NAMED_VALUES = 'Client/tools/architecture/named-values.tsv'
NAMED_VALUES_BASELINE = 'Client/tools/architecture/named-values-baseline.tsv'
MOVES = 'Client/docs/research/coverage/referencia/entradas/moves.tsv'
WINDOW = 'Client/src/Scenes/GuildWindow.cpp'
WINDOW_TEXT = '#include "Scenes/GuildState.h"\n#include "Core/Expected.h"\n\nint Draw(); // Scenes/GuildState.h\n'
LAB = 'labs/cl5.0-pipeline/gl/LabGuild.h'
LAB_TEXT = '#include "LabGl.h"\n#include "Scenes/GuildState.h"\n'


class MoveTest(unittest.TestCase):
    def setUp(self):
        self.repo = ScratchRepo(self)
        self.repo.write('Client/src/Scenes/GuildState.h', '#pragma once\n\n#include "Core/Expected.h"\n\nint Rank();\n')
        self.repo.write(WINDOW, WINDOW_TEXT)
        self.repo.write('Client/src/Core/Expected.h', '#pragma once\n')
        self.repo.write('Client/tests/scenes/test_guild.cpp', '#include "Scenes/GuildState.h"\n')
        self.repo.write(LAB, LAB_TEXT)
        self.repo.write('Client/CMakeLists.txt',
                        'set_source_files_properties(src/Scenes/GuildState.h PROPERTIES HEADER_FILE_ONLY ON)\n'
                        'list(FILTER sources EXCLUDE REGEX "/src/Scenes/GuildStateX\\\\.h$")\n')
        self.repo.write('.github/workflows/ci.yml', 'paths: [Client/src/Core/Expected.h]\n')
        self.repo.write(MAP, 'file\tlines\tcurrent_folder\ttarget\tconfidence\twhy\n'
                             'Core/Expected.h\t1\tCore\tengine/Core\thigh\t\n'
                             'Scenes/GuildState.h\t5\tScenes\tgame/Guild\thigh\t\n'
                             'Scenes/GuildWindow.cpp\t4\tScenes\tgame/Guild/Window\tmedium\t\n')
        self.repo.write(BASELINE, 'file\tinclude\tfrom_module\tto_module\n'
                                  'Core/Expected.h\tScenes/GuildState.h\tengine/Core\tgame/Guild\n')
        self.repo.write(NAMED_VALUES, 'space\thome\tmatch\tpolicy\tnote\n'
                                      'rank\tScenes/GuildState.h\tname=.*Rank\twatch\t\n'
                                      'opcode\tgame/Protocol\tname=.*Opcode\thome-only\t\n')
        self.repo.write(NAMED_VALUES_BASELINE, 'space\tfile\tname\tvalue\n'
                                               'rank\tCore/Expected.h\tFirstRank\t0x1\n'
                                               'rank\tScenes/GuildWindow.cpp\tFirstRank\t0x1\n')
        self.repo.write(MOVES, 'commit\tfrom\tto\n')
        self.repo.write('.git-blame-ignore-revs', '# Commits that git blame skips.\n')
        self.repo.commit('base')

    def move(self, *args):
        return run_main(move.main, '--repo', str(self.repo.path), *args)

    def test_move_rewrites_includes_across_folders(self):
        code, output = self.move('--modules', 'game/Guild')
        self.assertEqual(code, 0, output)
        self.assertFalse(self.repo.exists('Client/src/Scenes/GuildState.h'))
        self.assertTrue(self.repo.exists('Client/src/game/Guild/GuildState.h'))
        self.assertTrue(self.repo.exists('Client/src/game/Guild/Window/GuildWindow.cpp'))
        self.assertEqual(self.repo.read('Client/src/game/Guild/Window/GuildWindow.cpp'),
                         WINDOW_TEXT.replace('#include "Scenes/GuildState.h"', '#include "Guild/GuildState.h"'))
        self.assertEqual(self.repo.read('Client/tests/scenes/test_guild.cpp'), '#include "Guild/GuildState.h"\n')
        self.assertIn('Core/Expected.h\t1\tCore\tengine/Core', self.repo.read(MAP))
        self.assertIn('game/Guild/GuildState.h\t5\tgame/Guild\tgame/Guild\t', self.repo.read(MAP))
        self.assertIn('Core/Expected.h\tgame/Guild/GuildState.h\tengine/Core\tgame/Guild', self.repo.read(BASELINE))
        self.assertEqual(self.repo.read(MOVES).splitlines()[1:],
                         ['\tClient/src/Scenes/GuildState.h\tClient/src/game/Guild/GuildState.h',
                          '\tClient/src/Scenes/GuildWindow.cpp\tClient/src/game/Guild/Window/GuildWindow.cpp'])
        cmake = self.repo.read('Client/CMakeLists.txt')
        self.assertIn('src/game/Guild/GuildState.h PROPERTIES', cmake)
        self.assertIn('GuildStateX', cmake)
        self.assertIn('Client/CMakeLists.txt:1', output)
        self.assertIn('/src/Scenes/', output)
        self.assertIn('Client/src/game is new', output)

    def test_move_rewrites_includes_in_the_labs(self):
        code, output = self.move('--modules', 'game/Guild')
        self.assertEqual(code, 0, output)
        self.assertEqual(self.repo.read(LAB), '#include "LabGl.h"\n#include "Guild/GuildState.h"\n')

    def test_move_keeps_includes_when_only_root_changes(self):
        code, output = self.move('--root', 'engine')
        self.assertEqual(code, 0, output)
        self.assertTrue(self.repo.exists('Client/src/engine/Core/Expected.h'))
        self.assertEqual(self.repo.read(WINDOW), WINDOW_TEXT)
        self.assertIn('#include "Core/Expected.h"', self.repo.read('Client/src/Scenes/GuildState.h'))
        self.assertIn('engine/Core/Expected.h\t1\tengine/Core\tengine/Core', self.repo.read(MAP))
        self.assertEqual(self.repo.read('.github/workflows/ci.yml'), 'paths: [Client/src/engine/Core/Expected.h]\n')

    def test_move_rewrites_the_named_values_files(self):
        code, output = self.move('--modules', 'game/Guild')
        self.assertEqual(code, 0, output)
        self.assertEqual(self.repo.read(NAMED_VALUES_BASELINE).splitlines()[1:],
                         ['rank\tCore/Expected.h\tFirstRank\t0x1',
                          'rank\tgame/Guild/Window/GuildWindow.cpp\tFirstRank\t0x1'])
        self.assertEqual([line.split('\t')[1] for line in self.repo.read(NAMED_VALUES).splitlines()[1:]],
                         ['game/Guild/GuildState.h', 'game/Protocol'])

    def test_move_keeps_the_named_values_baseline_sorted(self):
        code, output = self.move('--root', 'engine')
        self.assertEqual(code, 0, output)
        self.assertEqual(self.repo.read(NAMED_VALUES_BASELINE).splitlines()[1:],
                         ['rank\tScenes/GuildWindow.cpp\tFirstRank\t0x1',
                          'rank\tengine/Core/Expected.h\tFirstRank\t0x1'])

    def test_record_adds_the_squash(self):
        self.move('--modules', 'game/Guild')
        squash = self.repo.commit('refactor(AR3.1): move the guild to Client/src/game (#500)')
        code, output = self.move('--record', squash)
        self.assertEqual(code, 0, output)
        self.assertTrue(self.repo.read('.git-blame-ignore-revs').endswith(
            f'\n# refactor(AR3.1): move the guild to Client/src/game (#500)\n{squash}\n'))
        self.assertTrue(all(line.startswith(squash + '\t') for line in self.repo.read(MOVES).splitlines()[1:]))

    def test_unknown_module_fails(self):
        code, output = self.move('--modules', 'game/Nowhere')
        self.assertEqual(code, 1)
        self.assertIn('game/Nowhere', output)


if __name__ == '__main__':
    unittest.main()
