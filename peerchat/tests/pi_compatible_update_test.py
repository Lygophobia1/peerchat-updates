"""Isolated backup checks; no SSH, live service, or application data access."""
import importlib.util
import pathlib
import sqlite3
import tempfile
import types
import unittest
from unittest.mock import patch
from contextlib import closing

spec = importlib.util.spec_from_file_location(
    'peerchat_update', pathlib.Path(__file__).resolve().parents[1] / 'scripts/pi-compatible-update.py')
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)


class CacheBackupTests(unittest.TestCase):
    def fixture(self, root):
        source = root / 'cache'
        source.mkdir()
        (source / '.peerchat-attachment-cache-instance').write_text('isolated-fixture')
        (source / 'ciphertext').write_bytes(bytes(range(256)))
        (source / 'partial').write_bytes(b'partial-ciphertext')
        with closing(sqlite3.connect(source / 'cache.sqlite')) as db:
            db.execute('create table objects(id text primary key)')
            db.execute("insert into objects values('isolated')")
            db.commit()
        return source

    def test_complete_backup_and_source_unchanged(self):
        with tempfile.TemporaryDirectory(prefix='peerchat-backup-test-') as tmp:
            root = pathlib.Path(tmp)
            source = self.fixture(root)
            before = update.tree_hashes(source)
            stats = types.SimpleNamespace(f_blocks=100*1024**3, f_frsize=1, f_bavail=80*1024**3)
            with patch.object(update.os, 'statvfs', return_value=stats, create=True):
                report = update.backup_cache(source, root / 'backup')
                self.assertEqual(before, update.tree_hashes(root / 'backup'))
                self.assertEqual(before, update.tree_hashes(source))
                self.assertTrue(report['hashes_verified'])
                self.assertEqual(report['index_integrity'], 'ok')
                with self.assertRaises(FileExistsError):
                    update.backup_cache(source, root / 'backup')
                self.assertEqual(before, update.tree_hashes(root / 'backup'))

    def test_reserve_rejection_before_copy(self):
        with tempfile.TemporaryDirectory(prefix='peerchat-backup-test-') as tmp:
            root = pathlib.Path(tmp)
            source = self.fixture(root)
            before = update.tree_hashes(source)
            stats = types.SimpleNamespace(f_blocks=100*1024**3, f_frsize=1, f_bavail=20*1024**3)
            with patch.object(update.os, 'statvfs', return_value=stats, create=True):
                with self.assertRaisesRegex(AssertionError, 'safe backup space'):
                    update.backup_cache(source, root / 'backup')
            self.assertFalse((root / 'backup').exists())
            self.assertEqual(before, update.tree_hashes(source))


if __name__ == '__main__':
    unittest.main()
