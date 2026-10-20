"""Supported game-build verification for the Killzone pipeline catalog."""
from functools import lru_cache
import hashlib
import json
from pathlib import Path

CATALOG = Path(__file__).resolve().parents[1] / 'catalogs/CUSA00190-01.81.json'


@lru_cache(maxsize=1)
def load_catalog():
    return json.loads(CATALOG.read_text(encoding='utf-8'))


@lru_cache(maxsize=32)
def _identity_digest(path, size, modified_ns, changed_ns):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_game_build(folder, *, full=False, progress=None):
    catalog = load_catalog()
    records = catalog['files']
    if not full:
        records = [r for r in records if r['file'] in catalog['identity_files']]
    for index, record in enumerate(records):
        path = Path(folder) / record['file']
        stat = path.stat()
        if stat.st_size != record['bytes']:
            raise ValueError(f"Unsupported game build: {record['file']} differs from CUSA00190 / 01.81 reference.")
        if full:
            with path.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        else:
            actual = _identity_digest(str(path.resolve()), stat.st_size,
                                      stat.st_mtime_ns, stat.st_ctime_ns)
        if actual != record['sha256']:
            raise ValueError(f"Unsupported game build: {record['file']} does not match the supported CUSA00190 / 01.81 files.")
        if progress and (index % 100 == 0 or index + 1 == len(records)):
            progress(f'Checking supported game files: {index + 1}/{len(records)}')
    return catalog['catalog_id']
