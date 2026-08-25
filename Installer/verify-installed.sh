#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

driver="/Library/Audio/Plug-Ins/HAL/AESBridge.driver"
manager="/Applications/AES Bridge.app"
engine="${manager}/Contents/Resources/aes-bridge-engine"
checker="${manager}/Contents/Resources/AESBridgeCoreAudioCheck"

fail() { print -u2 -- "Vérification AES Bridge: $*"; exit 1; }
bundle_id() { /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist" 2>/dev/null; }

[[ -d "${driver}" ]] || fail "pilote non installé: ${driver}"
[[ -d "${manager}" ]] || fail "application non installée: ${manager}"
[[ -x "${engine}" ]] || fail "moteur embarqué absent"
[[ -x "${checker}" ]] || fail "diagnostic Core Audio embarqué absent: ${checker}"
[[ "$(bundle_id "${driver}")" == "org.maxpierr.aesbridge.driver" ]] || fail "bundle ID du pilote incorrect"
[[ "$(bundle_id "${manager}")" == "org.maxpierr.aesbridge.manager" ]] || fail "bundle ID de l'application incorrect"
/usr/bin/codesign --verify --deep --strict "${driver}" || fail "signature du pilote invalide"
/usr/bin/codesign --verify --deep --strict "${manager}" || fail "signature de l'application invalide"
if /usr/bin/xattr -p com.apple.quarantine "${driver}" >/dev/null 2>&1; then
    fail "le pilote installé porte encore un attribut de quarantaine"
fi
if /usr/bin/xattr -p com.apple.quarantine "${manager}" >/dev/null 2>&1; then
    fail "l'application installée porte encore un attribut de quarantaine"
fi
"${engine}" --list-interfaces >/dev/null || fail "moteur embarqué non exécutable"
stage_root="$(/usr/bin/mktemp -d "${TMPDIR:-/private/tmp}/aes-bridge-installed-check.XXXXXX")"
trap '/bin/rm -rf -- "${stage_root}"' EXIT INT TERM
stage_checker="${stage_root}/AESBridgeCoreAudioCheck"
/usr/bin/ditto --norsrc --noextattr "${checker}" "${stage_checker}"
/usr/bin/xattr -cr "${stage_checker}"
/bin/chmod 0755 "${stage_checker}"
/usr/bin/codesign --force --sign - "${stage_checker}"
/usr/bin/codesign --verify --strict "${stage_checker}" || fail "signature du diagnostic Core Audio invalide"
"${stage_checker}" --wait 15
print -- "Installation AES Bridge vérifiée: bundles, signatures et périphérique Core Audio 64×64/48 kHz valides."
