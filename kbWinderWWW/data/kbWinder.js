/**
 * @file kbWinder.js
 * @brief Dynamic configuration and control logic for kbWinder Web UI.
 */

let configuration = {}; // Global storage for current config values
let originalApIp = ""; // Store the IP we connected to initially
let ccNames = [];
const UPDATE_URL = "https://kamilbaranski.com/kbWinder/firmware/update.json";

/**
 * @brief Initialize the page, fetch config and setup interactions.
 */
document.addEventListener("DOMContentLoaded", async () => {
  console.log("[kbWinder.js] Loaded from HOME.");

  if (document.querySelector("#reboot-page")) {
    handleRebootLogic();
    return;
  }

  await fetchAndParseVariables();
  await initUI();

  if (document.querySelector("#about-page")) {
    initAboutPage();
    initSectionSorting();
    checkIfUpdateAvailable();
    return;
  }

  setupGenericEventListeners();
  //syncStatus();
  initWebSocket();
  initSectionSorting();
  //checkIfUpdateAvailable();
});

function sendCommand(cmdText) {
  console.log("Sending command:", cmdText);
  // encodeURIComponent dba o to, by znaki specjalne (spacje, #, &)
  // nie zepsuły struktury adresu URL
  fetch("/api/cmd?cmd=" + encodeURIComponent(cmdText))
    .then((response) => response.text())
    .then((data) => console.log("Odpowiedź z ESP:", data))
    .catch((err) => console.error("Błąd:", err));
}

let jogInterval = null;
let isJogging = false;

function startJog(command) {
  if (isJogging) return;
  isJogging = true;

  // 2. Wyślij główną komendę ruchu (np. 'JW -1000')
  sendCommand(command);

  // 3. Odpal Watchdog (JPING co 700ms)
  jogInterval = setInterval(() => {
    sendCommand("JOG PING");
  }, 700);
}

function stopJog() {
  if (!isJogging) return;

  isJogging = false;

  // 1. Zatrzymaj wysyłanie pingów
  if (jogInterval) {
    clearInterval(jogInterval);
    jogInterval = null;
    console.log("🛑 Jog Watchdog stopped");
  }

  // 2. Wyślij natychmiastowe zatrzymanie
  sendCommand("STOP");
}

function setAllSettings(containerId) {
  const container = document.getElementById(containerId);
  if (!container) return;

  const elements = container.querySelectorAll("input, select, textarea");

  elements.forEach((el) => {
    if (typeof el.onchange === "function") {
      el.dispatchEvent(new Event("change", { bubbles: true }));
    }
  });
}

function generateDynamicUI() {
  const container = document.getElementById("runtime-stats"); // Twoja sekcja w HTML
  if (!container) return;

  // Filtrujemy tylko zmienne RUNTIME, których jeszcze nie ma w DOM
  deviceVariables
    .filter((v) => v.category === "C_RUNTIME")
    .forEach((v) => {
      const elementId = v.name.replace(/ /g, "_");
      if (!document.getElementById(elementId)) {
        const div = document.createElement("div");
        div.innerHTML = `<strong>${v.name}:</strong> <span id="${elementId}">--</span>`;
        container.appendChild(div);
      }
    });
}

/**
 * @brief Fetches configuration, metadata, and firmware info from the ESP8266.
 */
async function initUI() {
  console.log("[kbWinder] initializing UI");
  try {
    await loadConfiguration();
    await loadCcNames();
  } catch (error) {
    console.warn("Using default UI layout (Config fetch failed):", error);
  }
  renderUI();
}

/**
 * Pobiera konfigurację z urządzenia (pełną lub UI) i scala ją z obiektem globalnym.
 * @returns {Promise<Object>} Zwraca scalony obiekt konfiguracji.
 */
let configPromise = null; // "Brama" dla zapytań

/**
 * renderUI
 */
function renderUI() {
  if (!configuration) {
    console.warn("[UI] Render aborted: Global configuration is null.");
    return;
  }

  sendCommand("GET");

  // 1. Update static UI elements
  updateVersionInfo();

  // 2. Render System and Network sections
  fillSystemFields();
  fillNetworkFields();

  transformCheckboxesToSwitches();

  if (typeof ui !== "undefined" && ui && ui.sectionOrder) {
    applySectionOrder(ui.sectionOrder);
  }

  initPasswordToggles();
}

/**
 * @brief Gathers all data from UI and sends the JSON to the server.
 */
async function uploadConfiguration() {
  const saveBtn = document.querySelector("#saveButton");
  const statusBanner = document.querySelector("#statusBanner"); // Załóżmy, że masz div na komunikaty

  saveBtn.disabled = true;
  saveBtn.innerText = "Saving...";

  // 1. Synchronizacja UI -> Obiekt
  syncUiToConfiguration();

  // 2. Klonowanie i czyszczenie haseł (nie wysyłamy "********")
  const payload = JSON.parse(JSON.stringify({ values: configuration }));

  const removePasswordMasks = (obj) => {
    for (const key in obj) {
      if (obj[key] === "********") {
        delete obj[key]; // ESP nie nadpisze hasła, jeśli klucza nie ma w JSON
      } else if (typeof obj[key] === "object" && obj[key] !== null) {
        removePasswordMasks(obj[key]);
      }
    }
  };
  removePasswordMasks(payload);

  try {
    const response = await fetch("/api/configuration", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });

    const result = await response.json();

    if (response.ok) {
      showNotification("Configuration saved. Connecting to WiFi...", "success");
    } else {
      showNotification("Server rejected configuration", "error");
    }
  } catch (e) {
    showNotification("Upload failed: " + e.message, "error");
  } finally {
    saveBtn.disabled = false;
    saveBtn.innerText = "Save Settings";
  }
}

