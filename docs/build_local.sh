#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# docs/build_local.sh
#
# Build the documentation site locally (docs/build.py -> docs/build/html) and
# serve it for viewing.
#
# Usage:
#   docs/build_local.sh                 # build, then serve at http://localhost:8000/
#   docs/build_local.sh --port 9000     # serve on a different port
#   docs/build_local.sh --no-serve      # just build, don't start a server
#
# Requirements: doxygen and python3 (the Sphinx toolchain is pip-installed by
# docs/build.py from docs/requirements.txt).
# ---------------------------------------------------------------------------
set -euo pipefail

PORT=8000
SERVE=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --no-serve) SERVE=0; shift ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
  esac
done

docs_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python3 "$docs_dir/build.py"

site="$docs_dir/build/html"
if [[ "$SERVE" -eq 1 ]]; then
  echo "Serving $site at http://localhost:$PORT/  (Ctrl-C to stop)"
  python3 -m http.server "$PORT" --directory "$site"
fi
