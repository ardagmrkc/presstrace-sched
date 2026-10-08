"use strict";

/*
 * Web Serial tabanli izleme arayuzu.
 *
 * Hat bicimi (firmware/Core/Inc/protocol.h): her mesaj ASCII metindir,
 * bosluklarla 63 bayta tamamlanir, 64. bayt LF'dir.
 *   TEL,<seq>,S<n>,<temp_centi_c>,<temp_raw>,<vdda_mv>,<extra_us>,<period_us>,<txq>
 *   BTN,<event_id>,S<n>,PRESSED,<seq>,<t0>,<t1>,<t2>
 *   ACK,<seq>,S<n>,<surum>        surum: A, B ya da C; hizli yol aciksa sonuna F,
 *                                 SystemView izlemesi aciksa T, kisa ikili
 *                                 cerceve aciksa K
 *
 * Kisa ikili cerceve (firmware PROTOCOL_COMPACT 1, ACK'te K): TEL ve BTN
 * 64 bayt yerine A5 'T'|'B' + alanlar (little-endian) + CRC-8 olarak gelir
 * (TEL 19, BTN 20 bayt; alanlar protocol.h'de). Cozulen cerceve ayni ASCII
 * satira cevrilir; ham oturuma onaltilik baytlarla birlikte yazilir.
 *   REC,S<n>,<event_id>,<t0>,<t1-t0>,<t2-t1>,<t3-t2>,<t4-t3>,<status>
 *   CNT,S<n>,<ad>,<deger>
 *   END,S<n>,<REC_sayisi>
 *
 * Karta yalnizca kullanici bir surum ya da senaryo sectiginde veya "Olcumu
 * bitir"e bastiginda 5 baytlik komut gider (AA 55 'V'|'C' arg checksum);
 * olcum sirasinda hatta komut trafigi yoktur.
 */

const LINE_SIZE = 64;
const LF = 0x0a;

// Kisa ikili cerceve (firmware PROTOCOL_COMPACT 1): TEL ve BTN.
const BIN_SYNC = 0xa5;
const BIN_TYPE_TEL = 0x54; // 'T'
const BIN_TYPE_BTN = 0x42; // 'B'
const BIN_SIZE = { [BIN_TYPE_TEL]: 19, [BIN_TYPE_BTN]: 20 };
const DEADLINE_US = 20000;

// Kartin UART_BAUDRATE degeriyle (firmware/Core/Inc/main.h) ayni olmali.
// Olcumler 230400 ile alindi; protokol 115200 ile de calisir.
const BAUD_RATE = 230400;

// Olcum protokolu: pencere basinda 5 s isinma, basislar arasi >= 0,5 s.
const WARMUP_MS = 5000;
const MIN_PRESS_GAP_US = 500000;
const REC_STATUSES = ["ok", "tx_drop", "btn_drop", "tx_error", "timeout"];

// Kart REC satirinda dusmenin nerede oldugunu ayri bildirir. CSV'ye ortak
// "drop" durumu yazilir; neden bos zamanlardan da okunur (buton kuyrugu: yalnizca t0,
// TX kuyrugu: t0..t2).
const DROP_REASON = { tx_drop: "TX kuyruğu", btn_drop: "buton kuyruğu" };
const csvStatus = (s) => (s in DROP_REASON ? "drop" : s);

const CMD_SYNC0 = 0xaa;
const CMD_SYNC1 = 0x55;
const CMD_TYPE_SCENARIO = 0x43; // 'C'
const CMD_TYPE_VARIANT = 0x56; // 'V', arg: 'A' | 'B' | 'C'
const CMD_ARG_DUMP = 0xfe;
const CMD_ARG_QUERY = 0xff;
const ACK_TIMEOUT_MS = 1500;
const DUMP_TIMEOUT_MS = 3000; // en fazla ~125 satir x 2,78 ms ~ 0,35 s
const QUERY_RETRY_MS = 2000; // surum bilinmiyorken durum sorgusu en sik bu aralikla

let port = null;
let reader = null;
let keepReading = false;
let rxBuffer = new Uint8Array(0);
let skipPartialLine = true; // baglanti anindaki yarim satir hata sayilmaz
const decoder = new TextDecoder("ascii");

let lastSeq = null;
let seqGapCount = 0;
let lineErrorCount = 0;
let btnCount = 0;
let selectedEventId = null; // null = otomatik: son "ok" olay

/** event_id -> { scenarioId, t: [t0..t4] (null = bilinmiyor), status, hostRecvIso } */
const events = new Map();
let windowCounters = {}; // CNT satirlari: ad -> deger
let endRecords = null; // END ile kartin bildirdigi REC sayisi

let activeScenario = null; // kartin onayladigi senaryo
let requestedScenario = null; // ACK bekleyen istek
let requestedVariant = null; // ACK bekleyen surum istegi ("A" | "B" | "C")
let ackTimer = null;
let lastQueryMs = -Infinity; // son durum sorgusunun zamani (performance.now)
let scenarioError = "";
let unsavedEvents = false;
let dumpState = null; // null | "requested" (ACK bekleniyor) | "receiving" (END bekleniyor)
let dumpReceived = 0;

let boardVariant = null; // ACK'teki surum: "A", "B", "C" (hizli yol aciksa "AF" gibi)
let rawLog = []; // pencerenin ham seri oturumu: gelen her satir, oldugu gibi
let windowStartMs = null; // yeni olcum penceresinin ACK'i geldigi an (performance.now)
let windowStartIso = "";
let warmupTimer = null;
let lastPressT0 = null;
let warmupPresses = 0;
let shortIntervals = 0;

const el = (id) => document.getElementById(id);
const nowIso = () => new Date().toISOString();
const u32diff = (b, a) => (b - a) >>> 0; // TIM2 sayaci basa sarsa da dogru

function setConnected(isConnected) {
  const status = el("conn-status");
  status.textContent = isConnected ? "Bağlı" : "Bağlı değil";
  status.className = "status " + (isConnected ? "status--connected" : "status--disconnected");
  el("btn-connect").disabled = isConnected;
  el("btn-disconnect").disabled = !isConnected;
}

