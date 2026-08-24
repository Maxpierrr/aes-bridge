#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
project_dir="${0:A:h:h}"
engine_binary="${1:-${project_dir}/build/aes-bridge-engine}"
build_dir="${project_dir}/build-manager"
stage_dir="$(mktemp -d /private/tmp/aes-bridge-manager.XXXXXX)"
trap 'rm -rf "${stage_dir}"' EXIT
app_dir="${stage_dir}/AES Bridge.app"
module_cache="${stage_dir}/ModuleCache"
mkdir -p "${app_dir}/Contents/MacOS" "${app_dir}/Contents/Resources" "${module_cache}"
export CLANG_MODULE_CACHE_PATH="${module_cache}"
swiftc -parse-as-library -O \
  -target arm64-apple-macos13.0 \
  -sdk "$(xcrun --sdk macosx --show-sdk-path)" \
  -framework SwiftUI -framework AppKit -framework Foundation \
  "${project_dir}/ManagerApp/AESBridgeManager.swift" \
  -o "${app_dir}/Contents/MacOS/AESBridgeManager"
[[ -x "${engine_binary}" ]] || { echo "Moteur absent: ${engine_binary}"; exit 1; }
cp "${engine_binary}" "${app_dir}/Contents/Resources/aes-bridge-engine"
cp "${project_dir}/ManagerApp/Info.plist" "${app_dir}/Contents/Info.plist"
codesign --force --sign - "${app_dir}"
rm -rf "${build_dir}/AES Bridge.app"
ditto --norsrc --noextattr "${app_dir}" "${build_dir}/AES Bridge.app"
echo "Built ${build_dir}/AES Bridge.app"
