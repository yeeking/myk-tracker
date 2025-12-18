const ui = {
  state: null,
  fetching: false,
  table: { cols: 0, rows: 0, cells: [], body: null, head: null },
  lastCursor: { seq: null, step: null },
  lastStepCursor: { row: null, col: null },
  lastConfigCursor: { seq: null, row: null },
};
let pendingState = null;
let rafId = null;

const dom = {
  bpm: null,
  pattern: null,
  step: null,
  seqRange: null,
  mode: null,
  led: null,
  meterSpans: [],
  dots: [],
  configTable: null,
  stepTable: null,
  seqTable: null,
  gridMain: null,
  gridStep: null,
  gridConfig: null,
};

const API = {
  async request(path, options = {}) {
    const start = performance.now();
    const res = await fetch(path, options);
    const fetchDuration = performance.now() - start;
    if (!res.ok) {
      throw new Error(`Request failed: ${res.status}`);
    }
    const jsonStart = performance.now();
    const json = await res.json();
    const jsonDuration = performance.now() - jsonStart;
    // console.log("API.request timings", {
    //   path,
    //   fetchMs: fetchDuration.toFixed(2),
    //   jsonMs: jsonDuration.toFixed(2),
    //   totalMs: (performance.now() - start).toFixed(2),
    // });
    return json;
  },
  async fetchState() {
    if (ui.fetching) return;
    ui.fetching = true;
    const fetchStart = performance.now();
    const timings = [];
    try {
      const requestStart = performance.now();
      const data = await this.request("/state");
      timings.push({ label: "request", duration: performance.now() - requestStart });

      const setStart = performance.now();
      ui.state = data;
      timings.push({ label: "state set", duration: performance.now() - setStart });

      const renderStart = performance.now();
      render(data);
      const renderDuration = performance.now() - renderStart;
      timings.push({ label: "render call", duration: renderDuration });

      const total = performance.now() - fetchStart;
      // console.log("fetchState timings", {
      //   totalMs: total.toFixed(2),
      //   steps: timings.map((t) => ({ label: t.label, ms: t.duration.toFixed(2) })),
      // });
    } catch (err) {
      console.error(err);
    } finally {
      ui.fetching = false;
    }
  },
  async sendCommand(action, payload = {}) {
    try {
      const data = await this.request("/command", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action, payload }),
      });
      if (data.state) {
        ui.state = data.state;
        render(data.state);
      }
      return data;
    } catch (err) {
      console.error(err);
    }
  },
};

function pad(num) {
  return String(num).padStart(2, "0");
}

function handleNativeState(state) {
  pendingState = state;
  if (rafId) return;
  rafId = requestAnimationFrame(() => {
    rafId = null;
    if (!pendingState) return;
    ui.state = pendingState;
    render(pendingState);
    pendingState = null;
  });
}

function renderNo(state) {
  console.log("Render");
}
function render(state){
  updateMeta(state);

  if (state.mode == "sequence") {
    const dims = getTableDims(state);
    ensureTable(state, dims);
    updateTableCells(state, dims);
  }
  if (state.mode == "step") {
    renderStepInspector(state);
  }
  if (state.mode == "config") {
    renderConfig(state);
  }
  
  updateStatus(state);
  toggleModals(state);
}

function renderLog(state) {
  // console.log("RENDER");
  if (!state) return;
  const timings = [];
  const timed = (label, fn) => {
    const start = performance.now();
    const result = fn();
    timings.push({ label, duration: performance.now() - start });
    return result;
  };

  timed("updateMeta", () => updateMeta(state));
  const dims = timed("getTableDims", () => getTableDims(state));
  timed("ensureTable", () => ensureTable(state, dims));
  timed("updateTableCells", () => updateTableCells(state, dims));
  timed("renderInspector", () => renderStepInspector(state));
  timed("renderConfig", () => renderConfig(state));
  timed("updateStatus", () => updateStatus(state));
  timed("toggleModals", () => toggleModals(state));

  const total = timings.reduce((sum, t) => sum + t.duration, 0);
  const fps = total > 0 ? 1000 / total : 0;
  // console.log("render timings", {
  //   totalMs: total.toFixed(2),
  //   estimatedFps: fps.toFixed(2),
  //   steps: timings.map((t) => ({ label: t.label, ms: t.duration.toFixed(2) })),
  // });
}