function showNotification(text, type = "info") {
  const note = document.querySelector("#js-notification") || createNotificationEl();
  note.innerText = text;
  note.className = `notification ${type}`;
  note.classList.remove("hidden");

  // Autohide po 5 sekundach
  setTimeout(() => note.classList.add("hidden"), 5000);
}

function createNotificationEl() {
  const el = document.createElement("div");
  el.id = "js-notification";
  el.classList.add("js-notification");
  document.body.appendChild(el);
  return el;
}

/**
 * @brief Maps current UI state back to the 'configuration' global object.
 */
function syncUiToConfiguration() {
  // System
  configuration.system.hostName = document.querySelector("#hostName").value;
  configuration.system.serialLogLevel = parseInt(document.querySelector("#serialLogLevel").value);
  configuration.system.webLogLevel = parseInt(document.querySelector("#webLogLevel").value);

  configuration.system.webStatusUpdate = document.querySelector("#webStatusUpdate").checked;
  configuration.system.webSystemLogEnabled = document.querySelector("#webSystemLogEnabled").checked;

  // Services
  configuration.system.pushOTAEnabled = document.querySelector("#pushOTAEnabled").checked;
  configuration.system.mdnsEnabled = document.querySelector("#mdnsEnabled").checked;
  configuration.system.nbsEnabled = document.querySelector("#nbsEnabled").checked;
  configuration.system.ssdpEnabled = document.querySelector("#ssdpEnabled").checked;
  configuration.system.dnsServerEnabled = document.querySelector("#dnsServerEnabled").checked;
  configuration.system.midiEnabled = document.querySelector("#midiEnabled").checked;
  configuration.system.hardwareInputsEnabled = document.querySelector("#hardwareInputsEnabled").checked;

  // Network
  configuration.network.wifiMode = parseInt(document.querySelector("#wifiMode").value);
  configuration.network.stationSsid = document.querySelector("#stationSsid").value;
  configuration.network.stationPassword = document.querySelector("#stationPassword").value;
  configuration.network.stationDhcpEnabled = document.querySelector("#stationDhcpEnabled").checked;
  configuration.network.stationStaticIp = document.querySelector("#stationStaticIp").value;
  configuration.network.stationStaticMask = document.querySelector("#stationStaticMask").value;
  configuration.network.stationStaticGateway = document.querySelector("#stationStaticGateway").value;

  configuration.network.accessPointSsid = document.querySelector("#accessPointSsid").value;
  configuration.network.accessPointPassword = document.querySelector("#accessPointPassword").value;
  configuration.network.accessPointIp = document.querySelector("#accessPointIp").value;
  configuration.network.accessPointMask = document.querySelector("#accessPointMask").value;

  // Security
  configuration.security.authenticationEnabled = document.querySelector("#authenticationEnabled").checked;
  configuration.security.adminUsername = document.querySelector("#adminUsername").value;
  configuration.security.adminPassword = document.querySelector("#adminPassword").value;

  configuration.ui.sectionOrder = getSectionOrder();
}

/**
 * @brief Renders the system configuration section.
 */
function fillSystemFields() {
  const sys = configuration.system;
  const meta = configuration.meta;

  if (!document.querySelector("#setupForm")) {
    return;
  }
  if (!sys || !meta || !meta.logLevelOptions) {
    console.warn("Metadata or logLevels missing!");
    return;
  }

  document.querySelector("#hostName").value = sys.hostName;
  renderLogLevelSelect("#serialLogLevel", sys.serialLogLevel, meta.logLevelOptions);
  renderLogLevelSelect("#webLogLevel", sys.webLogLevel, meta.logLevelOptions);

  // Services
  document.querySelector("#pushOTAEnabled").checked = sys.pushOTAEnabled;
  document.querySelector("#mdnsEnabled").checked = sys.mdnsEnabled;
  document.querySelector("#nbsEnabled").checked = sys.nbsEnabled;
  document.querySelector("#ssdpEnabled").checked = sys.ssdpEnabled;
  document.querySelector("#dnsServerEnabled").checked = sys.dnsServerEnabled;
  document.querySelector("#midiEnabled").checked = sys.midiEnabled;
  document.querySelector("#hardwareInputsEnabled").checked = sys.hardwareInputsEnabled;
}

/**
 * @brief Universal function to fill a log level dropdown.
 */
function renderLogLevelSelect(selector, currentValue, options) {
  const select = document.querySelector(selector);
  if (!select) return;

  select.innerHTML = "";
  options.forEach((opt) => {
    const o = document.createElement("option");
    o.value = opt.value; // These will be 1 to 8
    o.text = opt.label; // These will be "ALWAYS", "ERROR", etc.
    if (opt.value == currentValue) o.selected = true;
    select.appendChild(o);
  });
}

/**
 * @brief Renders the network configuration section.
 */
function fillNetworkFields() {
  if (!document.querySelector("#setupForm")) {
    return;
  }
  let net = configuration.network;
  if (!net) {
    console.warn("Network config missing!");
    return;
  }

  document.querySelector("#wifiMode").value = net.wifiMode;
  document.querySelector("#stationSsid").value = net.stationSsid;
  document.querySelector("#stationPassword").value = net.stationPassword;
  document.querySelector("#stationDhcpEnabled").checked = net.stationDhcpEnabled;
  document.querySelector("#stationStaticIp").value = net.stationStaticIp;
  document.querySelector("#stationStaticMask").value = net.stationStaticMask;
  document.querySelector("#stationStaticGateway").value = net.stationStaticGateway;

  document.querySelector("#accessPointSsid").value = net.accessPointSsid;
  document.querySelector("#accessPointPassword").value = net.accessPointPassword;
  document.querySelector("#accessPointIp").value = net.accessPointIp;
  document.querySelector("#accessPointMask").value = net.accessPointMask;

  document.querySelector("#authenticationEnabled").checked = configuration.security.authenticationEnabled;
  document.querySelector("#adminUsername").value = configuration.security.adminUsername;
  document.querySelector("#adminPassword").value = configuration.security.adminPassword;
}

