AES Bridge Windows backend development build 0.2.0 (x64)
========================================================

This archive contains the real AES67 network backend, not a Windows virtual
audio device. It runs RTP L24 RX/TX, SAP/SDP discovery/publication, PTPv2 E2E,
eight 8-channel banks and the 64-channel named shared-memory contract intended
for a future Windows endpoint.

Run the isolated validation first:

  AESBridgeWindowsBackendSelfTest.exe --self-test

List IPv4 adapters:

  aes-bridge-windows-backend.exe --list-interfaces

Example for the second side of a computer-to-computer 64x64 link:

  aes-bridge-windows-backend.exe --run --interface "Ethernet" --profile computer-b

The other computer must use profile computer-a. Windows Firewall may request
permission for inbound UDP on the selected private Ethernet network.

The backend is statically linked to the Microsoft C/C++ runtime. It still needs
real two-computer, PTP, packet-loss and long-duration validation. No WASAPI,
AVStream or ASIO endpoint is included or claimed by this build.
