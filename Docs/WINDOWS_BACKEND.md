# Windows backend preparation

The Windows work is intentionally limited to the reusable backend until the
macOS/Raspberry Pi path is validated on real hardware. It is not presented as a
Windows virtual audio device yet.

What already builds and runs on Windows:

- the portable L24, RTP, SDP, SAP, jitter and lock-free ring code;
- the portable PTPv2 packet codec and E2E four-timestamp calculation tests;
- a Winsock 2.2 UDP implementation with interface-specific multicast and
  source-specific multicast membership;
- a named shared-memory audio layout (`Local\\AESBridge.Audio.v3`) exposing 64
  channels as eight fixed banks of eight;
- `aes-bridge-windows-backend`, the same continuous RTP RX/TX, SAP discovery and
  publication, PTPv2 E2E and reconnect engine used by macOS;
- explicit selection by Windows adapter friendly name or by IPv4 address;
- an isolated Winsock/L24/shared-memory self-test;
- the complete 64-channel/eight-bank live loopback, SAP discovery/deletion,
  source restart, packet phase and simulated-grandmaster PTP tests;
- a two-process CLI lifecycle test which starts the backend, reads and validates
  its JSON status through the named mapping, and checks a clean timed shutdown.

The future Windows audio endpoint is a separate layer. It must expose exactly
64 capture and 64 render channels at 48 kHz and exchange only preallocated blocks
with the backend. A production virtual endpoint will require the Windows Driver
Kit, an appropriate Microsoft virtual-audio/AVStream design, driver signing and
tests in DAWs. None of those are claimed by the current scaffold.

Build on a Visual Studio developer prompt:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\Release\AESBridgeWindowsBackendSelfTest.exe --self-test
build\Release\aes-bridge-windows-backend.exe --list-interfaces
build\Release\aes-bridge-windows-backend.exe --run --interface "Ethernet" --profile computer-b
```

The backend command line intentionally matches the macOS engine. `computer-a`
receives `.80`–`.87` and transmits `.96`–`.103`; `computer-b` reverses those
groups. One side must use each profile. The Windows firewall may require an
inbound UDP rule for the selected private Ethernet network during physical
testing.

The intended boundary is:

```text
Windows audio endpoint (future WDK project)
    ↕ 64-channel named mapping + lock-free per-channel rings
AES Bridge Windows backend
    ↕ eight 8-channel RTP L24 banks + SAP/SDP + PTP state
AES67 network
```
