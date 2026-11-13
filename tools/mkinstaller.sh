#!/bin/bash
# Manual, exact-tag installer build. No installer runs or release uploads here.
export PATH="${MINGW_PREFIX:-/mingw64}/bin:/usr/bin:$PATH"
cd "$(dirname "$0")/.." || exit 1
set -euo pipefail
tag="${1:?Usage: tools/mkinstaller.sh TAG [BUILD_DIR]}"
build="${2:-build}"
python tools/stage_build.py --platform windows-x86_64 --build-dir "$build" \
    --output "release/installer-$tag-staging" --mingw-prefix "${MINGW_PREFIX:-/mingw64}" --release-tag "$tag"
python tools/build_installer.py --tag "$tag" --source-root . \
    --staging "release/installer-$tag-staging" --output "release/installer-$tag" --mingw-prefix "${MINGW_PREFIX:-/mingw64}"
