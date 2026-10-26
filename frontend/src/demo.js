// Fake firmware for trying the page without hardware or a bridge.
// Sends the same packets as the mainboard (SED tables 4.27/4.28, firmware v0.2)
// and answers commands with the same state rules.

export function createDemo(onPacket) {
  const s = { st: 'STANDBY', soe: 0, lo: 0, cam: 0, led: [0, 0, 0, 0], tv: 0, rx: 0, bad: 0, armedAt: 0, t0: Date.now() };
  const ms = () => Date.now() - s.t0;
  const r = (base, spread) => +(base + (Math.random() - 0.5) * spread).toFixed(2);
  const emit = (p) => onPacket(JSON.stringify(p));

  emit({ t: 'PON', fw: 'capilux-demo' });

  const timer = setInterval(() => {
    const t = ms();
    emit({ t: 'STA', ms: t, st: s.st, soe: s.soe, lo: s.lo, cam: s.cam, tf: s.st === 'ACTIVE' && s.soeAt ? Math.floor((Date.now() - s.soeAt) / 1000) : -1 });
    emit({ t: 'ACT', ms: t, led: s.led, rx: s.rx, bad: s.bad });
    emit({ t: 'HK', ms: t, ok: 1, v: r(5.02, 0.04), ns: 6, sd: 1 });
    emit({ t: 'CH1', ms: t, p: [r(1012, 0.4), r(1011.5, 0.4)], T: [r(22.1, 0.1), r(22.3, 0.1)] });
    emit({ t: 'CH2', ms: t, p: [r(1012.2, 0.4), null], T: [r(21.9, 0.1), null] });  // chamber 4 sensor missing
    emit({ t: 'BAR', ms: t, p: r(1013.2, 0.2), T: r(23.4, 0.1) });
    emit({ t: 'ACC', ms: t, a: [r(0, 0.02), r(0, 0.02), r(1, 0.02)] });
    emit({ t: 'GYR', ms: t, g: [r(0, 0.6), r(0, 0.6), r(0, 0.6)] });
    if (t % 1000 < 200) emit({ t: 'CAM', rt: Array.from({ length: 8 }, (_, i) => (s.cam ? (i === 5 ? -1 : 362 - i) : -1)), rec: s.st === 'ACTIVE' ? 0xdf : 0 });
  }, 200);

  const reply = (cmd, err) => {
    s.rx++;
    if (err) { s.bad++; emit({ t: 'ERR', ms: ms(), err }); }
    else emit({ t: 'ACK', ms: ms(), cmd: cmd.split(' ')[0] });
  };
  const pre = () => ['STANDBY', 'TEST', 'SAFE'].includes(s.st);
  const allOff = () => { s.cam = 0; s.led = [0, 0, 0, 0]; };

  function send(line) {
    const [cmd, a, b] = line.split(' ');
    const test = s.st === 'TEST';
    switch (line === 'CLEAR_MEMORY CONFIRM' ? line : cmd) {
      case 'PING': case 'GET_STATUS': case 'GET_HK': case 'SAVE_MEMORY': return reply(line);
      case 'GET_OBS_STATUS':
        emit({ t: 'OBS', ms: ms(), cam: s.cam, tv: s.tv, ta: s.ta ?? 1, led: s.led });
        return reply(line);
      case 'GET_MEMORY_STATUS':
        emit({ t: 'MEM', sd: 1, sav: 0, bt: 42, n: Math.floor(ms() / 1000), fl: 504, er: 0 });
        return reply(line);
      case 'ENTER_TEST_MODE': if (s.st !== 'STANDBY') return reply(line, 'STATE'); s.st = 'TEST'; s.cam = 1; s.led = [255, 255, 255, 255]; return reply(line);
      case 'SELF_TEST': emit({ t: 'STS', ms: ms(), ok: 0, s: 59, m: 15, c: s.cam ? 223 : -1, r: s.cam ? 223 : -1 }); return reply(line);
      case 'REARM': if (s.st !== 'SHUTDOWN') return reply(line, 'STATE'); s.st = 'STANDBY'; return reply(line);
      case 'EXIT_TEST_MODE': if (!test) return reply(line, 'STATE'); s.st = 'STANDBY'; return reply(line);  // cameras keep recording
      case 'ENTER_SAFE_MODE': if (s.st !== 'STANDBY' && !test) return reply(line, 'STATE'); allOff(); s.st = 'SAFE'; return reply(line);
      case 'EXIT_SAFE_MODE': if (s.st !== 'SAFE') return reply(line, 'STATE'); s.st = 'STANDBY'; return reply(line);
      case 'CLEAR_MEMORY':
        if (!pre()) return reply(line, 'INHIBIT');
        s.rx++; s.armedAt = Date.now();
        return emit({ t: 'CFR', ms: ms(), cmd: 'CLEAR_MEMORY', win: 5 });
      case 'CLEAR_MEMORY CONFIRM':
        if (!pre()) return reply(line, 'INHIBIT');
        if (Date.now() - s.armedAt > 5000) return reply(line, 'NOT_ARMED');
        s.armedAt = 0; return reply(line);
      case 'REBOOT': if (s.st === 'ACTIVE') return reply(line, 'INHIBIT'); allOff(); s.st = 'STANDBY'; s.t0 = Date.now(); return reply(line);
      case 'TV':
        if (!test && s.st !== 'ACTIVE') return reply(line, 'STATE');
        s.tv = +a === 8 ? 0 : +a; s.ta = +a === 8 ? 1 : 0; return reply(line);
      case 'CAM_ON': case 'CAM_OFF': case 'LED_ALL': case 'LED': case 'LED_OFF':
        if (!test) return reply(line, 'STATE');
        if (cmd === 'CAM_ON') s.cam = 1;
        if (cmd === 'CAM_OFF') s.cam = 0;
        if (cmd === 'LED_ALL') s.led = s.led.map(() => +a);
        if (cmd === 'LED') s.led[+a - 1] = +b;
        if (cmd === 'LED_OFF') s.led = [0, 0, 0, 0];
        return reply(line);
      default: return reply(line, 'BAD_CMD');
    }
  }

  // demo only: what the RXSM SOE / LO lines would do
  function soe() { s.soe = 1; if (s.st === 'STANDBY' || s.st === 'SAFE') { s.st = 'ACTIVE'; s.soeAt = Date.now(); s.cam = 1; s.led = [255, 255, 255, 255];
    emit({ t: 'STS', ms: ms(), ok: 0, s: 59, m: 15, c: 223, r: 223 }); emit({ t: 'EVT', ms: ms(), e: 'NOGO' }); } }
  function lo() { s.lo = 1; }

  return { send, soe, lo, close: () => clearInterval(timer) };
}
