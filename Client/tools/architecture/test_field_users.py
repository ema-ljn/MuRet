"""Exercise field_users.py through its isolated command-line entry point."""

import pathlib
import subprocess
import sys
import unittest

from scratch_repo import ScratchRepo

TOOL = pathlib.Path(__file__).with_name('field_users.py').resolve()
HEADER = '''#pragma once
struct Other
{
    int shared = 0;
};
struct WorldRuntime
{
    void RequestSound(int id, bool loop = false);
    // A comment with a fake member;
    int alone = 0;
    std::vector<int> names = std::vector<int>(5);
    std::array<int, 2> targets{-1, -1};
    struct Clock
    {
        int ticks = 0;
    } clock{};
    int shared = 0;
    Other* other = nullptr;
};
'''


class FieldUsersTest(unittest.TestCase):
    def setUp(self):
        self.repo = ScratchRepo(self)
        (self.repo.path / 'src').mkdir()
        self.repo.write('modules.tsv',
                        'module\troot\tlayer\tfolders_today\tmay_use\tnote\n'
                        'game/Kernel\tgame\t7\tKernel/**\t\t\n'
                        'game/Guild\tgame\t9\tGuild/**\t\t\n'
                        'game/Chat\tgame\t9\tChat/**\t\t\n')
        self.repo.write('module-map.tsv', 'file\ttarget\n')
        self.source('Kernel/WorldRuntime.h', HEADER)

    def source(self, path, code):
        self.repo.write(f'src/{path}', code)

    def run_tool(self, *extra):
        result = subprocess.run(
            [sys.executable, '-I', str(TOOL), '--src', 'src', '--modules', 'modules.tsv', '--map', 'module-map.tsv',
             '--header', 'Kernel/WorldRuntime.h', '--struct', 'WorldRuntime', *extra],
            cwd=self.repo.path, capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.stderr, '')
        self.assertEqual(result.returncode, 0)
        rows = [line.split('\t') for line in result.stdout.splitlines()[1:]]
        return {row[0]: row[1].split() for row in rows}

    def test_lists_the_data_members_and_not_the_functions_or_nested_fields(self):
        self.assertEqual(list(self.run_tool()), ['alone', 'names', 'targets', 'clock', 'shared', 'other'])

    def test_a_parameter_a_member_and_the_accessor_are_uses(self):
        self.source('Guild/Guild.cpp', 'void F(WorldRuntime& runtime) { runtime.alone = 1; }\n')
        self.source('Chat/Chat.h', 'struct Ctx { WorldRuntime* m_runtime; };\n')
        self.source('Chat/Chat.cpp',
                    'void G(Ctx& c) { c.m_runtime->names.clear(); world.Runtime().clock.ticks = 2; }\n')
        found = self.run_tool()
        self.assertEqual(found['alone'], ['game/Guild'])
        self.assertEqual(found['names'], ['game/Chat'])
        self.assertEqual(found['clock'], ['game/Chat'])

    def test_a_name_bound_to_the_accessor_is_a_use(self):
        self.source('Guild/Guild.cpp', 'void F() { auto& state = context.world.Runtime(); state.targets[0] = 1; }\n')
        self.assertEqual(self.run_tool()['targets'], ['game/Guild'])

    def test_the_same_field_of_another_struct_is_not_a_use(self):
        self.source('Guild/Guild.cpp', 'void F(Other& pass) { pass.shared = 1; }\n')
        self.assertEqual(self.run_tool()['shared'], [])

    def test_a_comment_or_a_string_is_not_a_use(self):
        self.source('Guild/Guild.cpp', '// runtime.alone\nconst char* t = "Runtime().alone";\n')
        self.assertEqual(self.run_tool()['alone'], [])

    def test_the_structs_own_functions_use_their_fields_bare(self):
        self.source('Chat/Audio.cpp', 'void WorldRuntime::RequestSound(int id, bool loop) { alone = id; }\n')
        self.assertEqual(self.run_tool()['alone'], ['game/Chat'])

    def test_single_keeps_the_fields_of_one_module(self):
        self.source('Guild/Guild.cpp', 'void F(WorldRuntime& runtime) { runtime.alone = runtime.shared; }\n')
        self.source('Chat/Chat.cpp', 'void G(WorldRuntime& runtime) { runtime.shared = 0; }\n')
        self.assertEqual(self.run_tool('--single'), {'alone': ['game/Guild']})


if __name__ == '__main__':
    unittest.main()
