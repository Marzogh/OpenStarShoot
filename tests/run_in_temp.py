"""Run a synthetic CLI test in an isolated temporary output directory."""
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="openstarshoot-test-") as directory:
    print(f"Running {sys.argv[1]} with temporary output", flush=True)
    result = subprocess.run([sys.argv[1], directory])
if result.returncode:
    print(f"FAIL: {sys.argv[1]} exited with code {result.returncode}", flush=True)
sys.exit(result.returncode)
