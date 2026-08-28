#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
control_dir=$(dirname -- "${script_dir}")
target_dir="${AES_BRIDGE_TAURI_TARGET_DIR:-/private/tmp/aes-bridge-tauri-target}"
app="${target_dir}/debug/bundle/macos/AES Bridge.app"

cd "${control_dir}"
CARGO_TARGET_DIR="${target_dir}" npm run tauri build -- --debug --bundles app --no-sign
/usr/bin/xattr -cr "${app}"
/usr/bin/codesign --force --deep --sign - "${app}"
/usr/bin/codesign --verify --deep --strict "${app}"
echo "AES Bridge construit et signé pour le développement: ${app}"