/**
 * @brief Helpers for dropdowns and radio buttons.
 */

function syncRadioButtons(name, value) {
  const radio = document.querySelector(`input[name="${name}"][value="${value}"]`);
  if (radio) radio.checked = true;
}

// Globalny uchwyt do timera
let webStatusUpdateTimer = null;

/**
 * Zarządza timerem odświeżania statusu na podstawie stanu checkboxa.
 * Polling jako rzadki backup (np. co 10 sekund), jeśli WS milczy
 */
function updateStatusPolling() {
  const updateCheckbox = document.querySelector("#webStatusUpdate");
  if (!updateCheckbox) return;

  const isEnabled = updateCheckbox.checked;

  // Najpierw zawsze czyścimy stary timer, żeby uniknąć nakładania się interwałów
  if (webStatusUpdateTimer) {
    clearInterval(webStatusUpdateTimer);
    webStatusUpdateTimer = null;
    console.log("Status Polling: STOPPED");
  }

  if (isEnabled) {
    // Uruchamiamy nowy interwał
    webStatusUpdateTimer = setInterval(async () => {
      // Jeśli WebSocket nie przysłał nic przez 5 sekund, zrób fetch
      if (Date.now() - lastWsMessageTime > 5000) {
        const data = await fetchSystemStatus();
        syncStatus(data);
      }
    }, 10000);
  }
}

/**
 * @brief UI Interactions & Event Listeners.
 */
function setupGenericEventListeners() {
  const setupForm = document.querySelector("#setupForm");
  if (!setupForm) return;

  // Save Form
  setupForm.addEventListener("submit", (e) => {
    e.preventDefault();
    uploadConfiguration();
  });

  // webStatusUpdate
  document.querySelector("#webStatusUpdate")?.addEventListener("change", updateStatusPolling);
  // btn start update
  document.getElementById("btn-start-update")?.addEventListener("click", handleFirmwareUpdate);
  // DHCP Toggle
  document.querySelector("#stationDhcpEnabled")?.addEventListener("change", updateDhcpState);

  // Restart Button
  document.querySelector("#restartButton")?.addEventListener("click", () => {
    if (confirm("Are you sure you want to restart the device?")) window.location.href = "/api/restart";
  });

  document.querySelector("#scanBtn")?.addEventListener("click", scanWifi);

  document.querySelectorAll('input[name="buttonOrder"]').forEach((radio) => radio.addEventListener("change", updateButtonOrder));
}

/**
 * @brief Toggles the visual state of static IP fields based on DHCP checkbox.
 */
function updateDhcpState() {
  if (!document.querySelector("#setupForm")) {
    return;
  }
  const dhcpEnabled = document.querySelector("#stationDhcpEnabled").checked;
  const staticSection = document.querySelector("#static-ip-settings");
  dhcpEnabled ? staticSection.classList.add("disabled") : staticSection.classList.remove("disabled");
}

/**
 * @brief Updates the footer version info from the JSON data.
 */
function updateVersionInfo() {
  const info = configuration?.info;
  const footer = document.querySelector("#footer");

  if (!info || !footer) {
    console.warn("[UI] Cannot update version info: configuration.info or #footer missing.");
    return;
  }
  const aboutUrl = document.querySelector("#about-page") ? info.productUrl : "about";
  footer.innerHTML = `<a href="${aboutUrl}">${info.name}</a> v${info.version} [${info.date}] &copy; <a href="https://${info.authorURL}">${info.author}</a>`;
}

/**
 * @brief Triggers WiFi scan and populates the SSID field.
 */
async function scanWifi() {
  const scanBtn = document.querySelector("#scanBtn");
  const resultsDiv = document.querySelector("#wifiScanResults");

  scanBtn.disabled = true;
  scanBtn.innerText = "Scanning...";
  resultsDiv.innerHTML = "<p>Searching for networks...</p>";
  resultsDiv.classList.remove("hidden");

  try {
    // 1. Uruchom skanowanie
    await fetch("/api/wifi-scan-start");

    // 2. Zacznij odpytywanie o wyniki (co 1.5 sekundy)
    const pollInterval = setInterval(async () => {
      try {
        const response = await fetch("/api/wifi-scan-results");
        const data = await response.json();

        if (data.status === "ready") {
          clearInterval(pollInterval);
          renderWifiList(data.networks);
          scanBtn.disabled = false;
          scanBtn.innerText = "Scan WiFi";
        } else if (data.status === "failed") {
          clearInterval(pollInterval);
          resultsDiv.innerHTML = "<p>Scan failed.</p>";
          scanBtn.disabled = false;
        }
      } catch (e) {
        clearInterval(pollInterval);
        console.error("Polling error", e);
      }
    }, 1500);
  } catch (e) {
    alert("Could not start scan.");
    scanBtn.disabled = false;
  }
}

