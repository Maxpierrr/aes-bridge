# AES Bridge for macOS

Experimental GPLv3 AES67 bridge and virtual Core Audio device. The first target
is deliberately fixed to 8 inputs, 8 outputs, L24, 48 kHz and 48 frames per
packet (1 ms). It is designed for LXToolPi/RASPIAUDIO and configurable pairs of
macOS computers; a Windows endpoint is planned after the macOS path is proven.

> Not production-ready. The protocol core and local UDP loopback are tested.
> RTP RX/TX passes an automated eight-channel UDP loopback test. The Core Audio
> bundle builds and signs ad hoc, but real PTP discipline and hardware validation
> are incomplete. RX, TX, PTP, loss behaviour and long-duration stability still
> require the real Raspberry Pi and a second physical endpoint.

## Current checkpoint

| Area | State |
|---|---|
| L24, RTP, SDP, SAP | Implemented and unit-tested |
| Channel order 1–8 | Explicit interleaving test passes |
| Lock-free SPSC audio rings | Implemented and tested |
| Jitter reorder/loss accounting | Implemented and tested |
| Reconnect backoff | Implemented and tested |
| Explicit interface-address UDP binding | Implemented; loopback tested |
| Live RTP RX/TX, 48 frames every 1 ms | Implemented; eight-channel loopback passes |
| Engine ↔ driver lock-free mmap bridge | Implemented and tested between mappings |
| SAP publication | Implemented; live discovery listener pending |
| `AES Bridge` HAL device, 8×8/48 kHz | Bundle compiles; not installed |
| SwiftUI manager and engine controls | Implemented in source; full build needs matching Xcode/SDK |
| PTPv2 slave/domain 0 | Hardware-facing implementation pending |
| Raspberry Pi validation | Pending |

The audio callback only performs bounded copies through preallocated SPSC
rings and atomic counter updates. It contains no network access, locks,
allocation, logging or blocking calls.

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

For Mac-to-Mac operation, use complementary groups: computer A receives `.80`
and transmits `.81`; computer B receives `.81` and transmits `.80`. Each side
must explicitly select its wired interface. This path is architecturally
supported but still needs a two-computer clock and endurance test.

## Build

Prerequisites discovered on the current Mac:

- Apple clang 21 and Swift 6.3.3 are present through Command Line Tools.
- Homebrew is present; CMake 4.4.2 was installed during this checkpoint.
- Xcode is absent.
- libASPL is not available from Homebrew anymore. CMake fetches its official
  source at a pinned commit instead.

Build the protocol core, tools, tests and driver:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAESBRIDGE_BUILD_MANAGER=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Network tests may need to run outside a restricted sandbox:

```sh
./build/AESBridgeTests
./build/aes-bridge-engine --list-interfaces
./build/aes-bridge-engine --print-tx-sdp 192.168.101.103
./build/aes-bridge-engine --run --interface en7 --duration 10
```

The current Swift compiler and Command Line Tools SDK do not match (compiler
6.3.3 versus SDK 6.3.2). Install a full Xcode version compatible with macOS
26.5.2, select it with `xcode-select`, then run `scripts/build-manager.sh`.

The workspace is on SMB. macOS code signing interprets SMB metadata as a
resource fork. Stage release bundles on local APFS (the installer strips
extended attributes and re-signs its exact destination during development).

## Installation policy

Do not install this checkpoint on a production machine. After reviewing the
remaining PTP limitation, a development install will be:

```sh
sudo ./Installer/install-dev.sh
```

It uses `/Library/Audio/Plug-Ins/HAL/AESBridge.driver`, the Apple-documented
HAL location. It does not disable SIP, request Reduced Security, or install a
kernel extension. The uninstaller removes only the exact AES Bridge paths:

```sh
sudo ./Installer/uninstall.sh
```

See [Docs/AUDIT.md](Docs/AUDIT.md) and
[Docs/TESTING_WITH_PI.md](Docs/TESTING_WITH_PI.md).

## License

AES Bridge is licensed under GNU GPL version 3 only. See `LICENSE` and
`THIRD_PARTY_NOTICES.md`. No Merging code or binary is used.