function showPressToast(eventId, scenarioId, warning = "") {
  const toast = el("press-toast");
  toast.textContent = `Butona basıldı · Olay ${eventId} (S${scenarioId})` + (warning ? ` · ${warning}` : "");
  toast.classList.toggle("toast--warn", !!warning);
  toast.hidden = false;
  toast.style.animation = "none";
  void toast.offsetHeight; // animasyonu yeniden tetikle
  toast.style.animation = "";
  clearTimeout(showPressToast._t);
  showPressToast._t = setTimeout(() => { toast.hidden = true; }, 1600);
}

/* ---------------------------------------------------------------------
 * Web Serial baglanti yonetimi
 * ------------------------------------------------------------------- */

async function connect() {
  if (!("serial" in navigator)) {
    el("serial-support-warning").hidden = false;
    return;
  }
  try {
    port = await navigator.serial.requestPort();
    // Varsayilan tampon 255 bayt (4 satir): sayfa kisa bir an takilirsa veri kaybolur.
    await port.open({ baudRate: BAUD_RATE, dataBits: 8, stopBits: 1, parity: "none", bufferSize: 65536 });
    keepReading = true;
    rxBuffer = new Uint8Array(0);
    skipPartialLine = true;
    lastSeq = null; // baglanti yokken cikan satirlar kayip sayilmasin
    setConnected(true);
    readLoop();
    renderScenario();
    queryScenario();
  } catch (err) {
    console.error("Bağlantı hatası:", err);
  }
}

async function disconnect() {
  keepReading = false;
  try {
    if (reader) await reader.cancel();
  } catch (err) {
    /* zaten kapanmis olabilir */
  }
  try {
    if (port) await port.close();
  } catch (err) {
    /* yoksay */
  }
  port = null;
  reader = null;
  setConnected(false);
  clearTimeout(ackTimer);
  activeScenario = null;
  requestedScenario = null;
  requestedVariant = null;
  dumpState = null;
  scenarioError = "";
  boardVariant = null;
  stopWarmup();
  renderScenario();
}

async function readLoop() {
  while (port && port.readable && keepReading) {
    reader = port.readable.getReader();
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.length) appendBytes(value);
      }
    } catch (err) {
      console.error("Okuma hatası:", err);
    } finally {
      reader.releaseLock();
    }
  }
}

/* ---------------------------------------------------------------------
 * Senaryo secimi ve olcum penceresi (yer istasyonu -> kart komutu)
 * Aktif senaryo YALNIZCA kartin ACK'i ile degisir.
 * ------------------------------------------------------------------- */

async function sendCommand(arg, type = CMD_TYPE_SCENARIO) {
  if (!port || !port.writable) return false;
  const b = new Uint8Array([CMD_SYNC0, CMD_SYNC1, type, arg, 0]);
  b[4] = (b[0] + b[1] + b[2] + b[3]) & 0xff;
  const writer = port.writable.getWriter();
  try {
    await writer.write(b);
    return true;
  } catch (err) {
    console.error("Komut gönderilemedi:", err);
    return false;
  } finally {
    writer.releaseLock();
  }
}

function armAckTimeout(message, ms = ACK_TIMEOUT_MS) {
  clearTimeout(ackTimer);
  ackTimer = setTimeout(() => {
    requestedScenario = null;
    requestedVariant = null;
    dumpState = null;
    scenarioError = message;
    renderScenario();
  }, ms);
}

async function queryScenario() {
  lastQueryMs = performance.now();
  scenarioError = "";
  if (await sendCommand(CMD_ARG_QUERY)) {
    armAckTimeout("Kart senaryo sorgusuna yanıt vermedi: senaryo değiştirilemez. USB-TTL TX → PA3 bağlı mı, firmware güncel mi?");
  }
}

const busy = () => requestedScenario !== null || requestedVariant !== null || dumpState !== null;

// Yeni pencere listeyi temizler; kaydedilmemis olay varsa sorar.
function confirmNewWindow(what) {
  return (
    !unsavedEvents ||
    events.size === 0 ||
    window.confirm(`Listede kaydedilmemiş ${events.size} olay var. ${what} değişince liste temizlenecek. Devam edilsin mi?`)
  );
}

// Aktif senaryo yeniden secilebilir: kart pencereyi sifirlar, yeni olcum
// penceresi (isinma dahil) baslar.
async function requestScenario(id) {
  if (!port || busy() || !confirmNewWindow("Senaryo")) return;
  scenarioError = "";
  requestedScenario = id;
  renderScenario();
  if (!(await sendCommand(id))) {
    requestedScenario = null;
    scenarioError = "Komut gönderilemedi.";
    renderScenario();
    return;
  }
  armAckTimeout(`Kart S${id} isteğine yanıt vermedi. USB-TTL TX → PA3 bağlantısını kontrol edin.`);
}

// Surum degisimi: kart ButtonTask onceligini ve TX secim kuralini degistirir,
// senaryo ayni kalir, yeni olcum penceresi (isinma dahil) baslar.
async function requestVariant(v) {
  if (!port || busy() || !confirmNewWindow("Sürüm")) return;
  scenarioError = "";
  requestedVariant = v;
  renderScenario();
  if (!(await sendCommand(v.charCodeAt(0), CMD_TYPE_VARIANT))) {
    requestedVariant = null;
    scenarioError = "Komut gönderilemedi.";
    renderScenario();
    return;
  }
  armAckTimeout(`Kart ${v} sürümü isteğine yanıt vermedi. Firmware güncel mi (sürüm komutu)?`);
}

// Olcum penceresini kapatir: kart susar (S0), kayitlari REC + CNT + END olarak doker.
async function requestDump() {
  if (!port || busy()) return;
  dumpState = "requested";
  dumpReceived = 0;
  endRecords = null;
  windowCounters = {};
  scenarioError = "";
  el("dump-summary").textContent = "";
  renderWindowCounters();
  renderScenario();
  if (!(await sendCommand(CMD_ARG_DUMP))) {
    dumpState = null;
    scenarioError = "Komut gönderilemedi.";
    renderScenario();
    return;
  }
  armAckTimeout("Kart ölçümü bitir komutuna yanıt vermedi. USB-TTL TX → PA3 bağlantısını kontrol edin.");
}