function renderWifiList(networks) {
  const resultsDiv = document.querySelector("#wifiScanResults");
  if (!networks || networks.length === 0) {
    resultsDiv.innerHTML = "<p>No networks found.</p>";
    return;
  }

  // Budujemy listę klikalnych elementów
  resultsDiv.innerHTML = "<strong>Select network:</strong><br>";
  networks.forEach((net) => {
    const row = document.createElement("div");
    row.className = "formfield spread pointer-row"; // Dodaj styl w CSS
    row.innerHTML = `
      <span>${net.ssid} <small>(${net.rssi}dBm)</small></span>
      <span>${net.secure ? "🔒" : "🔓"}</span>
    `;

    // Po kliknięciu - wpisz SSID i schowaj listę
    row.onclick = () => {
      document.querySelector("#stationSsid").value = net.ssid;
      resultsDiv.classList.add("hidden");
    };

    resultsDiv.appendChild(row);
  });
}

// --- 2. Data Fetching Function ---
/**
 * @brief Fetches current system status from the API.
 * @return {Promise<Object|null>}
 */
async function fetchSystemStatus() {
  const isLocalFile = window.location.protocol === "file:";

  // Jeśli na dysku, to jedyny sensowny cel to pełny adres urządzenia
  // (pod warunkiem, że w ESP jest Access-Control-Allow-Origin: *)
  const urls = isLocalFile ? ["http://kbwinder-6b33.local/api/status"] : ["/api/status"];

  for (const url of urls) {
    try {
      const res = await fetch(url);
      if (res.ok) return await res.json();
    } catch (e) {}
  }
  return null;
}

// --- 3. Periodic Sync Function ---
/**
 * @brief Periodic synchronization of UI with the device state.
 */
const syncStatus = async (wsdata = false) => {
  try {
    // 1. Safe data retrieval
    // If fetch fails (e.g. during reboot), it will throw an error caught by 'catch'
    const data = wsdata || (await fetchSystemStatus());
    if (!data) return;

    // 2. Update Speed (if exists)
    //if (data.actualSpeed !== undefined) updateSpeedUI(data.actualSpeed);

    // 4. Update system info fields
    safeUpdate("freeHeap", data.freeHeap);
    safeUpdate("wifiRSSI", data.wifiRSSI);
    if (data.uptime !== undefined) safeUpdate("uptime", formatUptime(data.uptime));

    if (data.justConnected !== undefined) checkNetworkTransition(data);

    // 5. debug more. F
    // if (data.webDebugEnabled !== undefined) configuration.system.webDebugEnabled = data.webDebugEnabled;
    updateWebDebug(data);
  } catch (error) {
    console.log("Status sync skipped (device offline or busy)");
  }
};

/**
 * Renders raw system data for debugging purposes.
 * Handles nested arrays (like pedals) by expanding them.
 */
function updateWebDebug(data) {
  const webDebugEl = document.querySelector("#webDebug");
  if (!webDebugEl) return;

  if (configuration.system.webDebugEnabled) {
    let html = "";

    for (const [key, value] of Object.entries(data)) {
      if (key === "pedals" && Array.isArray(value)) {
        // Special rendering for the nested pedals array
        html += `<b>${key}:</b><div class="debug-indent">`;
        value.forEach((p, i) => {
          // Format each pedal object as a compact string
          const pDetails = Object.entries(p)
            .map(([pk, pv]) => `<b>${pk}:</b> <val>${pv}</val>`)
            .join("<br>");
          html += `[${i}] { ${pDetails} }<br>`;
        });
        html += `</div>`;
      } else {
        // Standard rendering for flat values
        html += `<b>${key}:</b> <val>${value}</val>`;
      }
    }

    webDebugEl.innerHTML = html;
    webDebugEl.classList.remove("hidden");
  } else {
    webDebugEl.classList.add("hidden");
  }
}

// 4. Helper for safe DOM updates
const safeUpdate = (id, value) => {
  if (value === undefined || value === null) return;

  // Normalizacja ID: Zamieniamy spacje na podkreślenia, jeśli tak masz w HTML
  // Np. "SCREW PITCH" -> "SCREW_PITCH"
  const elementId = id.replace(/ /g, "_");
  const el = document.getElementById(elementId);

  if (el) {
    if (el.type === "checkbox") {
      // Obsługa logiczna dla różnych formatów prawdy
      const isTrue = ["ON", "TRUE", "1", "BACKWARD"].includes(value.toString().toUpperCase());
      el.checked = isTrue;
    } else if (el.tagName === "INPUT" || el.tagName === "SELECT") {
      el.value = value;
    } else {
      el.innerText = value;
    }
    triggerFlash(el);
  }
};

// Pomocnicza funkcja do restartu animacji CSS
const triggerFlash = (el) => {
  // Usunięcie klasy resetuje animację
  el.classList.remove("updated-flash");

  // MAGIA: Wymuszenie "Reflow".
  // Przeglądarka musi przeliczyć układ strony, co resetuje stan animacji CSS.
  // Bez tego, dodanie klasy od razu po usunięciu nie uruchomiłoby jej ponownie.
  void el.offsetWidth;

  // Dodanie klasy uruchamia animację od 0%
  el.classList.add("updated-flash");
};

/**
 * @brief Formats seconds into H:M:S.
 */
