#!/usr/bin/env python3
"""Build the pals-cpp documentation.

Runs Doxygen (C/C++ API -> XML) and then Sphinx (MyST + Furo narrative, with the
API embedded via Breathe). The finished site is written to ``docs/build/html``.

Run from anywhere:  python docs/build.py

Requires ``doxygen`` on PATH. The Python toolchain in ``requirements.txt`` is
pip-installed automatically unless ``--no-install`` is passed.
"""

import shutil
import subprocess
import sys
from pathlib import Path

docs_dir = Path(__file__).parent.resolve()


def run(cmd, cwd):
    print(f"\n$ {' '.join(str(c) for c in cmd)}  (in {cwd})")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(result.returncode)


# 1. Generate the C/C++ API XML with Doxygen (-> docs/doxygen/xml).
if shutil.which("doxygen") is None:
    sys.exit("error: 'doxygen' not found on PATH — install it and try again.")
print("==> Generating API XML (Doxygen)…")
run(["doxygen", "Doxyfile"], cwd=docs_dir)

# 2. Install the Sphinx toolchain (skip with --no-install).
if "--no-install" not in sys.argv:
    print("\n==> Installing Sphinx dependencies…")
    run([sys.executable, "-m", "pip", "install", "-r", "requirements.txt"],
        cwd=docs_dir)

# 3. Build the narrative site with Sphinx (Breathe embeds the API XML).
print("\n==> Building site (Sphinx + Furo + Breathe)…")
run(["sphinx-build", "-b", "html", "src", "build/html"], cwd=docs_dir)

site = docs_dir / "build" / "html"
print(f"\nDone! Site in {site}")
print(f"Open {site / 'index.html'} (API reference under api.html).")
