"""Tests for reference_anchors.py: anchors of hand-classified cuts and how they are found again after a move or an edit.

    python3 -I -m unittest discover -s Client/docs/research/scripts/tools -p 'test_reference_anchors.py' -v
"""
import unittest

from client_reference_symbols import symbols
from reference_anchors import anchor_entries, anchor_of, cut_sites, resolve, resolve_entries

GUILD = '''namespace Client::Scenes
{
Core::Expected<void, std::string> ApplyGuildMark(int mark)
{
    if (mark < 0)
        return Core::Unexpected(std::string("0x4d3200: the guild mark is not ported"));
    return {};
}
} // namespace Client::Scenes
'''


def sites_of(path, text):
    return cut_sites(path, text, symbols(text.split('\n')))


def entry(path, line, **fields):
    return dict(archivo=path, línea=str(line), fin=str(line), mensaje='', **fields)


class AnchorTest(unittest.TestCase):
    def test_anchor_ignores_whitespace_and_trailing_comment(self):
        tidy = anchor_of('return Unexpected("0x4d3200: missing");', [0x4d3200], 'Unexpected', 'Apply')
        loose = anchor_of('return   Unexpected("0x4d3200: missing");  // see 07', [0x4d3200], 'Unexpected', 'Apply')
        self.assertEqual(tidy, loose)

    def test_anchor_keeps_a_comment_marker_inside_a_string(self):
        with_url = anchor_of('Unexpected("http://host");', [], 'Unexpected', 'Apply')
        without = anchor_of('Unexpected("http:");', [], 'Unexpected', 'Apply')
        self.assertNotEqual(with_url, without)

    def test_anchor_ignores_how_a_long_string_is_split(self):
        one = anchor_of('Unexpected("0x4d3200: the mark is not ported")', [0x4d3200], 'Unexpected', 'F')
        split = anchor_of('Unexpected("0x4d3200: the mark "\n           "is not ported")', [0x4d3200], 'Unexpected', 'F')
        self.assertEqual(one, split)

    def test_anchor_orders_the_addresses(self):
        self.assertEqual(anchor_of('x', [0x4d3200, 0x401000], 'Cut', 'F'), anchor_of('x', [0x401000, 0x4d3200], 'Cut', 'F'))


class CutSitesTest(unittest.TestCase):
    def test_a_cut_spanning_lines_is_one_site(self):
        text = ('int F()\n{\n    return Core::Unexpected(\n'
                '        std::format("0x4d3200: {} is not ported", 3));\n}\n')
        [site] = sites_of('Scenes/X.cpp', text)
        self.assertEqual((site['line'], site['end'], site['name'], site['vas'], site['symbol']),
                         (3, 4, 'Unexpected', [0x4d3200], 'F'))

    def test_definitions_comments_and_strings_are_not_sites(self):
        text = ('Core::Unexpected<std::string> Cut(std::string_view what)\n{\n'
                '    // return Unexpected("0x4d3200");\n'
                '    Log("Unexpected(0x4d3200)");\n'
                '    return Core::Unexpected(std::string(what));\n}\n')
        self.assertEqual([s['line'] for s in sites_of('Scenes/X.cpp', text)], [5])


class ResolveTest(unittest.TestCase):
    def test_resolve_finds_moved_cut(self):
        [old] = sites_of('Scenes/GuildMark.cpp', '\n\n\n\n' + GUILD)
        [new] = sites_of('Guild/GuildMark.cpp', '\n\n\n\n\n\n' + GUILD)
        found = resolve(old['anchor'], [new])
        self.assertEqual((found['file'], found['line'], found['resolved_by']), ('Guild/GuildMark.cpp', 12, 'anchor'))

    def test_resolve_edited_line_falls_back_to_similar(self):
        [old] = sites_of('Scenes/GuildMark.cpp', GUILD)
        edited = GUILD.replace('is not ported', 'is still not ported')
        other = GUILD.replace('ApplyGuildMark', 'DrawGuildMark')
        current = sites_of('Guild/GuildMark.cpp', other) + sites_of('Guild/GuildMark.cpp', edited)
        found = resolve(old['anchor'], current, old['text'])
        self.assertEqual((found['symbol'], found['resolved_by']), ('ApplyGuildMark', 'similar'))

    def test_resolve_missing_returns_none(self):
        [old] = sites_of('Scenes/GuildMark.cpp', GUILD)
        ported = GUILD.replace('0x4d3200', '0x4d3300')
        self.assertIsNone(resolve(old['anchor'], sites_of('Scenes/GuildMark.cpp', ported)))


class EntriesTest(unittest.TestCase):
    def test_migration_writes_the_anchor_of_the_cut_at_its_line(self):
        sites = sites_of('Scenes/GuildMark.cpp', GUILD)
        [row] = anchor_entries([entry('Scenes/GuildMark.cpp', 6)], sites)
        self.assertEqual(row['anchor'], sites[0]['anchor'])

    def test_entries_take_the_cut_where_it_is_now(self):
        [old] = sites_of('Scenes/GuildMark.cpp', GUILD)
        current = sites_of('Guild/GuildMark.cpp', '\n\n' + GUILD)
        [row] = resolve_entries([entry('Scenes/GuildMark.cpp', 6, anchor=old['anchor'])], current)
        self.assertEqual((row['archivo'], row['línea'], row['fin'], row['resolved_by']),
                         ('Guild/GuildMark.cpp', '8', '8', 'anchor'))

    def test_two_equal_cuts_keep_their_order(self):
        twice = GUILD.replace('    return {};', '    if (mark > 9)\n'
                              '        return Core::Unexpected(std::string("0x4d3200: the guild mark is not ported"));\n'
                              '    return {};')
        old = sites_of('Scenes/GuildMark.cpp', twice)
        rows = anchor_entries([entry('Scenes/GuildMark.cpp', 6), entry('Scenes/GuildMark.cpp', 8)], old)
        moved = resolve_entries(rows, sites_of('Guild/GuildMark.cpp', '\n' + twice))
        self.assertEqual([r['línea'] for r in moved], ['7', '9'])

    def test_an_entry_without_its_cut_stays_and_says_unresolved(self):
        [old] = sites_of('Scenes/GuildMark.cpp', GUILD)
        [row] = resolve_entries([entry('Scenes/GuildMark.cpp', 6, anchor=old['anchor'])], [])
        self.assertEqual((row['archivo'], row['línea'], row['resolved_by']), ('Scenes/GuildMark.cpp', '6', 'unresolved'))


if __name__ == '__main__':
    unittest.main()
