#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# macOS and Linux counterpart of the Windows launcher's BUILD GAME page: ./build_game.sh /path/to/doa3.iso
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="python3"
if [[ -f "${SCRIPT_DIR}/private/venv/bin/python3" ]]; then
    PYTHON="${SCRIPT_DIR}/private/venv/bin/python3"
fi
exec "${PYTHON}" "${SCRIPT_DIR}/tools/build_game.py" "$@"
