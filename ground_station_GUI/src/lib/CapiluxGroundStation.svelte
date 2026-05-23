<script lang="ts">
  import { onMount, onDestroy } from "svelte";

  type StatusReport = {
    type: "STATUS_REPORT";
    t_ms: number;
    state: string;
    uptime_ms: number;
    soe: number;
    lo: number;
    camera_power: number;
    led1: number;
    led2: number;
    led3: number;
    led4: number;
    commands_received: number;
    bad_commands: number;
  };

  type HkReport = {
    type: "HK_REPORT";
    t_ms: number;
    hk_ok?: number;
    voltage_ok?: number;
    v5?: number;
    v3v3?: number;
    vin?: number;
    board_temp_ok?: number;
    board_temp_c?: number;
    pressure_ok?: number;
    pressure_hpa?: number;
    sd_mounted?: number;
    logger_ok?: number;
    memory_free_percent?: number;
    sensor_mode?: string;
  };

  type ObsReport = {
    type: "OBS_REPORT";
    t_ms: number;
    camera_power: number;
    camera_status: string;
    led1: number;
    led2: number;
    led3: number;
    led4: number;
  };

  type AckReport = {
    type: "ACK";
    t_ms: number;
    cmd: string;
  };

  type MissionTimelineItem = {
    time: string;
    label: string;
    status: "done" | "active" | "pending";
  };

  type CommandButton = {
    id: string;
    label: string;
    command: string;
    danger: boolean;
  };

  type MainBoardItem = {
    label: string;
    value: string;
    state: "ok" | "warn" | "bad";
  };

  type ChamberItem = {
    id: string;
    pressure: string;
    temp: string;
    led: string;
    camera: string;
    recording: string;
    leak: string;
    state: string;
  };

  type GraphItem = {
    title: string;
    value: string;
    subtitle: string;
    min: string;
    mid: string;
    max: string;
    points: string;
  };

  let socket: WebSocket | null = null;
  let connected = false;

  let status: StatusReport | null = null;
  let hk: HkReport | null = null;
  let obs: ObsReport | null = null;
  let lastAck: AckReport | null = null;

  let rawLines: string[] = [
    "Waiting for telemetry...",
    "Start bridge: node bridge.js COM8 115200 8081"
  ];

  const websocketUrl = "ws://localhost:8081";

  const missionStates = [
    "BOOT",
    "GROUND LINK",
    "TEST MODE",
    "FLIGHT READY",
    "AWAITING SOE",
    "EXPERIMENT ACTIVE",
    "SAFE SHUTDOWN"
  ];

  const commands: CommandButton[] = [
    { id: "CMD-00", label: "PING", command: "PING", danger: false },
    { id: "CMD-01", label: "ENTER TEST MODE", command: "ENTER_TEST_MODE", danger: false },
    { id: "CMD-02", label: "EXIT TEST MODE", command: "EXIT_TEST_MODE", danger: false },
    { id: "CMD-03", label: "GET STATUS", command: "GET_STATUS", danger: false },
    { id: "CMD-04", label: "GET HK", command: "GET_HK", danger: false },
    { id: "CMD-05", label: "GET OBS STATUS", command: "GET_OBS_STATUS", danger: false },
    { id: "CMD-06", label: "CAM ON", command: "CAM_ON", danger: false },
    { id: "CMD-07", label: "CAM OFF", command: "CAM_OFF", danger: false },
    { id: "CMD-08", label: "LED 120", command: "LED_ALL 120", danger: false },
    { id: "CMD-09", label: "LED OFF", command: "LED_OFF", danger: false },
    { id: "CMD-10", label: "START HK", command: "START_HK_STREAM", danger: false },
    { id: "CMD-11", label: "STOP HK", command: "STOP_HK_STREAM", danger: false },
    { id: "CMD-12", label: "SAVE MEMORY", command: "SAVE_MEMORY", danger: false },
    { id: "CMD-13", label: "SAFE MODE", command: "ENTER_SAFE_MODE", danger: true },
    { id: "CMD-14", label: "EXIT SAFE MODE", command: "EXIT_SAFE_MODE", danger: true },
    { id: "CMD-15", label: "REBOOT", command: "REBOOT", danger: true }
  ];

  $: activeState = mapFirmwareStateToGuiState(status?.state);
  $: missionTime = status ? formatDuration(status.uptime_ms) : "T+ --:--:--";
  $: linkText = connected ? "USB / COM8" : "NO LINK";

  $: mainBoard = buildMainBoard(status, hk, connected);
  $: timeline = buildTimeline(status, connected);
  $: graphItems = buildGraphs(hk);
  $: chambers = buildChambers(hk, obs, status);

  function isValidNumber(value: unknown): value is number {
    return typeof value === "number" && Number.isFinite(value);
  }

  function flagOk(value: unknown): boolean {
    return value === 1 || value === true;
  }

  function formatNumber(value: unknown, unit: string, decimals = 2): string {
    if (!isValidNumber(value)) return "---";
    return `${value.toFixed(decimals)} ${unit}`;
  }

  function mapFirmwareStateToGuiState(state: string | undefined): string {
    if (!state) return connected ? "GROUND LINK" : "BOOT";
    if (state === "BOOT") return "BOOT";
    if (state === "TEST_MODE") return "TEST MODE";
    if (state === "SAFE_MODE") return "SAFE SHUTDOWN";
    if (state === "STREAMING") return "EXPERIMENT ACTIVE";
    if (state === "STANDBY") return "GROUND LINK";
    return "GROUND LINK";
  }

  function formatDuration(ms: number): string {
    const totalSeconds = Math.floor(ms / 1000);
    const h = Math.floor(totalSeconds / 3600);
    const m = Math.floor((totalSeconds % 3600) / 60);
    const s = totalSeconds % 60;

    return `T+ ${h.toString().padStart(2, "0")}:${m
      .toString()
      .padStart(2, "0")}:${s.toString().padStart(2, "0")}`;
  }

  function buildTimeline(currentStatus: StatusReport | null, isConnected: boolean): MissionTimelineItem[] {
    return [
      { time: "T-15:00", label: "Power on", status: isConnected ? "done" : "pending" },
      { time: "T-10:00", label: "Status report", status: currentStatus ? "active" : "pending" },
      { time: "T-06:00", label: "Enter test mode", status: currentStatus?.state === "TEST_MODE" ? "active" : "pending" },
      { time: "T-05:00", label: "Test completed", status: "pending" },
      { time: "T-01:45", label: "Final pre-flight status", status: "pending" },
      { time: "T-00:45", label: "SOE / start recording", status: currentStatus?.soe === 1 ? "active" : "pending" },
      { time: "T+00:00", label: "Lift-off", status: "pending" },
      { time: "T+EXP", label: "Experiment active", status: currentStatus?.state === "STREAMING" ? "active" : "pending" },
      { time: "T+END", label: "Finalize files", status: "pending" },
      { time: "T+END+30", label: "Safe shutdown", status: currentStatus?.state === "SAFE_MODE" ? "active" : "pending" }
    ];
  }

  function buildMainBoard(currentStatus: StatusReport | null, currentHk: HkReport | null, isConnected: boolean): MainBoardItem[] {
    const voltageAvailable = flagOk(currentHk?.voltage_ok);
    const vinAvailable = voltageAvailable && isValidNumber(currentHk?.vin);
    const v5Available = voltageAvailable && isValidNumber(currentHk?.v5);
    const sdKnown = currentHk !== null && currentHk.sd_mounted !== undefined;
    const memoryKnown = currentHk !== null && isValidNumber(currentHk.memory_free_percent);

    return [
      { label: "POWER", value: isConnected ? "ON" : "---", state: isConnected ? "ok" : "warn" },
      { label: "WATCHDOG", value: currentStatus ? "ALIVE" : "---", state: currentStatus ? "ok" : "warn" },
      {
        label: "SUPPLY",
        value: vinAvailable ? formatNumber(currentHk?.vin, "V") : v5Available ? formatNumber(currentHk?.v5, "V") : "---",
        state: vinAvailable || v5Available ? "ok" : "warn"
      },
      { label: "LINK", value: connected ? "USB LOCK" : "---", state: connected ? "ok" : "warn" },
      { label: "MEMORY", value: memoryKnown ? `${currentHk?.memory_free_percent?.toFixed(1)}% FREE` : "---", state: memoryKnown ? "ok" : "warn" },
      { label: "SD", value: sdKnown && currentHk?.sd_mounted ? "MOUNTED" : "---", state: currentHk?.sd_mounted ? "ok" : "warn" }
    ];
  }

  function buildGraphs(currentHk: HkReport | null): GraphItem[] {
    const pressureAvailable = flagOk(currentHk?.pressure_ok) && isValidNumber(currentHk?.pressure_hpa);
    const tempAvailable = flagOk(currentHk?.board_temp_ok) && isValidNumber(currentHk?.board_temp_c);

    return [
      {
        title: "PRESSURE",
        value: pressureAvailable ? formatNumber(currentHk?.pressure_hpa, "hPa") : "--- hPa",
        subtitle: pressureAvailable ? "real pressure telemetry" : "no pressure data",
        min: "980",
        mid: "1000",
        max: "1020",
        points: pressureAvailable ? "50,118 95,118 140,116 185,112 230,96 275,66 320,38 365,30 410,44 455,72 500,98 545,112 590,118 635,120 680,119 725,116 770,110 815,102 860,96 905,92 950,90" : ""
      },
      {
        title: "BOARD TEMPERATURE",
        value: tempAvailable ? formatNumber(currentHk?.board_temp_c, "°C") : "--- °C",
        subtitle: tempAvailable ? "real board temperature" : "no temperature data",
        min: "0",
        mid: "25",
        max: "50",
        points: tempAvailable ? "50,132 95,128 140,122 185,110 230,94 275,76 320,58 365,46 410,41 455,47 500,60 545,78 590,96 635,112 680,124 725,130 770,126 815,112 860,92 905,68 950,44" : ""
      }
    ];
  }

  function buildChambers(currentHk: HkReport | null, currentObs: ObsReport | null, currentStatus: StatusReport | null): ChamberItem[] {
    const pressureAvailable = flagOk(currentHk?.pressure_ok) && isValidNumber(currentHk?.pressure_hpa);
    const tempAvailable = flagOk(currentHk?.board_temp_ok) && isValidNumber(currentHk?.board_temp_c);

    const ledValues = [
      currentObs?.led1 ?? currentStatus?.led1,
      currentObs?.led2 ?? currentStatus?.led2,
      currentObs?.led3 ?? currentStatus?.led3,
      currentObs?.led4 ?? currentStatus?.led4
    ];

    const cameraKnown = currentObs !== null || currentStatus !== null;
    const cameraPower = Boolean(currentObs?.camera_power ?? currentStatus?.camera_power ?? 0);

    return [1, 2, 3, 4].map((ch) => {
      const ledValue = ledValues[ch - 1];
      const ledKnown = typeof ledValue === "number";

      return {
        id: `CH-${ch}`,
        pressure: pressureAvailable ? formatNumber(currentHk?.pressure_hpa, "hPa") : "--- hPa",
        temp: tempAvailable ? formatNumber(currentHk?.board_temp_c, "°C") : "--- °C",
        led: ledKnown ? (ledValue > 0 ? `PWM ${ledValue}` : "OFF") : "---",
        camera: cameraKnown ? (cameraPower ? "POWERED" : "OFF") : "---",
        recording: "---",
        leak: "---",
        state: pressureAvailable || tempAvailable ? "DATA" : "---"
      };
    });
  }

  function addRawLine(line: string): void {
    rawLines = [line, ...rawLines].slice(0, 120);
  }

  function sendCommand(command: string): void {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      addRawLine(`GUI    ERROR  Bridge not connected. Tried: ${command}`);
      return;
    }

    socket.send(JSON.stringify({ type: "command", command }));
    addRawLine(`GUI    TX     ${command}`);
  }

  function handleIncomingTelemetry(text: string): void {
    addRawLine(`RX     ${text}`);

    try {
      const packet = JSON.parse(text);

      if (packet.type === "STATUS_REPORT") status = packet as StatusReport;
      else if (packet.type === "HK_REPORT") hk = packet as HkReport;
      else if (packet.type === "OBS_REPORT") obs = packet as ObsReport;
      else if (packet.type === "ACK") lastAck = packet as AckReport;
    } catch {
      addRawLine(`GUI    ERROR  Invalid JSON: ${text}`);
    }
  }

  onMount(() => {
    socket = new WebSocket(websocketUrl);

    socket.onopen = () => {
      connected = true;
      addRawLine(`GUI    LINK   Connected to ${websocketUrl}`);
    };

    socket.onclose = () => {
      connected = false;
      addRawLine("GUI    LINK   Disconnected from Node bridge");
    };

    socket.onerror = () => {
      addRawLine("GUI    ERROR  WebSocket error. Is bridge.js running?");
    };

    socket.onmessage = (event: MessageEvent<string>) => {
      handleIncomingTelemetry(event.data);
    };
  });

  onDestroy(() => {
    if (socket) socket.close();
  });
