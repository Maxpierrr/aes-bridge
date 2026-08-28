// SPDX-License-Identifier: GPL-3.0-only
import { invoke } from "@tauri-apps/api/core";
import type {
  BridgeConfiguration,
  ConfigurationProfile,
  EngineCompatibility,
  EngineStatus,
  FlowConfiguration,
  FlowDirection,
  NetworkInterface,
  RuntimeStatus,
  ValidationReport,
} from "./model";

const app = document.querySelector<HTMLDivElement>("#app")!;

const state: {
  profiles: ConfigurationProfile[];
  interfaces: NetworkInterface[];
  configuration?: BridgeConfiguration;
  validation?: ValidationReport;
  engine?: EngineStatus;
  compatibility?: EngineCompatibility;
  notice: string;
} = { profiles: [], interfaces: [], notice: "" };

const escapeHtml = (value: unknown) =>
  String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

const clone = <T>(value: T): T => structuredClone(value);

async function load() {
  try {
    const [profiles, interfaces, saved, engine] = await Promise.all([
      invoke<ConfigurationProfile[]>("configuration_profiles"),
      invoke<NetworkInterface[]>("list_interfaces").catch(() => []),
      invoke<BridgeConfiguration | null>("load_configuration"),
      invoke<EngineStatus>("engine_status"),
    ]);
    state.profiles = profiles;
    state.interfaces = interfaces;
    state.configuration = saved ?? clone(profiles[0].configuration);
    state.engine = engine;
    await validate();
  } catch (error) {
    app.innerHTML = `<section class="fatal"><h1>AES Bridge</h1><p>${escapeHtml(error)}</p></section>`;
  }
}

async function validate(renderAfter = true) {
  if (!state.configuration) return;
  [state.validation, state.compatibility] = await Promise.all([
    invoke<ValidationReport>("validate_configuration", { configuration: state.configuration }),
    invoke<EngineCompatibility>("engine_compatibility", { configuration: state.configuration }),
  ]);
  if (renderAfter) render();
}

function metric(label: string, value: string, tone = "") {
  return `<div class="metric ${tone}"><span>${label}</span><strong>${value}</strong></div>`;
}

const compactNumber = (value = 0) => new Intl.NumberFormat("fr-FR", { notation: "compact" }).format(value);
const runtime = (): RuntimeStatus | undefined => state.engine?.status;

function sessionRows() {
  const sessions = runtime()?.sessions ?? [];
  if (!sessions.length) {
    return `<div class="empty"><strong>Aucune session SAP reçue</strong><span>Démarrez le moteur avec la découverte SAP activée.</span></div>`;
  }
  return `<div class="session-list">${sessions.map((session) => {
    const compatible = [1, 2, 4, 8].includes(session.channels)
      && session.sampleRate === 48000
      && session.framesPerPacket === 48
      && session.ptpDomain === 0;
    return `<article class="session-row">
      <span class="status-dot ${compatible ? "ok" : "bad"}"></span>
      <div><strong>${escapeHtml(session.name)}</strong><small>${escapeHtml(session.sourceAddress || session.originAddress)} → ${escapeHtml(session.multicastAddress)}:${session.port}</small></div>
      <span>${session.channels} ch</span><span>${session.sampleRate / 1000} kHz</span><span>${session.framesPerPacket} samples</span>
      <button class="secondary" data-session="${escapeHtml(session.id)}" ${compatible ? "" : "disabled"}>Router</button>
    </article>`;
  }).join("")}</div>`;
}

function flowRows(direction: FlowDirection) {
  const flows = state.configuration!.flows.filter((flow) => flow.direction === direction);
  if (!flows.length) return `<div class="empty">Aucun flux ${direction === "receive" ? "reçu" : "émis"}.</div>`;
  return flows.map((flow) => flowRow(flow)).join("");
}

