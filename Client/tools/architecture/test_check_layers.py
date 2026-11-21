"""Tests for check_layers.py on small made-up source trees.

    python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_layers.py' -v
"""
import contextlib
import io
import pathlib
import tempfile
import unittest

import check_layers

MODULE_COLUMNS = ['module', 'root', 'layer', 'folders_today', 'may_use', 'note']
MAP_COLUMNS = ['file', 'lines', 'current_folder', 'target', 'confidence', 'why']
BASELINE_COLUMNS = ['file', 'include', 'from_module', 'to_module']


def write_tsv(path, columns, rows):
    lines = ['\t'.join(columns)] + ['\t'.join(row.get(column, '') for column in columns) for row in rows]
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def module(name, layer, folders, may_use=None):
    return {'module': name, 'root': name.split('/')[0], 'layer': str(layer), 'folders_today': folders,
            'may_use': f'*layer<{layer}' if may_use is None else may_use}


class CheckLayersTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.dir = pathlib.Path(temporary.name)
        self.src = self.dir / 'src'
        self.src.mkdir()
        self.modules = [
            module('engine/Core', 0, 'Core/**'),
            module('game/Guild', 9, 'Guild/**'),
            module('game/Scenes', 11, 'Scenes/**'),
        ]
        self.map_rows = []
        self.baseline_rows = []

    def source(self, path, *includes, extra=''):
        file = self.src / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(''.join(f'#include "{include}"\n' for include in includes) + extra, encoding='utf-8')

    def mapped(self, path, target):
        self.map_rows.append({'file': path, 'target': target})

    def write_inputs(self):
        write_tsv(self.dir / 'modules.tsv', MODULE_COLUMNS, self.modules)
        write_tsv(self.dir / 'module-map.tsv', MAP_COLUMNS, self.map_rows)
        if not (self.dir / 'baseline.tsv').exists():
            write_tsv(self.dir / 'baseline.tsv', BASELINE_COLUMNS, self.baseline_rows)

    def load(self):
        self.write_inputs()
        modules = check_layers.load_modules(self.dir / 'modules.tsv')
        module_map = check_layers.load_module_map(self.dir / 'module-map.tsv', modules)
        return check_layers.include_graph(self.src), module_map

    def run_check(self, *extra):
        self.write_inputs()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = check_layers.main(['--src', str(self.src), '--modules', str(self.dir / 'modules.tsv'),
                                      '--map', str(self.dir / 'module-map.tsv'),
                                      '--baseline', str(self.dir / 'baseline.tsv'), *extra])
        return code, output.getvalue()

    def test_engine_including_game_is_a_violation(self):
        self.source('Core/A.h', 'Scenes/B.h')
        self.source('Scenes/B.h')
        graph, module_map = self.load()
        self.assertEqual(check_layers.violations(graph, module_map),
                         [('Core/A.h', 'Scenes/B.h', 'engine/Core', 'game/Scenes')])

    def test_allowed_dependency_is_not_reported(self):
        self.source('Guild/G.cpp', 'Core/A.h')
        self.source('Core/A.h')
        graph, module_map = self.load()
        self.assertEqual(check_layers.violations(graph, module_map), [])

    def test_same_layer_include_needs_may_use(self):
        self.modules.append(module('game/Chat', 9, 'Chat/**'))
        self.source('Guild/G.cpp', 'Chat/C.h')
        self.source('Chat/C.h')
        graph, module_map = self.load()
        self.assertEqual(check_layers.violations(graph, module_map),
                         [('Guild/G.cpp', 'Chat/C.h', 'game/Guild', 'game/Chat')])
        self.modules[1] = module('game/Guild', 9, 'Guild/**', may_use='*layer<9;game/Chat')
        graph, module_map = self.load()
        self.assertEqual(check_layers.violations(graph, module_map), [])

    def test_baseline_hides_old_violations_only(self):
        self.source('Core/A.h', 'Scenes/B.h')
        self.source('Scenes/B.h')
        self.baseline_rows.append({'file': 'Core/A.h', 'include': 'Scenes/B.h', 'from_module': 'engine/Core',
                                   'to_module': 'game/Scenes'})
        code, _ = self.run_check()
        self.assertEqual(code, 0)
        self.source('Core/C.h', 'Scenes/B.h')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Core/C.h -> Scenes/B.h', output)
        self.assertNotIn('Core/A.h -> Scenes/B.h', output)

    def test_fixed_violation_is_reported_as_stale_without_failing(self):
        self.source('Core/A.h')
        self.source('Scenes/B.h')
        self.baseline_rows.append({'file': 'Core/A.h', 'include': 'Scenes/B.h', 'from_module': 'engine/Core',
                                   'to_module': 'game/Scenes'})
        code, output = self.run_check()
        self.assertEqual(code, 0)
        self.assertIn('1 baseline rows no longer happen', output)
        self.assertIn('Core/A.h -> Scenes/B.h', output)

    def test_write_baseline_then_check_passes(self):
        self.source('Core/A.h', 'Scenes/B.h')
        self.source('Scenes/B.h')
        code, _ = self.run_check('--write-baseline')
        self.assertEqual(code, 0)
        self.assertIn('Core/A.h\tScenes/B.h\tengine/Core\tgame/Scenes',
                      (self.dir / 'baseline.tsv').read_text(encoding='utf-8'))
        code, _ = self.run_check()
        self.assertEqual(code, 0)

    def test_cycle_between_two_modules(self):
        self.modules += [module('game/A', 9, 'A/**'), module('game/B', 9, 'B/**')]
        self.source('A/x.h', 'B/y.h')
        self.source('B/y.h', 'A/x.h')
        graph, module_map = self.load()
        self.assertEqual(check_layers.cycles(graph, module_map), [['game/A', 'game/B']])

    def test_unmapped_file_fails(self):
        self.source('Stray/z.cpp')
        code, output = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn('Stray/z.cpp', output)

    def test_map_row_wins_over_folder_glob(self):
        self.source('Scenes/GuildPanel.cpp')
        self.mapped('Scenes/GuildPanel.cpp', 'game/Guild')
        _, module_map = self.load()
        self.assertEqual(check_layers.module_of('Scenes/GuildPanel.cpp', module_map), 'game/Guild')

    def test_submodule_target_belongs_to_its_module(self):
        self.modules.append(module('game/Events', 9, 'Events/**'))
        self.source('Scenes/KalimaGate.cpp')
        self.mapped('Scenes/KalimaGate.cpp', 'game/Events/Kalima')
        _, module_map = self.load()
        self.assertEqual(check_layers.module_of('Scenes/KalimaGate.cpp', module_map), 'game/Events')

    def test_longest_folder_glob_wins(self):
        self.modules[1] = module('game/Guild', 9, 'Guild/**;Scenes/Guild*')
        self.source('Scenes/GuildWar.cpp')
        _, module_map = self.load()
        self.assertEqual(check_layers.module_of('Scenes/GuildWar.cpp', module_map), 'game/Guild')
        self.assertEqual(check_layers.module_of('Scenes/Other.cpp', module_map), 'game/Scenes')

    def test_only_quoted_includes_inside_src_are_edges(self):
        self.source('Core/A.h', 'SDL3/SDL.h', extra='#include <vector>\n// #include "Scenes/B.h"\n')
        self.source('Scenes/B.h')
        graph, _ = self.load()
        self.assertEqual(graph['Core/A.h'], set())


if __name__ == '__main__':
    unittest.main()
