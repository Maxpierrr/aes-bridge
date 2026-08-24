#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

project_dir="${0:A:h:h}"
cmake_build_dir="${1:-${project_dir}/build}"
manager="${2:-/private/tmp/aes-bridge-dist/AES Bridge.app}"
runtime_check=true
[[ "${3:-}" != "--no-runtime" ]] || runtime_check=false
driver="${cmake_build_dir}/AESBridge.driver"
driver_binary="${driver}/Contents/MacOS/AESBridge"
manager_binary="${manager}/Contents/MacOS/AESBridgeManager"
engine_binary="${manager}/Contents/Resources/aes-bridge-engine"

fail() { print -u2 -- "Préflight AES Bridge: $*"; exit 1; }
bundle_id() { /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist" 2>/dev/null; }
check_architecture() {
    local binary="$1"
    local label="$2"
    local architecture="$(/usr/bin/uname -m)"
    local architectures="$(/usr/bin/lipo -archs "${binary}" 2>/dev/null)" || fail "binaire ${label} invalide"
    [[ " ${architectures} " == *" ${architecture} "* ]] || fail "${label} ne contient pas l’architecture ${architecture} (${architectures})"
}

[[ -d "${driver}" ]] || fail "pilote absent: ${driver}"
[[ -d "${manager}" ]] || fail "application absente: ${manager}"
[[ -x "${driver_binary}" ]] || fail "binaire du pilote absent"
[[ -x "${manager_binary}" ]] || fail "binaire du gestionnaire absent"
[[ -x "${engine_binary}" ]] || fail "moteur embarqué absent"
[[ "$(bundle_id "${driver}")" == "org.maxpierr.aesbridge.driver" ]] || fail "bundle ID du pilote incorrect"
[[ "$(bundle_id "${manager}")" == "org.maxpierr.aesbridge.manager" ]] || fail "bundle ID de l’application incorrect"
check_architecture "${driver_binary}" "pilote"
check_architecture "${manager_binary}" "gestionnaire"
check_architecture "${engine_binary}" "moteur"

symbols="$(/usr/bin/nm -gjU "${driver_binary}" 2>/dev/null)" || fail "symboles du pilote illisibles"
[[ "${symbols}" == *"AESBridgePlugInFactory"* ]] || fail "factory Core Audio absente"

stage_root="$(/usr/bin/mktemp -d "${TMPDIR:-/private/tmp}/aes-bridge-preflight.XXXXXX")"
trap '/bin/rm -rf -- "${stage_root}"' EXIT INT TERM
stage_driver="${stage_root}/AESBridge.driver"
stage_manager="${stage_root}/AES Bridge.app"
/usr/bin/ditto --norsrc --noextattr "${driver}" "${stage_driver}"
/usr/bin/ditto --norsrc --noextattr "${manager}" "${stage_manager}"
/usr/bin/xattr -cr "${stage_driver}" "${stage_manager}"
/usr/bin/codesign --force --sign - "${stage_driver}"
/usr/bin/codesign --force --sign - "${stage_manager}/Contents/Resources/aes-bridge-engine"
/usr/bin/codesign --force --sign - "${stage_manager}"
/usr/bin/codesign --verify --deep --strict "${stage_driver}" || fail "signature pilote invalide"
/usr/bin/codesign --verify --deep --strict "${stage_manager}" || fail "signature application invalide"
"${stage_manager}/Contents/Resources/aes-bridge-engine" --list-interfaces >/dev/null || fail "moteur non exécutable"

if ${runtime_check}; then
    smoke_test="${cmake_build_dir}/AESBridgeDriverSmoke"
    if [[ -e "/Library/Audio/Plug-Ins/HAL/AESBridge.driver" ]]; then
        print -- "Préflight: test runtime HAL ignoré car un pilote AES Bridge est déjà installé."
    elif [[ -x "${smoke_test}" ]]; then
        stage_smoke_test="${stage_root}/AESBridgeDriverSmoke"
        /usr/bin/ditto --norsrc --noextattr "${smoke_test}" "${stage_smoke_test}"
        /usr/bin/xattr -cr "${stage_smoke_test}"
        /bin/chmod 0755 "${stage_smoke_test}"
        /usr/bin/codesign --force --sign - "${stage_smoke_test}"
        /usr/bin/codesign --verify --strict "${stage_smoke_test}" || fail "signature du smoke test invalide"
        "${stage_smoke_test}" "${stage_driver}/Contents/MacOS/AESBridge" || fail "test HAL/RTP bout en bout échoué"
    else
        fail "exécutable AESBridgeDriverSmoke absent"
    fi
fi

print -- "Préflight AES Bridge réussi: bundles, IDs, architecture, factory et signatures valides."
