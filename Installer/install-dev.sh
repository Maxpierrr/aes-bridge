#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
if (( EUID != 0 )); then echo "Exécutez avec sudo."; exit 1; fi
project_dir="${0:A:h:h}"
driver="${project_dir}/build/AESBridge.driver"
manager="${project_dir}/build-manager/AES Bridge.app"
[[ -d "${driver}" ]] || { echo "Pilote absent: ${driver}"; exit 1; }
[[ -d "${manager}" ]] || { echo "Application absente: ${manager}"; exit 1; }
install -d -m 0755 "/Library/Audio/Plug-Ins/HAL"
ditto --norsrc --noextattr "${driver}" "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
ditto --norsrc --noextattr "${manager}" "/Applications/AES Bridge.app"
chown -R root:wheel "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
xattr -cr "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
codesign --force --sign - "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
codesign --verify --deep --strict "/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
echo "Installé. Redémarrez le Mac avant le premier essai dans Configuration Audio et MIDI."