function updateMeta(state) {
  if (dom.bpm) dom.bpm.textContent = Math.round(state.bpm || 0);
  const seqIdx = state.currentSequence != null ? state.currentSequence : 0;
  const stepIdx = state.currentStep != null ? state.currentStep : 0;
  if (dom.pattern) dom.pattern.textContent = pad(seqIdx + 1);
  if (dom.step) dom.step.textContent = pad(stepIdx + 1);
  const totalSeqs = state.sequenceGrid ? state.sequenceGrid.length : 0;
  if (dom.seqRange) dom.seqRange.textContent = `${pad(seqIdx + 1)}/${pad(totalSeqs)}`;
  if (dom.mode) dom.mode.textContent = state.mode || "sequence";
  if (dom.led) {
    dom.led.textContent = state.isPlaying ? "Play" : "Stop";
    dom.led.classList.toggle("active", !!state.isPlaying);
  }
}

function getTableDims(state) {
  const cols = state.sequenceGrid ? state.sequenceGrid.length : 0;
  let rows = 0;
  if (state.sequenceLengths && state.sequenceLengths.length) {
    rows = Math.max(...state.sequenceLengths);
  } else if (state.sequenceGrid && state.sequenceGrid.length) {
    rows = Math.max(...state.sequenceGrid.map((c) => c.length));
  }
  return { cols, rows };
}

function ensureTable(state, dims) {
  const table = dom.seqTable;
  if (!table) return;
  const head = table.querySelector("thead");
  const body = table.querySelector("tbody");
  const { cols, rows } = dims;
  if (cols === ui.table.cols && rows === ui.table.rows && ui.table.cells.length) {
    return;
  }

  head.innerHTML = "";
  body.innerHTML = "";
  ui.table.cells = [];

  const headRow = document.createElement("tr");
  for (let c = 0; c < cols; c++) {
    const th = document.createElement("th");
    th.textContent = `S${c + 1}`;
    th.addEventListener("click", () => {
      API.sendCommand("setMode", { mode: "config", sequence: c });
    });
    headRow.appendChild(th);
  }
  head.appendChild(headRow);

  for (let r = 0; r < rows; r++) {
    const tr = document.createElement("tr");
    const rowCells = [];
    for (let c = 0; c < cols; c++) {
      const td = document.createElement("td");
      td.className = "seq-cell";
      td.dataset.seq = c;
      td.dataset.step = r;
      td.addEventListener("click", () => {
        API.sendCommand("setCursor", { sequence: c, step: r });
      });
      tr.appendChild(td);
      rowCells.push(td);
    }
    ui.table.cells.push(rowCells);
    body.appendChild(tr);
  }

  ui.table.cols = cols;
  ui.table.rows = rows;
  ui.table.body = body;
  ui.table.head = head;
}