function flowRow(flow: FlowConfiguration) {
  const issue = state.validation?.issues.find((item) => item.flowId === flow.id);
  const end = flow.coreAudioStartChannel + flow.channels - 1;
  return `<article class="flow-row ${flow.enabled ? "" : "disabled"} ${issue ? "invalid" : ""}" data-flow="${escapeHtml(flow.id)}">
    <label class="toggle compact" title="Activer ce flux">
      <input type="checkbox" data-field="enabled" ${flow.enabled ? "checked" : ""}/><i></i>
    </label>
    <input class="flow-name" data-field="name" value="${escapeHtml(flow.name)}" aria-label="Nom du flux" />
    <div class="address"><input data-field="multicastAddress" value="${escapeHtml(flow.multicastAddress)}" aria-label="Adresse multicast"/><span>:</span><input class="port" type="number" data-field="port" value="${flow.port}" aria-label="Port"/></div>
    <div class="format"><select data-field="channels" aria-label="Nombre de canaux">${[1, 2, 4, 8].map((count) => `<option value="${count}" ${count === flow.channels ? "selected" : ""}>${count} ch</option>`).join("")}</select><label>PT<input type="number" min="0" max="127" data-field="payloadType" value="${flow.payloadType}" aria-label="Payload RTP"/></label></div>
    <label class="route">Canal <input type="number" min="1" max="64" data-field="coreAudioStartChannel" value="${flow.coreAudioStartChannel}"/> <span>→ ${end}</span></label>
    <label class="jitter"><input type="number" min="2" max="63" data-field="jitterPackets" value="${flow.jitterPackets}"/> ms</label>
    <button class="icon danger" data-action="delete-flow" title="Supprimer">×</button>
    ${issue ? `<p class="flow-error">${escapeHtml(issue.message)}</p>` : ""}
  </article>`;
}