function formatUptime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${h}h ${m}m ${s}s`;
}

/**
 * Unified fetcher that tries multiple sources and handles environment-specific constraints.
 * @param {Array<string>} urls - List of URLs to try.
 * @param {Object} options - Standard fetch options + 'bustCache' flag.
 * @returns {Promise<any|null>} Parsed JSON or null if all failed.
 */
async function smartFetch(urls, options = {}) {
  console.log("smartFetch: ", urls);
  const isLocalFile = window.location.protocol === "file:";

  // Filter URLs: Don't even try relative paths (like /api/...) if running from a local file
  const validUrls = urls.filter((url) => {
    if (isLocalFile && (url.startsWith("/") || !url.startsWith("http"))) {
      return false; // Skip relative paths on file:// to avoid CORS/Network noise
    }
    return true;
  });

  for (let url of validUrls) {
    console.log("smartFetch: trying " + url);
    try {
      // Add cache buster if needed
      const finalUrl = options.bustCache ? `${url}${url.includes("?") ? "&" : "?"}t=${Date.now()}` : url;

      const response = await fetch(finalUrl, {
        method: options.method || "GET",
        headers: options.headers || {},
        mode: "cors", // Explicitly request CORS
        // Don't wait forever - 3 seconds timeout
        signal: AbortSignal.timeout(3000),
      });

      if (response.ok) {
        const data = await response.json();
        console.log(`%c[SmartFetch] SUCCESS %c${url}`, "color: white; background: green; padding: 2px 5px; border-radius: 3px;", "color: gray;");
        if (data && data.values && typeof data.values !== "function") {
          return data.values;
        }
        return data;
      }
    } catch (e) {
      // Quiet fail - continue to next URL
    }
  }

  console.warn(`[SmartFetch] Failed to load from: ${urls.join(", ")}`);
  return null;
}

let socket;
let lastWsMessageTime = Date.now();

function initWebSocket() {
  // Jeśli hostname jest pusty (file://) lub nie jesteśmy na urządzeniu - wychodzimy
  if (!isRunningOnDevice()) {
    console.info("WebSocket: Connection skipped (local file or web preview detected).");
    return;
  }

  console.log("WebSocket: Attempting to connect...");

  // Tworzymy socket tylko, gdy jesteśmy na prawdziwym IP/hostnamie urządzenia
  socket = new WebSocket(`ws://${window.location.hostname}/ws`);

  socket.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      handleWebsocketMessage(data);
    } catch (e) {
      console.error("WS JSON Error:", e);
    }
  };

  socket.onclose = () => {
    console.warn("WebSocket: Connection lost. Reconnecting in 2s...");
    setTimeout(initWebSocket, 2000);
  };

  socket.onerror = (err) => {
    console.error("WebSocket Error:", err);
  };
}

function handleWebsocketMessage(data) {
  lastWsMessageTime = Date.now();

  //console.log(data);

  if (data.type === "status") {
    // console.log("WS Status Update:", data.actualSpeed);
    syncStatus(data);
  }

  if (data.type === "log") {
    console.log(data);
    appendLog(data.level, data.message);
  }

  if (data.type === "ota") {
    document.getElementById("ota-modal").style.display = "block";
    let bar = document.getElementById("ota-bar");
    let msg = document.getElementById("ota-msg");
    let perc = document.getElementById("ota-perc");

    bar.style.width = data.progress + "%";
    perc.innerText = data.progress + "%";

    if (data.stage === "FS_START") msg.innerText = "Step 1/2: Updating Filesystem...";
    if (data.stage === "BIN_START") msg.innerText = "Step 2/2: Updating Firmware...";
    if (data.stage === "SUCCESS") {
      msg.innerText = "Success! Device is rebooting...";
      setTimeout(() => {
        window.location.reload();
      }, 5000);
    }
    if (data.stage === "ERROR") {
      msg.innerText = "Error: " + data.message;
      msg.style.color = "red";
      setTimeout(() => {
        document.getElementById("ota-modal").style.display = "none";
      }, 5000);
    }
  }
}

const parseNanoResponse = (text) => {
  // Regex wyłapuje: [KATEGORIA] KLUCZ: WARTOŚĆ
  // g1: kategoria, g2: klucz, g3: wartość
  const regex = /\[(\w+)\]\s+([^:]+):\s+(.+)/g;
  let match;

  while ((match = regex.exec(text)) !== null) {
    const category = match[1]; // MACHINE, PRESET lub RUNTIME
    const key = match[2].trim(); // np. SCREW PITCH
    const value = match[3].trim(); // np. 1.000

    // Próbujemy zaktualizować element o ID takim samym jak KLUCZ
    safeUpdate(key, value);

    // Opcjonalnie: logowanie postępu w konsoli
    console.log(`Parsed ${category}: ${key} = ${value}`);
  }
};

function appendLog(level, message) {
  parseNanoResponse(message);
  const consoleElem = document.getElementById("systemLog");
  if (!consoleElem) return;

  const line = document.createElement("div");
  // level.toLowerCase() da nam klasy log-info, log-error itd.
  line.className = `log-line log-${level.toLowerCase()}`;

  line.innerHTML = `<span class="log-prefix">[${level}]</span>${message}`;

  consoleElem.appendChild(line);

  // Auto-scroll jeśli użytkownik nie przewija ręcznie góry
  consoleElem.scrollTop = consoleElem.scrollHeight;

  // Limit linii, by nie spowolnić przeglądarki
  while (consoleElem.childNodes.length > 150) {
    consoleElem.removeChild(consoleElem.firstChild);
  }
}

// Rejestr zmiennych wyciągnięty z variables.h
let deviceVariables = [];

async function fetchAndParseVariables() {
  try {
    const response = await fetch("/variables.h"); // ESP musi serwować ten plik
    const text = await response.text();

    // Regex dopasowany do Twojej struktury: { "NAZWA", &wskaźnik, TYP, KATEGORIA, DLUGOSC }
    const regex = /\{\s*"([^"]+)"\s*,\s*[^,]+\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\d+)\s*\}/g;
    let match;
    deviceVariables = [];

    while ((match = regex.exec(text)) !== null) {
      deviceVariables.push({
        name: match[1], // np. "SCREW PITCH"
        type: match[2], // np. "T_FLOAT"
        category: match[3], // np. "C_MACHINE"
        length: parseInt(match[4]),
      });
    }

    console.log("✅ Parsed variables:", deviceVariables);
    // Po sparsowaniu możemy np. wygenerować brakujące pola w HTML
    generateDynamicUI();
  } catch (err) {
    console.error("❌ Failed to parse variables.h:", err);
  }
}

