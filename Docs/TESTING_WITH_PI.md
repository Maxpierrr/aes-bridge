# Progressive Raspberry Pi test procedure

Stop at the first failed stage. Keep speakers/amplifiers muted until channel
identity and full-scale behaviour are proven.

## 0. Record the baseline

On the Mac, record the wired interface name/address and ensure Wi-Fi is not
selected for AES67. On the Pi, run:

```sh
AES67_INTERFACE=eth0 ./scripts/check_aes67_pi.sh
```

Save `ip -brief address`, `ethtool eth0`, `aplay -l`, `arecord -l`, the full
generated PipeWire AES67 configuration, `pw-dump`, `ptp4l` logs and the output
of `pmc -u -b 0 'GET TIME_STATUS_NP'`.

Pass: 1 Gb/s link, RASPIAUDIO capture and playback present, ptp4l active, and
the expected eight-port nodes. Do not infer PTP lock merely from a running
process; require a grandmaster, stable offset and no domain mismatch.

## 1. Protocol-only Mac tests

```sh
cd macos/AESBridge
ctest --test-dir /private/tmp/aes-bridge-build --output-on-failure
/private/tmp/aes-bridge-build/aes-bridge-engine --list-interfaces
/private/tmp/aes-bridge-build/aes-bridge-engine --print-tx-sdp <MAC_ETHERNET_IP>
/private/tmp/aes-bridge-build/aes-bridge-engine --run --interface <MAC_ETHERNET_INTERFACE> --duration 30
```

Pass: all tests, correct wired interface, and generated SDP showing
`L24/48000/8`, `ptime:1`, `framecount:48`, domain 0 and multicast `.81`.

## 2. SAP visibility without audio

Start AES Bridge, then confirm that the manager lists the Pi announcement from
`239.255.255.255:9875`. Select it and press **Utiliser la session sélectionnée**;
verify that multicast, source address, port and payload type match the Pi SDP.
Repeat after unplugging/replugging Ethernet.

Pass: the Pi session disappears after timeout, reappears automatically, and no
session from another interface is selected.

## 3. Pi → Mac RTP receive, no Core Audio driver

Send deterministic channel identifiers from the Pi: IN1=400 Hz, IN2=500 Hz,
then 600, 700, 800, 900, 1000 and 1100 Hz at conservative level. Capture RTP
into the diagnostic receiver, verify 48-frame timestamp increments, monotonic
16-bit sequence handling, 1152-byte L24 payloads and no channel permutation.

Pass: each channel contains only its expected tone; packet loss and malformed
counts remain zero on an unloaded wired link for at least 30 minutes.

## 4. Mac → Pi RTP transmit, muted outputs

Generate the same eight distinct channel tones on the Mac diagnostic sender to
`239.69.83.81:5004`. Inspect at PipeWire first; do not connect speakers.

Pass: eight PipeWire links, correct channel identity, 48 samples/packet,
correct big-endian L24 polarity/full-scale values, and stable timestamps.

## 5. PTP validation

Choose one grandmaster. Observe both Pi linuxptp and Mac engine state. Test
domain mismatch deliberately, then restore domain 0. Record lock acquisition
time, offset, path delay, drift and unlock/relock after cable interruption.

The manager must show received PTP messages, offset, E2E mean path delay and
zero protocol errors. First validate with software timestamps, then compare the
measured values with `pmc` on the Pi. Do not infer hardware-grade lock from the
passing simulated-grandmaster unit test.

Pass: no false “locked” state, stable offset suitable for the selected latency,
and automatic recovery. Local-clock fallback must be displayed as unlocked or
degraded, never locked.

## 6. Core Audio device

Only after the shared-memory bridge tests pass, install the ad-hoc build on
a non-production Mac and reboot. In Audio MIDI Setup verify exactly one device
named `AES Bridge`, 64 inputs, 64 outputs and only 48 kHz. Then test Reaper,
Logic Pro and QLab separately, starting at 6 ms network jitter latency.

The device should expose 64 inputs and 64 outputs; in the Raspberry profile,
only channels 1–8 carry the physical RASPIAUDIO interface and channels 9–64
remain reserved for the additional computer-to-computer banks.

Pass: start/stop does not crash `coreaudiod`; idle engine and repeated client
open/close cycles do not leak threads or memory; channels 1–8 keep exact Pi
order and inactive channels 9–64 remain silent.

## 7. Fault and endurance matrix

Test Ethernet unplug/replug, Pi reboot, Mac sleep/wake, SAP deletion, RTP source
restart, sequence wrap, 1/2/5/10-packet bursts of loss, reorder and duplicate
packets. Then run bidirectional 8×8 audio for 2 h, 8 h and 24 h while logging
PTP offset, RX/TX packets, losses, malformed packets, underruns and overruns.

Production consideration begins only after all stages pass repeatedly on the
actual target Mac, Pi, RASPIAUDIO board and switch. A passing synthetic test is
not evidence of hardware interoperability.
