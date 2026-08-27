# Hosting a Harmonia Server

## Quick Start

1. Build `HarmoniaServer.exe` (Debug config)
2. Run it from the `12_harmonia/` directory:
   ```
   HarmoniaServer.exe
   ```
   Or with a custom config:
   ```
   HarmoniaServer.exe C:\path\to\server.cfg
   ```
3. The server listens on **port 4440 TCP** (A440 — the concert pitch standard)

---

## LAN Play (same network)

No configuration needed. Players on the same local network connect using your **local IP address**:

1. Find your local IP: open PowerShell, type `ipconfig`, look for `IPv4 Address` (e.g. `192.168.1.42`)
2. Tell your friends: connect to `192.168.1.42:4440`

---

## Internet Play (port forwarding)

To host over the internet, you need to forward port **4440 TCP** in your router.

### Steps (general — varies by router brand)

1. Log into your router admin panel (usually `http://192.168.1.1` or `http://192.168.0.1`)
2. Find **Port Forwarding** (sometimes under "NAT", "Virtual Servers", or "Advanced")
3. Add a new rule:
   - **Protocol**: TCP
   - **External Port**: 4440
   - **Internal Port**: 4440
   - **Internal IP**: your machine's local IP (e.g. `192.168.1.42`)
4. Save and apply

### Find your public IP

Go to [https://whatismyip.com](https://whatismyip.com) — this is the address your friends use to connect.

### Dynamic IP

If your public IP changes (most home connections), consider:
- A free dynamic DNS service (e.g. [DuckDNS](https://www.duckdns.org/)) — gives you a stable hostname like `myharmonia.duckdns.org`
- Update the DuckDNS record when your IP changes (they have an auto-update client)

---

## Server Configuration

Edit `server.cfg` in the same directory as `HarmoniaServer.exe`. Key options:

| Setting | Default | Description |
|---------|---------|-------------|
| `name` | My Harmonia Server | Server name shown to clients |
| `port` | 4440 | TCP port |
| `max_players` | 16 | Total simultaneous players |
| `tick_rate` | 20 | CA generations per second |
| `default_rule` | gameoflife3d | CA rule: `gameoflife3d`, `grayscott`, or `lenia` |
| `grid_width` | 24 | Living Grid X (pitch classes) |
| `grid_height` | 7 | Living Grid Y (octaves) |
| `grid_depth` | 32 | Living Grid Z (beat slots) |

---

## Firewall

Windows Firewall may block incoming connections. If clients can't connect:

1. Open **Windows Defender Firewall with Advanced Security**
2. Click **Inbound Rules** → **New Rule**
3. Rule type: **Port**
4. Protocol: **TCP**, port **4440**
5. Action: **Allow the connection**
6. Apply to: Domain + Private + Public (or just Private for LAN)
7. Name: `Harmonia Server`

---

## Logs

Server logs are written to `logs/harmonia_server.log` (configurable in `server.cfg`).
Console output uses color: green = INFO, yellow = WARN, red = ERROR.

---

## Protocol Reference

Harmonia uses **HARP** — Harmonia Application-layer Real-time Protocol.
Custom binary protocol over TCP. All details in `Source/Shared/Network/Protocol.h`.

```
Port:      4440 TCP
Encoding:  Binary, little-endian
Handshake: HARP magic (4 bytes) + version + MsgType + payloadLen + payload
Messages:  version + MsgType + payloadLen + payload (7-byte header)
```
