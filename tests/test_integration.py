"""Integration tests for Context-Aware Hybrid Compressor CLI and end-to-end workflows."""

import os
import subprocess
import sys
import tempfile
import unittest


class TestCLIIntegration(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.dir_path = self.temp_dir.name
        self.cli_path = os.path.abspath("compressor.py")

        # Create realistic multi-pattern test file
        self.test_content = (
            b"# Log prefix line 1000\n" * 50
            + bytes(i % 256 for i in range(2000))
            + b"The quick brown fox jumps over the lazy dog!\n" * 40
            + b"\x00" * 500
        )
        self.input_file = os.path.join(self.dir_path, "sample_input.dat")
        with open(self.input_file, "wb") as f:
            f.write(self.test_content)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def _run_cli(self, args: list[str]) -> subprocess.CompletedProcess:
        cmd = [sys.executable, self.cli_path] + args
        return subprocess.run(cmd, capture_output=True, text=True, cwd=os.getcwd())

    def test_cli_help(self) -> None:
        res = self._run_cli(["--help"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("compress", res.stdout)
        self.assertIn("decompress", res.stdout)
        self.assertIn("inspect", res.stdout)
        self.assertIn("benchmark", res.stdout)

    def test_cli_compress_decompress_heuristic_round_trip(self) -> None:
        comp_file = os.path.join(self.dir_path, "sample.cacz")
        dec_file = os.path.join(self.dir_path, "sample.reconstructed")

        # 1. Compress in heuristic mode
        res_comp = self._run_cli(["compress", self.input_file, comp_file, "--mode", "heuristic"])
        self.assertEqual(res_comp.returncode, 0, f"Compress failed: {res_comp.stderr}")
        self.assertTrue(os.path.exists(comp_file))
        self.assertIn("Storage Ratio", res_comp.stdout)

        # 2. Inspect container
        res_insp = self._run_cli(["inspect", comp_file, "--verbose"])
        self.assertEqual(res_insp.returncode, 0, f"Inspect failed: {res_insp.stderr}")
        self.assertIn("HEURISTIC", res_insp.stdout)
        self.assertIn("Codec Selection Distribution", res_insp.stdout)

        # 3. Decompress
        res_dec = self._run_cli(["decompress", comp_file, dec_file])
        self.assertEqual(res_dec.returncode, 0, f"Decompress failed: {res_dec.stderr}")
        self.assertTrue(os.path.exists(dec_file))

        with open(dec_file, "rb") as f:
            reconstructed = f.read()

        self.assertEqual(reconstructed, self.test_content)

    def test_cli_compress_oracle_round_trip(self) -> None:
        comp_file = os.path.join(self.dir_path, "sample_oracle.cacz")
        dec_file = os.path.join(self.dir_path, "sample_oracle.reconstructed")

        res_comp = self._run_cli(["compress", self.input_file, comp_file, "--mode", "oracle"])
        self.assertEqual(res_comp.returncode, 0, f"Oracle compress failed: {res_comp.stderr}")

        res_dec = self._run_cli(["decompress", comp_file, dec_file])
        self.assertEqual(res_dec.returncode, 0, f"Oracle decompress failed: {res_dec.stderr}")

        with open(dec_file, "rb") as f:
            reconstructed = f.read()

        self.assertEqual(reconstructed, self.test_content)

    def test_cli_deterministic_reproducibility(self) -> None:
        comp1 = os.path.join(self.dir_path, "run1.cacz")
        comp2 = os.path.join(self.dir_path, "run2.cacz")

        res1 = self._run_cli(["compress", self.input_file, comp1, "--mode", "heuristic"])
        res2 = self._run_cli(["compress", self.input_file, comp2, "--mode", "heuristic"])
        self.assertEqual(res1.returncode, 0)
        self.assertEqual(res2.returncode, 0)

        with open(comp1, "rb") as f1, open(comp2, "rb") as f2:
            self.assertEqual(f1.read(), f2.read(), "Repeated compression must be bit-for-bit identical")

    def test_cli_missing_input_file_returns_error(self) -> None:
        nonexistent = os.path.join(self.dir_path, "missing_input.dat")
        comp = os.path.join(self.dir_path, "out.cacz")
        res = self._run_cli(["compress", nonexistent, comp])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("does not exist", res.stderr)

    def test_cli_same_input_output_path_rejected(self) -> None:
        res = self._run_cli(["compress", self.input_file, self.input_file])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("cannot be identical", res.stderr)


if __name__ == "__main__":
    unittest.main()