function render() {
  const config = state.configuration;
  if (!config) return;
  const report = state.validation;
  const engine = state.engine;
  const live = runtime();
  const engineTone = engine?.running ? "ok" : engine?.available ? "warn" : "bad";
  const issueSummary = report?.valid && state.compatibility?.supported
    ? `<span class="validation ok">Prête à démarrer</span>`
    : report?.valid
      ? `<span class="validation warn" title="${escapeHtml(state.compatibility?.message)}">${escapeHtml(state.compatibility?.message ?? "Configuration incomplète")}</span>`
    : `<span class="validation bad">${report?.issues.length ?? 0} erreur(s)</span>`;

  app.className = "app-shell";
  app.innerHTML = `
    <aside class="sidebar">
      <div class="brand"><div class="brand-mark">A<span>B</span></div><div><strong>AES Bridge</strong><small>CONTROL · 64×64</small></div></div>
      <nav>
        <h2>PROFILS</h2>
        ${state.profiles.map((profile) => `<button class="profile ${config.profileId === profile.id ? "active" : ""}" data-profile="${profile.id}"><strong>${escapeHtml(profile.title)}</strong><small>${escapeHtml(profile.description)}</small></button>`).join("")}
      </nav>
      <div class="sidebar-note"><strong>Format verrouillé</strong><span>AES67 · L24 · 48 kHz · 1 ms</span></div>
      <footer>GPLv3 · aucun pilote Merging</footer>
    </aside>
    <main class="workspace">
      <header class="topbar">
        <div><p class="eyebrow">CONFIGURATION ACTIVE</p><input id="config-name" value="${escapeHtml(config.name)}" aria-label="Nom de la configuration"/></div>
        <div class="engine-state"><span class="status-dot ${engineTone}"></span><div><strong>${engine?.running ? "Moteur actif" : "Moteur arrêté"}</strong><small>${escapeHtml(engine?.message ?? "État inconnu")}</small></div></div>
        <button class="secondary" id="refresh">Actualiser</button>
        <button class="secondary stop" id="stop-engine" ${engine?.running ? "" : "disabled"}>Arrêter</button>
        <button class="secondary" id="restart-engine" ${engine?.running && report?.valid && state.compatibility?.supported ? "" : "disabled"}>Relancer</button>
        <button class="primary start" id="start-engine" ${!engine?.running && engine?.available && report?.valid && state.compatibility?.supported ? "" : "disabled"} title="${escapeHtml(state.compatibility?.message)}">Démarrer</button>
        <button class="primary" id="save" ${report?.valid ? "" : "disabled"}>Enregistrer</button>
      </header>

      <section class="overview">
        ${metric("PTP", !config.ptpEnabled ? "Désactivé" : live?.ptpLocked ? "Verrouillé" : engine?.running ? "En attente" : `Domaine ${config.ptpDomain}`, live?.ptpLocked ? "ok" : "warn")}
        ${metric("Réception", engine?.running ? `${compactNumber(live?.rxPackets)} paquets` : `${report?.receiveChannels ?? 0} canaux`, "rx")}
        ${metric("Émission", engine?.running ? `${compactNumber(live?.txPackets)} paquets` : `${report?.transmitChannels ?? 0} canaux`, "tx")}
        ${metric("Pertes", compactNumber(live?.lostPackets), live?.lostPackets ? "bad" : "")}
        ${metric("Reconnexions", compactNumber(live?.reconnects), live?.reconnects ? "warn" : "")}
      </section>

      ${state.notice ? `<div class="notice">${escapeHtml(state.notice)}</div>` : ""}

      <section class="panel network-panel">
        <div class="panel-heading"><div><p class="eyebrow">RÉSEAU</p><h2>Interface et découverte</h2></div>${issueSummary}</div>
        <div class="network-grid">
          <label class="field"><span>Interface Ethernet explicite</span><select id="interface"><option value="">Choisir une interface…</option>${state.interfaces.map((item) => `<option value="${escapeHtml(item.name)}" data-address="${escapeHtml(item.address)}" ${item.name === config.interfaceName ? "selected" : ""}>${escapeHtml(item.name)} · ${escapeHtml(item.address)}</option>`).join("")}</select></label>
          <label class="switch-line"><span><strong>PTPv2</strong><small>Synchronisation, domaine 0</small></span><span class="toggle"><input id="ptp" type="checkbox" ${config.ptpEnabled ? "checked" : ""}/><i></i></span></label>
          <label class="switch-line"><span><strong>Découverte SAP</strong><small>Écouter les annonces SDP</small></span><span class="toggle"><input id="sap-discovery" type="checkbox" ${config.sapDiscovery ? "checked" : ""}/><i></i></span></label>
          <label class="switch-line"><span><strong>Publication SAP</strong><small>Annoncer les flux transmis</small></span><span class="toggle"><input id="sap-publication" type="checkbox" ${config.sapPublication ? "checked" : ""}/><i></i></span></label>
        </div>
      </section>

      ${flowPanel("receive", "Flux reçus", "AES67 vers entrées audio")}
      ${flowPanel("transmit", "Flux émis", "Sorties audio vers AES67")}

      <section class="panel diagnostics">
        <div class="panel-heading"><div><p class="eyebrow">TEMPS RÉEL</p><h2>Diagnostic moteur</h2></div><span>${live ? `${live.activeStreamCount} banque(s) active(s)` : "Moteur arrêté"}</span></div>
        <div class="diagnostic-grid">
          ${diagnostic("RX actif", live?.rxActive ? "Oui" : "Non", live?.rxActive)}
          ${diagnostic("TX actif", live?.txActive ? "Oui" : "Non", live?.txActive)}
          ${diagnostic("Erreurs RTP/SAP", compactNumber((live?.malformedPackets ?? 0) + (live?.sapMalformedPackets ?? 0)), !((live?.malformedPackets ?? 0) + (live?.sapMalformedPackets ?? 0)))}
          ${diagnostic("Sous-alimentations", compactNumber((live?.inputUnderruns ?? 0) + (live?.outputUnderruns ?? 0)), !((live?.inputUnderruns ?? 0) + (live?.outputUnderruns ?? 0)))}
          ${diagnostic("Débordements ring", compactNumber(live?.ringOverruns), !(live?.ringOverruns ?? 0))}
          ${diagnostic("Offset PTP", live ? `${live.ptpOffsetNanoseconds} ns` : "—", live?.ptpLocked)}
        </div>
      </section>

      <section class="panel discovery">
        <div class="panel-heading"><div><p class="eyebrow">SAP / SDP</p><h2>Sessions découvertes</h2></div><button class="secondary" id="scan">Actualiser</button></div>
        ${sessionRows()}
      </section>

      ${report && !report.valid ? `<section class="errors"><strong>À corriger avant enregistrement</strong>${report.issues.map((issue) => `<p>${escapeHtml(issue.flowId ? `${issue.flowId} · ` : "")}${escapeHtml(issue.message)}</p>`).join("")}</section>` : ""}
    </main>`;
  bindEvents();
}