/**
 * Obsługa inteligentnego przeładowania strony po restarcie
 * @param {string} target - URL docelowy (np. '/setup')
 */
function handleRebootLogic(target = "/setup") {
  const statusEl = document.getElementById("status");
  const barEl = document.getElementById("reboot-bar");
  let progress = 0;
  let rebootDetected = false;

  // 1. Symulacja paska postępu (dojeżdża do 90% i czeka na realne połączenie)
  const progressInterval = setInterval(() => {
    if (progress < 90) {
      progress += Math.random() * 2; // Losowy przyrost dla naturalnego efektu
      if (progress > 90) progress = 90;
      if (barEl) barEl.style.width = progress + "%";
    }
  }, 400);

  const checkConnection = () => {
    // fetch z no-store, żeby ominąć cache przeglądarki i routera
    fetch("/api/status", { cache: "no-store" })
      .then((response) => {
        if (response.ok) {
          clearInterval(progressInterval);
          if (barEl) barEl.style.width = "100%";
          if (statusEl) statusEl.innerText = "Device back online! Redirecting...";

          // Małe opóźnienie, żeby użytkownik zobaczył 100%
          setTimeout(() => {
            window.location.href = target;
          }, 500);
        } else {
          setTimeout(checkConnection, 1000);
        }
      })
      .catch(() => {
        // Connection refused / timeout - ESP jest w trakcie bootowania
        setTimeout(checkConnection, 1000);
      });
  };

  // Zacznij pingowanie po 3 sekundach (daj ESP czas na zgaszenie starego stosu TCP)
  setTimeout(checkConnection, 3000);
}

// Funkcja pomocnicza do detekcji środowiska
const isRunningOnDevice = () => {
  const host = window.location.hostname;
  if (!host || host === "localhost" || host === "127.0.0.1") return false;
  return !host.includes("kamilbaranski.com");
};

/**
 * Refactored About page logic using smartFetch.
 */
async function initAboutPage() {
  await loadConfiguration();
  updateVersionInfo();
}

async function checkIfUpdateAvailable() {
  let installedVersion = configuration?.info?.version || null;

  if (isRunningOnDevice()) {
    const localVerRow = document.getElementById("local-ver-row");
    const currentVerEl = document.getElementById("current-version");

    if (localVerRow) localVerRow.classList.remove("hidden");
    if (currentVerEl && installedVersion) currentVerEl.innerText = installedVersion;
  }

  // 2. Pobierz dane zdalne
  const remoteData = await smartFetch([UPDATE_URL], { bustCache: true });
  const remoteVerEl = document.getElementById("remote-version");

  if (remoteData && remoteData.version) {
    const remoteVersion = remoteData.version;
    if (remoteVerEl) remoteVerEl.innerText = remoteVersion;

    // 3. Bezpieczne porównanie (trim() usuwa białe znaki)
    const vLocal = String(installedVersion || "").trim();
    const vRemote = String(remoteVersion).trim();

    if (vLocal && vRemote && vLocal !== vRemote) {
      const updateStatusEl = document.getElementById("update-status");
      const updateLink = document.getElementById("update-link");

      if (updateStatusEl) {
        // Bezpieczne ustawianie linku - tylko jeśli element istnieje
        if (updateLink) {
          const isRemoteServer = window.location.hostname.includes("kamilbaranski.com");
          updateLink.href = isRemoteServer ? "firmware/" : "https://kamilbaranski.com/kbWinder/firmware/";
        }

        console.log("%c[Update] New version detected!", "color: orange; font-weight: bold;");
        updateStatusEl.classList.remove("hidden");
      }
    }
  } else {
    if (remoteVerEl) remoteVerEl.innerText = "Not available";
  }
}

/**
 * Automatically wraps all checkboxes in a 'switch' label and adds the slider span.
 */
function transformCheckboxesToSwitches() {
  const checkboxes = document.querySelectorAll('input[type="checkbox"]');

  checkboxes.forEach((cb) => {
    // 1. Skip if already transformed
    if (cb.parentNode.classList.contains("switch")) return;

    // 2. Create the wrapper <label class="switch">
    const wrapper = document.createElement("label");
    wrapper.className = "switch";

    // 3. Create the <span class="slider"></span>
    const slider = document.createElement("span");
    slider.className = "slider";

    // 4. Place the wrapper in the DOM where the checkbox was
    cb.parentNode.insertBefore(wrapper, cb);

    // 5. Move the checkbox into the wrapper and add the slider after it
    wrapper.appendChild(cb);
    wrapper.appendChild(slider);
  });
}

/**
 * Advanced Drag and Drop for sections with Mobile Ghost Preview.
 * Optimized for low-end devices using GPU transforms and requestAnimationFrame.
 */
