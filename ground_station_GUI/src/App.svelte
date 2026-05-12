<script>
  /**
   * CAPILUX / REXUS Ground Station GUI mock-up
   * Single-file Svelte component.
   */

  const missionStates = [
    "BOOT",
    "GROUND LINK",
    "TEST MODE",
    "FLIGHT READY",
    "AWAITING SOE",
    "EXPERIMENT ACTIVE",
    "SAFE SHUTDOWN"
  ];

  const activeState = "GROUND LINK";

  const missionTimeline = [
    { time: "T-15:00", label: "Power on", status: "done" },
    { time: "T-10:00", label: "Status report", status: "active" },
    { time: "T-06:00", label: "Enter test mode", status: "pending" },
    { time: "T-05:00", label: "Test completed", status: "pending" },
    { time: "T-01:45", label: "Final pre-flight status", status: "pending" },
    { time: "T-00:45", label: "SOE / start recording", status: "pending" },
    { time: "T+00:00", label: "Lift-off", status: "pending" },
    { time: "T+EXP", label: "Experiment active", status: "pending" },
    { time: "T+END", label: "Finalize files", status: "pending" },
    { time: "T+END+30", label: "Safe shutdown", status: "pending" }
  ];

  const commands = [
    { id: "CMD-01", label: "ENTER TEST MODE", danger: false },
    { id: "CMD-02", label: "EXIT TEST MODE", danger: false },
    { id: "CMD-03", label: "GET STATUS", danger: false },
    { id: "CMD-04", label: "GET HK", danger: false },
    { id: "CMD-05", label: "GET OBS STATUS", danger: false },
    { id: "CMD-06", label: "SAVE MEMORY", danger: false },
    { id: "CMD-07", label: "CLEAR MEMORY", danger: true },
    { id: "CMD-08", label: "EXIT SAFE MODE", danger: true }
  ];

  const mainComputer = [
    { label: "POWER", value: "ON", state: "ok" },
    { label: "WATCHDOG", value: "ALIVE", state: "ok" },
    { label: "SUPPLY", value: "28.0 V", state: "ok" },
    { label: "RS-422 LINK", value: "LOCK", state: "ok" },
    { label: "MEMORY", value: "84% FREE", state: "ok" },
    { label: "SD", value: "MOUNTED", state: "ok" }
  ];

  const imu = [
    { label: "ACC X", value: "-- g" },
    { label: "ACC Y", value: "-- g" },
    { label: "ACC Z", value: "-- g" },
    { label: "GYRO Z", value: "-- Hz" },
    { label: "ROLL", value: "-- °" },
    { label: "PITCH", value: "-- °" }
  ];

  const graphs = [
    {
      title: "GYRO Z FREQUENCY",
      value: "-- Hz",
      subtitle: "rocket rotation rate",
      min: "0",
      mid: "4",
      max: "8",
      points:
        "50,132 95,128 140,122 185,110 230,94 275,76 320,58 365,46 410,41 455,47 500,60 545,78 590,96 635,112 680,124 725,130 770,126 815,112 860,92 905,68 950,44"
    },
    {
      title: "VERTICAL ACCELERATION",
      value: "-- g",
      subtitle: "IMU vertical axis",
      min: "-5",
      mid: "0",
      max: "+20",
      points:
        "50,118 95,118 140,116 185,112 230,96 275,66 320,38 365,30 410,44 455,72 500,98 545,112 590,118 635,120 680,119 725,116 770,110 815,102 860,96 905,92 950,90"
    }
  ];

  const chambers = [
    {
      id: "CH-1",
      pressure: "-- hPa",
      temp: "-- °C",
      led: "OFF",
      camera: "STBY",
      recording: "NO",
      leak: "No leak detected",
      state: "IDLE"
    },
    {
      id: "CH-2",
      pressure: "-- hPa",
      temp: "-- °C",
      led: "OFF",
      camera: "STBY",
      recording: "NO",
      leak: "No leak detected",
      state: "IDLE"
    },
    {
      id: "CH-3",
      pressure: "-- hPa",
      temp: "-- °C",
      led: "OFF",
      camera: "STBY",
      recording: "NO",
      leak: "No leak detected",
      state: "IDLE"
    },
    {
      id: "CH-4",
      pressure: "-- hPa",
      temp: "-- °C",
      led: "OFF",
      camera: "STBY",
      recording: "NO",
      leak: "No leak detected",
      state: "IDLE"
    }
  ];

  const rawLines = [
    "00001234  TM-01  POWER_ON_TIME   00:12:34.182", 
    "00001235  TM-02  STATE           GROUND_LINK", 
    "00001236  TM-03  VIN             28.07 V", 
    "00001237  TM-03  5V_BUS          5.03 V", 
    "00001238  TM-03  SD_FREE         84.2 %", 
    "00001239  IMU    GYRO_Z          -- Hz", 
    "00001240  IMU    ACC_Z           -- g", 
    "00001241  CH-1   P/T             1012.4 hPa   21.8 C", 
    "00001242  CH-2   P/T             1012.1 hPa   21.7 C", 
    "00001243  CH-3   P/T             1012.6 hPa   21.8 C", 
    "00001244  CH-4   P/T             1012.3 hPa   21.9 C", 
    "00001245  OBS    LEDS            OFF", 
    "00001246  OBS    CAMERAS         STANDBY", 
    "00001247  RXSM   SOE             LOW", 
    "00001248  CRC    OK              0x7A3F"
  ];
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
      <div class="mission-time"><span>TIME</span>T+ --:--:--</div>
      <div><span>LINK</span>RS-422 / --</div>
    </div>
  </header>

  <main class="dashboard">
    <section class="panel graph-panel">
      <div class="panel-header">
        <div>
          <div class="eyebrow">IMU DATA</div>
          <h1>Rotation and acceleration</h1>
        </div>
        <div class="big-number">
          <span>ROTATION</span>
          -- Hz
        </div>
      </div>

      <div class="graph-grid">
        {#each graphs as graph}
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

                <polyline points={graph.points} class="trace"></polyline>
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
        {#each missionTimeline as item}
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
              <div>
                <div class="chamber-id">{chamber.id}</div>
              </div>
              
            </div>

            <div class="chamber-values">
              <div>
                <span>Pressure</span>
                <strong>{chamber.pressure}</strong>
              </div>
              <div>
                <span>Temp</span>
                <strong>{chamber.temp}</strong>
              </div>
              <div>
                <span>LED</span>
                <strong>{chamber.led}</strong>
              </div>
              <div>
                <span>Camera</span>
                <strong>{chamber.camera}</strong>
              </div>
              <div>
                <span>Recording</span>
                <strong>{chamber.recording}</strong>
              </div>
              <div>
                <span>Leak</span>
                <strong>{chamber.leak}</strong>
              </div>
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
        {#each mainComputer as item}
          <div class="hk-card" class:ok={item.state === "ok"}>
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
          <button type="button" class:danger={command.danger}>
            <span>{command.id}</span>
            {command.label}
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

      <pre>{rawLines.join("\n")}</pre>
    </section>
  </main>
</div>

<style>
  :global(html, body) {
    margin: 0;
    width: 100%;
    height: 100%;
    background: #24282f;
    color: #d7dce3;
    font-family: "Segoe UI", Arial, sans-serif;
    overflow: hidden;
  }

  :global(#app) {
    width: 100%;
    height: 100%;
  }

  .app {
    width: 100vw;
    height: 100vh;
    display: flex;
    flex-direction: column;
    background: #24282f;
  }

  .topbar {
    display: grid;
    grid-template-columns: 250px minmax(0, 1fr) 170px;
    align-items: center;
    gap: 14px;
    padding: 8px 12px;
    border-bottom: 1px solid #56606d;
    background: #303640;
    box-sizing: border-box;
  }

  .mission {
    font-weight: 700;
    font-size: 15px;
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
    padding: 6px 9px;
    border: 1px solid #5a6471;
    background: #3a414c;
    color: #c2cad5;
    font-size: 10px;
    letter-spacing: 0.4px;
    text-transform: uppercase;
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
    font-size: 18px;
    font-weight: 600;
  }

  .top-meta span {
    display: inline-block;
    min-width: 42px;
    margin-right: 8px;
    color: #a9b2bf;
    font-family: "Segoe UI", Arial, sans-serif;
    font-size: 10px;
    letter-spacing: 0.6px;
  }

  .dashboard {
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(0, 1fr) 360px;
    grid-template-rows: 40% 36% 24%;
    grid-template-areas:
      "graphs graphs timeline"
      "chambers chambers system"
      "commands raw raw";
    gap: 9px;
    padding: 9px;
    box-sizing: border-box;
  }

  .panel {
    min-height: 0;
    overflow: hidden;
    border: 2px solid #5b6572;
    background: #343b46;
    padding: 10px;
    box-sizing: border-box;
  }

  .graph-panel { grid-area: graphs; display: flex; flex-direction: column; }
  .timeline-panel { grid-area: timeline; display: flex; flex-direction: column; }
  .chamber-panel { grid-area: chambers; display: flex; flex-direction: column; }
  .system-panel { grid-area: system; display: flex; flex-direction: column; gap: 8px; }
  .command-panel { grid-area: commands; display: flex; flex-direction: column; }
  .raw-panel { grid-area: raw; display: flex; flex-direction: column; }

  .panel-header {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 12px;
    margin-bottom: 6px;
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

  h1, h2, h3, p {
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
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: 8px;
    max-height: 100%;
  }

  .graph-card {
    min-height: 0;
    display: grid;
    grid-template-rows: auto 1fr;
    gap: 5px;
    border: 2px solid #4f5966;
    background: #2c323b;
    padding: 7px;
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
    grid-template-columns: 32px 1fr;
    gap: 6px;
  }

  .y-axis {
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    padding: 3px 0 13px 0;
    box-sizing: border-box;
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
    flex: 1;
    min-height: 0;
    display: grid;
    gap: 5px;
    overflow: auto;
  }

  .timeline-item {
    display: grid;
    grid-template-columns: 75px 1fr;
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
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(4, minmax(0, 1fr));
    gap: 8px;
  }

  .chamber-card {
    display: flex;
    flex-direction: column;
    min-width: 0;
    min-height: 0;
    background: #2c323b;
    border: 2px solid #4f5966;
    padding: 8px;
    box-sizing: border-box;
  }

  .chamber-head {
    display: flex;
    justify-content: space-between;
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

  .chamber-card h3 {
    margin-top: 2px;
    color: #f0f3f7;
    font-size: 13px;
    font-weight: 600;
  }

  .status-light {
    height: fit-content;
    padding: 4px 7px;
    border: 1px solid #5a6471;
    background: #3a414c;
    color: #c2cad5;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.6px;
  }

  .chamber-values {
    flex: 1;
    min-height: 0;
    margin-top: 8px;
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 6px;
  }

  .chamber-values div,
  .hk-card,
  .imu-card {
    background: #3a414c;
    border: 1px solid #66717f;
    padding: 6px;
    box-sizing: border-box;
    min-height: 0;
  }

  .chamber-values span,
  .hk-card span,
  .imu-card span {
    display: block;
    color: #b7c0cc;
    font-size: 9px;
    letter-spacing: 0.5px;
    text-transform: uppercase;
  }

  .chamber-values strong,
  .hk-card strong,
  .imu-card strong {
    display: block;
    margin-top: 3px;
    color: #ffffff;
    font-family: Consolas, monospace;
    font-size: 13px;
    font-weight: 600;
  }

  .hk-grid {
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    grid-template-rows: repeat(3, 1fr);
    gap: 8px;
  }

  .hk-card.ok {
    border-color: #66717f;
  }

  .imu-grid {
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: 7px;
    overflow: auto;
  }

  .command-grid {
    flex: 1;
    min-height: 0;
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    grid-template-rows: repeat(4, 1fr);
    gap: 7px;
  }

  button {
    border: 1px solid #5a6471;
    background: #3a414c;
    color: #f0f3f7;
    padding: 5px 6px;
    text-align: left;
    cursor: pointer;
    font-weight: 600;
    font-size: 10px;
    letter-spacing: 0.2px;
  }

  button:hover {
    border-color: #d0d7e0;
    background: #4a5361;
  }

  button.danger {
    border-color: #7b8797;
    background: #3a414c;
    color: #f0f3f7;
  }

  button span {
    display: block;
    margin-bottom: 2px;
    color: #b7c0cc;
    font-family: Consolas, monospace;
    font-size: 8px;
  }

  pre {
    flex: 1;
    min-height: 0;
    margin: 0;
    padding: 10px;
    overflow: auto;
    color: #e4e9ef;
    background: #20252c;
    border: 1px solid #66717f;
    font-family: Consolas, monospace;
    font-size: 12px;
    line-height: 1.5;
  }

  @media (max-width: 1320px) {
    :global(html, body) {
      overflow: auto;
    }

    .app {
      height: auto;
      min-height: 100vh;
    }

    .topbar {
      grid-template-columns: 1fr;
      align-items: start;
    }

    .state-strip {
      justify-content: flex-start;
    }

    .top-meta {
      text-align: left;
    }

    .dashboard {
      grid-template-columns: 1fr;
      grid-template-rows: auto;
      grid-template-areas:
        "graphs"
        "timeline"
        "chambers"
        "system"
        "commands"
        "raw";
      overflow: visible;
    }

    .graph-panel,
    .timeline-panel,
    .chamber-panel,
    .system-panel,
    .command-panel,
    .raw-panel {
      min-height: 300px;
    }

    .graph-grid,
    .chamber-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }
  }

  @media (max-width: 760px) {
    .graph-grid,
    .chamber-grid,
    .hk-grid,
    .imu-grid,
    .command-grid {
      grid-template-columns: 1fr;
    }

    .panel-header {
      flex-direction: column;
    }
  }
</style>
