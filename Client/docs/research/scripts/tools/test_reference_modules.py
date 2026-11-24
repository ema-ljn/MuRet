"""The module of each row of the client reference and the per-module totals (reference_modules.py), with small made-up
tables.

    python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_modules.py' -v
"""
import pathlib
import unittest

import reference_modules as rm
from build_client_reference import ADDR

MODULES = ('module\troot\tlayer\tfolders_today\tmay_use\tnote\n'
           'game/Guild\tgame\t3\tGuild/**\t\t\n'
           'game/Items\tgame\t3\t\t\t\n')
MODULE_MAP = ('file\tlines\tcurrent_folder\ttarget\tconfidence\twhy\n'
              'Scenes/GuildWindow.cpp\t300\tScenes\tgame/Guild\thigh\t\n'
              'Scenes/ItemTooltip.cpp\t200\tScenes\tgame/Items/Tooltip\tmedium\t\n')


def function(**changes):
    row = dict(va='f1', bytes='1024', clase='game', viva='sí', estado='implemented', module='game/Guild')
    row.update(changes)
    return row


class ReferenceModulesTest(unittest.TestCase):
    def test_file_takes_module_of_map_then_folder(self):
        module_map = rm.load(MODULES, MODULE_MAP)
        self.assertEqual(rm.module_of_file('Scenes/GuildWindow.cpp', module_map), 'game/Guild')
        self.assertEqual(rm.module_of_file('Scenes/ItemTooltip.cpp', module_map), 'game/Items')
        self.assertEqual(rm.module_of_file('Guild/GuildMark.h', module_map), 'game/Guild')
        self.assertEqual(rm.module_of_file('Nowhere/X.cpp', module_map), '')
        self.assertEqual(rm.module_of_file('Scenes/GuildWindow.cpp', rm.load('', '')), '')

    def test_function_takes_module_of_the_files_citing_it_most(self):
        module_map = rm.load(MODULES, MODULE_MAP)
        cites = ['Scenes/ItemTooltip.cpp', 'Scenes/GuildWindow.cpp', 'Scenes/ItemTooltip.cpp']
        self.assertEqual(rm.module_of_citations(cites, module_map), 'game/Items')
        self.assertEqual(rm.module_of_citations([], module_map), '')

    def test_gap_takes_module_of_its_callers(self):
        caller_module = {1: 'game/Items', 2: 'game/Items', 3: 'game/Guild', 4: 'game/Guild', 5: ''}
        self.assertEqual(rm.module_of_callers([1, 2], caller_module), 'game/Items')
        self.assertEqual(rm.module_of_callers([1, 2, 3], caller_module), 'game/Items')
        self.assertEqual(rm.module_of_callers([2, 4], caller_module), 'game/Guild')
        self.assertEqual(rm.module_of_callers([5], caller_module), '')
        self.assertEqual(rm.module_of_callers([], caller_module), '')

    def test_module_table_counts_cuts_and_gaps(self):
        funciones = [function(), function(va='f2', bytes='512', estado='unknown'),
                     function(va='f3', bytes='2048', estado='partial', module='game/Items'),
                     function(va='f4', bytes='100', viva='no', module='game/Items')]
        cortes = [dict(module='game/Guild', archivo='Scenes/GuildWindow.cpp', cierra_el_cliente='sí'),
                  dict(module='game/Guild', archivo='Scenes/GuildWindow.cpp', cierra_el_cliente='-'),
                  dict(module='game/Items', archivo='Scenes/ItemTooltip.cpp', cierra_el_cliente=''),
                  dict(module='', archivo='', cierra_el_cliente='sí, sin resolver')]
        huecos = [dict(module='game/Items', bytes='3072'), dict(module='game/Items', bytes='1024'),
                  dict(module='', bytes='512')]
        archivos = [dict(module='game/Guild', lineas='300'), dict(module='game/Items', lineas='200'),
                    dict(module='game/Items', lineas='50')]
        totals = rm.module_totals(funciones, cortes, huecos, archivos, vm_steps=['game/Guild', ''],
                                  open_08_ids=['game/Items'])
        self.assertEqual(totals['game/Guild'], dict(lines=300, cited_functions=1, cited_kb=1.0, live_cuts=2,
                                                    closing_cuts=1, gaps=0, gap_kb=0.0, vm_steps=1, open_08_ids=0))
        self.assertEqual(totals['game/Items'], dict(lines=250, cited_functions=1, cited_kb=2.0, live_cuts=1,
                                                    closing_cuts=0, gaps=2, gap_kb=4.0, vm_steps=0, open_08_ids=1))
        self.assertEqual(totals[rm.UNASSIGNED], dict(lines=0, cited_functions=0, cited_kb=0.0, live_cuts=0,
                                                     closing_cuts=1, gaps=1, gap_kb=0.5, vm_steps=1, open_08_ids=0))

    def test_this_file_cites_no_address(self):
        self.assertEqual(ADDR.findall(pathlib.Path(__file__).read_text(encoding='utf-8')), [])


if __name__ == '__main__':
    unittest.main()
