"""Tests for check_rounded_math.py on small made-up source trees.

    python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_rounded_math.py' -v
"""
import pathlib
import tempfile
import unittest

import check_rounded_math
from scratch_repo import run_main

SOURCE = 'Client/src/World/Wave.cpp'
EXCEPTIONS = 'exceptions.tsv'


class CheckRoundedMathTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.exceptions = []

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def allow(self, file, call):
        self.exceptions.append(f'{file}\t{call}\tA made-up reason.')

    def check(self, text=None):
        if text is not None:
            self.write(SOURCE, text)
        self.write(EXCEPTIONS, '\n'.join(['file\tcall\twhy'] + self.exceptions) + '\n')
        return run_main(check_rounded_math.main, '--root', str(self.root), '--src', 'Client/src', 'Client/tests',
                        '--exceptions', str(self.root / EXCEPTIONS))

    def assert_found(self, text, call):
        code, output = self.check(text)
        self.assertEqual(code, 1, text)
        self.assertIn(f'{SOURCE}:1: {call}\n', output)

    def assert_clean(self, text):
        code, output = self.check(text)
        self.assertEqual(code, 0, f'{text}\n{output}')

    def test_c_library_function_from_std_fails_with_its_line(self):
        code, output = self.check('#include <cmath>\n\nfloat Wave(float t)\n{\n    return std::sin(t);\n}\n')
        self.assertEqual(code, 1)
        self.assertIn(f'{SOURCE}:5: std::sin\n', output)
        self.assertIn('RoundedMath.h', output)

    def test_every_function_the_c_library_rounds_fails(self):
        for name in ('sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'atan2', 'sinh', 'cosh', 'tanh', 'exp', 'exp2',
                     'log', 'log2', 'log10', 'pow', 'cbrt', 'hypot', 'sinf', 'cosf', 'acosl', 'atan2f', 'log10f'):
            with self.subTest(name=name):
                self.assert_found(f'auto y = std::{name}(x);\n', f'std::{name}')

    def test_function_taken_by_name_fails(self):
        self.assert_found('std::ranges::transform(angles, sines.begin(), [](float a) { return a; }, &std :: cos);\n',
                          'std::cos')
        self.assert_found('using std::tan;\n', 'std::tan')

    def test_what_ieee_754_fixes_and_other_names_pass(self):
        self.assert_clean('auto a = std::sqrt(x) + std::fabs(x) + std::floor(x) + std::fmod(x, y) + std::ldexp(x, 2);\n'
                          'std::expected<int, std::logic_error> b;\n'
                          'auto c = std::numbers::log2e + std::numbers::log10e;\n'
                          'auto d = RoundedSin(x) + Render::Models::RoundedCos(x) + Libc::sin(x) + mpfr_sin(x);\n'
                          'auto e = glm::radians(x) + glm::length(v) + glm::dot(v, glm::normalize(v));\n'
                          'auto f = glm::inverse(glm::translate(glm::ortho(0.0f, 1.0f, 0.0f, 1.0f), v));\n')

    def test_comments_and_literals_are_not_read(self):
        self.assert_clean('// std::sin(x) is what the original calls\n'
                          '/* glm::rotate(m, a, axis)\n   std::cos(x) */\n'
                          'const char* text = "std::tan(x) and \\"glm::slerp\\"";\n'
                          'const char* raw = R"vectors(std::acos(x) " glm::perspective)vectors";\n'
                          "const char quote = '\"'; // std::log10(x)\n")

    def test_call_after_a_comment_or_a_literal_keeps_its_line(self):
        code, output = self.check('/* two\n   lines */\nconst char* text = "//";\nauto y = std::sin(x);\n')
        self.assertEqual(code, 1)
        self.assertIn(f'{SOURCE}:4: std::sin\n', output)

    def test_digit_separators_do_not_hide_a_call(self):
        self.assert_found("auto y = 1'000 * std::sin(x) + 2'000;\n", 'std::sin')

    def test_bare_c_call_fails(self):
        for line, call in (('auto y = sinf(x);', 'sinf'), ('return cos(x);', 'cos'), ('auto y = ::tan(x);', 'tan'),
                           ('auto y = (float)sin((double)x);', 'sin'), ('auto y = a * atan2 (b, c);', 'atan2'),
                           ('auto y = static_cast<float>(pow(x, 2.0));', 'pow'), ('auto y = logf(x);', 'logf')):
            with self.subTest(line=line):
                self.assert_found(line + '\n', call)

    def test_own_names_that_look_like_c_calls_pass(self):
        self.assert_clean('std::string log(static_cast<std::size_t>(length), 0);\n'
                          'Harness() : log(sink, LogLevel::Debug), drawer(cache, log) {}\n'
                          'file.log(text);\n'
                          'sink->log(text);\n'
                          'float Shader::pow(float x);\n'
                          'auto y = Tables::sin(x) + sinTable(x) + my_cos(x);\n')

    def test_glm_trigonometry_fails(self):
        for line, call in (('auto m = glm::rotate(model, angle, axis);', 'glm::rotate'),
                           ('auto p = glm::perspective(fov, aspect, 1.0f, 2.0f);', 'glm::perspective'),
                           ('auto p = glm::perspectiveFovRH_NO(fov, w, h, 1.0f, 2.0f);', 'glm::perspectiveFovRH_NO'),
                           ('auto q = glm::slerp(a, b, t);', 'glm::slerp'),
                           ('auto q = glm::angleAxis(angle, axis);', 'glm::angleAxis'),
                           ('auto e = glm::eulerAngles(q);', 'glm::eulerAngles'),
                           ('auto s = glm::sin(v);', 'glm::sin'),
                           ('auto a = glm::angle(q);', 'glm::angle'),
                           ('auto q = glm::quat(glm::vec3(pitch, yaw, roll));', 'glm::quat(vec3)'),
                           ('const glm::quat q{glm::radians(degrees)};', 'glm::quat(vec3)')):
            with self.subTest(line=line):
                self.assert_found(line + '\n', call)

    def test_quaternion_from_its_fields_passes(self):
        self.assert_clean('const glm::quat q = glm::quat::wxyz(w, x, y, z);\nglm::quat r(1.0f, 0.0f, 0.0f, 0.0f);\n')

    def test_only_sources_under_src_are_read(self):
        self.write('Client/docs/notes.md', 'std::sin(x)\n')
        self.write('Client/src/World/notes.txt', 'std::sin(x)\n')
        self.write('Client/labs/Wave.cpp', 'auto y = std::sin(x);\n')
        self.assert_clean('int Answer();\n')

    def test_tests_and_headers_are_read(self):
        self.write('Client/tests/world/test_wave.cpp', 'auto y = std::cos(x);\n')
        self.write('Client/src/World/Wave.inc', 'auto y = std::tan(x);\n')
        code, output = self.check('int Answer();\n')
        self.assertEqual(code, 1)
        self.assertIn('Client/tests/world/test_wave.cpp:1: std::cos\n', output)
        self.assertIn('Client/src/World/Wave.inc:1: std::tan\n', output)
        self.assertIn('2 calls', output)

    def test_exception_allows_that_call_in_that_file_only(self):
        self.allow(SOURCE, 'std::pow')
        self.assert_clean('auto gain = std::pow(10.0f, x) + std::pow(2.0f, x);\n')
        self.assert_found('auto gain = std::pow(10.0f, x) + std::exp(x);\n', 'std::exp')
        self.write('Client/src/World/Other.cpp', 'auto gain = std::pow(10.0f, x);\n')
        code, output = self.check('auto gain = std::pow(10.0f, x);\n')
        self.assertEqual(code, 1)
        self.assertIn('Client/src/World/Other.cpp:1: std::pow\n', output)
        self.assertNotIn(f'{SOURCE}:1', output)

    def test_exception_that_no_longer_happens_fails(self):
        self.allow(SOURCE, 'std::pow')
        code, output = self.check('int Answer();\n')
        self.assertEqual(code, 1)
        self.assertIn(f'{SOURCE}\tstd::pow', output)
        self.assertIn('no longer happen', output)


if __name__ == '__main__':
    unittest.main()
