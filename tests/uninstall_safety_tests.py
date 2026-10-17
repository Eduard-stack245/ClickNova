# SPDX-License-Identifier: Apache-2.0
"""Safety regression tests for the portable Linux removal helper.
The Windows PowerShell implementation still requires native Windows tests.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/uninstall-clicknova.sh'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def make_install(base):
    base.mkdir(parents=True)
    (base / 'ClickNova').write_bytes(b'ClickNova standalone executable')
    (base / 'libQt6Core.so').write_bytes(b'bundled lib')
    (base / 'shared-preexisting.so').write_bytes(b'library from another app')
    (base / 'user-notes.txt').write_bytes(b'private user file')
    data = base / 'data'
    (data / 'config').mkdir(parents=True)
    (data / 'config' / 'ClickNova.ini').write_text('personal')
    (data / 'scenarios').mkdir()
    (data / 'scenarios' / 'test.json').write_text('important scenario')
    (data / 'templates').mkdir()
    (data / 'templates' / 'sample.png').write_bytes(b'private image template')
    (data / 'tessdata').mkdir()
    (data / 'tessdata' / 'eng.traineddata').write_bytes(b'ClickNova model')
    (data / 'tessdata' / 'rus.traineddata').write_bytes(b'preexisting language model')
    (data / 'owned-components.tsv').write_text(sha(b'ClickNova model') + '\ttessdata/eng.traineddata\n')
    (base / 'install-manifest.tsv').write_text('# ClickNova portable manifest v1\n' +
        sha(b'ClickNova standalone executable') + '\tClickNova\n' +
        sha(b'bundled lib') + '\tlibQt6Core.so\n')
    shutil.copyfile(SCRIPT, base / 'uninstall-clicknova.sh')
    return data

def run_clean(base, *flags):
    return subprocess.run(['bash', str(base / 'uninstall-clicknova.sh'), '--confirmed', *flags],
                          text=True, capture_output=True)

with tempfile.TemporaryDirectory() as tmp:
    base = Path(tmp) / 'portable'
    data = make_install(base)
    result = run_clean(base, '--remove-components')
    assert result.returncode == 0, result.stderr + result.stdout
    assert not (base / 'ClickNova').exists()
    assert not (base / 'libQt6Core.so').exists()
    assert not (data / 'tessdata' / 'eng.traineddata').exists()
    assert (data / 'tessdata' / 'rus.traineddata').read_bytes() == b'preexisting language model'
    assert (data / 'scenarios' / 'test.json').read_text() == 'important scenario'
    assert (data / 'templates' / 'sample.png').read_bytes() == b'private image template'
    assert (data / 'config' / 'ClickNova.ini').read_text() == 'personal'
    assert (base / 'shared-preexisting.so').read_bytes() == b'library from another app'
    assert (base / 'user-notes.txt').read_bytes() == b'private user file'

with tempfile.TemporaryDirectory() as tmp:
    base = Path(tmp) / 'portable'
    data = make_install(base)
    result = run_clean(base, '--remove-settings', '--remove-scenarios')
    assert result.returncode == 0, result.stderr + result.stdout
    assert not (data / 'scenarios').exists()
    assert not (data / 'templates').exists()
    assert not (data / 'config').exists()
    assert (data / 'tessdata' / 'eng.traineddata').exists()  # not opted to delete

with tempfile.TemporaryDirectory() as tmp:
    base = Path(tmp) / 'portable'
    data = make_install(base)
    (base / 'ClickNova').write_bytes(b'MODIFIED binary')
    result = run_clean(base)
    assert result.returncode == 5, result.stderr + result.stdout
    assert (base / 'ClickNova').read_bytes() == b'MODIFIED binary'
    assert (base / 'install-manifest.tsv').exists()  # allow retry

with tempfile.TemporaryDirectory() as tmp:
    base = Path(tmp) / 'portable'
    data = make_install(base)
    outside = Path(tmp) / 'outside'
    outside.write_bytes(b'important outside')
    (base / 'other-linked.dat').symlink_to(outside)
    with (base / 'install-manifest.tsv').open('a') as manifest:
        manifest.write(sha(b'important outside') + '\tother-linked.dat\n')
    result = run_clean(base)
    assert result.returncode == 5, result.stderr + result.stdout
    assert outside.read_bytes() == b'important outside'
print('portable uninstaller safety tests passed')
