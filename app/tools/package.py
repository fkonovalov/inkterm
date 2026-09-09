import argparse
from pathlib import Path
import tempfile
import zipfile

root = Path(__file__).resolve().parents[2]
app = root / 'app'


def source_files():
    files = [root / name for name in ('README.md', 'LICENSE', 'THIRD_PARTY.md', '.gitignore')]
    files += [app / name for name in ('README.md', 'VERSION', 'config.xml', 'menu.json')]
    for directory, names in (
        ('src', ('main.c', 'input.c', 'input.h')),
        ('layouts', ('en.ini', 'ru.ini', 'de.ini', 'fr.ini', 'es.ini')),
        ('tools', ('build.sh', 'check_input.c', 'check_release.py', 'package.py', 'provision.py', 'repair_sdk.py')),
        ('bin', ('start.sh', 'connect.sh')),
    ):
        files += [app / directory / name for name in names]
    files += [app / 'vendor/kterm' / name for name in ('kindle.c', 'kindle.h', 'config.h', 'COPYING')]
    for path in files:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Expected a regular source file: {path.relative_to(root)}')
    return sorted(files)


def write_archive(destination, entries):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as directory:
        temporary = Path(directory) / 'source.zip'
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, path in sorted(entries):
                info = zipfile.ZipInfo(name)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = (0o100755 if path.suffix == '.sh' else 0o100644) << 16
                archive.writestr(info, path.read_bytes())
        temporary.replace(destination)



def main():
    parser = argparse.ArgumentParser(description='Build a source-only InkTerm release archive')
    parser.add_argument('--output', type=Path, default=root / 'dist')
    args = parser.parse_args()
    version = (app / 'VERSION').read_text().strip()
    if not version or any(c not in '0123456789.-abcdefghijklmnopqrstuvwxyz' for c in version):
        parser.error('Invalid VERSION')
    destination = args.output / f'inkterm-{version}-source.zip'
    write_archive(destination, [(str(p.relative_to(root)), p) for p in source_files()])
    print(destination)


if __name__ == '__main__':
    main()
