"""Exercise check_named_values.py through its isolated command-line entry point."""

import pathlib
import subprocess
import sys
import unittest

from scratch_repo import ScratchRepo

CHECKER = pathlib.Path(__file__).with_name('check_named_values.py').resolve()
BASELINE_HEADER = 'space\tfile\tname\tvalue\n'


class CheckNamedValuesTest(unittest.TestCase):
    def setUp(self):
        self.repo = ScratchRepo(self)
        (self.repo.path / 'src').mkdir()
        self.repo.write('modules.tsv',
                        'module\troot\tlayer\tfolders_today\tmay_use\tnote\n'
                        'engine/Core\tengine\t0\tCore/**\t\t\n'
                        'engine/Math\tengine\t1\tMath/**\t\t\n'
                        'game/Protocol\tgame\t6\tProtocol/**;game/Protocol/**\t\t\n'
                        'game/Tables\tgame\t6\tTables/**\t\t\n'
                        'game/Maps\tgame\t6\tMaps/**;game/Maps/**\t\t\n'
                        'game/Assets\tgame\t8\tAssets/**\t\t\n'
                        'game/Scenes\tgame\t11\tScenes/**\t\t\n')
        self.write_map()
        self.repo.write('baseline.tsv', BASELINE_HEADER)
        self.spaces = [
            'opcode\tgame/Protocol\tname=.*Opcode\thome-only\t',
            'subcode\tgame/Protocol\tname=.*Subcode\thome-only\t',
            'map\tgame/Maps\tname=.*Map\tsingle-definition\t',
            'item-code\tgame/Tables\tname=.*ItemCode\tsingle-definition\t',
            'item-model\tgame/Assets\tname=.*ItemModel\tsingle-definition\t',
            'text\tgame/Tables\tname=.*Text;values=0..9999\tsingle-definition\t',
            'sound\tgame/Assets\tname=.*Sound\twatch\t',
        ]

    def write_map(self, *rows):
        self.repo.write('module-map.tsv', 'file\ttarget\n' + ''.join(f'{row}\n' for row in rows))

    def source(self, path, code):
        self.repo.write(f'src/{path}', code)

    def run_check(self, *extra):
        self.repo.write('spaces.tsv', 'space\thome\tmatch\tpolicy\tnote\n' + '\n'.join(self.spaces) + '\n')
        result = subprocess.run(
            [sys.executable, '-I', str(CHECKER), '--src', 'src', '--modules', 'modules.tsv',
             '--map', 'module-map.tsv', '--spaces', 'spaces.tsv', '--baseline', 'baseline.tsv', *extra],
            cwd=self.repo.path, capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.stderr, '')
        return result.returncode, result.stdout

    def assert_clean(self):
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('0 new', output)

    def test_second_literal_definition_fails(self):
        self.source('Scenes/One.cpp', 'constexpr int FirstEventMap = 18;\n')
        self.assert_clean()
        self.source('Scenes/Two.cpp', 'constexpr int DifferentRoleMap = 0x12u;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('Scenes/Two.cpp:1: map 0x12 (DifferentRoleMap):', output)

    def test_alias_of_the_home_passes(self):
        self.source('Maps/MapId.h', 'inline constexpr int EventMap = 18;\n')
        self.source('Scenes/One.cpp', 'constexpr int FirstEventMap = Maps::EventMap;\n')
        self.assert_clean()

    def test_literal_outside_an_existing_home_fails(self):
        self.source('Maps/MapId.h', 'inline constexpr int EventMap = 18;\n')
        self.source('Scenes/One.cpp', 'constexpr int FirstEventMap = 18;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('Scenes/One.cpp:1: map 0x12 (FirstEventMap):', output)
        self.assertNotIn('Maps/MapId.h:1:', output)

    def test_home_only_outside_home_fails(self):
        self.source('Scenes/One.cpp', 'constexpr int ConnectOpcode = 0xf1;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('Scenes/One.cpp:1: opcode 0xf1 (ConnectOpcode):', output)

    def test_home_only_uses_logical_module(self):
        self.write_map('Scenes/One.cpp\tgame/Protocol', 'Protocol/Two.cpp\tgame/Scenes')
        self.source('Scenes/One.cpp', 'constexpr int ConnectOpcode = 0xf1;\n')
        self.source('Protocol/Two.cpp', 'constexpr int AuthOpcode = 0xf2;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('Protocol/Two.cpp:1: opcode 0xf2 (AuthOpcode):', output)
        self.assertNotIn('Scenes/One.cpp:1:', output)

    def test_same_number_in_two_spaces_passes(self):
        self.source('Scenes/One.cpp', 'constexpr int RenaItemCode = 0x1c15;\n')
        self.source('Scenes/Two.cpp', 'constexpr int HornOfDinorantItemModel = 0x1c15;\n')
        self.assert_clean()

    def test_two_roles_one_name_two_values_pass(self):
        self.source('Scenes/One.cpp', 'constexpr int FirstEventMap = 18;\n')
        self.source('Scenes/Two.cpp', 'constexpr int FirstEventMap = 24;\n')
        self.assert_clean()

    def test_two_roles_of_one_value_in_the_same_file_pass(self):
        self.source('Scenes/One.cpp', 'constexpr int FirstEventMap = 18, MinEventMap = 18;\n')
        self.assert_clean()

    def test_subcodes_repeat_inside_protocol(self):
        self.source('Protocol/One.h', 'constexpr int FirstSubcode = 0x01;\n')
        self.source('Protocol/Two.h', 'constexpr int SecondSubcode = 0x01;\n')
        self.assert_clean()

    def test_values_bound_drops_a_color(self):
        for file in ('One', 'Two'):
            self.source(f'Scenes/{file}.cpp', 'constexpr unsigned WhiteText = 0xffffffff;\n')
        self.assert_clean()

    def test_values_bound_includes_its_endpoints(self):
        for file in ('One', 'Two'):
            self.source(f'Scenes/{file}.cpp', 'constexpr int EmptyText = 0, LastText = 9999;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('text 0x0 (EmptyText)', output)
        self.assertIn('text 0x270f (LastText)', output)

    def test_integer_bound_excludes_floating_point_literals(self):
        self.spaces[0] = 'opcode\tgame/Protocol\tname=.*Opcode;values=0..255\thome-only\t'
        self.source('Scenes/One.cpp', 'constexpr float LocalOpcode = 0.25f;\n'
                    'constexpr double OtherOpcode = 25.0;\n')
        self.assert_clean()

    def test_comments_and_strings_are_ignored(self):
        self.source('Scenes/One.cpp', '// constexpr int CommentOpcode = 0x01;\n'
                    '/* constexpr int BlockOpcode = 0x02;\n'
                    '   constexpr int SecondLineOpcode = 0x03; */\n'
                    'const char* quoted = "constexpr int QuotedOpcode = 0x04;";\n'
                    'const char* raw = R"code(constexpr int RawOpcode = 0x05;)code";\n')
        self.assert_clean()

    def test_assignments_and_nonliteral_initializers_are_ignored(self):
        self.source('Scenes/One.cpp', 'void F(int LocalOpcode) { LocalOpcode = 0xf1; }\n'
                    'constexpr int CalculatedOpcode = 0xf0 + 1;\n'
                    'constexpr int CallOpcode = Convert(0xf1);\n')
        self.assert_clean()

    def test_default_function_argument_is_not_a_constant_definition(self):
        self.source('Scenes/One.cpp', 'void Open(const int LocalOpcode = 0xf1, int Other = 0);\n')
        self.assert_clean()

    def test_stale_baseline_row_fails(self):
        self.source('Scenes/One.cpp', 'constexpr int ConnectOpcode = Protocol::ConnectOpcode;\n')
        self.repo.write('baseline.tsv', BASELINE_HEADER + 'opcode\tScenes/One.cpp\tConnectOpcode\t0xf1\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('baseline rows no longer happen', output)
        self.assertIn('ConnectOpcode', output)

    def test_write_baseline_round_trip(self):
        self.source('Scenes/Two.cpp', 'constexpr int WorldOpcode = 0xf3;\n')
        self.source('Scenes/One.cpp', 'constexpr int ConnectOpcode = 241u;\n')
        self.source('Protocol/Wire.h', 'constexpr int ServerOpcode = 0xf4;\n')
        code, output = self.run_check('--write-baseline')
        self.assertEqual(code, 0, output)
        self.assertEqual(self.repo.read('baseline.tsv'), BASELINE_HEADER
                         + 'opcode\tScenes/One.cpp\tConnectOpcode\t0xf1\n'
                         + 'opcode\tScenes/Two.cpp\tWorldOpcode\t0xf3\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('2 named values: 2 in the baseline, 0 new', output)

    def test_engine_file_defining_a_game_value_fails(self):
        self.write_map('Scenes/One.cpp\tengine/Core')
        self.source('Scenes/One.cpp', 'constexpr int LocalSound = 0x19;\n')
        self.assert_clean()
        code, output = self.run_check('--clean-root', 'engine')
        self.assertEqual(code, 1, output)
        self.assertIn('Scenes/One.cpp:1: sound 0x19 (LocalSound):', output)

    def test_clean_root_debt_fails_even_when_baselined(self):
        self.source('Core/One.cpp', 'constexpr int LocalSound = 0x19;\n')
        self.source('Scenes/One.cpp', 'constexpr int SharedSound = 25;\n')
        code, output = self.run_check('--write-baseline')
        self.assertEqual(code, 0, output)
        original = self.repo.read('baseline.tsv')
        self.assertIn('sound\tCore/One.cpp\tLocalSound\t0x19', original)
        for extra in ((), ('--write-baseline',)):
            with self.subTest(extra=extra):
                code, output = self.run_check('--clean-root', 'engine', *extra)
                self.assertEqual(code, 1, output)
                self.assertIn('Core/One.cpp:1: sound 0x19 (LocalSound):', output)
                self.assertEqual(self.repo.read('baseline.tsv'), original)

    def test_clean_root_resolves_root_relative_header_with_a_map_row(self):
        self.spaces[2] = 'map\tLegacy/MapId.h\tname=.*Map\twatch\t'
        self.write_map('game/Legacy/MapId.h\tgame/Maps')
        self.source('game/Legacy/MapId.h', 'inline constexpr int EventMap = 18;\n')
        self.source('Core/One.cpp', 'constexpr int FirstEventMap = 18;\n')
        code, output = self.run_check('--clean-root', 'engine')
        self.assertEqual(code, 1, output)
        self.assertIn('Core/One.cpp:1: map 0x12 (FirstEventMap):', output)

    def test_clean_root_resolves_root_relative_header_from_module_prefix(self):
        modules = self.repo.read('modules.tsv').replace('Maps/**;game/Maps/**', '')
        self.repo.write('modules.tsv', modules)
        self.spaces[2] = 'map\tMaps/MapId.h\tname=.*Map\twatch\t'
        self.source('Core/One.cpp', 'constexpr int FirstEventMap = 18;\n')
        code, output = self.run_check('--clean-root', 'engine')
        self.assertEqual(code, 1, output)
        self.assertIn('Core/One.cpp:1: map 0x12 (FirstEventMap):', output)

    def test_clean_root_allows_engine_values_and_game_files(self):
        self.spaces.append('number\tengine/Math\tname=.*Number\twatch\t')
        self.source('Core/One.cpp', 'constexpr int LocalNumber = 4;\n')
        self.source('Scenes/One.cpp', 'constexpr int LocalSound = 0x19;\n')
        code, output = self.run_check('--clean-root', 'engine')
        self.assertEqual(code, 0, output)

    def test_unknown_clean_root_fails(self):
        code, output = self.run_check('--clean-root', 'engine/', '--write-baseline')
        self.assertEqual(code, 1, output)
        self.assertIn("unknown --clean-root 'engine/'", output)
        self.assertEqual(self.repo.read('baseline.tsv'), BASELINE_HEADER)

    def test_watch_space_reports_and_passes(self):
        self.source('Scenes/One.cpp', 'constexpr int OpenSound = 0x19;\n')
        self.source('Scenes/Two.cpp', 'constexpr int ClickSound = 25;\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('Scenes/Two.cpp:1: sound 0x19 (ClickSound):', output)

    def test_stale_watch_row_fails(self):
        self.source('Scenes/One.cpp', 'constexpr int OpenSound = 0x19;\n')
        self.source('Scenes/Two.cpp', 'constexpr int ClickSound = 25;\n')
        code, output = self.run_check('--write-baseline')
        self.assertEqual(code, 0, output)
        self.source('Scenes/Two.cpp', 'constexpr int ClickSound = Sounds::OpenSound;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('ClickSound', output)

    def test_numeric_spellings_share_one_value(self):
        self.source('Maps/MapId.h', 'inline constexpr int EventMap = 0X1Eull;\n')
        for initializer in ('30', '036L', "0b1'1110u", '+ 30'):
            with self.subTest(initializer=initializer):
                self.source('Scenes/One.cpp', f'constexpr int OtherMap{{{initializer}}};\n')
                code, output = self.run_check()
                self.assertEqual(code, 1, output)
                self.assertIn('Scenes/One.cpp:1: map 0x1e (OtherMap):', output)

    def test_multiline_enum_and_typed_declaration_share_one_value(self):
        self.spaces.append('class\tgame/Tables\ttype=CharacterClass\tsingle-definition\t')
        self.source('Tables/Class.h', 'enum class CharacterClass : unsigned char\n{\n'
                    '    Wizard = 2,\n    Mage = Wizard,\n};\n')
        self.source('Scenes/One.cpp', 'constexpr\n    CharacterClass\n    First{2};\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('Scenes/One.cpp:3: class 0x2 (First):', output)

    def test_first_file_with_one_value_in_an_enum_passes(self):
        self.spaces.append('class\tgame/Tables\ttype=CharacterClass\tsingle-definition\t')
        self.source('Scenes/One.cpp', 'enum class CharacterClass { Wizard = 2, Mage = Wizard };\n')
        self.source('Scenes/Two.cpp', 'enum class CharacterClass { Mage = CharacterClass::Wizard };\n')
        self.assert_clean()

    def test_concrete_home_can_move_to_its_root(self):
        self.spaces[2] = 'map\tMaps/MapId.h\tname=.*Map\thome-only\t'
        self.source('game/Maps/MapId.h', 'inline constexpr int EventMap = 18;\n')
        self.assert_clean()
        self.source('game/Maps/Other.h', 'inline constexpr int EventMap = 19;\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('game/Maps/Other.h:1: map 0x13 (EventMap):', output)

    def test_float_literal_and_widened_literal_remain_distinct(self):
        self.spaces.append('number\tengine/Math\tname=.*Tenth\twatch\t')
        self.source('Scenes/One.cpp', 'constexpr float Tenth = 0.1f;\n')
        self.source('Scenes/Two.cpp', 'constexpr double Tenth = 0.1f;\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertNotIn(' (Tenth):', output)
        self.source('Scenes/One.cpp', 'constexpr float Tenth = 0.1;\n')
        self.source('Scenes/Two.cpp', 'constexpr double Tenth = 0.1;\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertNotIn(' (Tenth):', output)

    def test_widened_literal_cast_round_trips_with_implicit_widening(self):
        self.spaces.append('number\tengine/Math\tname=.*Tenth\twatch\t')
        self.source('Scenes/One.cpp', 'constexpr double Tenth = 0.10f;\n')
        self.source('Scenes/Two.cpp', 'constexpr double Tenth = static_cast<double>(0.1F);\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('Scenes/Two.cpp:1: number', output)
        code, output = self.run_check('--write-baseline')
        self.assertEqual(code, 0, output)
        self.assertIn('number\tScenes/Two.cpp\tTenth\t', self.repo.read('baseline.tsv'))
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('2 named values: 2 in the baseline, 0 new', output)

    def test_parenthesized_literal_constant_is_a_definition(self):
        self.source('Scenes/One.cpp', 'constexpr int ConnectOpcode(0xf1);\n')
        code, output = self.run_check()
        self.assertEqual(code, 1, output)
        self.assertIn('Scenes/One.cpp:1: opcode 0xf1 (ConnectOpcode):', output)

    def test_parenthesized_function_declaration_is_ignored(self):
        self.source('Scenes/One.cpp', 'constexpr int ConnectOpcode(int value = 0xf1);\n')
        self.assert_clean()

    def test_bit_patterns_find_duplicate_narrow_values(self):
        self.spaces.append('number\tengine/Math\tname=.*Tenth\twatch\t')
        self.source('Scenes/One.cpp', 'constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);\n')
        self.source('Scenes/Two.cpp', 'constexpr float SomeTenth = std::bit_cast<float>(std::uint32_t{0x3dcccccd});\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertIn('Scenes/Two.cpp:1: number', output)
        self.assertIn('0x3dcccccd', output)

    def test_bit_patterns_keep_widened_values_distinct(self):
        self.spaces.append('number\tengine/Math\tname=.*Tenth\twatch\t')
        self.source('Scenes/One.cpp', 'constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);\n')
        self.source('Scenes/Two.cpp',
                    'constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));\n')
        code, output = self.run_check()
        self.assertEqual(code, 0, output)
        self.assertNotIn(' (Tenth):', output)


if __name__ == '__main__':
    unittest.main()
