# AES Bridge for macOS

GPLv3 AES67 bridge and experimental 64-input/64-output virtual Core Audio
device. Network transport is deliberately split into eight banks of eight
channels, each using L24, 48 kHz and 48 frames per packet (1 ms). The
LXToolPi/RASPIAUDIO profile activates bank 1 only; computer-to-computer profiles
activate all eight. The Windows backend now runs the same RTP/SAP/PTP engine
against a named 64-channel memory contract for a future virtual Windows audio
endpoint.

> Not production-ready. The protocol core and local UDP loopback are tested.
> RTP RX/TX passes an automated 64-channel/eight-stream UDP loopback test. The Core Audio
> bundle builds and signs ad hoc. A software-timestamped PTPv2 E2E client now
> passes a simulated-grandmaster test, but hardware timestamping and real-device
> validation are incomplete. A non-installed HAL smoke test loads the bundle factory,
> verifies its 64×64/48 kHz properties and exercises a complete 64-channel
> Core Audio → eight-bank RTP/L24 loopback → Core Audio path.
> RX, TX, PTP, loss behaviour and long-duration stability still
> require the real Raspberry Pi and a second physical endpoint.

## Current checkpoint

| Area | State |
|---|---|
| L24, RTP, SDP, SAP | Implemented and unit-tested |
| Channel order 1–64 | Explicit interleaving and eight-bank loopback tests pass |
| Lock-free SPSC audio rings | Implemented and tested |
| Jitter reorder/loss accounting | Implemented and tested |
| Reconnect backoff | Implemented and tested |
| Explicit interface-address UDP binding | Implemented; loopback tested |
| Live RTP RX/TX, 48 frames every 1 ms | Implemented; eight-stream/64-channel loopback passes |
| Engine ↔ driver lock-free mmap bridge | Implemented and tested between mappings |
| Driver-first start and engine restart | Driver prepares safe silence before the engine; owner restart preserves the live mapping and queued audio |
| Crash-safe engine status | Status requires both a live owner lock and `engineRunning`; forced-crash lifecycle test rejects stale state |
| SAP publication, discovery, deletion and expiry | Implemented; live UDP tests pass |
| Session selection, payload type and source filter | Implemented in engine and manager |
| `AES Bridge` HAL device, 64×64/48 kHz | Bundle properties and a full HAL/eight-bank RTP/HAL loopback pass automatically; not installed |
| SwiftUI manager and engine controls | Async status polling, validated parameters, termination-aware restart and CLI lifecycle test; app compiles/signs ad hoc |
| Windows network/shared-memory backend | Common live RX/TX/SAP/PTP engine plus 64-channel named mapping; full loopback and CLI lifecycle run in Windows CI |
| PTPv2 E2E client/domain 0 | Codec, four-timestamp offset/delay, lock timeout and PTP-derived RTP timestamps implemented; simulated GM passes |
| Raspberry Pi validation | Pending |

The audio callback only performs bounded copies through preallocated SPSC
rings and atomic counter updates. It contains no network access, locks,
allocation, logging or blocking calls.

The driver prepares a valid shared block during non-real-time initialization
when the engine has never run. Starting or restarting the engine then takes
exclusive ownership without reconstructing a valid block that a DAW may already
be using. This keeps the callback pointer and queued lock-free rings stable.

PTP currently uses software ingress/egress timestamps. A lock requires fresh
Announce and Sync/Follow_Up messages plus four stable Delay_Req/Delay_Resp E2E
measurements. RTP timestamps are then derived from estimated master time and
all active banks share the same one-millisecond transmit and jitter-playout
epoch. This must not be
treated as hardware-qualified PTP until tested against the real grandmaster,
NIC and switch.

## Network profile matched to LXToolPi

- Pi interface: `eth0`
- Pi → Mac session: `LXToolPi-Inputs-1-8`
- Pi → Mac RTP: `239.69.83.80:5004`
- Mac → Pi proposed RTP: `239.69.83.81:5004`
- SAP: `239.255.255.255:9875`
- RTP TTL: 32
- Format: 8-channel L24, 48,000 Hz, 48 frames / 1 ms
- PTPv2: domain 0, UDPv4, end-to-end delay mechanism
- Safe initial jitter/latency target: 6 packets / 6 ms

The separate `.81` transmit group prevents the two directions from sharing a
multicast destination. Payload type defaults to 96.

For computer-to-computer operation, eight consecutive groups carry channels
1–8 through 57–64. Computer A receives `.80`–`.87` and transmits `.96`–`.103`;
computer B uses the complementary direction. Each side must explicitly select
its wired interface. This path passes synthetic loopback but still needs a
two-computer clock and endurance test. A single 64-channel packet is not used:
its 9,216-byte L24 payload would exceed a standard Ethernet MTU.

