"""The registry of the original's globals (reference_globals.py), with small made-up headers.

    python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_globals.py' -v
"""
import csv
import pathlib
import tempfile
import unittest

import reference_globals as rg

MODULES = {'Scenes/GuildState.h': 'game/Guild', 'Scenes/GuildWindow.cpp': 'game/Guild',
           'UI/CastleSiegeState.h': 'game/CastleSiege', 'Scenes/WorldRuntime.h': 'game/Kernel',
           'Scenes/SessionEventApplier.cpp': 'game/Session', 'World/Weather.cpp': 'game/Weather'}


def module_of(path):
    return MODULES.get(path, '')


def build(sources):
    return {row['va']: row for row in rg.build_globals({f'Client/src/{p}': t for p, t in sources.items()},
                                                        module_of)}


class ReferenceGlobalsTest(unittest.TestCase):
    def test_a_member_citing_it_in_its_own_comment_is_its_home(self):
        rows = build({'Scenes/GuildState.h': 'struct GuildState\n{\n    std::uint8_t count = 0; // 0x787a5e8\n};\n'})
        self.assertEqual(rows['0x787a5e8']['home'], 'GuildState::count')
        self.assertEqual(rows['0x787a5e8']['file'], 'Client/src/Scenes/GuildState.h')
        self.assertEqual(rows['0x787a5e8']['line'], '3')
        self.assertEqual(rows['0x787a5e8']['module'], 'game/Guild')
        self.assertEqual(rows['0x787a5e8']['flag'], '')

    def test_a_member_under_a_comment_citing_it_is_its_home(self):
        rows = build({'Scenes/GuildState.h': ('struct GuildState\n{\n    // 0x4d7b60: the members, from 0x787a5e8\n'
                                              '    // until the guilds.\n    std::array<Member, 80> members{};\n};\n')})
        self.assertEqual(rows['0x787a5e8']['home'], 'GuildState::members')
        self.assertEqual(rows['0x787a5e8']['line'], '5')

    def test_a_nested_struct_names_the_inner_one(self):
        rows = build({'UI/CastleSiegeState.h': ('struct CastleSiegeState\n{\n    struct Panel\n    {\n'
                                                '        std::uint8_t kind = 0; // 0x7239e4\n    };\n'
                                                '    Panel panel;\n    bool active = false; // 0x7239d8\n};\n')})
        self.assertEqual(rows['0x7239e4']['home'], 'Panel::kind')
        self.assertEqual(rows['0x7239d8']['home'], 'CastleSiegeState::active')

    def test_an_address_cited_only_in_a_cpp_has_no_home(self):
        rows = build({'Scenes/GuildWindow.cpp': 'void Draw()\n{\n    Clear(); // 0x787aa48\n}\n'})
        self.assertEqual(rows['0x787aa48']['home'], '')
        self.assertEqual(rows['0x787aa48']['flag'], 'no-home')
        self.assertEqual(rows['0x787aa48']['users'], 'game/Guild')

    def test_two_members_citing_it_have_two_homes(self):
        rows = build({'Scenes/GuildState.h': 'struct GuildState\n{\n    int count = 0; // 0x787a5e8\n};\n',
                      'Scenes/WorldRuntime.h': 'struct WorldRuntime\n{\n    int guildCount = 0; // 0x787a5e8\n};\n'})
        self.assertEqual(rows['0x787a5e8']['flag'], 'two-homes')
        self.assertEqual(rows['0x787a5e8']['home'], 'GuildState::count WorldRuntime::guildCount')
        self.assertEqual(rows['0x787a5e8']['file'], 'Client/src/Scenes/GuildState.h')

    def test_the_rdata_and_the_code_give_no_row(self):
        rows = build({'Scenes/GuildState.h': ('struct GuildState\n{\n    float scale = 1.0f; // 0x6b87d4\n'
                                              '    int step = 0; // 0x4d7b60\n};\n')})
        self.assertEqual(rows, {})

    def test_the_users_are_the_other_files_modules(self):
        rows = build({'Scenes/WorldRuntime.h': 'struct WorldRuntime\n{\n    std::uint8_t weather = 0; // 0x5763358\n};\n',
                      'World/Weather.cpp': '// 0x5763358 is the target.\n',
                      'Scenes/SessionEventApplier.cpp': 'Apply(); // 0x5763358\n'})
        self.assertEqual(rows['0x5763358']['module'], 'game/Kernel')
        self.assertEqual(rows['0x5763358']['users'], 'game/Session game/Weather')

    def test_functions_constants_and_enums_are_no_homes(self):
        rows = build({'Scenes/GuildState.h': ('struct GuildState\n{\n    void Clear(); // 0x787a5e8\n'
                                              '    static constexpr int Limit = 80; // 0x787aa48\n'
                                              '    enum class Kind : std::uint8_t\n    {\n'
                                              '        Member, // 0x787a5ec\n    };\n};\n')})
        self.assertEqual({va: row['flag'] for va, row in rows.items()},
                         {'0x787a5e8': 'no-home', '0x787aa48': 'no-home', '0x787a5ec': 'no-home'})

    def test_a_plain_enum_is_no_home(self):
        rows = build({'Scenes/GuildState.h': ('struct GuildState\n{\n    enum Kind\n    {\n'
                                              '        Member, // 0x787a5ec\n    };\n};\n')})
        self.assertEqual(rows['0x787a5ec']['flag'], 'no-home')

    def test_an_empty_comment_line_keeps_the_block_above(self):
        rows = build({'Scenes/GuildState.h': ('struct GuildState\n{\n    // 0x787a5e8: the members.\n    //\n'
                                              '    // Until the guilds.\n    std::array<Member, 80> members{};\n};\n')})
        self.assertEqual(rows['0x787a5e8']['home'], 'GuildState::members')

    def test_a_reference_member_is_no_home(self):
        rows = build({'Scenes/GuildState.h': ('struct GuildView\n{\n    // 0x787a5e8, as the guild left it.\n'
                                              '    const std::array<int, 5>& count;\n};\n')})
        self.assertEqual(rows['0x787a5e8']['flag'], 'no-home')

    def test_a_power_of_two_is_a_flag_and_no_row(self):
        rows = build({'Scenes/GuildWindow.cpp': '// the flags 0x800000 and 0x1000000 of 0x7d0d348\n'})
        self.assertEqual(list(rows), ['0x7d0d348'])

    def test_an_address_in_a_string_is_cited_and_one_in_the_code_is_not(self):
        rows = build({'Scenes/GuildWindow.cpp': ('Cut("0x4d7b60: no guild list (0x787aa48)");\n'
                                                 'constexpr std::uint32_t Line = 0x7732158;\n')})
        self.assertEqual(list(rows), ['0x787aa48'])

    def test_a_member_outside_any_struct_is_no_home(self):
        rows = build({'Scenes/GuildState.h': 'inline int guildCount = 0; // 0x787a5e8\n'})
        self.assertEqual(rows['0x787a5e8']['flag'], 'no-home')

    def test_the_table_keeps_every_home(self):
        homes = {f'Scenes/Context{n}.h': f'struct Context{n}\n{{\n    float worldClockOfTheFrame = 0; // 0x5767b88\n}};\n'
                 for n in range(12)}
        rows = rg.build_globals({f'Client/src/{p}': t for p, t in homes.items()}, module_of)
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / 'globals.tsv'
            rg.write(path, rows)
            written = list(csv.DictReader(path.read_text(encoding='utf-8').splitlines(), delimiter='\t'))
        self.assertEqual(written, rows)
        self.assertEqual(len(written[0]['home'].split()), 12)

    def test_rows_go_by_address(self):
        rows = rg.build_globals({'Client/src/Scenes/GuildWindow.cpp': '// 0x787aa48 and 0x6c7b8c\n'}, module_of)
        self.assertEqual([row['va'] for row in rows], ['0x6c7b8c', '0x787aa48'])


if __name__ == '__main__':
    unittest.main()