function onAck(scenarioId) {
  clearTimeout(ackTimer);
  if (dumpState === "requested") {
    // Olcum bitti: kart S0'a gecti, kayitlar geliyor. Liste TEMIZLENMEZ.
    dumpState = "receiving";
    activeScenario = scenarioId;
    scenarioError = "";
    stopWarmup();
    armAckTimeout("Döküm tamamlanmadı: END gelmedi. Tekrar 'Ölçümü bitir'e basabilirsiniz.", DUMP_TIMEOUT_MS);
    renderScenario();
    return;
  }
  const requested =
    requestedScenario === scenarioId || (requestedVariant !== null && boardVariant?.[0] === requestedVariant);
  const unexpected = !requested && activeScenario !== null && activeScenario !== scenarioId;
  activeScenario = scenarioId;
  requestedScenario = null;
  requestedVariant = null;
  scenarioError = unexpected ? `Kart senaryoyu kendisi S${scenarioId} yaptı (yeniden başlatıldı mı?).` : "";
  if (requested) {
    // Yeni olcum penceresi: liste, sayaclar, ham oturum ve eski telemetri
    // degerleri temizlenir; isinma sayaci baslar.
    clearEvents();
    resetTelemetryPanel();
    startWarmup();
  }
  renderScenario();
}

/* ---------------------------------------------------------------------
 * Isinma: pencere basindan 5 s sonra basislara baslanir (olcum protokolu).
 * Sure, yeni pencerenin ACK'inin bu sayfaya geldigi andan sayilir.
 * ------------------------------------------------------------------- */

const fmt1 = (v) => v.toLocaleString("tr-TR", { minimumFractionDigits: 1, maximumFractionDigits: 1 });
const warmupLeftMs = () => (windowStartMs === null ? 0 : Math.max(0, WARMUP_MS - (performance.now() - windowStartMs)));

function startWarmup() {
  windowStartMs = performance.now();
  windowStartIso = nowIso();
  clearInterval(warmupTimer);
  warmupTimer = setInterval(renderWarmup, 200);
  renderWarmup();
}

function stopWarmup() {
  windowStartMs = null;
  clearInterval(warmupTimer);
  warmupTimer = null;
  renderWarmup();
}

function renderWarmup() {
  const box = el("warmup");
  if (windowStartMs === null) {
    box.textContent = "";
    box.className = "warmup";
    return;
  }
  const left = warmupLeftMs();
  if (left > 0) {
    box.textContent = `Isınma: ${fmt1(left / 1000)} s — henüz basmayın.`;
    box.className = "warmup warmup--wait";
    return;
  }
  box.textContent = "Isınma tamam: basışlara başlayın. Aralık en az 0,5 s olsun ve hep aynı aralıkla basmayın.";
  box.className = "warmup warmup--go";
  clearInterval(warmupTimer);
  warmupTimer = null;
}

function onEnd(records) {
  clearTimeout(ackTimer);
  dumpState = null;
  endRecords = records;

  // Kayitsiz kalan canli olaylar: kart bu olay icin REC gondermedi.
  for (const ev of events.values()) {
    if (ev.status === "pending") ev.status = "no_record";
  }

  const byStatus = {};
  for (const ev of events.values()) {
    const s = ev.status in DROP_REASON ? `drop (${DROP_REASON[ev.status]})` : ev.status;
    byStatus[s] = (byStatus[s] || 0) + 1;
  }
  const parts = Object.entries(byStatus).map(([s, n]) => `${s} ${n}`).join(", ");
  const accepted = windowCounters.accepted;

  const problems = [];
  if (dumpReceived !== records) problems.push(`kart ${records} kayıt gönderdi, ${records - dumpReceived} tanesi yolda kayboldu`);
  if (accepted !== undefined && accepted !== records) problems.push(`kabul edilen ${accepted} basıştan ${accepted - records} tanesinin kaydı yok`);
  for (const k of ["pool_overflow", "droplog_overflow", "tx_start_fail", "spurious_tc", "encode_error"]) {
    if (windowCounters[k] > 0) problems.push(`${k} = ${windowCounters[k]}`);
  }

  const summary = el("dump-summary");
  summary.textContent =
    `Ölçüm penceresi kapandı: ${dumpReceived} kayıt alındı (${parts || "olay yok"}).` +
    (problems.length ? ` Uyarı: ${problems.join("; ")}.` : " Kayıt bütünlüğü tam.");
  summary.classList.toggle("bad", problems.length > 0);

  renderScenario();
  renderEvents();
  renderBreakdown();
  drawChart();
}

function renderScenario() {
  const connected = !!port;
  const waiting = busy();
  for (const btn of document.querySelectorAll("#sc-buttons .sc-btn")) {
    const id = Number(btn.dataset.sc);
    btn.disabled = !connected || waiting;
    btn.classList.toggle("active", id === activeScenario);
    btn.classList.toggle("pending", id === requestedScenario);
  }
  for (const btn of document.querySelectorAll("#var-buttons .var-btn")) {
    const v = btn.dataset.var;
    btn.disabled = !connected || waiting;
    btn.classList.toggle("active", boardVariant !== null && v === boardVariant[0]);
    btn.classList.toggle("pending", v === requestedVariant);
  }
  el("btn-dump").disabled = !connected || waiting;
  el("stat-scenario").textContent = activeScenario === null ? "—" : "S" + activeScenario;

  const fastPath = boardVariant !== null && boardVariant.includes("F");
  const trace = boardVariant !== null && boardVariant.includes("T");
  const compact = boardVariant !== null && boardVariant.includes("K");
  el("stat-variant").textContent =
    boardVariant === null
      ? "—"
      : boardVariant[0] + (fastPath ? " (hızlı yol açık)" : "") + (trace ? " (SystemView izlemesi açık)" : "") +
        (compact ? " (kısa ikili çerçeve)" : "");

  const status = el("sc-status");
  status.classList.toggle("bad", !!scenarioError || fastPath);
  status.textContent = !connected
    ? "Bağlı değil"
    : scenarioError
    ? scenarioError
    : fastPath
    ? "Kartta hızlı yol açık (BTN_FAST_PATH 1): butonlar görev ve kuyruktan geçmiyor, A/B/C ölçümü için kapatın."
    : dumpState === "requested"
    ? "Ölçüm bitiriliyor, kartın onayı bekleniyor…"
    : dumpState === "receiving"
    ? `Kart sustu (S0), kayıtlar alınıyor… (${dumpReceived})`
    : requestedScenario !== null
    ? `S${requestedScenario} isteniyor, kartın onayı bekleniyor…`
    : requestedVariant !== null
    ? `${requestedVariant} sürümü isteniyor, kartın onayı bekleniyor…`
    : activeScenario !== null
    ? `Aktif: ${boardVariant ?? "?"}/S${activeScenario} (kart onayladı). Yeni ölçüm penceresi için bir sürüm ya da senaryo seçin.` +
      (trace ? " SystemView izlemesi açık: ölçümlere izleme maliyeti dahildir." : "") +
      (compact ? " Kısa ikili TEL/BTN çerçevesi açık (64 baytlık temel protokol için PROTOCOL_COMPACT 0)." : "")
    : "Kartın senaryosu bekleniyor…";
}

