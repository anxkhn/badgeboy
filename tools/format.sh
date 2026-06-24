#!/usr/bin/env bash
# Format BadgeBoy's own C sources in place with clang-format.
# Vendored code under third_party/ and generated headers are left untouched.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v clang-format >/dev/null; then
  echo "clang-format not found. Install it, for example:" >&2
  echo "  sudo apt install -y clang-format" >&2
  echo "  # or in a venv: pip install clang-format" >&2
  exit 1
fi

mapfile -t files < <(find "$HERE/src" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \))

mode="${1:-write}"
case "$mode" in
  --check) clang-format --dry-run --Werror "${files[@]}"; echo "format OK" ;;
  *)       clang-format -i "${files[@]}"; echo "formatted ${#files[@]} files" ;;
esac
