# Bridge

Not written yet. Contract the frontend expects:

- Mainboard to page: one telemetry packet per WebSocket message, the JSON line exactly as the
  firmware sends it, without the newline.
- Page to mainboard: one command per message as plain text (e.g. `LED 2 128`). The bridge adds
  `\n` and writes it to the serial port or the Esrange TCP port.

Without any bridge, commands can be sent from a terminal: `picocom -b 38400 --omap crlf /dev/ttyUSB0`
on the bench, or `nc <host> <port>` on the Esrange network.