function updateTableCells(state, dims) {
  const { cols, rows } = dims;
  if (cols !== ui.table.cols || rows !== ui.table.rows || !ui.table.cells.length) {
    ensureTable(state, dims);
  }
  const grid = state.sequenceGrid || [];
  const playHeads = state.playHeads || [];
  const activeMap = new Set();
  playHeads.forEach((p) => activeMap.add(`${p.sequence}:${p.step}`));

  for (let r = 0; r < ui.table.rows; r++) {
    const rowCells = ui.table.cells[r];
    if (!rowCells) continue;
    for (let c = 0; c < ui.table.cols; c++) {
      const cell = rowCells[c];
      const col = grid[c];
      const val = col && col[r] ? col[r] : "";
      if (cell.textContent !== val) cell.textContent = val;

      cell.className = "seq-cell";
      const playing = activeMap.has(`${c}:${r}`);
      if (playing) cell.classList.add("active-step");
      if (state.armedSequence === c) cell.classList.add("armed-step");
      const isCursor = state.currentSequence === c && state.currentStep === r;
      if (isCursor) {
        cell.classList.add("cursor-step");
        if (
          ui.lastCursor.seq !== c ||
          ui.lastCursor.step !== r
        ) {
          ui.lastCursor = { seq: c, step: r };
          const container = dom.gridMain;
          if (container) {
            // console.log("Scrolling into view...");
            cell.scrollIntoView({ block: "center", inline: "center", behavior: "auto" });
          }
        }
      }
    }
  }
}

function renderStepInspector(state) {
  // console.log("renderInspector");
  const table = dom.stepTable;
  if (!table) return;
  const thead = table.querySelector("thead");
  const tbody = table.querySelector("tbody");
  thead.innerHTML = "";
  tbody.innerHTML = "";

  const data = state.stepData || [];
  const rows = Array.isArray(data) ? data : [];
  const maxCols = rows.reduce((m, r) => Math.max(m, Array.isArray(r) ? r.length : 0), 0);

  if (maxCols === 0) return;

  const headRow = document.createElement("tr");
  const emptyTh = document.createElement("th");
  headRow.appendChild(emptyTh);
  for (let c = 0; c < maxCols; c++) {
    const th = document.createElement("th");
    th.textContent = `C${c + 1}`;
    headRow.appendChild(th);
  }
  thead.appendChild(headRow);

  let td_to_scrollto;

  rows.forEach((row, rIdx) => {
    const tr = document.createElement("tr");
    const label = document.createElement("th");
    label.textContent = pad(rIdx + 1);
    tr.appendChild(label);
    for (let c = 0; c < maxCols; c++) {
      const td = document.createElement("td");
      td.textContent = row && row[c] != null ? row[c] : "";
      if (state.currentStepRow === rIdx && state.currentStepCol === c) {
        td.classList.add("cursor-step");
        // td.scrollIntoView({ block: "center", inline: "center", behavior: "auto" });
        // console.log("renderStepInspector scrolling col " + c + " into view");
        // td.scrollIntoView();
        if (ui.lastStepCursor.row !== rIdx || ui.lastStepCursor.col !== c) {
          ui.lastStepCursor = { row: rIdx, col: c };
          if (dom.gridStep) {
            td_to_scrollto = td; 
          }
        }
      }
      tr.appendChild(td);
    }
    tbody.appendChild(tr);
    if (td_to_scrollto != undefined){
      td_to_scrollto.scrollIntoView({ block: "center", inline: "center", behavior: "auto" });
    }
  });
}

function renderConfig(state) {

  // console.log("renderConfig");
  
  const table = dom.configTable;
  if (!table) return;
  const thead = table.querySelector("thead");
  const tbody = table.querySelector("tbody");
  thead.innerHTML = "";
  tbody.innerHTML = "";

  const configCols = state.sequenceConfigs || [];
  const cols = configCols.length;
  const rows = cols ? Math.max(...configCols.map((c) => (Array.isArray(c) ? c.length : 0))) : 0;
  if (!cols) return;

  const headRow = document.createElement("tr");
  for (let c = 0; c < cols; c++) {
    const th = document.createElement("th");
    th.textContent = `Seq ${pad(c + 1)}`;
    if (state.currentSequence === c && state.mode === "config") {
      th.classList.add("cursor-step");
    }
    headRow.appendChild(th);
  }
  thead.appendChild(headRow);

  let td_to_scrollto;
  // console.log("renderConfig:: state.currentStepRow at " + state.currentStepRow);
  for (let r = 0; r < rows; r++) {
    const tr = document.createElement("tr");
    for (let c = 0; c < cols; c++) {
      const td = document.createElement("td");
      const col = configCols[c] || [];
      td.textContent = col[r] != null ? col[r] : "";
      if (state.currentSequence === c && state.mode === "config" && r === state.currentSeqParam) {
        td.classList.add("cursor-step");
        if (ui.lastConfigCursor.seq !== c || ui.lastConfigCursor.row !== r) {
          ui.lastConfigCursor = { seq: c, row: r };
          if (dom.gridConfig) {
              td_to_scrollto = td; 
          }
        }
      }
      tr.appendChild(td);
    }
    tbody.appendChild(tr);
      if (td_to_scrollto != undefined){
      td_to_scrollto.scrollIntoView({ block: "center", inline: "center", behavior: "auto" });
    }
  }
}

