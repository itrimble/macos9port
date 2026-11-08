"""Exercise real Mach-O dependency metadata used by standalone packaging."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'complete_package', Path(__file__).resolve().parents[1] / 'scripts/complete-package.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class DependencyTests(unittest.TestCase):
    def test_transitive_rpath_and_parenthesized_helper(self):
        with tempfile.TemporaryDirectory(prefix='os999-packaging-test-') as directory:
            root = Path(directory)
            (root / 'b.c').write_text('int fixture_b(void) { return 7; }\n')
            (root / 'a.c').write_text('extern int fixture_b(void); int fixture_a(void) { return fixture_b(); }\n')
            common = root / 'libfixture_b.dylib'
            helper = root / 'Helper (Renderer)'
            subprocess.run(['cc', '-dynamiclib', str(root / 'b.c'), '-o', str(common),
                            '-Wl,-install_name,@rpath/libfixture_b.dylib'], check=True)
            subprocess.run(['cc', '-dynamiclib', str(root / 'a.c'), '-o', str(helper),
                            '-L' + str(root), '-lfixture_b',
                            '-Wl,-install_name,libfixture_a.dylib',
                            '-Wl,-rpath,@loader_path'], check=True)
            dependencies = PACKAGE.dependencies(helper)
            self.assertNotIn('libfixture_a.dylib', dependencies)
            self.assertIn('@rpath/libfixture_b.dylib', dependencies)
            self.assertEqual(PACKAGE.resolve_dependency(helper, helper,
                             '@rpath/libfixture_b.dylib'), common.resolve())
            self.assertEqual(PACKAGE.resolve_dependency(helper, helper,
                             '@loader_path/libfixture_b.dylib'), common.resolve())
            with self.assertRaises(ValueError):
                PACKAGE.resolve_dependency(helper, helper, '@rpath/missing.dylib')
            # Reproduce Homebrew's inherited-rpath/sibling arrangement.
            subprocess.run(['install_name_tool', '-delete_rpath', '@loader_path', str(helper)], check=True)
            self.assertEqual(PACKAGE.resolve_dependency(helper, helper,
                             '@rpath/libfixture_b.dylib'), common.resolve())


if __name__ == '__main__':
    unittest.main()
