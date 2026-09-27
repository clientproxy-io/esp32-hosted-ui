#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ui_dist="$repo_dir/ui/dist"
archive="$repo_dir/dist/esp32-hosted-ui.zip"

if [[ ! -f "$ui_dist/index.html" ]]; then
  echo "Missing ui/dist/index.html; run 'npm run build' in ui/ first" >&2
  exit 1
fi

mkdir -p "$repo_dir/dist"
rm -f "$archive"
(
  cd "$ui_dist"
  zip -q -r "$archive" . -x '*.map'
)

if ! unzip -Z1 "$archive" | grep -qx 'index.html'; then
  echo "Archive must contain index.html at its root" >&2
  exit 1
fi

size=$(wc -c < "$archive" | tr -d ' ')
if (( size > 10 * 1024 * 1024 )); then
  echo "Archive exceeds the 10 MB App File Cache upload limit" >&2
  exit 1
fi
echo "Created $archive ($size bytes)"
