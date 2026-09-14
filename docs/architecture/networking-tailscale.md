# Embedded Tailscale & HA Failover

The firmware embeds a full WireGuard and Tailscale coordination client via a custom-tuned build of [microlink](https://github.com/CamM2325/microlink). This enables end-to-end encrypted remote access from anywhere in the world without port forwarding, dynamic DNS, or cloud subscription fees.

---

## Mesh Topology & Subnet Route Advertising

```
┌─────────────────────────────────────────────────────────────┐
│                   TAILSCALE MESH NETWORK                    │
│                (MagicDNS: http://esp32/main)                │
└───────────────┬─────────────────────────────┬───────────────┘
                │                             │
    Direct Peer-to-Peer            Relayed via Tailscale DERP
   (UDP WireGuard on LAN)           (Encrypted HTTPS Relay)
                │                             │
                ▼                             ▼
┌─────────────────────────────────────────────────────────────┐
│                 ESP32-SwitchBot Microcontroller             │
│            • Modified Microlink WireGuard Stack             │
│            • Advertises Subnet: 192.168.1.0/24              │
│            • Outbound CGNAT (100.64.0.0/10) Intercept       │
└─────────────────────────────────────────────────────────────┘
```

### Key Networking Enhancements in [`components/microlink`](https://github.com/CamM2325/microlink):
1. **High-Availability Subnet Advertising:** Advertises the local LAN subnet (`192.168.1.0/24`) directly to the Tailnet.
2. **Linker-Wrapped Outbound Routing:** Uses a GNU linker hook (`__wrap_ip4_route_src_hook` in `ml_wg_mgr.c`) to route return packets destined for CGNAT (`100.64.0.0/10`) through the WireGuard interface.
3. **Inbound Packet Remapping:** Interface hooks in `wireguardif.c` ensure packets addressed to the local IP (`192.168.1.50`) pass directly to the local lwIP stack rather than looping back.

---

## Cold Standby & Subnet Router Watchdog

To minimize power and thermal load, the ESP32 includes an automated failover engine:

1. **Watchdog Monitoring:** If a primary subnet router (such as an always-on phone or home server e.g. `"moto-g32"`) is configured with a read-only `TAILSCALE_API_KEY`, the ESP32 periodically queries `api.tailscale.com`.
2. **Cold Standby State:** While the primary subnet router is alive, the ESP32 shuts down its WireGuard engine (`ml_stop`). In this state, the device remains fully reachable through the primary router's subnet bridge while keeping chip thermals low (~40°C).
3. **Automated Failover:** If the primary router goes offline or suffers an outage, the ESP32 instantly starts its embedded WireGuard client (`ml_start`), taking over subnet advertising and restoring remote connectivity within seconds.

---

## Multi-Network Wi-Fi Failover & ARP Keepalives

* **6 Network Profiles:** The ESP32 can store up to 6 distinct Wi-Fi credentials (`WIFI_SSID_1..6`). If the primary network loses internet or connection, the firmware automatically cycles to fallback networks.
* **Gratuitous ARP Keepalives:** Consumer ISP routers frequently drop sleeping Wi-Fi clients from their ARP tables. The firmware emits periodic Gratuitous ARP packets (`CONFIG_LWIP_ESP_GRATUITOUS_ARP=y`), preventing router connection drops during modem sleep.
