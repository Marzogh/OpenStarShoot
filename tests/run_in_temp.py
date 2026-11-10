"""Run a synthetic CLI test in an isolated temporary output directory."""
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="openstarshoot-test-") as directory:
    print(f"Running {sys.argv[1]} with temporary output", flush=True)
    result = subprocess.run([sys.argv[1], directory])
sys.exit(result.returncode)
