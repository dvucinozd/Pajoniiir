"""Execute the production CMake resolver against isolated Git histories."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CMAKE = shutil.which('cmake')
if not CMAKE:
    candidates = list(Path('C:/Espressif/tools/cmake').glob('*/bin/cmake.exe'))
    if candidates:
        CMAKE = str(sorted(candidates)[-1])
if not CMAKE:
    raise RuntimeError('CMake is required for the board version qualification gate')


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.repo = Path(self.temp.name)
        shutil.copyfile(ROOT/'firmware/p4-core/cmake/project_version.cmake', self.repo/'project_version.cmake')
        (self.repo/'resolve.cmake').write_text(
            'include("${CMAKE_CURRENT_LIST_DIR}/project_version.cmake")\n'
            'pajoniiir_resolve_version("${PATTERN}")\n'
            'message("resolved=${PROJECT_VER}")\n')
        self.git('init', '-q')
        self.git('add', '.')
        self.git('-c', 'user.email=fixture@example.invalid', '-c', 'user.name=Fixture', 'commit', '-qm', 'fixture')
        self.git('tag', 'M2.5')

    def tearDown(self):
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], text=True).strip()

    def resolve(self, pattern, *options, success=True):
        result = subprocess.run([CMAKE, f'-DPATTERN={pattern}', *options, '-P', str(self.repo/'resolve.cmake')], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout+result.stderr)
        if success:
            return re.search(r'resolved=([^\s]+)', result.stderr).group(1)

    def test_m3_never_inherits_donor_and_marks_dirty(self):
        self.assertRegex(self.resolve('M3-*'), r'^M3-dev-g[0-9a-f]{12}$')
        (self.repo/'operator-note').write_text('uncommitted')
        self.assertRegex(self.resolve('M3-*'), r'^M3-dev-g[0-9a-f]{12}-dirty$')

    def test_board_tag_ancestry_is_isolated(self):
        self.git('tag', 'M3-51')
        self.git('tag', 'M3-51-4-gabcdef1')
        self.assertEqual(self.resolve('M2*'), 'M2.5')
        self.assertEqual(self.resolve('M3-*'), 'M3-51')

    def test_release_requires_explicit_clean_bounded_version(self):
        self.resolve('M3-*', '-DPAJONIIIR_RELEASE_BUILD=ON', success=False)
        for version in ['M2.5', 'M3-dev-gabcdef123456', 'M3-51junk', 'M3-4294967296', 'M3-51-2-ga', 'M3-51-4294967296-gabcdef1', 'M3-51-2-g'+'a'*30]:
            self.resolve('M3-*', f'-DPAJONIIIR_RELEASE_VERSION={version}', success=False)
        self.assertEqual(self.resolve('M3-*', '-DPAJONIIIR_RELEASE_VERSION=M3-52', '-DPAJONIIIR_RELEASE_BUILD=ON'), 'M3-52')
        (self.repo/'operator-note').write_text('uncommitted')
        self.resolve('M3-*', '-DPAJONIIIR_RELEASE_VERSION=M3-52', '-DPAJONIIIR_RELEASE_BUILD=ON', success=False)


if __name__ == '__main__':
    unittest.main()
