#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
if (( EUID != 0 )); then echo "Exécutez avec sudo."; exit 1; fi
driver="/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
manager="/Applications/AES Bridge.app"
shared_audio_v2="/private/tmp/org.maxpierr.aesbridge.audio.v2"
shared_audio_v3="/private/tmp/org.maxpierr.aesbridge.audio.v3"
bundle_id() { /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist" 2>/dev/null; }
manager_running() { /usr/bin/pgrep -f '[/]AESBridgeManager( |$)' >/dev/null; }
engine_running() { /usr/bin/pgrep -f '[/]aes-bridge-engine( |$)' >/dev/null; }

if [[ -e "${driver}" && "$(bundle_id "${driver}")" != "org.maxpierr.aesbridge.driver" ]]; then
    print -u2 -- "Refus de supprimer ${driver}: bundle ID inattendu."
    exit 1
fi
if [[ -e "${manager}" && "$(bundle_id "${manager}")" != "org.maxpierr.aesbridge.manager" ]]; then
    print -u2 -- "Refus de supprimer ${manager}: bundle ID inattendu."
    exit 1
fi

/usr/bin/pkill -TERM -f '[/]AESBridgeManager( |$)' 2>/dev/null || true
/usr/bin/pkill -TERM -f '[/]aes-bridge-engine( |$)' 2>/dev/null || true
for attempt in {1..20}; do
    if ! manager_running && ! engine_running; then break; fi
    /bin/sleep 0.1
done

[[ ! -e "${driver}" ]] || /bin/rm -R -- "${driver}"
[[ ! -e "${manager}" ]] || /bin/rm -R -- "${manager}"
[[ ! -e "${shared_audio_v2}" ]] || /bin/rm -- "${shared_audio_v2}"
[[ ! -e "${shared_audio_v3}" ]] || /bin/rm -- "${shared_audio_v3}"
print -- "AES Bridge a été supprimé. Redémarrez le Mac pour finaliser le déchargement du pilote."
