"""Collect pinned source archives and license notices without extracting tar paths."""
import argparse
import hashlib
import json
import shutil
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_member(archive, member, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with archive.extractfile(member) as source, destination.open('wb') as output:
        shutil.copyfileobj(source, output)


def collect(stage):
    licenses = stage / 'licenses'
    downloads = ROOT / '.tools/release-downloads'
    pins = json.loads((ROOT / 'packaging/qt-sources.json').read_text())
    for pin in pins:
        archive_path = downloads / pin['Name']
        if digest(archive_path).upper() != pin['SHA256']:
            raise RuntimeError(f'Source archive checksum mismatch: {archive_path}')
        module = pin['Name'].split('-everywhere-')[0]
        with tarfile.open(archive_path) as archive:
            for member in archive.getmembers():
                if not member.isfile():
                    continue
                parts = PurePosixPath(member.name).parts[1:]
                if not parts or '..' in parts or any(':' in part or '\\' in part for part in parts):
                    raise RuntimeError('Unsafe archive member')
                relative = Path(*parts)
                lower = relative.name.lower()
                if ('LICENSES' in parts or lower.startswith(('license', 'copying', 'copyright', 'authors', 'notice'))
                        or lower in ('qt_attribution.json', 'readme', 'readme.md', 'readme.txt')):
                    write_member(archive, member, licenses / module / relative)
                if module == 'qttranslations' and str(relative).replace('\\', '/') == 'translations/qtbase_es.ts':
                    write_member(archive, member, stage.parent / 'qtbase_es.ts')
    for package in (ROOT / '.tools/vcpkg_installed/x64-windows/share').iterdir():
        notice = package / 'copyright'
        if notice.is_file():
            target = licenses / 'vcpkg' / (package.name + '.txt')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, target)
    engine = ROOT / '.tools/PDF4QT'
    for name, origin in {
        'PDF4QT.txt': 'LICENSE',
        'Adobe-CMaps.txt': 'Pdf4QtLibCore/cmaps/LICENSE.txt',
        'Liberation-fonts.txt': 'Pdf4QtLibCore/liberation-fonts-ttf/LICENSE',
        'Liberation-AUTHORS.txt': 'Pdf4QtLibCore/liberation-fonts-ttf/AUTHORS',
        'Adobe-AATL-origin.txt': 'Pdf4QtLibCore/aatl/source.txt',
    }.items():
        target = licenses / 'pdf4qt' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(engine / origin, target)
    shutil.copytree(engine / '3rdparty_licenses', licenses / 'pdf4qt/upstream-notices', dirs_exist_ok=True)
    shutil.copy2(ROOT / '.tools/vcpkg_installed/vcpkg/status', licenses / 'vcpkg/installed-packages.txt')
    sbom = ROOT / '.tools/Qt/6.9.3/msvc2022_64/sbom'
    if sbom.exists():
        shutil.copytree(sbom, licenses / 'qt-sdk-sbom', dirs_exist_ok=True)
    (stage / 'source-archives.json').write_text(json.dumps(pins, indent=2) + '\n')


def archives(output, version):
    with zipfile.ZipFile(output / f'PDF-Editor-{version}-source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in ['src', 'tests', 'cmake', 'scripts', 'tools', 'resources', 'packaging', 'docs', 'third_party', '.github']:
            folder = ROOT / name
            if folder.exists():
                for path in sorted(folder.rglob('*')):
                    if path.is_file() and '__pycache__' not in path.parts:
                        archive.write(path, Path('PDF-Editor') / path.relative_to(ROOT))
        for path in sorted(ROOT.iterdir()):
            if path.is_file() and (path.suffix.lower() in ('.md', '.json', '.txt') or path.name in ('LICENSE', '.gitignore')):
                archive.write(path, Path('PDF-Editor') / path.name)
    with zipfile.ZipFile(output / f'PDF-Editor-{version}-dependency-sources.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for pin in json.loads((ROOT / 'packaging/qt-sources.json').read_text()):
            archive.write(ROOT / '.tools/release-downloads' / pin['Name'], 'qt/' + pin['Name'])
        engine = ROOT / '.tools/PDF4QT'
        for path in sorted(engine.rglob('*')):
            relative = path.relative_to(engine)
            if any(part in ('.git', 'build', 'vcpkg_installed', '__pycache__') for part in relative.parts):
                continue
            if path.is_file():
                archive.write(path, Path('PDF4QT-patched') / relative)
        for name in ('dependencies.json', 'packaging/qt-sources.json', 'packaging/FUENTES.md'):
            archive.write(ROOT / name, name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['collect', 'archives'])
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    if args.action == 'collect':
        collect(args.directory.resolve())
    else:
        archives(args.directory.resolve(), json.loads((ROOT / 'packaging/release.json').read_text())['version'])
