#!/usr/bin/env python3
"""Recupera evidencia fijada por hash; no ejecuta originales, VM, procesos ni CTest."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shlex
import subprocess
import sys
import tempfile
import zlib


sys.dont_write_bytecode = True
TOOL = 'MuRet recover_cl9_evidence.py schema1'
MARKER = '# ' + TOOL + '\n'
ROOT = Path(__file__).resolve().parents[6]
INVENTORY_SCRIPT = Path(__file__).resolve().parent.parent / 'wire/inventory_fixture.py'
MANIFEST = Path(__file__).with_name('cl9-vm-evidence.json')
METADATA = ('env.sh', 'env.ps1', 'inventory-report.json')


class EvidenceError(Exception):
    """Sólo códigos propios: nunca propagar mensajes con bytes o stderr de entradas privadas."""


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise EvidenceError('argumentos_invalidos')


def require(condition, reason):
    if not condition:
        raise EvidenceError(reason)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fingerprint(path):
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            size += len(chunk)
            digest.update(chunk)
    return {'size': size, 'sha256': digest.hexdigest()}


def relative(value):
    require(isinstance(value, str) and value, 'ruta_relativa_invalida')
    require(not any(ord(c) < 32 or ord(c) == 127 for c in value), 'ruta_con_control')
    require('\\' not in value and ':' not in value, 'ruta_no_portable')
    require(not PureWindowsPath(value).drive and not PurePosixPath(value).is_absolute(), 'ruta_absoluta')
    require(all(part not in ('', '.', '..') and part.lower() != '.git'
                for part in value.split('/')), 'ruta_con_traversal')
    return Path(*PurePosixPath(value).parts)


def without_symlinks(path):
    for candidate in (path, *path.parents):
        require(not candidate.is_symlink(), 'ruta_con_symlink')


def outside_checkouts(path):
    without_symlinks(path)
    for candidate in (path, *path.parents):
        marker = candidate / '.git'
        # Un directorio vacío llamado .git (por ejemplo, un placeholder del host) no es un checkout.
        checkout = marker.is_file() or marker.is_symlink() or (marker.is_dir() and any(
            (marker / name).exists() for name in ('HEAD', 'config', 'objects')))
        require(not checkout, 'salida_dentro_de_git')


def normalized(path):
    absolute = path.absolute()
    without_symlinks(absolute)
    return absolute.resolve()


def destination(root, name):
    path = root / relative(name)
    without_symlinks(path)
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        raise EvidenceError('destino_fuera_de_salida') from None
    # resolve cubre también junctions de Windows en Python3.10, sin depender de is_junction(3.12).
    outside_checkouts(resolved)
    return resolved


def validate_record(record, path_key):
    require(isinstance(record, dict), 'registro_invalido')
    relative(record.get(path_key))
    require(isinstance(record.get('sha256'), str) and
            re.fullmatch('[0-9a-f]{64}', record['sha256']), 'sha256_invalido')
    require(type(record.get('size')) is int and 0 <= record['size'] < sys.maxsize, 'tamano_invalido')


def load_manifest(path):
    raw = path.read_bytes()
    manifest = json.loads(raw)
    require(isinstance(manifest, dict) and manifest.get('schema_version') == 1, 'esquema_invalido')
    pins = manifest.get('source_commits', {})
    require(isinstance(pins, dict) and all(isinstance(pins.get(source), str) and
            re.fullmatch('[0-9a-f]{40}', pins[source]) for source in ('research', 'lab', 'graphics')),
            'commits_invalidos')
    profiles = manifest.get('profiles', {})
    require(isinstance(profiles, dict) and all(isinstance(profiles.get(key), str) and
            re.fullmatch('[0-9a-f]{64}', profiles[key])
            for key in ('cl9_sha256', 'historical_memory_sha256')), 'perfiles_invalidos')
    outputs = {}
    for group in ('artifacts', 'generated', 'assets', 'tests'):
        require(isinstance(manifest.get(group), list), 'lista_invalida')
    for record in manifest['artifacts']:
        validate_record(record, 'output')
        relative(record.get('path'))
        require(record.get('source') in pins, 'origen_invalido')
        add_output(outputs, record)
        if 'unpack' in record:
            validate_record(record['unpack'], 'output')
            add_output(outputs, record['unpack'])
    for record in manifest['generated']:
        validate_record(record, 'output')
        require(relative(record['output']).parts[0] == 'wire', 'sidecar_fuera_de_wire')
        add_output(outputs, record)
    for record in manifest['assets']:
        validate_record(record, 'path')
    for test in manifest['tests']:
        require(isinstance(test, dict) and isinstance(test.get('name'), str) and
                isinstance(test.get('required_outputs'), list), 'test_invalido')
        require(all(name in outputs for name in test['required_outputs']), 'dependencia_desconocida')
    return manifest, outputs, sha256(raw)


def add_output(outputs, record):
    name = record['output']
    require(name not in outputs and name not in METADATA, 'salida_duplicada_o_reservada')
    outputs[name] = record


def verify_bytes(data, record):
    require(len(data) == record['size'] and sha256(data) == record['sha256'], 'contenido_no_coincide')


def existing(path, record):
    without_symlinks(path)
    if not path.exists():
        return False
    require(path.is_file(), 'salida_no_es_archivo')
    actual = fingerprint(path)
    require(actual['size'] == record['size'] and actual['sha256'] == record['sha256'],
            'archivo_existente_diferente')
    return True


def metadata_owned(path):
    without_symlinks(path)
    if not path.exists():
        return
    require(path.is_file(), 'metadata_no_es_archivo')
    if path.name == 'inventory-report.json':
        old = json.loads(path.read_text(encoding='utf-8'))
        require(isinstance(old, dict) and old.get('_tool') == TOOL and old.get('schema_version') == 1
                and old.get('native_verified') is False and old.get('tests_launched') is False,
                'metadata_ajena')
    else:
        require(path.read_text(encoding='utf-8-sig').startswith(MARKER), 'metadata_ajena')


def asset_path(root, name):
    current = root
    without_symlinks(root)
    for part in relative(name).parts:
        if not current.is_dir():
            return current / part
        matches = [p for p in current.iterdir() if p.name.lower() == part.lower()]
        require(len(matches) <= 1, 'asset_ambiguo')
        if not matches:
            return current / part
        current = matches[0]
        without_symlinks(current)
    return current


def assets_report(root, records):
    rows = []
    for record in records:
        path = asset_path(root, record['path'])
        ready = existing(path, record)
        rows.append({'path': record['path'], 'present': ready,
                     'actual': fingerprint(path) if ready else None,
                     'expected_sha256': record['sha256'], 'expected_size': record['size']})
    return rows


def git_environment():
    return {**os.environ, 'GIT_TERMINAL_PROMPT': '0', 'GIT_NO_LAZY_FETCH': '1'}


def git_read_environment():
    # La allowlist vacía vence también configuraciones protocol.<nombre>.allow del repo/usuario.
    return {**git_environment(), 'GIT_ALLOW_PROTOCOL': ''}


def verify_commit_kind(repository, pin):
    result = subprocess.run(['git', '-C', str(repository), 'cat-file', '-t', pin],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            env=git_read_environment(), check=False)
    if result.returncode == 0:
        require(result.stdout.strip() == b'commit', 'pin_no_es_commit')
        return True
    return False


def fetch_pin(repository, pin):
    # Sólo metadatos del commit fijado. Los blobs se piden después, por cada pin:path del manifiesto.
    result = subprocess.run(['git', '-C', str(repository), 'fetch', '--no-tags',
                             '--no-write-fetch-head', '--no-recurse-submodules',
                             '--filter=blob:none', 'origin', pin], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, env=git_environment(), check=False)
    require(result.returncode == 0, 'fetch_del_commit_fallo')


def git_blob(repository, pin, path, size, allow_fetch, fetched):
    if allow_fetch and (repository, pin) not in fetched:
        if not verify_commit_kind(repository, pin):
            fetch_pin(repository, pin)
            verify_commit_kind(repository, pin)
        fetched.add((repository, pin))
    command = ['git', '-C', str(repository)]
    environment = git_read_environment()
    if allow_fetch:
        # También sirve si el commit filtrado se agregó a un clon que antes era completo.
        command += ['-c', 'remote.origin.promisor=true', '-c', 'remote.origin.partialclonefilter=blob:none']
        environment = git_environment()
        environment.pop('GIT_NO_LAZY_FETCH', None)
    else:
        # Bloquea el transporte aunque el Git del host ignore GIT_NO_LAZY_FETCH.
        command += ['-c', 'protocol.allow=never']
    command += ['show', '--no-ext-diff', '--no-textconv', pin + ':' + path]
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                          env=environment) as process:
        data = process.stdout.read(size + 1)
        if len(data) > size:
            process.kill()
            process.wait()
            raise EvidenceError('blob_excede_tamano')
        code = process.wait()
    return data if code == 0 else None


def unpack(data, record):
    with gzip.GzipFile(fileobj=io.BytesIO(data), mode='rb') as source:
        clear = source.read(record['size'] + 1)
    verify_bytes(clear, record)
    return clear


def prepare_artifacts(manifest, repositories, output, allow_fetch):
    prepared = {}
    fetched = set()
    for record in manifest['artifacts']:
        path = destination(output, record['output'])
        data = path.read_bytes() if path.is_file() else git_blob(
            repositories[record['source']], manifest['source_commits'][record['source']],
            record['path'], record['size'], allow_fetch, fetched)
        if data is None:
            continue
        verify_bytes(data, record)
        prepared[record['output']] = data
        if 'unpack' in record:
            prepared[record['unpack']['output']] = unpack(data, record['unpack'])
    return prepared


def publication_path(path):
    without_symlinks(path)
    require(path == path.resolve(), 'destino_redirigido')
    outside_checkouts(path)


def publish(path, data, metadata=False):
    publication_path(path)
    if path.exists():
        if path.read_bytes() == data:
            return
        require(metadata, 'archivo_existente_diferente')
        metadata_owned(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    publication_path(path)
    descriptor, name = tempfile.mkstemp(prefix='.cl9-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'wb') as target:
            target.write(data)
            target.flush()
            os.fsync(target.fileno())
        publication_path(path)
        if metadata:
            metadata_owned(path)
            os.replace(temporary, path)
        else:
            try:
                os.link(temporary, path)
            except FileExistsError:
                require(path.read_bytes() == data, 'archivo_existente_diferente')
    finally:
        temporary.unlink(missing_ok=True)


def generate_inventory(output, main_exe, data_root, records):
    inputs = ('wire/muproxy.log', 'wire/Dec1.dat', 'wire/Enc2.dat', 'tools/c3c4_reference.py')
    if not all((output / name).is_file() for name in inputs):
        return
    with tempfile.TemporaryDirectory(prefix='.cl9-inventory-', dir=output) as directory:
        stage = Path(directory)
        for name in ('muproxy.log', 'Dec1.dat', 'Enc2.dat'):
            publish(stage / 'wire' / name, (output / 'wire' / name).read_bytes())
        environment = {**os.environ, 'MURET_MAIN_EXE': str(main_exe),
                       'MURET_CLIENT_WIRE_DIR': str(stage / 'wire'),
                       'MURET_C3C4_REFERENCE': str(output / 'tools/c3c4_reference.py'),
                       'MURET_CLIENT_GOLDEN_DIR': str(data_root),
                       'MURET_CLIENT_DATA': str(data_root), 'PYTHONDONTWRITEBYTECODE': '1'}
        result = subprocess.run([sys.executable, '-I', '-B', str(INVENTORY_SCRIPT),
                                 '--output-dir', str(stage / 'wire')], env=environment,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        require(result.returncode == 0, 'generacion_de_sidecars_fallo')
        ready = {}
        for record in records:
            path = stage / relative(record['output'])
            require(existing(path, record), 'sidecar_no_generado')
            ready[record['output']] = path.read_bytes()
        for name, payload in ready.items():
            publish(destination(output, name), payload)


def file_rows(output, records):
    return [{'path': name, 'present': existing(destination(output, name), record),
             'actual': fingerprint(output / name) if (output / name).is_file() else None,
             'expected_sha256': record['sha256'], 'expected_size': record['size']}
            for name, record in records.items()]


def environment_files(output, data_root, main_exe):
    values = {'MURET_CLIENT_MEMORY_DIR': str(output / 'memory'),
              'MURET_CLIENT_WIRE_DIR': str(output / 'wire'),
              'MURET_CLIENT_GL_TRACE_DIR': str(output / 'gl'),
              'MURET_C3C4_REFERENCE': str(output / 'tools/c3c4_reference.py'),
              'MURET_CLIENT_GOLDEN_DIR': str(data_root), 'MURET_CLIENT_DATA': str(data_root)}
    if main_exe is not None:
        values['MURET_MAIN_EXE'] = str(main_exe)
    shell = MARKER + ''.join('export ' + key + '=' + shlex.quote(value) + '\n' for key, value in values.items())
    powershell = MARKER + ''.join("$env:" + key + " = '" + value.replace("'", "''") + "'\n"
                                 for key, value in values.items())
    return {'env.sh': shell.encode('utf-8'), 'env.ps1': powershell.encode('utf-8-sig')}


def arguments(argv):
    parser = Parser(description=__doc__)
    for name in ('research', 'lab', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--manifest', type=Path, default=MANIFEST)
    parser.add_argument('--main-exe', type=Path)
    parser.add_argument('--data-dir', type=Path, default=ROOT / 'Client/assets/Data')
    parser.add_argument('--fetch', action='store_true')
    parser.add_argument('--check-only', action='store_true')
    return parser.parse_args(argv)


def recover(args):
    manifest, records, manifest_hash = load_manifest(args.manifest)
    output = normalized(args.output)
    outside_checkouts(output)
    require(not (args.fetch and args.check_only), 'check_only_no_permite_fetch')
    main_exe = args.main_exe.resolve() if args.main_exe is not None else None
    main_hash = fingerprint(main_exe)['sha256'] if main_exe is not None else None
    require(main_hash is None or main_hash == manifest['profiles']['cl9_sha256'], 'main_exe_no_es_cl9')
    data_root = normalized(args.data_dir)
    assets = assets_report(data_root, manifest['assets'])
    for name, record in records.items():
        existing(destination(output, name), record)
    for name in METADATA:
        metadata_owned(destination(output, name))
    repositories = {'research': args.research.resolve(), 'lab': args.lab.resolve(),
                    'graphics': args.lab.resolve()}
    require(all(path.is_dir() for path in repositories.values()), 'repositorio_ausente')
    for source, repository in repositories.items():
        verify_commit_kind(repository, manifest['source_commits'][source])
    if not args.check_only:
        prepared = prepare_artifacts(manifest, repositories, output, args.fetch)
        for name, payload in prepared.items():
            publish(destination(output, name), payload)
        missing = [record for record in manifest['generated']
                   if not existing(destination(output, record['output']), record)]
        if missing and main_exe is not None and all(row['present'] for row in assets):
            generate_inventory(output, main_exe, data_root, manifest['generated'])
    files = file_rows(output, records)
    present = {row['path'] for row in files if row['present']}
    tests = [{'name': test['name'], 'group': test.get('group'),
              'inputs_ready': all(name in present for name in test['required_outputs'])}
             for test in manifest['tests']]
    report = {'_tool': TOOL, 'schema_version': 1, 'manifest_sha256': manifest_hash,
              'source_commits': manifest['source_commits'], 'profiles': manifest['profiles'],
              'main_exe_sha256': main_hash, 'native_verified': False, 'tests_launched': False,
              'files': files, 'assets': assets, 'tests': tests}
    if not args.check_only:
        for name, payload in environment_files(output, data_root, main_exe).items():
            publish(destination(output, name), payload, metadata=True)
        publish(destination(output, 'inventory-report.json'),
                (json.dumps(report, indent=2, ensure_ascii=False) + '\n').encode('utf-8'), metadata=True)
    ready = all(row['present'] for row in files + assets)
    print(json.dumps({'files_ready': len(present), 'files_expected': len(files),
                      'assets_ready': sum(row['present'] for row in assets),
                      'assets_expected': len(assets), 'manifest_sha256': manifest_hash,
                      'native_verified': False, 'tests_launched': False}))
    return 0 if ready else 3


def main(argv=None):
    try:
        return recover(arguments(argv))
    except (EvidenceError, OSError, ValueError, TypeError, KeyError, OverflowError, EOFError, zlib.error) as problem:
        reason = str(problem) if isinstance(problem, EvidenceError) else 'entrada_o_sistema_invalido'
        print(json.dumps({'error': reason, 'native_verified': False, 'tests_launched': False}))
        return 1


if __name__ == '__main__':
    sys.exit(main())
