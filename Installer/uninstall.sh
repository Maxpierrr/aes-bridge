#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
if (( EUID != 0 )); then echo "Exécutez avec sudo."; exit 1; fi
driver="/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
manager="/Applications/AES Bridge.app"
shared_audio="/private/tmp/org.maxpierr.aesbridge.audio.v2"
[[ ! -e "${driver}" ]] || rm -R "${driver}"
[[ ! -e "${manager}" ]] || rm -R "${manager}"
[[ ! -e "${shared_audio}" ]] || rm "${shared_audio}"
echo "AES Bridge a été supprimé. Redémarrez le Mac pour finaliser le déchargement du pilote."