## Build

Prerequisites discovered on the current Mac:

- Apple clang 21 and Swift 6.3.3 are present through Command Line Tools.
- Homebrew is present; CMake 4.4.2 was installed during this checkpoint.
- Xcode is absent.
- libASPL is not available from Homebrew anymore. CMake fetches its official
  source at a pinned commit instead.

Build the protocol core, tools, tests and driver. A local APFS build directory
avoids SMB resource-fork metadata during signing:

```sh
cmake -S . -B /private/tmp/aes-bridge-build -DCMAKE_BUILD_TYPE=Release -DAESBRIDGE_BUILD_MANAGER=OFF
cmake --build /private/tmp/aes-bridge-build --parallel
ctest --test-dir /private/tmp/aes-bridge-build --output-on-failure
scripts/build-manager.sh /private/tmp/aes-bridge-build/aes-bridge-engine
```

The runnable development app is written to
`/private/tmp/aes-bridge-dist/AES Bridge.app`. This local APFS destination avoids
the provenance metadata added by the SMB workspace. It is ad-hoc signed for
local testing only. Public distribution without a Gatekeeper warning requires
an Apple Developer ID signature and notarization.

Create the same checksum-protected development archive produced by CI:

```sh
scripts/package-macos.sh /private/tmp/aes-bridge-build \
  "/private/tmp/aes-bridge-dist/AES Bridge.app" artifacts
```

Every successful GitHub Actions run uploads a 14-day macOS development archive
and a statically linked Windows x64 backend archive. Each artifact contains its
own `.sha256` file. These are test builds, not notarized production releases.

Network tests may need to run outside a restricted sandbox:

```sh
/private/tmp/aes-bridge-build/AESBridgeTests
/private/tmp/aes-bridge-build/aes-bridge-engine --list-interfaces
/private/tmp/aes-bridge-build/aes-bridge-engine --print-tx-sdp 192.168.101.103
/private/tmp/aes-bridge-build/aes-bridge-engine --run --interface en7 --duration 10
/private/tmp/aes-bridge-build/aes-bridge-engine --run --interface en7 --profile computer-a --duration 10
```

The manager currently builds with the installed Command Line Tools. Full Xcode
will still be needed later for Developer ID signing, notarization and a proper
installer package, but not for the present ad-hoc development build.

The manager never waits for the status subprocess on the UI thread. Restart
waits for the previous engine process to terminate instead of relying on a
fixed delay, and closing its window terminates the child engine. The automated
CLI lifecycle test covers start, JSON status and SIGTERM shutdown; the macOS CI
also compiles and signs the complete SwiftUI application. No network interface
is selected automatically: the user must explicitly choose the wired Ethernet
port before the engine can start.

The workspace is on SMB. macOS code signing interprets SMB metadata as a
resource fork. Stage release bundles on local APFS (the installer strips
extended attributes and re-signs its exact destination during development).

## Installation policy

Do not install this checkpoint on a production machine. After reviewing the
remaining PTP limitation, first run the non-administrator preflight. It checks
bundle IDs, native architecture, the Core Audio factory, signatures and the
full HAL/RTP loopback when no older driver is installed:

```sh
./Installer/preflight.sh /private/tmp/aes-bridge-build "/private/tmp/aes-bridge-dist/AES Bridge.app"
```

Then perform the development install:

```sh
sudo ./Installer/install-dev.sh /private/tmp/aes-bridge-build "/private/tmp/aes-bridge-dist/AES Bridge.app"
```

It uses `/Library/Audio/Plug-Ins/HAL/AESBridge.driver`, the Apple-documented
HAL location. It does not disable SIP, request Reduced Security, or install a
kernel extension. Both bundles are copied to hidden staging directories,
ad-hoc signed and verified before the previous exact AES Bridge destinations
are replaced, preventing stale files from older versions. The uninstaller
checks both bundle IDs and removes only the exact AES Bridge paths:

```sh
sudo ./Installer/uninstall.sh
```

See [Docs/AUDIT.md](Docs/AUDIT.md) and
[Docs/TESTING_WITH_PI.md](Docs/TESTING_WITH_PI.md). The compilable future
Windows boundary is documented in [Docs/WINDOWS_BACKEND.md](Docs/WINDOWS_BACKEND.md).

## License

AES Bridge is licensed under GNU GPL version 3 only. See `LICENSE` and
`THIRD_PARTY_NOTICES.md`. No Merging code or binary is used.