</script>

<div class="app">
  <header class="topbar">
    <div class="brand-block">
      <div class="mission">REXUS 38 / CAPILUX</div>
      <div class="subtitle">Ground support GUI</div>
    </div>

    <div class="state-strip" aria-label="Mission states">
      {#each missionStates as state}
        <div class="state-pill" class:active={state === activeState}>{state}</div>
      {/each}
    </div>

    <div class="top-meta">
      <div class="mission-time"><span>TIME</span>{missionTime}</div>
      <div><span>LINK</span>{linkText}</div>
      {#if lastAck}
        <div><span>ACK</span>{lastAck.cmd}</div>
      {/if}
    </div>
  </header>

  <main class="dashboard">
    <section class="panel graph-panel">
      <div class="panel-header">
        <div>
          <div class="eyebrow">IMU / SENSOR DATA</div>
          <h1>Rotation and acceleration</h1>
        </div>
        <div class="big-number">
          <span>STATE</span>
          {status?.state ?? "---"}
        </div>
      </div>

      <div class="graph-grid">
        {#each graphItems as graph}
          <div class="graph-card">
            <div class="graph-labels">
              <div>
                <strong>{graph.title}</strong>
                <span>{graph.subtitle}</span>
              </div>
              <div class="graph-value">{graph.value}</div>
            </div>

            <div class="graph-body">
              <div class="y-axis">
                <span>{graph.max}</span>
                <span>{graph.mid}</span>
                <span>{graph.min}</span>
              </div>

              <svg viewBox="0 0 1000 240" preserveAspectRatio="none" role="img" aria-label={graph.title + " graph"}>
                <rect x="0" y="0" width="1000" height="240" class="graph-bg"></rect>
                <line x1="50" y1="24" x2="50" y2="205" class="axis"></line>
                <line x1="50" y1="205" x2="950" y2="205" class="axis"></line>

                {#each [24, 84, 144, 205] as y}
                  <line x1="50" y1={y} x2="950" y2={y} class="grid"></line>
                {/each}

                {#each [50, 200, 350, 500, 650, 800, 950] as x}
                  <line x1={x} y1="24" x2={x} y2="205" class="grid vertical"></line>
                {/each}

                {#if graph.points}
                  <polyline points={graph.points} class="trace"></polyline>
                {/if}
              </svg>
            </div>
          </div>
        {/each}
      </div>
    </section>

    <aside class="panel timeline-panel">
      <div class="panel-header compact">
        <div>
          <div class="eyebrow">MISSION</div>
          <h2>Timeline</h2>
        </div>
      </div>

      <div class="timeline">
        {#each timeline as item}
          <div class="timeline-item" class:done={item.status === "done"} class:active={item.status === "active"}>
            <span class="timecode">{item.time}</span>
            <span>{item.label}</span>
          </div>
        {/each}
      </div>
    </aside>

    <section class="panel chamber-panel">
      <div class="panel-header compact">
        <div>
          <div class="eyebrow">TEST CHAMBERS</div>
          <h2>Chamber status</h2>
        </div>
      </div>

      <div class="chamber-grid">
        {#each chambers as chamber}
          <article class="chamber-card">
            <div class="chamber-head">
              <div class="chamber-id">{chamber.id}</div>
              <div class="status-light">{chamber.state}</div>
            </div>

            <div class="chamber-values">
              <div><span>Pressure</span><strong>{chamber.pressure}</strong></div>
              <div><span>Temp</span><strong>{chamber.temp}</strong></div>
              <div><span>LED</span><strong>{chamber.led}</strong></div>
              <div><span>Camera</span><strong>{chamber.camera}</strong></div>
              <div><span>Recording</span><strong>{chamber.recording}</strong></div>
              <div><span>Leak</span><strong>{chamber.leak}</strong></div>
            </div>
          </article>
        {/each}
      </div>
    </section>

    <section class="panel system-panel">
      <div class="panel-header compact">
        <div>
          <div class="eyebrow">MAIN BOARD</div>
          <h2>Housekeeping</h2>
        </div>
      </div>

      <div class="hk-grid">
        {#each mainBoard as item}
          <div class="hk-card" class:ok={item.state === "ok"} class:warn={item.state === "warn"} class:bad={item.state === "bad"}>
            <span>{item.label}</span>
            <strong>{item.value}</strong>
          </div>
        {/each}
      </div>
    </section>

    <section class="panel command-panel">
      <div class="panel-header compact">
        <div>
          <div class="eyebrow">GROUND COMMANDS</div>
          <h2>Commands</h2>
        </div>
      </div>

      <div class="command-grid">
        {#each commands as command}
          <button type="button" class:danger={command.danger} on:click={() => sendCommand(command.command)}>
            <span>{command.id}</span>
            <strong>{command.label}</strong>
          </button>
        {/each}
      </div>
    </section>

    <section class="panel raw-panel">
      <div class="panel-header compact">
        <div>
          <div class="eyebrow">TELEMETRY</div>
          <h2>Raw data</h2>
        </div>
      </div>

      <div class="raw-log">
        <pre>{rawLines.join("\n")}</pre>
      </div>
    </section>
  </main>
</div>

<style>
  :global(html, body) {
    margin: 0;
    width: 100%;
    min-width: 0;
    min-height: 100%;
    background: #24282f;
    color: #d7dce3;
    font-family: "Segoe UI", Arial, sans-serif;
    overflow: auto;
  }

  :global(#app) {
    width: 100%;
    min-height: 100%;
  }

  :global(*) {
    box-sizing: border-box;
  }

  .app {
    width: 100%;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    background: #24282f;
  }

  .topbar {
    flex: 0 0 auto;
    display: grid;
    grid-template-columns: 250px minmax(0, 1fr) 180px;
    align-items: center;
    gap: 12px;
    padding: 8px 12px;
    border-bottom: 1px solid #56606d;
    background: #303640;
  }

  .mission {
    font-weight: 700;
    font-size: 14px;
    letter-spacing: 0.7px;
    color: #f0f3f7;
  }

  .subtitle {
    margin-top: 3px;
    color: #a9b2bf;
    font-size: 12px;
  }

  .state-strip {
    display: flex;
    justify-content: center;
    gap: 5px;
    flex-wrap: wrap;
  }

  .state-pill {
    padding: 6px 8px;
    border: 1px solid #5a6471;
    background: #3a414c;
    color: #c2cad5;
    font-size: 10px;
    letter-spacing: 0.3px;
    text-transform: uppercase;
    white-space: nowrap;
  }

  .state-pill.active {
    color: #ffffff;
    border-color: #d0d7e0;
    background: #566170;
  }

  .top-meta {
    display: grid;
    gap: 3px;
    text-align: right;
    font-family: Consolas, monospace;
    font-size: 12px;
    color: #edf1f5;
  }

  .mission-time {
    font-size: 16px;
    font-weight: 600;
  }

  .top-meta span {
    display: inline-block;
    min-width: 42px;
    margin-right: 6px;
    color: #a9b2bf;
    font-family: "Segoe UI", Arial, sans-serif;
    font-size: 10px;
    letter-spacing: 0.6px;
  }

  .dashboard {
    flex: 1 0 auto;
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(0, 1fr) 360px;
    grid-template-rows: 330px 330px 300px;
    grid-template-areas:
      "graphs graphs timeline"
      "chambers chambers system"
      "commands raw raw";
    gap: 9px;
    padding: 9px;
    min-height: 0;
  }

  .panel {
    min-width: 0;
    min-height: 0;
    overflow: hidden;
    border: 2px solid #5b6572;
    background: #343b46;
    padding: 10px;
  }

  .graph-panel,
  .timeline-panel,
  .chamber-panel,
  .system-panel,
  .command-panel,
  .raw-panel {
    display: flex;
    flex-direction: column;
  }

  .graph-panel { grid-area: graphs; }
  .timeline-panel { grid-area: timeline; }
  .chamber-panel { grid-area: chambers; }
  .system-panel { grid-area: system; }
  .command-panel { grid-area: commands; }
  .raw-panel { grid-area: raw; }

  .panel-header {
    flex: 0 0 auto;
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 12px;
    margin-bottom: 8px;
  }

  .panel-header.compact {
    margin-bottom: 8px;
  }

  .eyebrow {
    color: #b2bcc9;
    font-size: 10px;
    letter-spacing: 0.9px;
    text-transform: uppercase;
    font-weight: 600;
  }

  h1,
  h2,
  h3,
  p {
    margin: 0;
  }

  h1 {
    margin-top: 2px;
    font-size: 20px;
    line-height: 1.1;
    color: #f0f3f7;
    font-weight: 600;
  }

  h2 {
    margin-top: 2px;
    font-size: 15px;
    color: #f0f3f7;
    font-weight: 600;
  }

  .big-number {
    min-width: 110px;
    text-align: right;
    font-family: Consolas, monospace;
    font-size: 24px;
    font-weight: 600;
    color: #f0f3f7;
  }

  .big-number span {
    display: block;
    margin-bottom: 2px;
    color: #b2bcc9;
    font-family: "Segoe UI", Arial, sans-serif;
    font-size: 10px;
    letter-spacing: 0.8px;
  }

  .graph-grid {
    flex: 1 1 auto;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
  }

  .graph-card {
    min-height: 0;
    display: grid;
    grid-template-rows: auto minmax(0, 1fr);
    gap: 6px;
    border: 2px solid #4f5966;
    background: #2c323b;
    padding: 8px;
  }

  .graph-labels {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 10px;
    color: #b8c2cf;
    font-size: 11px;
  }

  .graph-labels strong {
    display: block;
    color: #f0f3f7;
    font-size: 12px;
    letter-spacing: 0.5px;
  }

  .graph-labels span {
    display: block;
    margin-top: 2px;
    color: #a9b2bf;
  }

  .graph-value {
    font-family: Consolas, monospace;
    color: #f0f3f7;
    font-size: 14px;
    white-space: nowrap;
  }

  .graph-body {
    min-height: 0;
    display: grid;
    grid-template-columns: 32px minmax(0, 1fr);
    gap: 6px;
  }

  .y-axis {
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    padding: 3px 0 13px 0;
    color: #a9b2bf;
    font-size: 10px;
    font-family: Consolas, monospace;
    text-align: right;
  }

  .graph-body svg {
    width: 100%;
    height: 100%;
    min-height: 0;
    border: 1px solid #56606d;
  }

  .graph-bg { fill: #20252c; }
  .axis { stroke: #c4ccd6; stroke-width: 1; }
  .grid { stroke: #3e4651; stroke-width: 1; }
  .grid.vertical { stroke: #37404a; }

  .trace {
    fill: none;
    stroke: #f0f3f7;
    stroke-width: 2.1;
    stroke-linecap: round;
    stroke-linejoin: round;
  }

  .timeline {
    flex: 1 1 auto;
    min-height: 0;
    display: grid;
    gap: 5px;
    overflow: auto;
    padding-right: 2px;
  }

  .timeline-item {
    display: grid;
    grid-template-columns: 75px minmax(0, 1fr);
    gap: 8px;
    align-items: center;
    padding: 7px;
    background: #2c323b;
    border: 1px solid #4b5563;
    border-left: 4px solid #7b8797;
    color: #cbd3de;
    font-size: 12px;
  }

  .timeline-item.done {
    border-left-color: #d0d7e0;
    color: #f0f3f7;
  }

  .timeline-item.active {
    border-left-color: #ffffff;
    color: #ffffff;
    background: #4a5361;
  }

  .timecode {
    font-family: Consolas, monospace;
    color: #f0f3f7;
  }

  .chamber-grid {
    flex: 1 1 auto;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 8px;
  }

  .chamber-card {
    display: grid;
    grid-template-rows: auto minmax(0, 1fr);
    min-width: 0;
    min-height: 0;
    background: #2c323b;
    border: 2px solid #4f5966;
    padding: 8px;
  }

  .chamber-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 8px;
    padding-bottom: 7px;
    border-bottom: 1px solid #4b5563;
  }

  .chamber-id {
    color: #f0f3f7;
    font-size: 10px;
    letter-spacing: 0.8px;
    font-weight: 700;
  }

  .status-light {
    padding: 4px 7px;
    border: 1px solid #5a6471;
    background: #3a414c;
    color: #c2cad5;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.6px;
  }

  .chamber-values {
    min-height: 0;
    margin-top: 8px;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    grid-template-rows: repeat(3, minmax(52px, 1fr));
    gap: 6px;
  }

  .chamber-values div,
  .hk-card {
    background: #3a414c;
    border: 1px solid #66717f;
    padding: 6px;
    min-height: 0;
  }

  .chamber-values div {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 4px;
    text-align: center;
    overflow: hidden;
  }

  .chamber-values span,
  .hk-card span {
    display: block;
    color: #b7c0cc;
    font-size: 9px;
    line-height: 1.1;
    letter-spacing: 0.5px;
    text-transform: uppercase;
  }

  .chamber-values strong,
  .hk-card strong {
    display: block;
    color: #ffffff;
    font-family: Consolas, monospace;
    font-size: 12px;
    line-height: 1.15;
    font-weight: 600;
    overflow-wrap: anywhere;
  }

  .hk-grid {
    flex: 1 1 auto;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    grid-template-rows: repeat(3, minmax(58px, 1fr));
    gap: 8px;
  }

  .hk-card {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 5px;
    text-align: center;
  }

  .hk-card.warn {
    border-color: #66717f;
  }

  .hk-card.bad {
    border-color: #b35b5b;
  }

  .command-grid {
    flex: 1 1 auto;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    grid-template-rows: repeat(4, minmax(48px, 1fr));
    gap: 7px;
  }

  button {
    display: flex;
    flex-direction: column;
    justify-content: center;
    gap: 3px;
    border: 1px solid #5a6471;
    background: #3a414c;
    color: #f0f3f7;
    padding: 6px 7px;
    text-align: left;
    cursor: pointer;
    min-width: 0;
    overflow: hidden;
  }

  button:hover {
    border-color: #d0d7e0;
    background: #4a5361;
  }

  button.danger {
    border-color: #b35b5b;
    background: #46353a;
    color: #f0f3f7;
  }

  button span {
    display: block;
    color: #b7c0cc;
    font-family: Consolas, monospace;
    font-size: 8px;
    line-height: 1.1;
  }

  button strong {
    display: block;
    color: #f0f3f7;
    font-size: 10px;
    line-height: 1.15;
    font-weight: 700;
    overflow-wrap: anywhere;
  }

  .raw-log {
    flex: 1 1 auto;
    min-height: 0;
    overflow: hidden;
    border: 1px solid #66717f;
    background: #20252c;
  }

  .raw-log pre {
    width: 100%;
    height: 100%;
    min-height: 0;
    max-height: 100%;
    margin: 0;
    padding: 10px;
    overflow-y: auto;
    overflow-x: hidden;
    color: #e4e9ef;
    background: #20252c;
    font-family: Consolas, monospace;
    font-size: 12px;
    line-height: 1.5;
    white-space: pre-wrap;
    word-break: break-word;
  }

  @media (max-width: 1320px) {
    .topbar {
      grid-template-columns: 1fr;
      align-items: start;
    }

    .state-strip {
      justify-content: flex-start;
    }

    .top-meta {
      text-align: left;
      grid-template-columns: repeat(3, max-content);
      gap: 12px;
    }

    .dashboard {
      grid-template-columns: minmax(0, 1fr);
      grid-template-rows:
        360px
        300px
        430px
        360px
        300px
        340px;
      grid-template-areas:
        "graphs"
        "timeline"
        "chambers"
        "system"
        "commands"
        "raw";
    }

    .graph-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .chamber-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }

    .command-grid {
      grid-template-columns: repeat(4, minmax(0, 1fr));
    }
  }

  @media (max-width: 760px) {
    .dashboard {
      padding: 6px;
      gap: 6px;
      grid-template-rows:
        520px
        300px
        760px
        420px
        620px
        360px;
    }

    .panel {
      padding: 7px;
    }

    .graph-grid,
    .chamber-grid,
    .hk-grid,
    .command-grid {
      grid-template-columns: 1fr;
    }

    .chamber-values {
      grid-template-columns: 1fr;
      grid-template-rows: repeat(6, minmax(52px, auto));
    }

    .command-grid {
      grid-template-rows: repeat(16, minmax(46px, auto));
    }

    .panel-header {
      flex-direction: column;
    }

    .top-meta {
      grid-template-columns: 1fr;
      gap: 3px;
    }
  }
</style>