function initSectionSorting() {
  const container = document.querySelector("main");
  const sections = container.querySelectorAll("section");

  let ghost = null;
  let activeSection = null;
  let activeTouchId = null;
  let startY = 0;
  let ticking = false;

  // Funkcja czyszcząca - wywoływana po zakończeniu każdego rodzaju przeciągania
  const cleanup = () => {
    if (ghost) {
      ghost.remove();
      ghost = null;
    }
    if (activeSection) {
      activeSection.classList.remove("dragging", "placeholder");
      activeSection.draggable = false;
      activeSection = null;
    }
    activeTouchId = null;
    updateConfigWithOrder();
  };

  sections.forEach((section) => {
    // --- MYSZKA (DESKTOP) ---
    section.addEventListener("mousedown", (e) => {
      const isHandle = e.target.closest("h3");
      const isInput = e.target.closest("input, button, select, .slider, a");
      // Włączamy draggable tylko jeśli kliknięto w uchwyt (H3) a nie w input
      section.draggable = !!(isHandle && !isInput);
    });

    section.addEventListener("dragstart", (e) => {
      if (!section.draggable) {
        e.preventDefault();
        return;
      }
      activeSection = section;
      section.classList.add("dragging");
    });

    section.addEventListener("dragend", cleanup);

    // --- DOTYK (MOBILE) ---
    section.addEventListener(
      "touchstart",
      (e) => {
        if (activeTouchId !== null) return; // Blokada multitouch

        const isHandle = e.target.closest("h3");
        const isInput = e.target.closest("input, button, select, .slider, a");

        if (isHandle && !isInput) {
          const touch = e.changedTouches[0];
          activeTouchId = touch.identifier;
          activeSection = section;

          startY = touch.clientY;
          const rect = section.getBoundingClientRect();

          // Tworzenie ducha (podglądu)
          ghost = section.cloneNode(true);
          ghost.classList.add("section-ghost");
          ghost.style.width = `${rect.width}px`;
          ghost.style.top = `${rect.top}px`;
          ghost.style.left = `${rect.left}px`;
          document.body.appendChild(ghost);

          section.classList.add("dragging", "placeholder");
          if (navigator.vibrate) navigator.vibrate(20);
        }
      },
      { passive: false },
    );

    section.addEventListener(
      "touchmove",
      (e) => {
        if (activeTouchId === null || !ghost) return;

        const touch = Array.from(e.changedTouches).find((t) => t.identifier === activeTouchId);
        if (!touch) return;

        if (e.cancelable) e.preventDefault();
        const deltaY = touch.clientY - startY;

        if (!ticking) {
          window.requestAnimationFrame(() => {
            if (!ghost) return;
            ghost.style.transform = `translateY(${deltaY}px)`;

            const target = document.elementFromPoint(touch.clientX, touch.clientY);
            const nextSection = target?.closest("section:not(.dragging)");

            if (nextSection && nextSection.parentNode === container) {
              const ops = container.querySelector("section:has(#saveButton)");
              const rect = nextSection.getBoundingClientRect();
              if (touch.clientY < rect.top + rect.height / 2) {
                container.insertBefore(activeSection, nextSection);
              } else if (nextSection !== ops) {
                container.insertBefore(activeSection, nextSection.nextSibling);
              }
            }
            ticking = false;
          });
          ticking = true;
        }
      },
      { passive: false },
    );

    section.addEventListener("touchend", (e) => {
      const touch = Array.from(e.changedTouches).find((t) => t.identifier === activeTouchId);
      if (touch) cleanup();
    });

    section.addEventListener("touchcancel", cleanup);
  });

  // --- LOGIKA UPUSZCZANIA DLA MYSZKI (DESKTOP) ---
  container.addEventListener("dragover", (e) => {
    e.preventDefault(); // Niezbędne, aby wywołać drop
    const draggingSection = container.querySelector(".dragging");
    if (!draggingSection) return;

    const afterElement = getDragAfterElement(container, e.clientY);
    const ops = container.querySelector("section:has(#saveButton)");

    if (afterElement == null) {
      // Jeśli jesteśmy na samym dole, wstaw przed przyciskami zapisu
      if (ops && ops !== draggingSection) {
        container.insertBefore(draggingSection, ops);
      } else {
        container.appendChild(draggingSection);
      }
    } else {
      container.insertBefore(draggingSection, afterElement);
    }
  });
}

/**
 * Funkcja pomocnicza dla myszki - oblicza pozycję kursora względem sekcji
 */
function getDragAfterElement(container, y) {
  const draggableElements = [...container.querySelectorAll("section:not(.dragging)")];

  return draggableElements.reduce(
    (closest, child) => {
      const box = child.getBoundingClientRect();
      const offset = y - box.top - box.height / 2;
      if (offset < 0 && offset > closest.offset) {
        return { offset: offset, element: child };
      } else {
        return closest;
      }
    },
    { offset: Number.NEGATIVE_INFINITY },
  ).element;
}

/**
 * Gets the current order of sections based on their IDs.
 * @returns {Array<string>} Array of section IDs in current DOM order.
 */
function getSectionOrder() {
  const container = document.querySelector("#setupForm");
  const sections = container.querySelectorAll("section[id]");
  return Array.from(sections).map((section) => section.id);
}

/**
 * Reorders sections in the DOM based on a saved array of IDs.
 * @param {Array<string>} orderArray Array of section IDs.
 */
function applySectionOrder(orderArray) {
  if (!orderArray || !Array.isArray(orderArray)) return;

  const container = document.querySelector("main");

  orderArray.forEach((id) => {
    const section = document.getElementById(id);
    if (section && section.parentNode === container) {
      // appendChild moves the existing element to the end of the container
      container.appendChild(section);
    }
  });

  // If there's a "submit" section (Operations) that should always be last:
  const opsSection = document.querySelector("section:has(#saveButton)");
  if (opsSection) {
    container.appendChild(opsSection);
  }
}

/**
 * Updates the global configuration object with the current section order.
 */
function updateConfigWithOrder() {
  if (!configuration.ui) configuration.ui = {};
  configuration.ui.sectionOrder = getSectionOrder();
}

/**
 * Handles the firmware update process: confirmation, UI state, and API trigger.
 */