/* ---------------------------------------------------------------------
 * Satir cozme: LF ile cercevele, her satirin tam 64 bayt oldugunu dogrula
 * ------------------------------------------------------------------- */

function appendBytes(chunk) {
  const merged = new Uint8Array(rxBuffer.length + chunk.length);
  merged.set(rxBuffer, 0);
  merged.set(chunk, rxBuffer.length);
  rxBuffer = merged;
  processBuffer();
}

function lineError() {
  lineErrorCount++;
  el("stat-frame-errors").textContent = String(lineErrorCount);
}

// Gecerli bir cerceve gelene kadar arka arkaya gelen bozuk baytlar tek hata
// sayilir (baglanti anindaki yarim satir hic sayilmaz: skipPartialLine).
function frameError() {
  if (!skipPartialLine) lineError();
  skipPartialLine = true;
}

const crc8 = (bytes) => {
  let crc = 0;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
  }
  return crc;
};

const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");

// Ikili TEL/BTN cercevesini ASCII satirin metnine cevirir: dogrulama ve
// isleme (handleLine) iki bicimde de ayni kalir. Alan sirasi protocol.h'de.
function binToLine(f) {
  const v = new DataView(f.buffer, f.byteOffset, f.byteLength);
  if (f[1] === BIN_TYPE_TEL) {
    return `TEL,${v.getUint16(2, true)},S${f[4]},${v.getInt16(5, true)},${v.getUint16(7, true)},` +
      `${v.getUint16(9, true)},${v.getUint16(11, true)},${v.getUint32(13, true)},${f[17]}`;
  }
  return `BTN,${v.getUint16(2, true)},S${f[4]},PRESSED,${v.getUint16(5, true)},` +
    `${v.getUint32(7, true)},${v.getUint32(11, true)},${v.getUint32(15, true)}`;
}

function processBuffer() {
  for (;;) {
    if (rxBuffer.length === 0) return;

    if (rxBuffer[0] === BIN_SYNC) {
      if (rxBuffer.length < 2) return;
      const size = BIN_SIZE[rxBuffer[1]];
      if (size !== undefined && rxBuffer.length < size) return; // cercevenin devami bekleniyor
      const frame = size === undefined ? null : rxBuffer.subarray(0, size);
      if (frame === null || crc8(frame.subarray(0, size - 1)) !== frame[size - 1]) {
        frameError(); // gecersiz tur ya da CRC: bir bayt atlanip senkron yeniden aranir
        rxBuffer = rxBuffer.slice(1);
        continue;
      }
      rxBuffer = rxBuffer.slice(size);
      skipPartialLine = false;
      const text = binToLine(frame);
      if (!handleLine(text)) lineError();
      rawLog.push(`${hex(frame)}  ${text}\n`);
      continue;
    }

    // ASCII satir. Satirlarda 0xA5 hic gecmez: LF'den once gelen bir senkron
    // bayti, oncesinin yarim kalmis bir satir ya da cop oldugunu gosterir.
    const lf = rxBuffer.indexOf(LF);
    const sync = rxBuffer.indexOf(BIN_SYNC);
    if (sync !== -1 && (lf === -1 || sync < lf)) {
      frameError();
      rawLog.push(decoder.decode(rxBuffer.subarray(0, sync)) + "\n");
      rxBuffer = rxBuffer.slice(sync);
      continue;
    }
    if (lf === -1) {
      if (rxBuffer.length > LINE_SIZE) {
        frameError(); // 64 bayt icinde LF yok: bozuk veri
        rxBuffer = new Uint8Array(0);
      }
      return;
    }
    let line = rxBuffer.subarray(0, lf + 1);
    rxBuffer = rxBuffer.slice(lf + 1);
    let recovered = false;
    if (line.length !== LINE_SIZE) {
      frameError(); // yarim satir ya da ikili cercevenin yukundeki bir LF bayti
      if (line.length < LINE_SIZE) {
        rawLog.push(decoder.decode(line));
        continue;
      }
      // Yarim kalan satir (kart reset'lenirken kesilen TEL ya da acilis
      // parazit bayti) arkasindaki tam satirla birlesmis. Her satir tam 64
      // bayt oldugu icin son 64 bayt o tam satirdir: acilis ACK'i kaybolmaz.
      rawLog.push(decoder.decode(line.subarray(0, line.length - LINE_SIZE)));
      line = line.subarray(line.length - LINE_SIZE);
      recovered = true;
    }
    skipPartialLine = false;
    const ok = handleLine(decoder.decode(line.subarray(0, LINE_SIZE - 1)).trimEnd());
    if (!ok && !recovered) lineError(); // kurtarilamayan birlesik satir zaten bir kez sayildi
    // Islemden SONRA: yeni pencereyi baslatan ACK, yeni ham oturumun ilk satiri olur.
    rawLog.push(decoder.decode(line));
  }
}

const num = (s) => (/^\d+$/.test(s) ? Number(s) : null);
const int = (s) => (/^-?\d+$/.test(s) ? Number(s) : null);
const scen = (s) => (/^S[0-6]$/.test(s) ? Number(s[1]) : null);
const allValid = (...xs) => xs.every((x) => x !== null);

function handleLine(text) {
  const f = text.split(",");
  switch (f[0]) {
    case "TEL": return f.length === 9 && onTel(f);
    case "BTN": return f.length === 8 && f[3] === "PRESSED" && onBtn(f);
    case "ACK": return f.length === 4 && onAckLine(f);
    case "REC": return f.length === 9 && onRec(f);
    case "CNT": return f.length === 4 && onCnt(f);
    case "END": return f.length === 3 && onEndLine(f);
    default: return false;
  }
}