function updateStatus(state) {
  if (!dom.dots.length) return;
  dom.dots.forEach((dot, idx) => {
    dot.classList.toggle("active", state.isPlaying && idx % 2 === 0);
  });
  dom.meterSpans.forEach((m, idx) => {
    m.classList.toggle("lit", state.isPlaying && idx < 4);
  });
}

function toggleModals(state) {
  if (!dom.gridMain || !dom.gridStep || !dom.gridConfig) return;
  const mode = state.mode || "sequence";
  dom.gridMain.hidden = mode !== "sequence";
  dom.gridStep.hidden = mode !== "step";
  dom.gridConfig.hidden = mode !== "config";
}

function bindActions() {
  document.querySelectorAll("[data-action]").forEach((btn) => {
    btn.addEventListener("click", (e) => {
      const action = e.currentTarget.dataset.action;
      handleAction(action);
    });
  });

  document.addEventListener("keydown", (event) => {
    if (event.target.closest("input, textarea")) return;
    const keysThatBlock = [
      "ArrowUp",
      "ArrowDown",
      "ArrowLeft",
      "ArrowRight",
      " ",
      "Tab",
      "Backspace",
    ];
    if (keysThatBlock.includes(event.key) || event.key.length === 1) {
      event.preventDefault();
    }
    API.sendCommand("key", {
      key: event.key,
      code: event.code,
      shift: event.shiftKey,
    });
  });
}

function handleAction(action) {
  switch (action) {
    case "play":
      return API.sendCommand("togglePlay");
    case "rewind":
      return API.sendCommand("rewind");
    case "addRow":
      return API.sendCommand("addRow");
    case "removeRow":
      return API.sendCommand("removeRow");
    case "bpmUp":
      return API.sendCommand("incrementBpm");
    case "bpmDown":
      return API.sendCommand("decrementBpm");
    case "config":
      return API.sendCommand("setMode", { mode: "config" });
    case "editStep":
      return API.sendCommand("setMode", { mode: "step" });
    case "closeModal":
      return API.sendCommand("setMode", { mode: "sequence" });
    case "arm":
      return API.sendCommand("armSequence");
    default:
      return null;
  }
}

function boot() {
  dom.bpm = document.querySelector("[data-bpm]");
  dom.pattern = document.querySelector("[data-pattern]");
  dom.step = document.querySelector("[data-step]");
  dom.seqRange = document.querySelector("[data-seq-range]");
  dom.mode = document.querySelector("[data-mode]");
  dom.led = document.querySelector("[data-led]");
  dom.meterSpans = Array.from(document.querySelectorAll("[data-meter] span"));
  dom.dots = Array.from(document.querySelectorAll("[data-status-text] .dot"));
  dom.configTable = document.querySelector("[data-config-table]");
  dom.stepTable = document.querySelector("[data-step-table]");
  dom.seqTable = document.querySelector("[data-seq-table]");
  dom.gridMain = document.querySelector('[data-view="main-grid"]');
  dom.gridStep = document.querySelector('[data-view="step-grid"]');
  dom.gridConfig = document.querySelector('[data-view="config-grid"]');
  bindActions();
}

window.addEventListener("DOMContentLoaded", boot);
