"""Check startup of the real CLI without arguments; this cannot open a camera."""
import subprocess
import sys

print("Checking real diagnostic CLI startup and usage; no camera access", flush=True)
result = subprocess.run([sys.argv[1]], capture_output=True, text=True)
print(result.stdout, end="", flush=True)
print(result.stderr, end="", file=sys.stderr, flush=True)
assert result.returncode == 2, f"CLI exited with {result.returncode}, expected usage exit 2"
assert "Usage: starshoot-diag" in result.stdout
print("PASS: real CLI usage and exit status", flush=True)
