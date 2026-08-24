#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
if (( EUID != 0 )); then echo "Exécutez avec sudo."; exit 1; fi
project_dir="${0:A:h:h}"
cmake_build_dir="${1:-${project_dir}/build}"
manager="${2:-/private/tmp/aes-bridge-dist/AES Bridge.app}"
driver="${cmake_build_dir}/AESBridge.driver"
[[ -d "${driver}" ]] || { echo "Pilote absent: ${driver}"; exit 1; }
[[ -d "${manager}" ]] || { echo "Application absente: ${manager}"; exit 1; }
install -d -m 0755 "/Library/Audio/Plug-Ins/HAL"
ditto --norsrc --noextattr "${driver}" "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
ditto --norsrc --noextattr "${manager}" "/Applications/AES Bridge.app"
chown -R root:wheel "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
xattr -cr "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
codesign --force --sign - "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
codesign --verify --deep --strict "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
xattr -cr "/Applications/AES Bridge.app"
codesign --force --sign - "/Applications/AES Bridge.app/Contents/Resources/aes-bridge-engine"
codesign --force --sign - "/Applications/AES Bridge.app"
codesign --verify --deep --strict "/Applications/AES Bridge.app"
echo "Installé. Redémarrez le Mac avant le premier essai dans Configuration Audio et MIDI."