function trackSeq(seq) {
  if (lastSeq !== null) {
    const expected = (lastSeq + 1) & 0xffff;
    if (seq !== expected) {
      seqGapCount += (seq - expected) & 0xffff;
      el("stat-seq-gaps").textContent = String(seqGapCount);
    }
  }
  lastSeq = seq;
}

// Canli satirlar (TEL/BTN) yalnizca aktif senaryoya aitse islenir; senaryo
// degisiminden once kuyruga girmis gec satirlar yok sayilir.
function acceptLive(sc) {
  // Surum yalnizca ACK'te gelir. Kart ACK'i kacmis bir anda yeniden
  // baslamissa (canli satir var ama surum bilinmiyor) durum yeniden sorulur;
  // sorgu karta yalnizca ACK yazdirir, olcum penceresini degistirmez.
  if (boardVariant === null && !busy() && performance.now() - lastQueryMs > QUERY_RETRY_MS) queryScenario();
  if (activeScenario === null) {
    activeScenario = sc; // sorgu yaniti henuz gelmedi: ilk satirdan ogren
    renderScenario();
    return true;
  }
  return sc === activeScenario;
}

function onTel(f) {
  const seq = num(f[1]), sc = scen(f[2]), temp = int(f[3]), raw = num(f[4]), vdda = num(f[5]);
  const extra = num(f[6]), period = num(f[7]), txq = num(f[8]);
  if (!allValid(seq, sc, temp, raw, vdda, extra, period, txq)) return false;
  trackSeq(seq);
  if (!acceptLive(sc)) return true;

  el("tel-temp").textContent = (temp / 100).toFixed(2) + " °C";
  el("tel-temp-raw").textContent = `${raw} / ${vdda} mV`;
  el("tel-period").textContent =
    period === 0 ? "—" : `${(period / 1000).toFixed(2)} ms (${(1e6 / period).toFixed(1)} Hz)`;
  el("tel-txq").textContent = `${txq} / 16`;
  el("tel-extra-load").textContent = extra + " µs";
  el("tel-last-time").textContent = `${new Date().toLocaleTimeString()} · seq ${seq}`;
  return true;
}

function onBtn(f) {
  const id = num(f[1]), sc = scen(f[2]), seq = num(f[4]);
  const t0 = num(f[5]), t1 = num(f[6]), t2 = num(f[7]);
  if (!allValid(id, sc, seq, t0, t1, t2)) return false;
  trackSeq(seq);
  if (!acceptLive(sc)) return true;

  unsavedEvents = true;
  btnCount++;
  el("stat-btn-count").textContent = String(btnCount);
  el("stat-last-event").textContent = String(id);

  // Olcum kurallari: isinma bitmeden ya da bir oncekine 0,5 s'den yakin
  // basislar isaretlenir (CSV'deki note kolonu); olcumden atilmazlar.
  const notes = [];
  const warnings = [];
  if (warmupLeftMs() > 0) {
    warmupPresses++;
    notes.push("warmup");
    warnings.push("ısınma bitmedi");
  }
  if (lastPressT0 !== null) {
    const gap = u32diff(t0, lastPressT0);
    if (gap < MIN_PRESS_GAP_US) {
      shortIntervals++;
      notes.push("short_interval");
      warnings.push(`aralık ${fmt1(gap / 1e6)} s < 0,5 s`);
    }
  }
  lastPressT0 = t0;

  // Canli BTN yalnizca t0..t2 tasir; t3/t4 ve durum kartin kayit havuzunda
  // kapanir ve "Olcumu bitir" ile REC olarak gelir.
  events.set(id, {
    scenarioId: sc, t: [t0, t1, t2, null, null], status: "pending", hostRecvIso: nowIso(), note: notes.join(";"),
  });
  showPressToast(id, sc, warnings.join(", "));
  renderEvents();
  renderBreakdown();
  return true;
}

function onAckLine(f) {
  const seq = num(f[1]), sc = scen(f[2]);
  if (!allValid(seq, sc) || !/^[ABC]F?T?K?$/.test(f[3])) return false;
  trackSeq(seq);
  boardVariant = f[3];
  onAck(sc);
  return true;
}

function onRec(f) {
  const sc = scen(f[1]), id = num(f[2]), t0 = num(f[3]), status = f[8];
  if (!allValid(sc, id, t0) || !REC_STATUSES.includes(status)) return false;
  const t = [t0, null, null, null, null];
  for (let i = 1; i <= 4; i++) {
    const s = f[3 + i];
    if (s === "") continue; // bilinmeyen ya da >= 1 s: bos birakilmis
    const d = num(s);
    if (d === null) return false;
    if (t[i - 1] !== null) t[i] = (t[i - 1] + d) >>> 0;
  }
  // Dokum kaydi senaryo filtresinden muaftir: kart bu sirada zaten S0'dadir.
  const ev = events.get(id) || { hostRecvIso: nowIso() };
  Object.assign(ev, { scenarioId: sc, t, status });
  events.set(id, ev);
  unsavedEvents = true;
  if (dumpState !== null) dumpReceived++;
  renderScenario();
  renderEvents();
  renderBreakdown();
  drawChart();
  return true;
}

function onCnt(f) {
  const sc = scen(f[1]), name = f[2], value = num(f[3]);
  if (!allValid(sc, value) || !/^[a-z_]+$/.test(name)) return false;
  windowCounters[name] = value;
  renderWindowCounters();
  return true;
}

function onEndLine(f) {
  const sc = scen(f[1]), records = num(f[2]);
  if (!allValid(sc, records)) return false;
  onEnd(records);
  return true;
}

/* ---------------------------------------------------------------------
 * Paneller
 * ------------------------------------------------------------------- */

