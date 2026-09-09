import configparser
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

root = Path(__file__).resolve().parents[2]
app = root / 'app'
spec = importlib.util.spec_from_file_location('package', app / 'tools/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
files = package.source_files()
patterns = [rb'/Us' + rb'ers/[^/\s]+/', rb'192\.168\.[0-9]+\.[0-9]+', rb'gh[pousr]_[A-Za-z0-9]{20,}', rb'-----BEGIN [A-Z ]*PRIVATE KEY-----']
for path in files:
    for pattern in patterns:
        assert not re.search(pattern, path.read_bytes()), f'Private data pattern in {path.relative_to(root)}'
assert ET.parse(app / 'config.xml').findtext('./information/version') == (app / 'VERSION').read_text().strip()
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    archive = temp / 'source.zip'
    entries = [(str(p.relative_to(root)), p) for p in files]
    package.write_archive(archive, entries)
    first = archive.read_bytes()
    package.write_archive(archive, entries)
    assert first == archive.read_bytes()
    with zipfile.ZipFile(archive) as z:
        assert set(z.namelist()) == {name for name, _ in entries}
        assert all(z.read(name) == path.read_bytes() for name, path in entries)
        assert all('/config/' not in name and '.runtime/' not in name for name in z.namelist())
    volume = temp / 'kindle'
    config = volume / 'extensions/inkterm/config'
    (volume / 'extensions/inkterm/bin').mkdir(parents=True)
    (volume / 'extensions/inkterm/bin/inkterm').touch()
    key, hosts = temp / 'key', temp / 'hosts'
    key.write_bytes(b'test-key')
    hosts.write_text('test-host')
    command = [sys.executable, str(app / 'tools/provision.py'), str(volume), '--key', str(key), '--known-hosts', str(hosts), '--host', '192.0.2.10', '--user', 'reader']
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    initial = (config / 'settings.ini').read_bytes()
    profile = configparser.ConfigParser()
    profile.read(config / 'settings.ini')
    assert profile['keyboard']['enabled'] == 'en.ini;'
    for field, bad in [('host', 'host;id'), ('user', '-root'), ('session', 'x;id'), ('port', '0'), ('port', '65536')]:
        result = subprocess.run(command + ['--' + field, bad], capture_output=True)
        assert result.returncode != 0 and (config / 'settings.ini').read_bytes() == initial
    assert (config / 'client.dropbear').stat().st_mode & 0o777 == 0o600
    startup = temp / 'start.sh'
    base = volume / 'extensions/inkterm'
    startup.write_text((app / 'bin/start.sh').read_text().replace('/mnt/us/extensions/inkterm', str(base)))
    binary = base / 'bin/inkterm'
    binary.write_text('#!/bin/sh\nexit 0\n')
    binary.chmod(0o755)
    for pin in ('first-host-key', 'replacement-host-key'):
        (config / 'known_hosts').write_text(pin)
        subprocess.run(['sh', str(startup)], check=True)
        assert (config / 'ssh-home/.ssh/known_hosts').read_text() == pin
    (config / 'known_hosts').unlink()
    subprocess.run(['sh', str(startup)], check=True)
    assert (config / 'ssh-home/.ssh/known_hosts').read_bytes() == b''
print(f'PASS: {len(files)} source files, privacy patterns, reproducible ZIP, provisioning validation and isolated SSH pins')
