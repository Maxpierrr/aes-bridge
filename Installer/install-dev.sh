#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
if (( EUID != 0 )); then echo "Exécutez avec sudo."; exit 1; fi
project_dir="${0:A:h:h}"
cmake_build_dir="${1:-${project_dir}/build}"
manager="${2:-/private/tmp/aes-bridge-dist/AES Bridge.app}"
driver="${cmake_build_dir}/AESBridge.driver"
manager_running() { /usr/bin/pgrep -f '[/]AESBridgeManager( |$)' >/dev/null; }
engine_running() { /usr/bin/pgrep -f '[/]aes-bridge-engine( |$)' >/dev/null; }

if manager_running || engine_running; then
    print -u2 -- "Quittez AES Bridge et arrêtez son moteur avant l’installation."
    exit 1
fi

/bin/zsh "${project_dir}/Installer/preflight.sh" "${cmake_build_dir}" "${manager}" --no-runtime

driver_parent="/Library/Audio/Plug-Ins/HAL"
manager_parent="/Applications"
driver_destination="${driver_parent}/AESBridge.driver"
manager_destination="${manager_parent}/AES Bridge.app"
/usr/bin/install -d -m 0755 "${driver_parent}"
driver_stage_root="$(/usr/bin/mktemp -d "${driver_parent}/.aesbridge-driver.XXXXXX")"
manager_stage_root="$(/usr/bin/mktemp -d "${manager_parent}/.aesbridge-manager.XXXXXX")"
cleanup() {
    [[ ! -e "${driver_stage_root}" ]] || /bin/rm -R -- "${driver_stage_root}"
    [[ ! -e "${manager_stage_root}" ]] || /bin/rm -R -- "${manager_stage_root}"
}
trap cleanup EXIT INT TERM
driver_stage="${driver_stage_root}/AESBridge.driver"
manager_stage="${manager_stage_root}/AES Bridge.app"

/usr/bin/ditto --norsrc --noextattr "${driver}" "${driver_stage}"
/usr/bin/ditto --norsrc --noextattr "${manager}" "${manager_stage}"
/usr/sbin/chown -R root:wheel "${driver_stage}" "${manager_stage}"
/usr/bin/xattr -cr "${driver_stage}" "${manager_stage}"
/usr/bin/codesign --force --sign - "${driver_stage}"
/usr/bin/codesign --force --sign - "${manager_stage}/Contents/Resources/aes-bridge-engine"
/usr/bin/codesign --force --sign - "${manager_stage}"
/usr/bin/codesign --verify --deep --strict "${driver_stage}"
/usr/bin/codesign --verify --deep --strict "${manager_stage}"

[[ ! -e "${driver_destination}" ]] || /bin/rm -R -- "${driver_destination}"
[[ ! -e "${manager_destination}" ]] || /bin/rm -R -- "${manager_destination}"
/bin/mv "${driver_stage}" "${driver_destination}"
/bin/mv "${manager_stage}" "${manager_destination}"
/usr/bin/codesign --verify --deep --strict "${driver_destination}"
/usr/bin/codesign --verify --deep --strict "${manager_destination}"

print -- "AES Bridge installé depuis des bundles prévalidés. Redémarrez le Mac avant le premier essai dans Configuration Audio et MIDI."
