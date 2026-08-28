#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu

engine="${1:?usage: EngineCLISmoke.sh <aes-bridge-engine>}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/aes-bridge-cli.XXXXXX")"
engine_pid=""
parent_pid=""

cleanup() {
    if [ -n "${engine_pid}" ] && kill -0 "${engine_pid}" 2>/dev/null; then
        kill -TERM "${engine_pid}" 2>/dev/null || true
        wait "${engine_pid}" 2>/dev/null || true
    fi
    if [ -n "${parent_pid}" ] && kill -0 "${parent_pid}" 2>/dev/null; then
        kill -TERM "${parent_pid}" 2>/dev/null || true
        wait "${parent_pid}" 2>/dev/null || true
    fi
    rm -rf -- "${test_dir}"
}
trap cleanup EXIT INT TERM

"${engine}" --run --interface-address 127.0.0.1 --profile raspberry \
    --rx-group 127.0.0.1 --tx-group 127.0.0.1 \
    --rx-port 55118 --tx-port 55118 --channels-per-stream 2 \
    --core-audio-start-channel 1 --jitter-packets 3 --no-sap --no-ptp --duration 5 \
    >"${test_dir}/planet-stereo.log" 2>&1 &
engine_pid="$!"

planet_status=""
attempt=0
while [ "${attempt}" -lt 40 ]; do
    if candidate="$("${engine}" --status 2>/dev/null)"; then
        case "${candidate}" in
            *'"engineRunning":true'*'"channelsPerStream":2'*'"activeStreamCount":1'*)
                planet_status="${candidate}"
                break
                ;;
        esac
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ -n "${planet_status}" ] || { cat "${test_dir}/planet-stereo.log" >&2; echo "stereo planet engine status did not become ready" >&2; exit 1; }
kill -TERM "${engine_pid}"
wait "${engine_pid}"
engine_pid=""

"${engine}" --run --interface-address 127.0.0.1 --profile computer-a \
    --rx-group 127.0.0.1 --tx-group 127.0.0.1 \
    --rx-port 55120 --tx-port 55120 --port-stride 1 \
    --jitter-packets 3 --no-sap --no-ptp --duration 5 \
    >"${test_dir}/engine.log" 2>&1 &
engine_pid="$!"

running_status=""
attempt=0
while [ "${attempt}" -lt 40 ]; do
    if ! kill -0 "${engine_pid}" 2>/dev/null; then
        sed -n '1,120p' "${test_dir}/engine.log" >&2
        echo "engine exited before reporting status" >&2
        exit 1
    fi
    if candidate="$("${engine}" --status 2>/dev/null)"; then
        case "${candidate}" in
            *'"engineRunning":true'*'"activeStreamCount":8'*)
                running_status="${candidate}"
                break
                ;;
        esac
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done

[ -n "${running_status}" ] || { echo "engine status did not become ready" >&2; exit 1; }
kill -TERM "${engine_pid}"
wait "${engine_pid}"
engine_pid=""

if "${engine}" --status >/dev/null 2>&1; then
    echo "stopped engine still reported an active status" >&2
    exit 1
fi

"${engine}" --run --interface-address 127.0.0.1 --profile computer-a \
    --rx-group 127.0.0.1 --tx-group 127.0.0.1 \
    --rx-port 55120 --tx-port 55120 --port-stride 1 \
    --jitter-packets 3 --no-sap --no-ptp --duration 5 \
    >"${test_dir}/crash.log" 2>&1 &
engine_pid="$!"

attempt=0
while [ "${attempt}" -lt 40 ]; do
    if "${engine}" --status >/dev/null 2>&1; then break; fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "${attempt}" -lt 40 ] || { echo "restarted engine status did not become ready" >&2; exit 1; }
kill -KILL "${engine_pid}"
wait "${engine_pid}" 2>/dev/null || true
engine_pid=""
if "${engine}" --status >/dev/null 2>&1; then
    echo "crashed engine left a false active status" >&2
    exit 1
fi

sleep 30 &
parent_pid="$!"
"${engine}" --run --interface-address 127.0.0.1 --profile computer-a \
    --rx-group 127.0.0.1 --tx-group 127.0.0.1 \
    --rx-port 55120 --tx-port 55120 --port-stride 1 \
    --jitter-packets 3 --no-sap --no-ptp --parent-pid "${parent_pid}" --duration 10 \
    >"${test_dir}/parent-watch.log" 2>&1 &
engine_pid="$!"

attempt=0
while [ "${attempt}" -lt 40 ]; do
    if "${engine}" --status >/dev/null 2>&1; then break; fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "${attempt}" -lt 40 ] || { echo "parent-watch engine did not become ready" >&2; exit 1; }
kill -TERM "${parent_pid}"
wait "${parent_pid}" 2>/dev/null || true
parent_pid=""

attempt=0
while [ "${attempt}" -lt 60 ] && kill -0 "${engine_pid}" 2>/dev/null; do
    attempt=$((attempt + 1))
    sleep 0.05
done
if kill -0 "${engine_pid}" 2>/dev/null; then
    echo "engine did not stop after its parent exited" >&2
    exit 1
fi
wait "${engine_pid}" 2>/dev/null || true
engine_pid=""
if "${engine}" --status >/dev/null 2>&1; then
    echo "parent-watch engine left an active status" >&2
    exit 1
fi

echo "AES Bridge engine CLI lifecycle passed"
