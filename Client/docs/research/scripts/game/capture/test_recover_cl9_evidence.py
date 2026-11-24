"""Repositorios y capturas sintéticos: no ejecuta originales ni aporta evidencia nativa."""

import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


sys.dont_write_bytecode = True
SCRIPT = Path(__file__).with_name('recover_cl9_evidence.py')
PRIVATE = b'PAYLOAD-PRIVADO-QUE-NO-DEBE-SALIR'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def description(output, data):
    return {'output': output, 'size': len(data), 'sha256': digest(data)}


class RecoverEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.research = self.root / 'research'
        self.lab = self.root / 'lab'
        self.data = self.root / 'Data'
        self.data.mkdir()
        (self.data / 'Gate.bmd').write_bytes(b'asset sintetico')
        self.main_exe = self.root / 'main.exe'
        self.main_exe.write_bytes(b'Esto es un fixture, no un PE ejecutable.')
        self.output = self.root / "salida privada's"
        for repository in (self.research, self.lab):
            repository.mkdir()
            self.git(repository, 'init', '-q')
        self.write(self.research / 'memory.bin', b'memoria sintetica')
        self.write(self.research / 'reference.py', b'# referencia sintetica\n')
        research = self.commit(self.research)
        self.write(self.lab / 'muproxy.log', PRIVATE)
        lab = self.commit(self.lab)
        self.trace = b'glClear()\nwglSwapBuffers()\n'
        compressed = gzip.compress(self.trace, mtime=0)
        self.write(self.lab / 'capture.txt.gz', compressed)
        graphics = self.commit(self.lab)
        self.manifest = {
            'schema_version': 1,
            'source_commits': {'research': research, 'lab': lab, 'graphics': graphics},
            'profiles': {'cl9_sha256': digest(self.main_exe.read_bytes()),
                         'historical_memory_sha256': '1' * 64},
            'artifacts': [
                {'source': 'research', 'path': 'memory.bin', **description('memory/capture/sample.bin', b'memoria sintetica')},
                {'source': 'research', 'path': 'reference.py', **description('tools/c3c4_reference.py', b'# referencia sintetica\n')},
                {'source': 'lab', 'path': 'muproxy.log', **description('wire/muproxy.log', PRIVATE)},
                {'source': 'graphics', 'path': 'capture.txt.gz', **description('gl/capture.txt.gz', compressed),
                 'unpack': description('gl/capture.txt', self.trace)},
            ],
            'generated': [],
            'assets': [{'path': 'Gate.bmd', 'size': len(b'asset sintetico'), 'sha256': digest(b'asset sintetico')}],
            'tests': [{'name': 'Synthetic.Test', 'source': 'fixture.cpp', 'group': 'memory',
                       'required_outputs': ['memory/capture/sample.bin']}],
        }
        self.manifest_file = self.root / 'manifest.json'

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    @staticmethod
    def git(repository, *arguments):
        result = subprocess.run(['git', '-C', str(repository), *arguments],
                                capture_output=True, check=False)
        if result.returncode:
            raise AssertionError('falló Git del fixture sintético')
        return result.stdout.decode('ascii').strip()

    def commit(self, repository):
        self.git(repository, 'add', '.')
        self.git(repository, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 'commit', '-qm', 'fixture')
        return self.git(repository, 'rev-parse', 'HEAD')

    @staticmethod
    def object_present(repository, oid):
        result = subprocess.run(['git', '-C', str(repository), 'cat-file', '-e', oid],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                env={**os.environ, 'GIT_NO_LAZY_FETCH': '1', 'GIT_ALLOW_PROTOCOL': ''},
                                check=False)
        return result.returncode == 0

    def load_helper(self):
        self.assertTrue(SCRIPT.is_file(), 'falta implementar el helper')
        spec = importlib.util.spec_from_file_location('recover_cl9_evidence_for_test', SCRIPT)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        return helper

    def arguments(self, *extra):
        self.manifest_file.write_text(json.dumps(self.manifest), encoding='utf-8')
        return ['--research', str(self.research), '--lab', str(self.lab),
                '--output', str(self.output), '--data-dir', str(self.data),
                '--manifest', str(self.manifest_file), *extra]

    def run_cli(self, *extra):
        result = subprocess.run([sys.executable, '-B', str(SCRIPT), *self.arguments(*extra)],
                                capture_output=True, check=False)
        self.assertNotIn(PRIVATE, result.stdout + result.stderr)
        self.assertNotIn(b'fatal:', result.stdout + result.stderr)
        return result

    def test_pinned_recovery_is_idempotent_and_reports_no_native_execution(self):
        self.write(self.research / 'memory.bin', b'HEAD posterior que no debe usarse')
        self.commit(self.research)
        first = self.run_cli()
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual((self.output / 'memory/capture/sample.bin').read_bytes(), b'memoria sintetica')
        self.assertEqual((self.output / 'gl/capture.txt').read_bytes(), self.trace)
        report = json.loads((self.output / 'inventory-report.json').read_text(encoding='utf-8'))
        self.assertFalse(report['native_verified'])
        self.assertFalse(report['tests_launched'])
        self.assertEqual(report['source_commits'], self.manifest['source_commits'])
        self.assertTrue((self.output / 'env.sh').is_file())
        self.assertTrue((self.output / 'env.ps1').is_file())
        before = {str(p.relative_to(self.output)): (p.read_bytes(), p.stat().st_mtime_ns)
                  for p in self.output.rglob('*') if p.is_file()}
        second = self.run_cli()
        self.assertEqual(second.returncode, 0, second.stderr)
        after = {str(p.relative_to(self.output)): (p.read_bytes(), p.stat().st_mtime_ns)
                 for p in self.output.rglob('*') if p.is_file()}
        self.assertEqual(before, after)
        if os.name != 'nt':
            self.assertEqual((self.output / 'wire/muproxy.log').stat().st_mode & 0o077, 0)

    def test_missing_generated_files_without_main_returns_incomplete(self):
        self.manifest['generated'] = [description('wire/inventory-capture-0.c4', b'sidecar')]
        result = self.run_cli()
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertFalse((self.output / 'wire/inventory-capture-0.c4').exists())
        report = json.loads((self.output / 'inventory-report.json').read_text(encoding='utf-8'))
        self.assertFalse(report['native_verified'])
        self.assertFalse(report['tests_launched'])

    def test_check_only_never_creates_or_updates_files(self):
        missing = self.run_cli('--check-only', '--main-exe', str(self.main_exe))
        self.assertEqual(missing.returncode, 3, missing.stderr)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.run_cli().returncode, 0)
        before = {str(p): p.stat().st_mtime_ns for p in self.output.rglob('*')}
        ready = self.run_cli('--check-only')
        self.assertEqual(ready.returncode, 0, ready.stderr)
        self.assertEqual(before, {str(p): p.stat().st_mtime_ns for p in self.output.rglob('*')})

    def test_output_inside_a_checkout_or_worktree_is_rejected(self):
        worktree = self.root / 'worktree'
        worktree.mkdir()
        (worktree / '.git').write_text('gitdir: no-necesita-ser-un-repo-real\n', encoding='utf-8')
        for parent in (self.research, worktree):
            with self.subTest(parent=parent.name):
                self.output = parent / 'deep' / 'bundle'
                result = self.run_cli()
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(self.output.exists())

    def test_an_empty_dot_git_directory_is_not_a_checkout(self):
        (self.root / '.git').mkdir()
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_output_dot_segments_are_normalized_before_checkout_detection(self):
        self.output = self.research / '..' / 'normalized'
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / 'normalized/env.sh').is_file())

    def test_powershell_five_environment_uses_a_bom_and_survives_a_second_run(self):
        self.output = self.root / "área privada's"
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        powershell = (self.output / 'env.ps1').read_bytes()
        self.assertTrue(powershell.startswith(b'\xef\xbb\xbf'))
        self.assertIn("área privada''s", powershell.decode('utf-8-sig'))
        self.assertFalse((self.output / 'env.sh').read_bytes().startswith(b'\xef\xbb\xbf'))
        self.assertEqual(self.run_cli().returncode, 0)

    def test_a_blob_oid_is_not_accepted_as_a_source_commit(self):
        self.manifest['source_commits']['research'] = self.git(self.research, 'hash-object', 'memory.bin')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(self.output.exists())

    def test_manifest_traversal_and_windows_absolute_paths_are_rejected(self):
        for value in ('../escape.bin', '/absolute.bin', 'C:\\escape.bin', 'wire/../../escape.bin'):
            with self.subTest(path=value):
                self.manifest['artifacts'][0]['output'] = value
                result = self.run_cli()
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(self.output.exists())

    def test_symlinked_output_components_cannot_escape(self):
        target = self.root / 'elsewhere'
        target.mkdir()
        self.output.mkdir()
        try:
            (self.output / 'memory').symlink_to(target, target_is_directory=True)
        except OSError:
            self.skipTest('el host no permite crear el symlink sintético')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(list(target.iterdir()), [])

    def test_canonical_destination_escape_is_rejected_even_without_a_symlink(self):
        helper = self.load_helper()
        redirected = self.output / 'wire/muproxy.log'
        target = self.research / 'private/muproxy.log'
        original = Path.resolve

        def redirected_resolve(path, *arguments, **keywords):
            return target if path == redirected else original(path, *arguments, **keywords)

        with mock.patch.object(Path, 'resolve', redirected_resolve):
            self.assertFalse(redirected.is_symlink())
            with self.assertRaises(helper.EvidenceError):
                helper.destination(self.output, 'wire/muproxy.log')

    def test_publication_rechecks_a_canonical_path_that_became_redirected(self):
        helper = self.load_helper()
        path = helper.destination(self.output, 'wire/muproxy.log')
        original = Path.resolve

        def redirected_resolve(candidate, *arguments, **keywords):
            if candidate == path:
                return self.research / 'private/muproxy.log'
            return original(candidate, *arguments, **keywords)

        with mock.patch.object(Path, 'resolve', redirected_resolve):
            with self.assertRaises(helper.EvidenceError):
                helper.publish(path, PRIVATE)
        self.assertFalse(path.exists())

    @unittest.skipUnless(os.name == 'nt', 'junction NTFS real requiere Windows')
    def test_a_windows_junction_into_a_checkout_cannot_receive_payloads(self):
        target = self.research / 'private'
        target.mkdir()
        self.output.mkdir()
        junction = self.output / 'wire'
        created = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(junction), str(target)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        self.assertEqual(created.returncode, 0, 'no se pudo crear el junction sintético')
        self.assertFalse(junction.is_symlink())
        try:
            result = self.run_cli()
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(list(target.iterdir()), [])
        finally:
            junction.rmdir()

    def test_existing_different_bytes_are_never_overwritten(self):
        path = self.output / 'wire/muproxy.log'
        self.write(path, b'conflicto que debe conservarse')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(path.read_bytes(), b'conflicto que debe conservarse')
        self.assertFalse((self.output / 'memory').exists())

    def test_compressed_and_unpacked_hashes_and_bounds_are_checked_before_publish(self):
        packed = self.manifest['artifacts'][-1]
        original = json.loads(json.dumps(packed))
        for change in ('compressed_hash', 'unpacked_hash', 'unpacked_size'):
            with self.subTest(change=change):
                packed.clear()
                packed.update(json.loads(json.dumps(original)))
                if change == 'compressed_hash':
                    packed['sha256'] = '0' * 64
                elif change == 'unpacked_hash':
                    packed['unpack']['sha256'] = '0' * 64
                else:
                    packed['unpack']['size'] -= 1
                result = self.run_cli()
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(self.output.exists())

    def test_wrong_main_and_asset_hashes_fail_before_any_output_write(self):
        self.main_exe.write_bytes(b'EXE sintetico de identidad incorrecta')
        result = self.run_cli('--main-exe', str(self.main_exe), '--fetch')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(self.output.exists())
        (self.data / 'Gate.bmd').write_bytes(b'asset incorrecto')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(self.output.exists())

    def test_truncated_gzip_is_reported_without_a_traceback_or_partial_publication(self):
        compressed = (self.lab / 'capture.txt.gz').read_bytes()[:-4]
        self.write(self.lab / 'capture.txt.gz', compressed)
        self.manifest['source_commits']['graphics'] = self.commit(self.lab)
        self.manifest['artifacts'][-1].update(description('gl/capture.txt.gz', compressed))
        result = self.run_cli()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr, b'')
        self.assertFalse(self.output.exists())

    def test_foreign_metadata_is_not_overwritten(self):
        path = self.output / 'env.sh'
        self.write(path, b'archivo ajeno')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(path.read_bytes(), b'archivo ajeno')
        self.assertFalse((self.output / 'memory').exists())

    def test_a_report_can_be_updated_when_missing_generated_files_arrive(self):
        payload = b'sidecar verificado'
        self.manifest['generated'] = [description('wire/inventory-capture-0.c4', payload)]
        self.assertEqual(self.run_cli().returncode, 3)
        self.write(self.output / 'wire/inventory-capture-0.c4', payload)
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((self.output / 'inventory-report.json').read_text(encoding='utf-8'))
        self.assertTrue(all(row['present'] for row in report['files']))
        self.assertFalse(report['native_verified'])
        self.assertFalse(report['tests_launched'])

    def test_missing_git_artifact_is_incomplete_without_raw_git_errors(self):
        self.manifest['artifacts'][0]['path'] = 'missing.bin'
        result = self.run_cli()
        self.assertEqual(result.returncode, 3, result.stderr)

    def test_explicit_fetch_uses_pinned_commits_without_checkout_or_fetch_head(self):
        heads = {}
        for repository in (self.research, self.lab):
            self.git(repository, 'remote', 'add', 'origin', str(repository))
            heads[repository] = self.git(repository, 'rev-parse', 'HEAD')
        result = self.run_cli('--fetch')
        self.assertEqual(result.returncode, 0, result.stderr)
        for repository, head in heads.items():
            self.assertEqual(self.git(repository, 'rev-parse', 'HEAD'), head)
            self.assertFalse((repository / '.git/FETCH_HEAD').exists())

    def test_partial_recovery_does_not_fetch_unrelated_blobs_or_change_refs(self):
        large = hashlib.shake_256(b'blob ajeno sintetico').digest(2 * 1024 * 1024)
        snapshots = []
        for source, remote in (('research', self.research), ('lab', self.lab)):
            old_pin = self.manifest['source_commits'][source]
            self.git(remote, 'branch', 'fixture-old', old_pin)
            self.write(remote / 'unrelated-large.bin', large)
            new_pin = self.commit(remote)
            large_oid = self.git(remote, 'hash-object', 'unrelated-large.bin')
            self.manifest['source_commits'][source] = new_pin
            if source == 'lab':
                self.manifest['source_commits']['graphics'] = new_pin
            self.git(remote, 'config', 'uploadpack.allowFilter', 'true')
            clone = self.root / (source + '-partial')
            self.git(self.root, 'clone', '--filter=blob:none', '--no-checkout', '--single-branch',
                     '--branch', 'fixture-old', remote.as_uri(), str(clone))
            self.git(clone, 'config', 'protocol.file.allow', 'always')
            self.assertFalse(self.object_present(clone, large_oid))
            self.assertFalse(self.object_present(clone, new_pin))
            fetch_head = clone / '.git/FETCH_HEAD'
            fetch_head.write_bytes((old_pin + '\t\tfixture preservado\n').encode('ascii'))
            snapshots.append((clone, large_oid, old_pin, fetch_head.read_bytes()))
            setattr(self, source, clone)
        self.assertEqual(self.run_cli().returncode, 3)
        for clone, large_oid, old_pin, fetch_head in snapshots:
            self.assertFalse(self.object_present(clone, large_oid))
        result = self.run_cli('--fetch')
        self.assertEqual(result.returncode, 0, result.stderr)
        for clone, large_oid, old_pin, fetch_head in snapshots:
            with self.subTest(repository=clone.name):
                self.assertEqual(self.git(clone, 'rev-parse', 'HEAD'), old_pin)
                self.assertEqual((clone / '.git/FETCH_HEAD').read_bytes(), fetch_head)
                self.assertFalse(self.object_present(clone, large_oid), 'se descargó un blob ajeno al manifiesto')

    def test_check_only_cannot_fetch(self):
        result = self.run_cli('--check-only', '--fetch')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(self.output.exists())

    def generator_fixture(self, wrong_second=False):
        helper = self.load_helper()
        self.manifest['generated'] = [description('wire/inventory-capture-0.c4', b'sidecar sintetico'),
                                      description('wire/inventory-reference-0.c2', b'referencia sintetica')]
        for name in ('Dec1.dat', 'Enc2.dat'):
            data = b'clave sintetica para el stub del generador'
            self.write(self.lab / name, data)
            self.manifest['artifacts'].append(
                {'source': 'lab', 'path': name, **description('wire/' + name, data)})
        self.manifest['source_commits']['lab'] = self.commit(self.lab)
        generator = self.root / 'generator.py'
        generator.write_text(
            "import os, pathlib, sys\n"
            "assert os.environ['MU_MAIN_EXE'].endswith('main.exe')\n"
            "assert os.environ['MU_C3C4_REFERENCE'].endswith('c3c4_reference.py')\n"
            "assert os.environ['MU_CLIENT_GOLDEN_DIR']\n"
            "out = pathlib.Path(sys.argv[sys.argv.index('--output-dir') + 1])\n"
            "out.mkdir(parents=True, exist_ok=True)\n"
            "(out / 'inventory-capture-0.c4').write_bytes(b'sidecar sintetico')\n"
            "(out / 'inventory-reference-0.c2').write_bytes(" +
            repr(b'referencia incorrecta' if wrong_second else b'referencia sintetica') + ")\n"
            "print('SALIDA-DEL-GENERADOR-QUE-NO-DEBE-REPUBLICARSE')\n",
            encoding='utf-8')
        return helper, generator

    def test_generation_uses_python_and_stages_then_verifies_sidecars(self):
        helper, generator = self.generator_fixture()
        output = io.StringIO()
        with mock.patch.object(helper, 'INVENTORY_SCRIPT', generator), contextlib.redirect_stdout(output):
            code = helper.main(self.arguments('--main-exe', str(self.main_exe)))
        self.assertEqual(code, 0)
        self.assertEqual((self.output / 'wire/inventory-capture-0.c4').read_bytes(), b'sidecar sintetico')
        self.assertNotIn('SALIDA-DEL-GENERADOR', output.getvalue())
        report = json.loads((self.output / 'inventory-report.json').read_text(encoding='utf-8'))
        self.assertFalse(report['native_verified'])
        self.assertFalse(report['tests_launched'])

    def test_generation_checks_every_hash_before_publishing_any_sidecar(self):
        helper, generator = self.generator_fixture(wrong_second=True)
        with mock.patch.object(helper, 'INVENTORY_SCRIPT', generator), contextlib.redirect_stdout(io.StringIO()):
            code = helper.main(self.arguments('--main-exe', str(self.main_exe)))
        self.assertEqual(code, 1)
        self.assertFalse((self.output / 'wire/inventory-capture-0.c4').exists())
        self.assertFalse((self.output / 'wire/inventory-reference-0.c2').exists())


if __name__ == '__main__':
    unittest.main()
