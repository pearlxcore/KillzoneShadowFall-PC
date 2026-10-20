"""link_modules.module on a synthetic SELF: reads the file once and reports its hash."""
import hashlib
import tempfile
import unittest
import struct
from unittest.mock import patch
from pathlib import Path

from paths import ROOT  # noqa: F401  (sys.path for scripts/)
import link_modules
from test_link_libc import self_fixture
import test_link_libc
from test_probe import package
import isa_rewrite


class ModuleTests(unittest.TestCase):
    def test_dynamic_exports_travel_with_the_linked_image(self):
        main, lib = test_link_libc.LinkTests().fixture()
        key = ('vSRWtiQDUXY', ('libc', 1), ('libc', 1))
        main['identity'] = lambda name: key
        lib['symbols'][1]['identity'] = key
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / 'boot.bin').write_bytes(package(b'\xc3', names=['fixture#q#q'], capabilities=1))
            with patch.object(link_modules, 'module', side_effect=[main, lib]):
                link_modules.link(Path('game'), out, ['libc.prx'], dynamic_exports=True)
            data = (out / 'boot-linked.bin').read_bytes()
        self.assertEqual(data[:8], b'G3PROBE6')
        self.assertEqual(data[160:288].rstrip(b'\0'), b'libc.prx')
        # One binding, then a count and one module-relative identity record.
        self.assertEqual(struct.unpack_from('<Q', data, 320), (1,))
        self.assertEqual(struct.unpack_from('<3Q16s', data, 328),
                         (0, 65552, 1, b'vSRWtiQDUXY'.ljust(16, b'\0')))
        segments, image = isa_rewrite.read_boot_linked(data)
        self.assertEqual(image[65552], 0xc3)
        self.assertEqual(segments[-1], (65536, 4096, 5))

    def test_module_reads_the_file_once_and_reports_its_hash(self):
        data = self_fixture()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'libc.prx'
            path.write_bytes(data)
            info = link_modules.module(path)
        self.assertEqual(info['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(info['missing'], [])
        self.assertEqual(info['symbols'][0]['identity'], ('bzQExy189ZI', ('libc', 1), ('libc', 1)))


if __name__ == '__main__':
    unittest.main()
