"""Game-free host ABI checks; ARM64 assembly must also run on the native ARM64 CI runner."""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import host_cpu


class CpuTests(unittest.TestCase):
    def test_arm64_cpu_selection(self):
        for name in ("aarch64", "arm64", "ARM64"):
            with patch("platform.machine", return_value=name), patch("host_cpu.os.name", "posix"):
                self.assertEqual(host_cpu.host_march(), "armv8-a")
                self.assertTrue(host_cpu.host_is_arm64())

    def test_x86_cpu_selection(self):
        with patch("platform.machine", return_value="x86_64"), patch("host_cpu.os.name", "posix"):
            self.assertFalse(host_cpu.host_is_arm64())
            self.assertIn(host_cpu.host_march(), ("x86-64", "x86-64-v2", "x86-64-v3"))

    @unittest.skipUnless(sys.platform == "linux", "POSIX exception translation is Linux-only")
    def test_native_platform(self):
        cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
        self.assertIsNotNone(cc, "a C compiler is required for the platform check")
        with tempfile.TemporaryDirectory(prefix="hookshot-platform-") as folder:
            for opt in ("-O0", "-O2"):
                exe = str(Path(folder) / "platform-test")
                subprocess.run([cc, opt, "-g", "-Wno-attributes",
                                "-I" + str(ROOT / "host"), "-I" + str(ROOT / "host/posix"),
                                str(ROOT / "tests/arm64_platform_test.c"),
                                str(ROOT / "host/posix/win32_posix.c"),
                                "-o", exe, "-pthread", "-ldl", "-lm"], check=True)
                subprocess.run([exe], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