async function handleFirmwareUpdate() {
  const remoteVer = document.getElementById("remote-version")?.innerText || "unknown";

  // 1. Double check with the user
  const confirmMsg = `Are you sure you want to update firmware to v${remoteVer}?\n\n` + `The device will reboot and be offline for a moment.`;

  if (!confirm(confirmMsg)) return;

  // 2. Prepare OTA Modal (Uses your existing WS logic for progress)
  const modal = document.getElementById("ota-modal");
  const msg = document.getElementById("ota-msg");
  const bar = document.getElementById("ota-bar");

  if (modal) {
    modal.style.display = "block";
    if (msg) msg.innerText = "Initializing update...";
    if (bar) bar.style.width = "0%";
  }

  // 3. Trigger the update on ESP
  try {
    const response = await fetch("/api/update", {
      method: "POST",
      cache: "no-cache",
    });

    if (!response.ok) throw new Error(`HTTP Error ${response.status}`);

    console.log("[OTA] Update command accepted. Monitoring via WebSocket...");
  } catch (err) {
    console.error("[OTA] Trigger failed:", err);
    alert("Could not start update: " + err.message);
    if (modal) modal.style.display = "none";
  }
}

/**
 * Restricts range input interaction to the thumb/handle only.
 * Prevents accidental value jumps when dragging sections.
 */
function setupRangeHandleOnly(input) {
  const handler = (e) => {
    const rect = input.getBoundingClientRect();
    const min = parseFloat(input.min || 0);
    const max = parseFloat(input.max || 100);
    const value = parseFloat(input.value);

    // Obliczamy pozycję rączki
    const percent = (value - min) / (max - min);
    const thumbWidth = 20; // Zakładana szerokość rączki w px
    const trackWidth = rect.width - thumbWidth;
    const thumbCenter = rect.left + thumbWidth / 2 + percent * trackWidth;

    const clickX = e.clientX || (e.touches ? e.touches[0].clientX : 0);
    const tolerance = 25; // Tolerancja kliknięcia

    if (Math.abs(clickX - thumbCenter) > tolerance) {
      // Jeśli kliknięcie jest poza rączką, zabijamy zdarzenie zanim range je złapie
      e.preventDefault();
      e.stopImmediatePropagation();
    }
  };

  // Używamy 'pointerdown' (obsługuje mysz i dotyk) w fazie przechwytywania (capture)
  input.addEventListener("pointerdown", handler, { capture: true });
}

// Initialization for all MIDI thresholds
function initRangeInputs() {
  document.querySelectorAll('input[type="range"]').forEach(setupRangeHandleOnly);
}

function checkNetworkTransition(data) {
  const banner = document.getElementById("wifiSuccessBanner");
  if (!banner) return;

  // We only show the banner if the ESP explicitly tells us it just connected
  if (data.justConnected) {
    const currentStaIp = data.ipSTA || "0.0.0.0";
    const currentSsid = data.staSSID || "Unknown WiFi";

    document.getElementById("valSSIDSTA").innerText = currentSsid;
    document.getElementById("valSTA").innerText = currentStaIp;
    document.getElementById("valSSIDAP").innerText = data.apSSID || "Unknown WiFi";
    document.getElementById("valAP").innerText = data.ipAP || "192.168.4.1";
    document.getElementById("valMDNS").innerText = data.hostname || "kbWinder.local";

    banner.classList.remove("hidden");

    if (navigator.vibrate) navigator.vibrate([100, 50, 100]);
    console.log(`[NET] ESP notified a fresh connection to ${currentSsid} - IP: ${currentStaIp}`);
  }
}

function copyToClipboard(text) {
  const dummy = document.createElement("textarea");
  document.body.appendChild(dummy);
  dummy.value = text;
  dummy.select();
  try {
    document.execCommand("copy");
    showNotification("Copied to clipboard!", "success");
  } catch (err) {
    console.error("Copy failed", err);
  }
  document.body.removeChild(dummy);
}

/**
 * Inicjalizuje przełączniki widoczności dla wszystkich pól haseł.
 * Powinno być wywołane po wygenerowaniu dynamicznego HTML.
 */
function initPasswordToggles() {
  document.querySelectorAll('input[type="password"]').forEach((input) => {
    // Sprawdzamy, czy już nie dodaliśmy przycisku (ochrona przed wielokrotną inicjalizacją)
    if (input.parentElement.classList.contains("password-wrapper")) return;

    // Tworzymy kontener dla inputa i przycisku
    const wrapper = document.createElement("div");
    wrapper.className = "password-wrapper";
    input.parentNode.insertBefore(wrapper, input);
    wrapper.appendChild(input);

    // Tworzymy przycisk
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "btn-toggle-password";
    btn.innerHTML = "👁️"; // Można użyć ikony SVG lub emoji
    btn.title = "Pokaż/Ukryj hasło";
    wrapper.appendChild(btn);

    // Logika przełączania
    btn.onclick = () => {
      const isPassword = input.type === "password";
      input.type = isPassword ? "text" : "password";
      btn.innerHTML = isPassword ? "🙈" : "👁️";
    };

    // Obsługa Twojego drugiego pytania: co z "********"?
    setupPlaceholderHandler(input);
  });
}

function setupPlaceholderHandler(input) {
  // Czyścimy pole przy focusie, jeśli zawiera tylko maskę z API
  input.addEventListener("focus", () => {
    if (input.value === "********") {
      input.value = "";
    }
  });

  // Jeśli użytkownik nic nie wpisał i opuścił pole, przywracamy maskę
  // (opcjonalne, wizualne potwierdzenie, że hasło nadal "tam jest")
  input.addEventListener("blur", () => {
    if (input.value === "") {
      // Możesz pobrać informację z oryginalnej konfiguracji,
      // czy hasło było tam wcześniej ustawione
      input.value = "********";
    }
  });
}
