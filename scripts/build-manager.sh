#!/bin/zsh
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
project_dir="${0:A:h:h}"
engine_binary="${1:-${project_dir}/build/aes-bridge-engine}"
build_dir="${2:-/private/tmp/aes-bridge-dist}"
core_audio_check="${3:-${engine_binary:h}/AESBridgeCoreAudioCheck}"
stage_dir="$(mktemp -d /private/tmp/aes-bridge-manager.XXXXXX)"
trap 'rm -rf "${stage_dir}"' EXIT
app_dir="${stage_dir}/AES Bridge.app"
module_cache="${stage_dir}/ModuleCache"
mkdir -p "${app_dir}/Contents/MacOS" "${app_dir}/Contents/Resources" "${module_cache}"
export CLANG_MODULE_CACHE_PATH="${module_cache}"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
clt_compat_sdk="/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk"
if [[ "$(xcode-select -p)" == "/Library/Developer/CommandLineTools" && -d "${clt_compat_sdk}" ]]; then
  # The macOS 26.5 CLT image currently pairs Swift 6.3.3 with 6.3.2 SDK
  # interfaces. The bundled 15.4 SDK is compatible and still targets macOS 13.
  sdk_path="${clt_compat_sdk}"
fi
swiftc -parse-as-library -O \
  -target arm64-apple-macos13.0 \
  -sdk "${sdk_path}" \
  -framework SwiftUI -framework AppKit -framework Foundation \
  "${project_dir}/ManagerApp/AESBridgeManager.swift" \
  -o "${app_dir}/Contents/MacOS/AESBridgeManager"
[[ -x "${engine_binary}" ]] || { echo "Moteur absent: ${engine_binary}"; exit 1; }
[[ -x "${core_audio_check}" ]] || { echo "Diagnostic Core Audio absent: ${core_audio_check}"; exit 1; }
cp "${engine_binary}" "${app_dir}/Contents/Resources/aes-bridge-engine"
cp "${core_audio_check}" "${app_dir}/Contents/Resources/AESBridgeCoreAudioCheck"
cp "${project_dir}/ManagerApp/Info.plist" "${app_dir}/Contents/Info.plist"
xattr -cr "${app_dir}"
codesign --force --sign - "${app_dir}/Contents/Resources/aes-bridge-engine"
codesign --force --sign - "${app_dir}/Contents/Resources/AESBridgeCoreAudioCheck"
codesign --force --sign - "${app_dir}"
codesign --verify --deep --strict "${app_dir}"
mkdir -p "${build_dir}"
rm -rf "${build_dir}/AES Bridge.app"
ditto --norsrc --noextattr "${app_dir}" "${build_dir}/AES Bridge.app"
xattr -cr "${build_dir}/AES Bridge.app"
codesign --verify --deep --strict "${build_dir}/AES Bridge.app"
echo "Built ${build_dir}/AES Bridge.app"