const COUNTER_LABELS = {
  accepted: "Kabul edilen basış",
  repeat: "Elenen kenar (sıçrama)",
  btn_queue_drop: "Buton kuyruğu dolu",
  tx_queue_drop: "TX kuyruğu dolu (TEL+BTN)",
  tx_queue_max: "TX kuyruğu tepe (16)",
  pool_overflow: "Kayıt havuzu taşması (64)",
  droplog_overflow: "Düşen olay kaydı taşması",
  tx_timeout: "TX zaman aşımı (TC)",
  tx_start_fail: "TX başlatma hatası",
  spurious_tc: "Sahte TC",
  encode_error: "63 bayta sığmayan satır",
  tel_sent: "Gönderilen TEL",
  tel_period_min_us: "TEL periyodu min",
  tel_period_avg_us: "TEL periyodu ort.",
  tel_period_max_us: "TEL periyodu maks",
  btn_direct: "BTN hızlı yol: hemen başladı",
  btn_latched: "BTN hızlı yol: mandaldan başladı",
  btn_fallback: "BTN yedek yol (ButtonTask)",
  btn_result_lost: "BTN sonucu kuyruğa giremedi",
  tel_drop: "TEL kaybı (TX kuyruğu dolu)",
  tel_wait_avg_us: "TEL bekleme ort. (kuyruk → hat)",
  tel_wait_max_us: "TEL bekleme maks",
  stack_hwm_telemetry: "Yığın en az boş: Telemetry",
  stack_hwm_button: "Yığın en az boş: Button",
  stack_hwm_uart_tx: "Yığın en az boş: UartTx",
};

function counterText(name, v) {
  if (name.startsWith("tel_period") || name.startsWith("tel_wait")) return `${(v / 1000).toFixed(2)} ms`;
  if (name.startsWith("stack_hwm")) return `${v} word (${v * 4} B)`;
  return String(v);
}

function renderWindowCounters() {
  const names = Object.keys(windowCounters);
  el("stat-window-hint").hidden = names.length > 0;
  el("stat-window").innerHTML = names
    .map((n) => `<dt>${COUNTER_LABELS[n] || n}</dt><dd>${counterText(n, windowCounters[n])}</dd>`)
    .join("");
}

function resetTelemetryPanel() {
  for (const id of ["tel-temp", "tel-temp-raw", "tel-period", "tel-txq", "tel-extra-load", "tel-last-time"]) {
    el(id).textContent = "—";
  }
}

const STATUS_BADGE = {
  pending: '<span class="badge badge--pending">ölçüm sürüyor</span>',
  no_record: '<span class="badge badge--drop">kayıt yok</span>',
  tx_drop: '<span class="badge badge--miss">drop · TX kuyruğu</span>',
  btn_drop: '<span class="badge badge--miss">drop · buton kuyruğu</span>',
  tx_error: '<span class="badge badge--miss">tx_error</span>',
  timeout: '<span class="badge badge--miss">timeout</span>',
};

const rOf = (ev) => (ev.t[4] !== null ? u32diff(ev.t[4], ev.t[0]) : null);

const NOTE_TEXT = { warmup: "ısınmada", short_interval: "aralık < 0,5 s" };

function noteBadge(ev) {
  if (!ev.note) return "";
  return ev.note
    .split(";")
    .map((n) => `<span class="badge badge--pending">${NOTE_TEXT[n] || n}</span>`)
    .join(" ");
}

function statusBadge(ev) {
  if (ev.status === "ok") {
    return rOf(ev) > DEADLINE_US
      ? '<span class="badge badge--miss">ok · aşıldı</span>'
      : '<span class="badge badge--ok">ok</span>';
  }
  return STATUS_BADGE[ev.status] || ev.status;
}

function renderEvents() {
  const rows = [...events.entries()].sort((a, b) => b[0] - a[0]);
  el("events-tbody").innerHTML = rows
    .map(([id, ev]) => {
      const r = rOf(ev);
      const budget =
        ev.status !== "ok"
          ? "—"
          : r > DEADLINE_US
          ? '<span class="badge badge--miss">R &gt; 20ms</span>'
          : '<span class="badge badge--ok">R ≤ 20ms</span>';
      const cls = id === selectedEventId ? ' class="selected"' : "";
      const cell = (v) => (v === null ? "—" : v);
      return `<tr data-id="${id}"${cls}>
        <td>${id}</td>
        <td>S${ev.scenarioId}</td>
        <td>${cell(ev.t[0])}</td>
        <td>${cell(ev.t[1])}</td>
        <td>${cell(ev.t[2])}</td>
        <td>${cell(ev.t[3])}</td>
        <td>${cell(ev.t[4])}</td>
        <td>${r === null ? "—" : (r / 1000).toFixed(2)}</td>
        <td>${budget}</td>
        <td>${statusBadge(ev)}</td>
        <td>${noteBadge(ev)}</td>
      </tr>`;
    })
    .join("");
}

/* ---------------------------------------------------------------------
 * Gecikme dagilimi: R = (t1-t0) + (t2-t1) + (t3-t2) + (t4-t3)
 * ------------------------------------------------------------------- */

const SEGMENTS = [
  { key: "wait", short: "Görev bekleme", label: "t₁−t₀: görev bekleme", cls: "seg-wait" },
  { key: "prep", short: "Hazırlama", label: "t₂−t₁: hazırlama", cls: "seg-prep" },
  { key: "txq", short: "TX öncesi", label: "t₃−t₂: TX öncesi", cls: "seg-txq" },
  { key: "uart", short: "UART + TC", label: "t₄−t₃: UART + TC", cls: "seg-uart" },
];

const fmtMs = (us) =>
  (us / 1000).toLocaleString("tr-TR", { minimumFractionDigits: 2, maximumFractionDigits: 2 });

// 1 ms altindaki bilesenler (orn. 2 us hazirlama) "0,00 ms" gorunmesin.
const fmtDur = (us) => (us < 1000 ? `${Math.round(us)} µs` : `${fmtMs(us)} ms`);

function partsOf(ev) {
  const d = (i) => (ev.t[i - 1] !== null && ev.t[i] !== null ? u32diff(ev.t[i], ev.t[i - 1]) : null);
  return { wait: d(1), prep: d(2), txq: d(3), uart: d(4) };
}

const sumParts = (p) => SEGMENTS.reduce((s, seg) => s + (p[seg.key] ?? 0), 0);

function pickEvent() {
  if (selectedEventId !== null && events.has(selectedEventId)) {
    return [selectedEventId, events.get(selectedEventId)];
  }
  const ok = [...events.entries()].filter(([, ev]) => ev.status === "ok");
  const pool = ok.length ? ok : [...events.entries()];
  if (!pool.length) return null;
  return pool.reduce((a, b) => (b[0] > a[0] ? b : a));
}

function axisFor(maxUs) {
  const maxMs = maxUs / 1000;
  const top = [25, 50, 100, 150, 200, 300, 500, 1000].find((v) => maxMs <= v * 0.98) ??
    Math.ceil(maxMs / 500) * 500;
  return { maxUs: top * 1000, stepMs: top / 5 };
}

