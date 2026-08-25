#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

project_dir="${0:A:h:h}"
build_dir="${1:-${project_dir}/build}"
manager="${2:-/private/tmp/aes-bridge-dist/AES Bridge.app}"
output_dir="${3:-${project_dir}/artifacts}"
driver="${build_dir}/AESBridge.driver"
smoke_test="${build_dir}/AESBridgeDriverSmoke"
core_audio_check="${build_dir}/AESBridgeCoreAudioCheck"
archive="${output_dir}/AES-Bridge-macOS-development.zip"
checksum="${archive}.sha256"

[[ -d "${driver}" ]] || { print -u2 -- "Pilote absent: ${driver}"; exit 1; }
[[ -x "${smoke_test}" ]] || { print -u2 -- "Smoke test absent: ${smoke_test}"; exit 1; }
[[ -x "${core_audio_check}" ]] || { print -u2 -- "Diagnostic Core Audio absent: ${core_audio_check}"; exit 1; }
[[ -d "${manager}" ]] || { print -u2 -- "Application absente: ${manager}"; exit 1; }

stage="$(/usr/bin/mktemp -d "${TMPDIR:-/private/tmp}/aes-bridge-package.XXXXXX")"
cleanup() { [[ ! -e "${stage}" ]] || /bin/rm -R -- "${stage}"; }
trap cleanup EXIT INT TERM
package_root="${stage}/AES Bridge macOS development"
/bin/mkdir -p "${package_root}/build" "${output_dir}"
/usr/bin/ditto --norsrc --noextattr "${driver}" "${package_root}/build/AESBridge.driver"
/usr/bin/ditto --norsrc --noextattr "${smoke_test}" "${package_root}/build/AESBridgeDriverSmoke"
/usr/bin/ditto --norsrc --noextattr "${core_audio_check}" "${package_root}/build/AESBridgeCoreAudioCheck"
/usr/bin/ditto --norsrc --noextattr "${manager}" "${package_root}/AES Bridge.app"
/usr/bin/ditto --norsrc --noextattr "${project_dir}/Installer" "${package_root}/Installer"
/usr/bin/ditto --norsrc --noextattr "${project_dir}/Packaging/macOS/INSTALLATION.txt" "${package_root}/INSTALLATION.txt"
/usr/bin/ditto --norsrc --noextattr "${project_dir}/LICENSE" "${package_root}/LICENSE"
/usr/bin/ditto --norsrc --noextattr "${project_dir}/THIRD_PARTY_NOTICES.md" "${package_root}/THIRD_PARTY_NOTICES.md"
/bin/chmod -R u+rwX,go+rX,go-w "${package_root}"
/bin/chmod 0755 "${package_root}/Installer/preflight.sh" "${package_root}/Installer/install-dev.sh" \
    "${package_root}/Installer/uninstall.sh" "${package_root}/Installer/repair-dev-permissions.sh" \
    "${package_root}/Installer/verify-installed.sh" \
    "${package_root}/build/AESBridgeDriverSmoke" "${package_root}/build/AESBridgeCoreAudioCheck"
[[ ! -e "${archive}" ]] || /bin/rm -- "${archive}"
[[ ! -e "${checksum}" ]] || /bin/rm -- "${checksum}"
/usr/bin/ditto -c -k --norsrc --noextattr --keepParent "${package_root}" "${archive}"
hash="$(/usr/bin/shasum -a 256 "${archive}" | /usr/bin/awk '{print $1}')"
/usr/bin/printf '%s  %s\n' "${hash}" "${archive:t}" > "${checksum}"
print -- "Paquet créé: ${archive}"
