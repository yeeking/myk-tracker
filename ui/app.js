const ui = {
  state: null,
  fetching: false,
  table: { cols: 0, rows: 0, cells: [], body: null, head: null },
};

const API = {
  async request(path, options = {}) {
    const res = await fetch(path, options);
    if (!res.ok) {
      throw new Error(`Request failed: ${res.status}`);
    }
    return res.json();
  },
  async fetchState() {
    if (ui.fetching) return;
    ui.fetching = true;
    try {
      const data = await this.request("/state");
      ui.state = data;
      render(data);
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

function render(state) {
  if (!state) return;
  updateMeta(state);
  ensureTable(state);
  updateTableCells(state);
  renderInspector(state);
  renderConfig(state);
  updateStatus(state);
  toggleModals(state);
}

function updateMeta(state) {
  const setText = (selector, text) => {
    const el = document.querySelector(selector);
    if (el) el.textContent = text;
  };
  setText("[data-bpm]", Math.round(state.bpm || 0));
  const seqIdx = state.currentSequence != null ? state.currentSequence : 0;
  const stepIdx = state.currentStep != null ? state.currentStep : 0;
  setText("[data-pattern]", pad(seqIdx + 1));
  setText("[data-step]", pad(stepIdx + 1));
  setText("[data-mode]", state.mode || "sequence");
  const led = document.querySelector("[data-led]");
  if (led) {
    led.textContent = state.isPlaying ? "Play" : "Stop";
    led.classList.toggle("active", !!state.isPlaying);
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

function ensureTable(state) {
  const table = document.querySelector("[data-seq-table]");
  if (!table) return;
  const head = table.querySelector("thead");
  const body = table.querySelector("tbody");
  const { cols, rows } = getTableDims(state);
  if (cols === ui.table.cols && rows === ui.table.rows && ui.table.cells.length) {
    return;
  }

  head.innerHTML = "";
  body.innerHTML = "";
  ui.table.cells = [];

  const headRow = document.createElement("tr");
  headRow.appendChild(document.createElement("th")); // corner cell
  for (let c = 0; c < cols; c++) {
    const th = document.createElement("th");
    th.textContent = `Seq ${pad(c + 1)}`;
    headRow.appendChild(th);
  }
  head.appendChild(headRow);

  for (let r = 0; r < rows; r++) {
    const tr = document.createElement("tr");
    const rowLabel = document.createElement("th");
    rowLabel.className = "row-label";
    rowLabel.textContent = pad(r + 1);
    tr.appendChild(rowLabel);

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

function updateTableCells(state) {
  const { cols, rows } = getTableDims(state);
  if (cols !== ui.table.cols || rows !== ui.table.rows || !ui.table.cells.length) {
    ensureTable(state);
  }
  const grid = state.sequenceGrid || [];
  const playHeads = state.playHeads || [];

  for (let r = 0; r < ui.table.rows; r++) {
    for (let c = 0; c < ui.table.cols; c++) {
      const cell = ui.table.cells[r][c];
      const val = grid[c] && grid[c][r] ? grid[c][r] : "";
      cell.textContent = val;

      cell.className = "seq-cell";
      const playing = playHeads.some((p) => p.sequence === c && p.step === r);
      if (playing) cell.classList.add("active-step");
      if (state.armedSequence === c) cell.classList.add("armed-step");
      if (state.currentSequence === c && state.currentStep === r) {
        cell.classList.add("cursor-step");
      }
    }
  }
}

function renderInspector(state) {
  const row = state.stepData && state.stepData[0] ? state.stepData[0] : [];
  const fieldMap = {
    note: row[2] ?? "",
    velocity: row[3] ?? "",
    length: row[4] ?? "",
    probability: row[5] ?? "",
    channel: row[1] ?? "",
  };

  document
    .querySelectorAll("[data-step-field]")
    .forEach((input) => {
      const key = input.dataset.stepField;
      const val = fieldMap[key];
      if (val !== undefined) {
        input.value = val;
      }
    });
}

function renderConfig(state) {
  const list = document.querySelector("[data-config-list]");
  if (!list) return;
  const seqIdx = state.currentSequence != null ? state.currentSequence : 0;
  const configCols = state.sequenceConfigs || [];
  const values = configCols[seqIdx] || [];
  list.innerHTML = "";
  values.forEach((val) => {
    const item = document.createElement("div");
    item.className = "config-item";
    item.textContent = val;
    list.appendChild(item);
  });
}

function updateStatus(state) {
  const dots = document.querySelectorAll("[data-status-text] .dot");
  if (!dots.length) return;
  dots.forEach((dot, idx) => {
    dot.classList.toggle("active", state.isPlaying && idx % 2 === 0);
  });
  const meter = document.querySelectorAll("[data-meter] span");
  meter.forEach((m, idx) => {
    m.classList.toggle("lit", state.isPlaying && idx < 4);
  });
}

function toggleModals(state) {
  const stepModal = document.querySelector('[data-modal="step"]');
  const configModal = document.querySelector('[data-modal="config"]');
  if (stepModal) {
    stepModal.classList.toggle("active", state.mode === "step");
  }
  if (configModal) {
    configModal.classList.toggle("active", state.mode === "config");
  }
}

function bindActions() {
  document.querySelectorAll("[data-action]").forEach((btn) => {
    btn.addEventListener("click", (e) => {
      const action = e.currentTarget.dataset.action;
      handleAction(action);
    });
  });

  document.querySelectorAll("[data-step-field]").forEach((input) => {
    input.addEventListener("change", (e) => {
      const field = e.currentTarget.dataset.stepField;
      const value = Number(e.currentTarget.value);
      API.sendCommand("setStepValue", { field, value });
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
  bindActions();
  API.fetchState();
  setInterval(() => API.fetchState(), 500);
}

window.addEventListener("DOMContentLoaded", boot);