function barHtml(parts, axisMaxUs) {
  const segs = SEGMENTS.filter((s) => parts[s.key] !== null)
    .map((s) => {
      const v = parts[s.key];
      const pct = (v / axisMaxUs) * 100;
      const text = pct >= 9 ? s.short : "";
      return `<div class="seg ${s.cls}" style="width:${pct}%" title="${s.label}: ${fmtDur(v)}">${text}</div>`;
    })
    .join("");
  return segs + `<div class="bd-deadline" style="left:${(DEADLINE_US / axisMaxUs) * 100}%"></div>`;
}

function setBig(id, text, bad) {
  el(id).textContent = text;
  el(id).className = "bd-big" + (text === "—" ? " muted" : bad ? " bad" : "");
}

const STATUS_VERDICT = {
  pending: "Ölçüm sürüyor: t₃, t₄ ve durum kartın kayıt havuzunda. Sonuç, 'Ölçümü bitir ve kayıtları al' ile gelir.",
  no_record: "Kart bu olay için kayıt göndermedi (havuz ya da düşen olay kaydı taşmış olabilir).",
  tx_drop: "drop (TX kuyruğu): yanıt t₂'de TX kuyruğuna giremedi (kuyruk dolu); t₃/t₄ yok. Kayıp yanıt deadline'ı karşılamış sayılmaz.",
  btn_drop: "drop (buton kuyruğu): olay ISR'de buton kuyruğuna giremedi (kuyruk dolu); yalnızca t₀ var.",
  tx_error: "tx_error: UART gönderimi başlatılamadı ya da TC başka bir aktarıma aitti.",
  timeout: "timeout: TC 50 ms içinde gelmedi ya da R ≥ 1 s (deney zaman aşımı).",
};

function renderBreakdown() {
  const picked = pickEvent();
  const verdict = el("bd-verdict");

  if (!picked) {
    for (const id of ["bd-bar-event", "bd-bar-avg", "bd-axis", "bd-legend"]) el(id).innerHTML = "";
    el("bd-row-avg").hidden = true;
    setBig("bd-r", "—");
    setBig("bd-margin", "—");
    el("bd-selected").textContent = "";
    verdict.className = "bd-verdict pending";
    verdict.textContent = "Henüz buton olayı yok.";
    return;
  }

  const [eventId, ev] = picked;
  const parts = partsOf(ev);
  const total = sumParts(parts);

  const okEvents = [...events.values()].filter((e) => e.status === "ok");
  let avg = null;
  if (okEvents.length) {
    avg = {};
    for (const s of SEGMENTS) {
      avg[s.key] = okEvents.reduce((acc, e) => acc + partsOf(e)[s.key], 0) / okEvents.length;
    }
  }

  const axis = axisFor(Math.max(total, avg ? sumParts(avg) : 0, DEADLINE_US));
  el("bd-label-event").textContent = `Olay #${eventId}`;
  el("bd-bar-event").innerHTML = barHtml(parts, axis.maxUs);
  el("bd-row-avg").hidden = !avg;
  if (avg) {
    el("bd-label-avg").textContent = `Ortalama (${okEvents.length} ok olay)`;
    el("bd-bar-avg").innerHTML = barHtml(avg, axis.maxUs);
  }

  let ticks = "";
  for (let ms = 0; ms <= axis.maxUs / 1000; ms += axis.stepMs) {
    const pct = ((ms * 1000) / axis.maxUs) * 100;
    ticks += `<span${pct >= 99.9 ? ' class="last"' : ""} style="left:${pct}%">${ms}</span>`;
  }
  el("bd-axis").innerHTML = ticks;

  el("bd-legend").innerHTML =
    SEGMENTS.map((s) => {
      const v = parts[s.key];
      return `<span><i class="${s.cls}"></i>${s.label}: <b>${v === null ? "—" : fmtDur(v)}</b></span>`;
    }).join("") + '<span><i class="bd-legend-dl"></i>D = 20 ms</span>';

  el("bd-selected").textContent =
    selectedEventId === null
      ? "Otomatik: son 'ok' olay. Başka bir olay için tablodan satıra tıklayın."
      : "Seçili olay. Otomatiğe dönmek için aynı satıra tekrar tıklayın.";

  if (ev.status !== "ok") {
    setBig("bd-r", "—");
    setBig("bd-margin", "—");
    verdict.className = "bd-verdict " + (ev.status === "pending" ? "pending" : "bad");
    verdict.textContent = STATUS_VERDICT[ev.status] || ev.status;
    return;
  }

  const margin = DEADLINE_US - total;
  const ok = margin >= 0;
  setBig("bd-r", fmtMs(total) + " ms", !ok);
  setBig("bd-margin", (ok ? "+" : "−") + fmtMs(Math.abs(margin)) + " ms", !ok);

  const biggest = SEGMENTS.reduce((a, b) => (parts[b.key] > parts[a.key] ? b : a));
  verdict.className = "bd-verdict" + (ok ? "" : " bad");
  verdict.textContent =
    (ok ? `Deadline karşılanıyor (${fmtMs(margin)} ms pay).` : `Deadline ${fmtMs(-margin)} ms aşıldı.`) +
    ` En büyük bileşen: ${biggest.label} (%${Math.round((parts[biggest.key] / total) * 100)}).`;
}

/* ---------------------------------------------------------------------
 * Grafik: olay numarasi -> R. Cizim tamponu, canvas'in gercek CSS boyutu
 * x devicePixelRatio'ya ayarlanir; aksi halde tarayici sabit genislikli
 * tamponu yatayda gerer (egik yazi, elips noktalar).
 * ------------------------------------------------------------------- */