function diagnostic(label: string, value: string, healthy?: boolean) {
  return `<div><span>${label}</span><strong class="${healthy === true ? "good" : healthy === false ? "problem" : ""}">${value}</strong></div>`;
}

function flowPanel(direction: FlowDirection, title: string, subtitle: string) {
  return `<section class="panel flows ${direction}">
    <div class="panel-heading"><div><p class="eyebrow">${direction === "receive" ? "RX" : "TX"}</p><h2>${title}</h2><span>${subtitle}</span></div><button class="secondary" data-action="add-flow" data-direction="${direction}">+ Ajouter</button></div>
    <div class="flow-head"><span></span><span>Nom</span><span>Adresse RTP</span><span>Format</span><span>Routage</span><span>Tampon</span><span></span></div>
    <div class="flow-list">${flowRows(direction)}</div>
  </section>`;
}

function bindEvents() {
  document.querySelectorAll<HTMLButtonElement>("[data-profile]").forEach((button) => {
    button.onclick = async () => {
      const profile = state.profiles.find((item) => item.id === button.dataset.profile);
      if (!profile) return;
      const interfaceName = state.configuration?.interfaceName ?? "";
      const interfaceAddress = state.configuration?.interfaceAddress ?? "";
      state.configuration = clone(profile.configuration);
      if (profile.id !== "diagnostic-loopback") {
        state.configuration.interfaceName = interfaceName === "lo0" ? "" : interfaceName;
        state.configuration.interfaceAddress = interfaceName === "lo0" ? "" : interfaceAddress;
      }
      state.notice = `Profil « ${profile.title} » chargé. Vérifiez les adresses avant de démarrer.`;
      await validate();
    };
  });

  document.querySelector<HTMLInputElement>("#config-name")!.onchange = async (event) => {
    state.configuration!.name = (event.target as HTMLInputElement).value;
    await validate();
  };
  document.querySelector<HTMLSelectElement>("#interface")!.onchange = async (event) => {
    const select = event.target as HTMLSelectElement;
    state.configuration!.interfaceName = select.value;
    state.configuration!.interfaceAddress = select.selectedOptions[0]?.dataset.address ?? "";
    await validate();
  };
  bindCheck("#ptp", "ptpEnabled");
  bindCheck("#sap-discovery", "sapDiscovery");
  bindCheck("#sap-publication", "sapPublication");

  document.querySelectorAll<HTMLElement>("[data-flow]").forEach((row) => {
    row.querySelectorAll<HTMLInputElement | HTMLSelectElement>("[data-field]").forEach((input) => {
      input.onchange = async () => {
        const flow = state.configuration!.flows.find((item) => item.id === row.dataset.flow)!;
        const field = input.dataset.field as keyof FlowConfiguration;
        const value = input instanceof HTMLInputElement && input.type === "checkbox"
          ? input.checked
          : input instanceof HTMLInputElement && input.type === "number" || input instanceof HTMLSelectElement && field === "channels"
            ? Number(input.value)
            : input.value;
        (flow as unknown as Record<string, unknown>)[field] = value;
        await validate();
      };
    });
  });

  document.querySelectorAll<HTMLButtonElement>("[data-action='delete-flow']").forEach((button) => {
    button.onclick = async () => {
      const id = button.closest<HTMLElement>("[data-flow]")!.dataset.flow;
      state.configuration!.flows = state.configuration!.flows.filter((flow) => flow.id !== id);
      await validate();
    };
  });
  document.querySelectorAll<HTMLButtonElement>("[data-action='add-flow']").forEach((button) => {
    button.onclick = async () => addFlow(button.dataset.direction as FlowDirection);
  });

  document.querySelector<HTMLButtonElement>("#save")!.onclick = save;
  document.querySelector<HTMLButtonElement>("#refresh")!.onclick = refreshEngine;
  document.querySelector<HTMLButtonElement>("#scan")!.onclick = refreshEngine;
  document.querySelector<HTMLButtonElement>("#start-engine")!.onclick = () => controlEngine("start_engine");
  document.querySelector<HTMLButtonElement>("#stop-engine")!.onclick = () => controlEngine("stop_engine");
  document.querySelector<HTMLButtonElement>("#restart-engine")!.onclick = () => controlEngine("restart_engine");
  document.querySelectorAll<HTMLButtonElement>("[data-session]").forEach((button) => {
    button.onclick = async () => importSession(button.dataset.session!);
  });
}

