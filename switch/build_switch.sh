#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build DOA3 NX (Nintendo Switch .nro) from the program ./build_game.sh
# already lifted and authenticated. Run from WSL/Linux with devkitPro:
#
#   ./switch/build_switch.sh            # Release build, 2 jobs
#   JOBS=3 ./switch/build_switch.sh     # more jobs if WSL has the RAM (~3 GB each)
#   OPT=-O3 ./switch/build_switch.sh    # lifted code at -O3 (slower build, more RAM)
#   PROFILE=1 ./switch/build_switch.sh  # diagnostic: time every lifted function
#   PGO=gen ./switch/build_switch.sh    # profile-guided build, step 1 (instrumented)
#   PGO=use ./switch/build_switch.sh    # step 2, with the profile in private/pgo
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${DEVKITPRO:=/opt/devkitpro}"
JOBS="${JOBS:-2}"
OPT="${OPT:--O2}"
PROFILE="${PROFILE:-0}"
if [[ "${PROFILE}" == "1" ]]; then FUNC_PROFILE=ON; else FUNC_PROFILE=OFF; fi
case "${PGO:-off}" in
    gen|GEN) PGO_MODE=GEN ;;
    use|USE)
        PGO_MODE=USE
        if ! compgen -G "${ROOT}/private/pgo/*.gcda" >/dev/null; then
            echo "No profile: copy sdmc:/switch/doa3-nx/pgo/*.gcda to ${ROOT}/private/pgo/" >&2
            exit 1
        fi ;;
    *) PGO_MODE=OFF ;;
esac

if [[ ! -f "${DEVKITPRO}/cmake/Switch.cmake" ]]; then
    echo "devkitPro not found at ${DEVKITPRO} (install switch-dev: sudo dkp-pacman -S switch-dev)" >&2
    exit 1
fi

# The generated program build_game.sh validated: the newest private/program-*
# folder whose generated/ holds the chunks.
GENERATED=""
for dir in $(ls -1dt "${ROOT}"/private/program-*/generated 2>/dev/null); do
    if [[ -f "${dir}/recomp_dispatch.c" ]]; then GENERATED="${dir}"; break; fi
done
if [[ -z "${GENERATED}" ]]; then
    echo "No lifted program in private/program-*/generated. Run ./build_game.sh <disc> first." >&2
    exit 1
fi

# The same manifest and EBP count build_game.py pins.
MANIFEST="$(sed -n 's/^PROGRAM_SHA256 = "\([0-9a-f]*\)"/\1/p' "${ROOT}/tools/build_game.py")"
EBP="$(sed -n 's/^PROGRAM_EBP = \([0-9]*\)/\1/p' "${ROOT}/tools/build_game.py")"
if [[ -z "${MANIFEST}" || -z "${EBP}" ]]; then
    echo "Could not read PROGRAM_SHA256/PROGRAM_EBP from tools/build_game.py" >&2
    exit 1
fi

echo "Program:  ${GENERATED}"
echo "Manifest: ${MANIFEST} (EBP ${EBP})"

# The homebrew menu icon (256x256 JPEG) is required. It is cover art, so it
# stays out of git: keep it in private/icon.jpg, which git never touches
# (switch/icon.jpg also works).
ICON=""
for candidate in "${ROOT}/switch/icon.jpg" "${ROOT}/private/icon.jpg"; do
    if [[ -f "${candidate}" ]]; then ICON="${candidate}"; break; fi
done
if [[ -z "${ICON}" ]]; then
    echo "No icon: copy a 256x256 JPEG to ${ROOT}/private/icon.jpg" >&2
    exit 1
fi
echo "Icon:     ${ICON}"
ICON_ARGS=(-DDOA3_NX_ICON="${ICON}")

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then GENERATOR=(-G Ninja); fi

cmake -S "${ROOT}/switch" -B "${ROOT}/build/switch" "${GENERATOR[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DRECOMP_PROGRAM_DIR="${GENERATED}" \
    -DRECOMP_PROGRAM_MANIFEST_SHA256="${MANIFEST}" \
    -DRECOMP_PROGRAM_EBP_EXPECTED="${EBP}" \
    -DDOA3_NX_GENERATED_OPT="${OPT}" \
    -DDOA3_NX_FUNC_PROFILE="${FUNC_PROFILE}" \
    -DDOA3_NX_PGO="${PGO_MODE}" \
    -DDOA3_NX_PGO_DIR="${ROOT}/private/pgo" \
    "${ICON_ARGS[@]}"
cmake --build "${ROOT}/build/switch" --parallel "${JOBS}" --target doa3-nx_nro

OUT="${ROOT}/build/switch/doa3-nx.nro"
ls -lh "${OUT}"
echo
echo "Copy to the SD card:"
echo "  ${OUT}  ->  sdmc:/switch/doa3-nx/doa3-nx.nro"
echo "  extracted USA disc  ->  sdmc:/switch/doa3-nx/disc/  (default.xbe inside)"