function drawChart() {
  const canvas = el("chart");
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  if (w === 0 || h === 0) return;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  const done = [...events.entries()].filter(([, ev]) => ev.status === "ok").sort((a, b) => a[0] - b[0]);

  const padL = 56, padR = 16, padT = 14, padB = 30;
  const plotW = w - padL - padR;
  const plotH = h - padT - padB;
  const maxR = Math.max(DEADLINE_US * 1.5, ...done.map(([, ev]) => rOf(ev)));
  const yOf = (us) => padT + plotH - (us / maxR) * plotH;

  ctx.strokeStyle = "#2a3348";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(padL, padT);
  ctx.lineTo(padL, padT + plotH);
  ctx.lineTo(padL + plotW, padT + plotH);
  ctx.stroke();

  ctx.fillStyle = "#9aa4b8";
  ctx.font = "11px sans-serif";
  ctx.textAlign = "right";
  ctx.textBaseline = "middle";
  for (const us of [0, DEADLINE_US, maxR]) {
    ctx.fillText(`${Math.round(us / 1000)} ms`, padL - 6, yOf(us));
  }

  ctx.strokeStyle = "#ff5c5c";
  ctx.setLineDash([5, 4]);
  ctx.beginPath();
  ctx.moveTo(padL, yOf(DEADLINE_US));
  ctx.lineTo(padL + plotW, yOf(DEADLINE_US));
  ctx.stroke();
  ctx.setLineDash([]);

  if (done.length === 0) return;

  const n = done.length;
  const xOf = (i) => padL + (n === 1 ? plotW / 2 : (i / (n - 1)) * plotW);
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  ctx.fillStyle = "#9aa4b8";
  ctx.fillText(`olay ${done[0][0]}`, xOf(0) + (n === 1 ? 0 : 16), padT + plotH + 8);
  if (n > 1) ctx.fillText(`olay ${done[n - 1][0]}`, xOf(n - 1) - 16, padT + plotH + 8);

  done.forEach(([, ev], i) => {
    const r = rOf(ev);
    ctx.fillStyle = r > DEADLINE_US ? "#ffb020" : "#4c9aff";
    ctx.beginPath();
    ctx.arc(xOf(i), yOf(r), 4, 0, Math.PI * 2);
    ctx.fill();
  });
}

/* ---------------------------------------------------------------------
 * Disa aktarma: <surum>_S<n>.csv, <surum>_S<n>_counters.csv, <surum>_S<n>_raw.txt
 * Olay CSV'si (PressTrace bicimi + note kolonu):
 *   scenario,event_id,t0_us,t1_us,t2_us,t3_us,t4_us,status,note
 * Eksik zaman 0 yazilmaz, bos birakilir. status ortak degerlerdir (ok,
 * drop, tx_error, timeout; bkz. csvStatus). note: warmup, short_interval.
 * Ham oturum, pencereyi baslatan ACK'ten itibaren karttan gelen her satirdir.
 * ------------------------------------------------------------------- */

function download(name, text, type = "text/csv") {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
}

function exportCsv() {
  if (events.size === 0) {
    window.alert("Kaydedilecek olay yok.");
    return;
  }
  const pending = [...events.values()].filter((e) => e.status === "pending").length;
  if (pending > 0 && !window.confirm(`Ölçüm bitirilmedi: ${pending} olay "pending" olarak kaydedilecek. Önce "Ölçümü bitir" önerilir. Yine de kaydedilsin mi?`)) {
    return;
  }

  const sorted = [...events.entries()].sort((a, b) => a[0] - b[0]);
  const scenario = sorted[0][1].scenarioId;
  const prefix = `${boardVariant ?? "X"}_S${scenario}`;
  const rows = ["scenario,event_id,t0_us,t1_us,t2_us,t3_us,t4_us,status,note"];
  for (const [id, ev] of sorted) {
    rows.push(
      [`S${ev.scenarioId}`, id, ...ev.t.map((v) => (v === null ? "" : v)), csvStatus(ev.status), ev.note || ""].join(","),
    );
  }
  download(`${prefix}.csv`, rows.join("\n") + "\n");

  const counters = ["name,value"];
  for (const [k, v] of Object.entries(windowCounters)) counters.push(`${k},${v}`);
  counters.push(`host_variant,${boardVariant ?? ""}`);
  counters.push(`host_baud,${BAUD_RATE}`);
  counters.push(`host_window_start,${windowStartIso}`);
  counters.push(`host_rec_received,${dumpReceived}`);
  counters.push(`host_rec_expected,${endRecords ?? ""}`);
  counters.push(`host_seq_gaps,${seqGapCount}`);
  counters.push(`host_line_errors,${lineErrorCount}`);
  counters.push(`host_warmup_presses,${warmupPresses}`);
  counters.push(`host_short_intervals,${shortIntervals}`);
  download(`${prefix}_counters.csv`, counters.join("\n") + "\n");

  download(`${prefix}_raw.txt`, rawLog.join(""), "text/plain");

  unsavedEvents = false;
}

function clearEvents() {
  events.clear();
  btnCount = 0;
  seqGapCount = 0;
  lineErrorCount = 0;
  lastSeq = null;
  dumpReceived = 0;
  endRecords = null;
  windowCounters = {};
  rawLog = [];
  lastPressT0 = null;
  warmupPresses = 0;
  shortIntervals = 0;
  el("stat-btn-count").textContent = "0";
  el("stat-seq-gaps").textContent = "0";
  el("stat-frame-errors").textContent = "0";
  el("stat-last-event").textContent = "—";
  el("dump-summary").textContent = "";
  selectedEventId = null;
  unsavedEvents = false;
  renderWindowCounters();
  renderEvents();
  renderBreakdown();
  drawChart();
}

/* ---------------------------------------------------------------------
 * Kurulum
 * ------------------------------------------------------------------- */

if (!("serial" in navigator)) {
  el("serial-support-warning").hidden = false;
}

el("btn-connect").addEventListener("click", connect);
el("btn-disconnect").addEventListener("click", disconnect);
el("btn-export-csv").addEventListener("click", exportCsv);
el("btn-clear").addEventListener("click", clearEvents);
el("btn-dump").addEventListener("click", requestDump);
el("events-tbody").addEventListener("click", (e) => {
  const row = e.target.closest("tr[data-id]");
  if (!row) return;
  const id = Number(row.dataset.id);
  selectedEventId = selectedEventId === id ? null : id;
  renderEvents();
  renderBreakdown();
});
el("sc-buttons").addEventListener("click", (e) => {
  const btn = e.target.closest(".sc-btn");
  if (btn && !btn.disabled) requestScenario(Number(btn.dataset.sc));
});
el("var-buttons").addEventListener("click", (e) => {
  const btn = e.target.closest(".var-btn");
  if (btn && !btn.disabled) requestVariant(btn.dataset.var);
});
window.addEventListener("resize", drawChart);

renderScenario();
renderWindowCounters();
renderBreakdown();
drawChart();