function bindCheck(selector: string, field: "ptpEnabled" | "sapDiscovery" | "sapPublication") {
  document.querySelector<HTMLInputElement>(selector)!.onchange = async (event) => {
    state.configuration![field] = (event.target as HTMLInputElement).checked;
    await validate();
  };
}

async function addFlow(direction: FlowDirection) {
  const now = Date.now();
  const used = state.configuration!.flows.filter((flow) => flow.direction === direction);
  const start = Math.min(64, used.reduce((maximum, flow) => Math.max(maximum, flow.coreAudioStartChannel + flow.channels), 1));
  state.configuration!.flows.push({
    id: `${direction}-${now}`,
    name: direction === "receive" ? "Nouveau flux reçu" : "Nouveau flux émis",
    enabled: true,
    direction,
    multicastAddress: direction === "receive" ? "239.69.83.90" : "239.69.83.106",
    sourceAddress: "",
    port: 5004,
    payloadType: 96,
    encoding: "L24",
    sampleRate: 48000,
    channels: 2,
    framesPerPacket: 48,
    packetTimeMicroseconds: 1000,
    coreAudioStartChannel: start,
    jitterPackets: 6,
  });
  await validate();
}

async function save() {
  try {
    const path = await invoke<string>("save_configuration", { configuration: state.configuration });
    state.notice = `Configuration enregistrée : ${path}`;
  } catch (error) {
    state.notice = String(error);
  }
  render();
}

async function controlEngine(command: "start_engine" | "stop_engine" | "restart_engine") {
  state.notice = command === "stop_engine" ? "Arrêt du moteur…" : "Démarrage du moteur…";
  render();
  try {
    if (command !== "stop_engine") {
      await invoke<string>("save_configuration", { configuration: state.configuration });
    }
    state.notice = await invoke<string>(command, command === "stop_engine" ? {} : {
      configuration: state.configuration,
    });
  } catch (error) {
    state.notice = String(error);
  }
  state.engine = await invoke<EngineStatus>("engine_status");
  render();
}

async function importSession(id: string) {
  const session = runtime()?.sessions.find((item) => item.id === id);
  if (!session) return;
  const receive = state.configuration!.flows.filter((flow) => flow.enabled && flow.direction === "receive");
  const start = Math.min(64, receive.reduce((maximum, flow) => Math.max(maximum, flow.coreAudioStartChannel + flow.channels), 1));
  state.configuration!.flows.push({
    id: `sap-${Date.now()}`,
    name: session.name,
    enabled: true,
    direction: "receive",
    multicastAddress: session.multicastAddress,
    sourceAddress: session.sourceAddress,
    port: session.port,
    payloadType: session.payloadType,
    encoding: "L24",
    sampleRate: session.sampleRate,
    channels: session.channels,
    framesPerPacket: session.framesPerPacket,
    packetTimeMicroseconds: 1000,
    coreAudioStartChannel: start,
    jitterPackets: 6,
  });
  state.notice = `Session « ${session.name} » ajoutée aux flux reçus.`;
  await validate();
}

async function refreshEngine() {
  state.engine = await invoke<EngineStatus>("engine_status");
  state.interfaces = await invoke<NetworkInterface[]>("list_interfaces").catch(() => state.interfaces);
  state.notice = "État du moteur et interfaces actualisés.";
  render();
}

load();
setInterval(async () => {
  state.engine = await invoke<EngineStatus>("engine_status").catch(() => state.engine);
  const focused = document.activeElement?.tagName;
  if (focused !== "INPUT" && focused !== "SELECT") render();
}, 2000);
