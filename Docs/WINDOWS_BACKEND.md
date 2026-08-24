# Windows backend preparation

The Windows work is intentionally limited to the reusable backend until the
macOS/Raspberry Pi path is validated on real hardware. It is not presented as a
Windows virtual audio device yet.

What already builds on Windows:

- the portable L24, RTP, SDP, SAP, jitter and lock-free ring code;
- the portable PTPv2 packet codec and E2E four-timestamp calculation tests;
- a Winsock 2.2 UDP implementation with interface-specific multicast and
  source-specific multicast membership;
- a named shared-memory audio layout (`Local\\AESBridge.Audio.v3`) exposing 64
  channels as eight fixed banks of eight;
- `aes-bridge-windows-backend --self-test`, which sends an eight-channel L24 RTP
  packet through Winsock loopback and verifies the shared audio mapping;
- protocol and backend tests in the Windows GitHub Actions job.

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
build\Release\aes-bridge-windows-backend.exe --self-test
```

The intended boundary is:

```text
Windows audio endpoint (future WDK project)
    ↕ 64-channel named mapping + lock-free per-channel rings
AES Bridge Windows backend
    ↕ eight 8-channel RTP L24 banks + SAP/SDP + PTP state
AES67 network
```
