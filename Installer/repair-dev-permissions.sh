#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

if (( EUID != 0 )); then
    print -u2 -- "Exécutez avec sudo."
    exit 1
fi

driver="/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
manager="/Applications/AES Bridge.app"
engine="${manager}/Contents/Resources/aes-bridge-engine"
checker="${manager}/Contents/Resources/AESBridgeCoreAudioCheck"

fail() { print -u2 -- "Réparation AES Bridge: $*"; exit 1; }
bundle_id() { /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist" 2>/dev/null; }

[[ -d "${driver}" ]] || fail "pilote non installé"
[[ -d "${manager}" ]] || fail "application non installée"
[[ "$(bundle_id "${driver}")" == "org.maxpierr.aesbridge.driver" ]] || fail "le chemin du pilote ne contient pas AES Bridge"
[[ "$(bundle_id "${manager}")" == "org.maxpierr.aesbridge.manager" ]] || fail "le chemin de l’application ne contient pas AES Bridge"
[[ -x "${engine}" ]] || fail "moteur embarqué absent"
[[ -x "${checker}" ]] || fail "diagnostic Core Audio embarqué absent"

/usr/sbin/chown -R root:wheel "${driver}" "${manager}"
/bin/chmod -R u+rwX,go+rX,go-w "${driver}" "${manager}"
/usr/bin/xattr -cr "${driver}" "${manager}"
/usr/bin/codesign --force --sign - "${driver}"
/usr/bin/codesign --force --sign - "${engine}"
/usr/bin/codesign --force --sign - "${checker}"
/usr/bin/codesign --force --sign - "${manager}"
/usr/bin/codesign --verify --deep --strict "${driver}" || fail "signature du pilote invalide après réparation"
/usr/bin/codesign --verify --deep --strict "${manager}" || fail "signature de l’application invalide après réparation"

print -- "Permissions AES Bridge réparées et signatures vérifiées. Redémarrez maintenant macOS."
