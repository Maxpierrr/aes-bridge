#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
control_dir=$(dirname -- "${script_dir}")
repository_dir=$(dirname -- "${control_dir}")
build_dir="${TMPDIR:-/private/tmp}/aes-bridge-sidecar-build"
host_target=$(rustc -vV | sed -n 's/^host: //p')

case "${host_target}" in
    aarch64-apple-darwin|x86_64-apple-darwin) ;;
    *)
        echo "Cible macOS Rust non prise en charge: ${host_target}" >&2
        exit 1
        ;;
esac

cmake -S "${repository_dir}" -B "${build_dir}" \
    -DAESBRIDGE_BUILD_DRIVER=OFF \
    -DAESBRIDGE_BUILD_MANAGER=OFF \
    -DAESBRIDGE_BUILD_TESTS=OFF
cmake --build "${build_dir}" --target aes-bridge-engine -j 4

destination_dir="${control_dir}/src-tauri/binaries"
mkdir -p "${destination_dir}"
cp "${build_dir}/aes-bridge-engine" "${destination_dir}/aes-bridge-engine-${host_target}"
chmod 0755 "${destination_dir}/aes-bridge-engine-${host_target}"
echo "Sidecar AES Bridge prêt pour ${host_target}."
