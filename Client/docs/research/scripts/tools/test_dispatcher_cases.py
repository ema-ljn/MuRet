import pathlib
import tempfile
import unittest

import dispatcher_cases


def tables(map1, jt1, map2=(0,), jt2=(0x4E5B4A,), map3=(0,), jt3=(0x4E2D66,)):
    return {'map1': list(map1), 'jt1': list(jt1), 'map2': list(map2), 'jt2': list(jt2), 'map3': list(map3),
            'jt3': list(jt3)}


class BuildCasesTest(unittest.TestCase):
    def test_a_code_with_a_handler_is_a_row_of_its_level(self):
        cases = dispatcher_cases.build_cases(tables(map1=[1, 0], jt1=[0x4E5B4A, 0x4E33FC]))
        self.assertEqual(cases, [{'level': '1', 'code': '0x00', 'parent': '', 'handler': '0x4e33fc'}])

    def test_a_code_that_jumps_to_the_default_has_no_row(self):
        cases = dispatcher_cases.build_cases(tables(map1=[0, 0, 0], jt1=[0x4E5B4A]))
        self.assertEqual(cases, [])

    def test_the_byte_after_an_f1_is_level_two(self):
        cases = dispatcher_cases.build_cases(tables(map1=[0], jt1=[0x4E5B4A], map2=[0, 1], jt2=[0x4E2B59, 0x4E5B4A]))
        self.assertEqual(cases, [{'level': '2', 'code': '0x00', 'parent': '0xf1', 'handler': '0x4e2b59'}])

    def test_the_result_of_an_f1_01_is_level_three_with_its_own_default(self):
        cases = dispatcher_cases.build_cases(
            tables(map1=[0], jt1=[0x4E5B4A], map3=[0, 1, 0], jt3=[0x4E2D66, 0x4E2BA9]))
        self.assertEqual(cases, [{'level': '3', 'code': '0x01', 'parent': '0xf1 0x01', 'handler': '0x4e2ba9'}])


class WriteTsvTest(unittest.TestCase):
    def test_writes_the_header_and_one_line_per_case(self):
        with tempfile.TemporaryDirectory() as folder:
            out = pathlib.Path(folder) / 'cases.tsv'
            dispatcher_cases.write_tsv([{'level': '1', 'code': '0x03', 'parent': '', 'handler': '0x4e33ad'}], out)
            self.assertEqual(out.read_text(encoding='utf-8'), 'level\tcode\tparent\thandler\n1\t0x03\t\t0x4e33ad\n')


CASES = 'level\tcode\tparent\thandler\n1\t0x00\t\t0x4e33e4\n1\t0xf1\t\t0x4e2b39\n2\t0x0e\t0xf1\t0x4e2b59\n'
GAME = ('inline constexpr Network::Opcode ChatOpcode{0x00};\n'
        'inline constexpr Network::Opcode JoinOpcode{0xf1};\n'
        'namespace Join\n{\ninline constexpr Network::Subcode HelloSubcode{0x00};\n}\n')


class CheckListsTest(unittest.TestCase):
    def check(self, *texts):
        codes = dispatcher_cases.head_codes_of(CASES)
        return dispatcher_cases.check_lists(codes, {f'list{index}': text for index, text in enumerate(texts)})

    def test_reads_only_the_head_codes_of_level_one(self):
        self.assertEqual(dispatcher_cases.head_codes_of(CASES), {0x00, 0xF1})

    def test_a_list_naming_every_head_code_once_passes(self):
        self.assertEqual(self.check(GAME), [])

    def test_a_head_code_may_be_named_in_another_list(self):
        connect = 'inline constexpr Network::Opcode ConnectHelloOpcode{0x00};\n'
        game = GAME.replace('inline constexpr Network::Opcode ChatOpcode{0x00};\n', '')
        self.assertEqual(self.check(game, connect), [])

    def test_a_head_code_without_a_name_fails(self):
        self.assertEqual(self.check(GAME.replace('{0xf1}', '{0xf2}; // client only')),
                         ['0xf1 has a case in 0x4e2b10 and no name'])

    def test_a_code_without_a_case_must_be_client_only(self):
        keepalive = 'inline constexpr Network::Opcode KeepaliveOpcode{0x0e};\n'
        self.assertEqual(self.check(GAME + keepalive),
                         ['list0: KeepaliveOpcode (0x0e) has no case in 0x4e2b10; mark it // client only'])
        self.assertEqual(self.check(GAME + keepalive.replace(';', '; // client only')), [])

    def test_a_dispatched_code_is_not_client_only(self):
        self.assertEqual(self.check(GAME.replace('{0x00};', '{0x00}; // client only')),
                         ['list0: ChatOpcode (0x00) is dispatched, not client only'])

    def test_a_list_names_a_code_once(self):
        whisper = 'inline constexpr Network::Opcode SecondChatOpcode{0x00};\n'
        self.assertEqual(self.check(GAME + whisper), ['list0: SecondChatOpcode and ChatOpcode both name 0x00'])


if __name__ == '__main__':
    unittest.main()
