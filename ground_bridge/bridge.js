
const { SerialPort } = require("serialport");
const { ReadlineParser } = require("@serialport/parser-readline");
const WebSocket = require("ws");
const readline = require("readline");

const portName = process.argv[2] || "COM8";
const baudRate = Number(process.argv[3] || 115200);
const wsPort = Number(process.argv[4] || 8080);

const serial = new SerialPort({
  path: portName,
  baudRate,
});

const parser = serial.pipe(new ReadlineParser({ delimiter: "\n" }));

const wss = new WebSocket.Server({ port: wsPort });

console.log(`Serial: ${portName} @ ${baudRate}`);
console.log(`WebSocket: ws://localhost:${wsPort}`);
console.log("Type commands here too: PING, GET_STATUS, GET_HK, GET_OBS_STATUS");

function sendCommandToExperiment(command) {
  const line = String(command || "").trim();
  if (!line) return;

  serial.write(line + "\n");
  console.log("TX:", line);
}

function broadcastToGui(text) {
  for (const client of wss.clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(text);
    }
  }
}

wss.on("connection", (ws) => {
  console.log("GUI connected");

  ws.on("message", (message) => {
    const text = message.toString();

    try {
      const msg = JSON.parse(text);

      if (msg.type === "command" && msg.command) {
        sendCommandToExperiment(msg.command);
        return;
      }
    } catch {
      // If GUI sends raw text, allow it.
    }

    sendCommandToExperiment(text);
  });

  ws.on("close", () => {
    console.log("GUI disconnected");
  });
});

parser.on("data", (line) => {
  const text = line.trim();
  if (!text) return;

  console.log("RX:", text);
  broadcastToGui(text);
});

serial.on("open", () => {
  console.log("Serial port opened.");
});

serial.on("error", (err) => {
  console.error("Serial error:", err.message);
});

const rl = readline.createInterface({
  input: process.stdin,
  output: process.stdout,
});

rl.on("line", (line) => {
  sendCommandToExperiment(line);
});
