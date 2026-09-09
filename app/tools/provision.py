import argparse
import configparser
from pathlib import Path
import shutil
import re
import os

parser = argparse.ArgumentParser(description='Configure an installed InkTerm with an existing SSH connection')
parser.add_argument('volume', type=Path)
parser.add_argument('--key', required=True, type=Path, help='Dropbear-format private client key')
parser.add_argument('--known-hosts', required=True, type=Path)
parser.add_argument('--host', required=True)
parser.add_argument('--user', required=True)
parser.add_argument('--port', type=int, default=22)
parser.add_argument('--session', default='kindle')
args = parser.parse_args()
if not 1 <= args.port <= 65535:
    parser.error('Port must be 1-65535')
for name, value, pattern in (
    ('host', args.host, r'[A-Za-z0-9][A-Za-z0-9.:-]{0,127}|:[A-Fa-f0-9:]{1,127}'),
    ('user', args.user, r'[A-Za-z0-9_.][A-Za-z0-9_.-]{0,127}'),
    ('session', args.session, r'[A-Za-z0-9_.][A-Za-z0-9_.-]{0,127}'),
):
    if not re.fullmatch(pattern, value):
        parser.error(f'Invalid {name}')
os.umask(0o077)
base = args.volume / 'extensions/inkterm'
if not (base / 'bin/inkterm').is_file():
    parser.error('Extract the InkTerm install archive to the Kindle first')
for path in (args.key, args.known_hosts):
    if not path.is_file():
        parser.error(f'File not found: {path}')
config = base / 'config'
config.mkdir(exist_ok=True)
shutil.copyfile(args.key, config / 'client.dropbear')
shutil.copyfile(args.known_hosts, config / 'known_hosts')
(config / 'client.dropbear').chmod(0o600)
settings = configparser.ConfigParser()
settings['connection'] = dict(host=args.host, user=args.user, port=str(args.port), session=args.session)
settings['display'] = dict(font='8')
settings['keyboard'] = dict(layout='en.ini', enabled='en.ini;')
with (config / 'settings.ini').open('w') as f:
    settings.write(f)
print('Connection configured. Eject the Kindle and launch InkTerm.')
