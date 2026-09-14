# Over-The-Air (OTA) & Multi-Network

The firmware allows wireless firmware flashing over your local Wi-Fi network or VPN, eliminating the need to physically access the ESP32 once mounted.

---

## 1. Zero-Attack-Surface Security Architecture

Leaving wireless update ports open 24/7 is a severe security vulnerability on IoT devices. The ESP32-SwitchBot solves this with gated access:

1. **Locked by Default:** OTA port `3232` is completely closed at boot. Port scans show closed/filtered.
2. **Secret-Key Authentication:** Unlocking requires submitting `OTA_KEY` via `POST /ota/enable` or typing the passphrase in the cURL terminal dashboard.
3. **10-Minute Auto-Timeout:** Once unlocked, the update window stays open for **10 minutes** (`OTA_AUTO_TIMEOUT_MS = 600,000 ms`). If no firmware upload is received, port 3232 automatically locks itself again.

---

## 2. Multi-Network & VPN Gotchas (`-I <lan_ip>`)

When invoking `espota.py` from a computer connected to multiple network interfaces (e.g., local Wi-Fi `192.168.1.2`, Tailscale `100.x.x.x`, Docker bridges, or corporate VPNs), uploads often fail with:

```text
Uploading...................
[ERROR]: Error Uploading: [Errno 32] Broken pipe
```

### Why This Occurs:
1. **Reverse Connection Handshake:** Arduino/ESP32 OTA does not stream firmware purely through the outgoing command channel. Instead:
   - `espota.py` sends a UDP invitation packet to port 3232 on the ESP32.
   - The packet tells the ESP32: *"I am ready, connect back to my IP to receive the binary stream."*
   - The ESP32 initiates an inbound **reverse TCP connection** back to the development host.
2. **Interface Ambiguity:** `espota.py` binds to `0.0.0.0` by default and guesses which IP address to advertise to the ESP32. On machines with VPNs or Tailscale, it often mistakenly picks the virtual IP (`100.x.x.x` or `172.17.x.x`).
3. **Unreachable Route:** The ESP32 is on the physical local Wi-Fi (`192.168.1.0/24`) and cannot route directly to the host's virtual interface. The reverse connection stalls and times out, triggering `[Errno 32] Broken pipe`.

### The Solution: Use `-I <lan_ip>`
Always pass the capital `-I` flag specifying your computer's local physical LAN IP:

```bash
python3 components/arduino/tools/espota.py -i 192.168.1.50 -p 3232 -I 192.168.1.2 -f build/ESP32-SwitchBot.bin
```

This binds `espota.py` to your physical local Wi-Fi adapter (`192.168.1.2`) and forces it to instruct the ESP32 to connect back to that exact local IP, ensuring a fast and reliable transfer.
