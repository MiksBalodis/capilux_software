# CAPILUX REXUS software

Software for the RX38 CAPILUX experiment (RTU HPR).

| Folder | What |
| --- | --- |
| `flight-firmware/` | RP2040 mainboard firmware (PlatformIO, arduino-pico core) |
| `bridge/` | Serial / Esrange TCP to WebSocket bridge for the frontend. Not written yet. |
| `frontend/` | Ground station page (Svelte 5 + Vite), with a demo mode that fakes the mainboard |

The link protocol is newline-delimited JSON telemetry down, plain-text command lines up
(SED v3.0 tables 4.28 and 4.29, with the changes listed in `flight-firmware/README.md`).
