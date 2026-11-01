import "./style.css";

interface RuleMeta {
  id: string;
  name: string;
  description: string;
  category: string;
  defaultOn: boolean;
}

interface LanguageOption {
  code: string;
  name: string;
  region: string;
  backupUrl?: string;
}

interface AppConfig {
  gameDir: string;
  language: string;
  replaceText: boolean;
  replaceFont: boolean;
  rules: Record<string, boolean>;
  preset: string;
}

interface TableEntry {
  key: string;
  name: string;
  folder: string;
  warning: string;
}

interface BootstrapInfo {
  version: string;
  resourceDir: string;
  offline: boolean;
  rules: RuleMeta[];
  languages: LanguageOption[];
  config: AppConfig;
  folders: Record<string, string>;
  tableList: TableEntry[];
  excluded?: string[];
}

interface FileStatus {
  name: string;
  exists: boolean;
  size: number;
}

interface GameInfo {
  gameDir: string;
  version: string;
  valid: boolean;
  files: FileStatus[];
  backupVersion: string;
  backupExists: boolean;
  region: string;
}

interface PatchRequest {
  gameDir: string;
  language: string;
  replaceText: boolean;
  replaceFont: boolean;
  rules: Record<string, boolean>;
  selectedTables: string[];
  preset: string;
}

interface ReplaceStats {
  Tables: number;
  Files: number;
  Entries: number;
  AppendedBytes: number;
}

interface FilePlan {
  enabled: boolean;
  action: string;
  files: string[] | null;
}

interface PlanSummary {
  region: string;
  gameVersion: string;
  cloudTag: string;
  cloudState: string;
  text: FilePlan;
  font: FilePlan;
  needFontConfirm: boolean;
  notes: string[] | null;
}

interface PatchResponse {
  plan: PlanSummary;
  text: ReplaceStats;
  font: ReplaceStats;
  cancelled: boolean;
  downloaded: string[] | null;
  warnings: string[] | null;
  notes: string[] | null;
}

interface RestoreResponse {
  restored: string[] | null;
}

interface PatchProgressEvent {
  stage: string;
  detail: string;
  done: number;
  total: number;
}

interface ClientStatusResult {
  status: "original" | "patched" | "modified" | "unknown";
  label: string;
  description: string;
}

interface GoApp {
  GetBootstrap(): Promise<BootstrapInfo>;
  SaveConfig(config: AppConfig): Promise<void>;
  DetectGamePaths(): Promise<string[]>;
  SelectGameDirectory(): Promise<string>;
  GetGameInfo(gameDir: string, language: string): Promise<GameInfo>;
  CheckClientStatus(gameDir: string, language: string): Promise<ClientStatusResult>;
  StartPatch(req: PatchRequest): Promise<PatchResponse>;
  AnswerFontConfirm(ok: boolean): Promise<void>;
  RestoreOriginal(gameDir: string, language: string): Promise<RestoreResponse>;
  OpenLink(url: string): Promise<void>;
}

declare global {
  interface Window {
    go?: { main?: { App?: Partial<GoApp> } };
    runtime?: {
      EventsOn?: (name: string, callback: (...data: unknown[]) => void) => void;
      WindowSetTitle?: (title: string) => void;
    };
  }
}

async function call<K extends keyof GoApp>(
  name: K,
  ...args: Parameters<GoApp[K]>
): Promise<Awaited<ReturnType<GoApp[K]>>> {
  const fn = window.go?.main?.App?.[name] as ((...a: unknown[]) => Promise<unknown>) | undefined;
  if (!fn) {
    throw new Error("后端服务未就绪，请稍后重试");
  }
  return (await fn(...args)) as Awaited<ReturnType<GoApp[K]>>;
}

function byId<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id);
  if (!node) {
    throw new Error(`页面缺少关键节点 #${id}`);
  }
  return node as T;
}

interface LogEntry {
  id: number;
  time: string;
  level: "info" | "ok" | "warn" | "error" | "patched";
  tag?: string;
  message: string;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function asProgress(data: unknown): PatchProgressEvent {
  if (isRecord(data)) {
    return {
      stage: typeof data.stage === "string" ? data.stage : "",
      detail: typeof data.detail === "string" ? data.detail : "",
      done: typeof data.done === "number" ? data.done : 0,
      total: typeof data.total === "number" ? data.total : 0,
    };
  }
  return { stage: "", detail: "", done: 0, total: 0 };
}


let bootstrap: BootstrapInfo | null = null;
let gameInfo: GameInfo | null = null;
let clientStatus: ClientStatusResult | null = null;
let checkingClient = false;
let modalResolver: ((ok: boolean) => void) | null = null;
let busy = false;

let logEntries: LogEntry[] = [];
let nextLogId = 1;
let currentLogFilter: "all" | "normal" | "error" = "all";
let errorLogCount = 0;

const selected = new Set<string>();
const expandedFolders = new Set<string>();
let filterOnlyUnselected = false;

let config: AppConfig = {
  gameDir: "",
  language: "GLOBAL",
  replaceText: true,
  replaceFont: true,
  rules: {},
  preset: "recommended",
};

const el = {
  viewProvisioning: byId("view-provisioning"),
  viewWorkbench: byId("view-workbench"),
  viewTables: byId("view-tables"),

  provisionDetail: byId("provision-detail"),
  provisionProgressBar: byId("provision-progress-bar"),
  provisionMeta: byId("provision-meta"),

  versionBadge: byId("version-badge"),

  gamePath: byId<HTMLInputElement>("game-path"),
  gameInfo: byId("game-info"),
  language: byId<HTMLSelectElement>("language"),
  btnBrowse: byId<HTMLButtonElement>("btn-browse"),
  btnDetect: byId<HTMLButtonElement>("btn-detect"),
  replaceText: byId<HTMLInputElement>("replace-text"),
  replaceFont: byId<HTMLInputElement>("replace-font"),
  rules: byId("rules"),

  btnOpenTables: byId<HTMLButtonElement>("btn-open-tables"),
  mainPreset: byId<HTMLSelectElement>("main-preset"),

  btnPatch: byId<HTMLButtonElement>("btn-patch"),
  btnPatchText: byId("btn-patch-text"),
  btnRestore: byId<HTMLButtonElement>("btn-restore"),

  progressBar: byId("progress-bar"),
  actionProgressWrap: byId("action-progress-wrap"),
  busyIndicator: byId("busy-indicator"),
  logPane: byId("log-pane"),
  filterLogAll: byId<HTMLButtonElement>("filter-log-all"),
  filterLogNormal: byId<HTMLButtonElement>("filter-log-normal"),
  filterLogError: byId<HTMLButtonElement>("filter-log-error"),
  logErrorCount: byId("log-error-count"),
  btnCopyLog: byId<HTMLButtonElement>("btn-copy-log"),
  btnCopyLogText: byId<HTMLElement>("btn-copy-log-text"),
  btnClearLog: byId<HTMLButtonElement>("btn-clear-log"),
  log: byId("log"),

  btnBackWorkbench: byId<HTMLButtonElement>("btn-back-workbench"),
  preset: byId<HTMLSelectElement>("preset"),
  search: byId<HTMLInputElement>("search"),
  btnFilterUnselected: byId<HTMLButtonElement>("btn-filter-unselected"),
  countUnselected: byId("count-unselected"),
  btnCollapseAll: byId<HTMLButtonElement>("btn-collapse-all"),
  btnExpandAll: byId<HTMLButtonElement>("btn-expand-all"),
  btnSelectAll: byId<HTMLButtonElement>("btn-select-all"),
  btnSelectNone: byId<HTMLButtonElement>("btn-select-none"),
  tableList: byId<HTMLTableSectionElement>("table-list"),

  modal: byId("modal"),
  modalTitle: byId("modal-title"),
  modalText: byId("modal-text"),
  modalOk: byId<HTMLButtonElement>("modal-ok"),
  modalCancel: byId<HTMLButtonElement>("modal-cancel"),
};

function switchView(view: "provisioning" | "workbench" | "tables"): void {
  el.viewProvisioning.classList.toggle("hidden", view !== "provisioning");
  el.viewWorkbench.classList.toggle("hidden", view !== "workbench");
  el.viewTables.classList.toggle("hidden", view !== "tables");
}

function parseLogKind(message: string, explicitKind?: "info" | "ok" | "warn" | "error" | "patched"): "info" | "ok" | "warn" | "error" | "patched" {
  if (explicitKind && explicitKind !== "info") {
    return explicitKind;
  }
  const lower = message.toLowerCase();
  if (message.includes("失败") || message.includes("错误") || lower.includes("error") || lower.includes("failed") || message.includes("超时")) {
    return "error";
  }
  if (message.includes("警告") || message.includes("注意") || message.includes("跳过")) {
    return "warn";
  }
  if (message.includes("成功") || message.includes("已是最新") || message.includes("已还原") || message.includes("已更新")) {
    return "ok";
  }
  return "info";
}

function parseLogTag(message: string): { tag: string; cleanMessage: string } {
  const match = /^\[([^\]]+)\]\s*(.*)$/.exec(message);
  if (match) {
    return { tag: match[1], cleanMessage: match[2] };
  }
  return { tag: "", cleanMessage: message };
}

function log(message: string, kind?: "info" | "ok" | "warn" | "error" | "patched"): void {
  const { tag, cleanMessage } = parseLogTag(message);
  const actualKind = parseLogKind(message, kind);
  const timeStr = new Date().toLocaleTimeString("zh-CN", { hour12: false });

  const entry: LogEntry = {
    id: nextLogId++,
    time: timeStr,
    level: actualKind,
    tag: tag || undefined,
    message: cleanMessage,
  };
  logEntries.push(entry);
  if (logEntries.length > 500) {
    logEntries.shift();
  }

  if (actualKind === "error" || actualKind === "warn") {
    errorLogCount++;
    el.logErrorCount.textContent = String(errorLogCount);
    el.logErrorCount.classList.remove("hidden");
  }

  const line = document.createElement("div");
  line.className = `log-line ${actualKind === "info" ? "" : actualKind}`.trim();
  line.dataset.level = actualKind;

  if (
    (currentLogFilter === "normal" && (actualKind === "error" || actualKind === "warn")) ||
    (currentLogFilter === "error" && actualKind !== "error" && actualKind !== "warn")
  ) {
    line.classList.add("filter-hidden");
  }

  const timeEl = document.createElement("span");
  timeEl.className = "time";
  timeEl.textContent = timeStr;
  line.append(timeEl);

  if (tag) {
    const tagEl = document.createElement("span");
    tagEl.className = "log-tag";
    if (tag.includes("更新")) tagEl.classList.add("tag-update");
    else if (tag.includes("资源")) tagEl.classList.add("tag-res");
    else if (tag.includes("错误") || tag.includes("失败")) tagEl.classList.add("tag-err");
    else if (tag.includes("提示") || tag.includes("警告")) tagEl.classList.add("tag-warn");
    else if (tag.includes("补丁") || tag.includes("完成")) tagEl.classList.add("tag-ok");
    tagEl.textContent = tag;
    line.append(tagEl);
  }

  const textEl = document.createElement("span");
  textEl.className = "log-text";
  textEl.textContent = cleanMessage;
  line.append(textEl);

  el.log.append(line);
  while (el.log.childElementCount > 500) {
    el.log.firstElementChild?.remove();
  }
  el.log.scrollTop = el.log.scrollHeight;
}

function setLogFilter(filter: "all" | "normal" | "error"): void {
  currentLogFilter = filter;
  el.filterLogAll.classList.toggle("active", filter === "all");
  el.filterLogNormal.classList.toggle("active", filter === "normal");
  el.filterLogError.classList.toggle("active", filter === "error");

  const lines = el.log.querySelectorAll<HTMLDivElement>(".log-line");
  for (const line of lines) {
    const level = line.dataset.level;
    const isErr = level === "error" || level === "warn";
    const hide = (filter === "normal" && isErr) || (filter === "error" && !isErr);
    line.classList.toggle("filter-hidden", hide);
  }
  el.log.scrollTop = el.log.scrollHeight;
}

async function copyLogs(): Promise<void> {
  if (logEntries.length === 0) return;
  const text = logEntries
    .map((e) => `[${e.time}] [${e.level.toUpperCase()}] ${e.tag ? `[${e.tag}] ` : ""}${e.message}`)
    .join("\n");
  try {
    await navigator.clipboard.writeText(text);
    const orig = el.btnCopyLogText ? el.btnCopyLogText.textContent : el.btnCopyLog.textContent;
    if (el.btnCopyLogText) {
      el.btnCopyLogText.textContent = "已复制";
    } else {
      el.btnCopyLog.textContent = "已复制";
    }
    setTimeout(() => {
      if (el.btnCopyLogText) {
        el.btnCopyLogText.textContent = orig;
      } else {
        el.btnCopyLog.textContent = orig;
      }
    }, 1500);
  } catch {
    // 忽略剪贴板写入异常
  }
}

function toggleConsole(_open?: boolean): void {
  el.log.scrollTop = el.log.scrollHeight;
}

function updateTextScopeState(): void {
  const isEnabled = el.replaceText.checked;
  el.mainPreset.disabled = !isEnabled || busy;
  el.btnOpenTables.disabled = !isEnabled || busy;
  const scopeControl = el.mainPreset.closest(".scope-control");
  scopeControl?.classList.toggle("disabled", !isEnabled);
}

function setBusy(value: boolean, label = ""): void {
  busy = value;
  el.busyIndicator.textContent = value ? label || "执行中…" : "准备就绪";
  el.actionProgressWrap.classList.toggle("hidden", !value);
  el.btnPatch.disabled = value;
  el.btnRestore.disabled = value;
  el.btnBrowse.disabled = value;
  el.btnDetect.disabled = value;
  el.preset.disabled = value;
  el.language.disabled = value;
  el.replaceText.disabled = value;
  el.replaceFont.disabled = value;
  updateTextScopeState();
}

function setWindowTitle(): void {
  const title = "FFXIV 汉化器";
  document.title = title;
  try {
    window.runtime?.WindowSetTitle?.(title);
  } catch {
    // 忽略非桌面环境下的调用失败
  }
}

function showConfirm(title: string, text: string, okText = "确定继续"): Promise<boolean> {
  return new Promise<boolean>((resolve) => {
    modalResolver = resolve;
    el.modalTitle.textContent = title;
    el.modalText.textContent = text;
    el.modalOk.textContent = okText;
    el.modal.classList.remove("hidden");
  });
}

function closeModal(ok: boolean): void {
  el.modal.classList.add("hidden");
  const resolver = modalResolver;
  modalResolver = null;
  resolver?.(ok);
}

el.modalOk.addEventListener("click", () => closeModal(true));
el.modalCancel.addEventListener("click", () => closeModal(false));

async function waitForBackend(timeoutMs: number): Promise<void> {
  const start = Date.now();
  while (!window.go?.main?.App?.GetBootstrap) {
    if (Date.now() - start > timeoutMs) {
      throw new Error("后端服务连接超时，请检查客户端启动状态");
    }
    await new Promise((resolve) => window.setTimeout(resolve, 100));
  }
}

async function persistConfig(): Promise<void> {
  try {
    await call("SaveConfig", config);
  } catch (error) {
    log(`保存本地配置失败: ${describeError(error)}`, "error");
  }
}

function describeError(error: unknown): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (typeof error === "string") {
    return error;
  }
  return String(error);
}

async function bootstrapApp(): Promise<void> {
  try {
    await waitForBackend(8000);
    wireRuntimeEvents();
    wireEvents();

    await loadBootstrap();
    switchView("workbench");

    if (config.gameDir) {
      await refreshGameInfo();
      void checkClientEnvironment();
    } else {
      renderGameInfo();
      void call("DetectGamePaths").then(async (detected) => {
        if (!config.gameDir && detected.length > 0 && detected[0]) {
          setGameDir(detected[0]);
          log(`自动探测并定位到游戏目录：${detected[0]}`);
          await refreshGameInfo();
          void checkClientEnvironment();
        }
      });
    }
  } catch (error) {
    log(`初始化失败: ${describeError(error)}`, "error");
    switchView("workbench");
  }
}

async function loadBootstrap(): Promise<void> {
  const info = await call("GetBootstrap");
  bootstrap = info;
  config = { ...info.config };
  if (!config.rules) {
    config.rules = {};
  }
  setWindowTitle();
  if (el.versionBadge) {
    el.versionBadge.textContent = `v${info.version}`;
  }
  el.gamePath.value = config.gameDir;
  el.replaceText.checked = config.replaceText;
  el.replaceFont.checked = config.replaceFont;
  updateTextScopeState();

  renderRules();
  renderLanguages();

  const activePreset = config.preset && config.preset !== "custom" ? config.preset : "recommended";
  applyPreset(activePreset, false);

  renderTables();
}

function renderRules(): void {
  if (!bootstrap) {
    return;
  }
  el.rules.replaceChildren();
  for (const rule of bootstrap.rules) {
    const label = document.createElement("label");
    label.className = "form-row rule-row";

    const wrap = document.createElement("span");
    wrap.className = "rule-text";

    const title = document.createElement("span");
    title.className = "rule-name";
    title.textContent = rule.name;

    const desc = document.createElement("span");
    desc.className = "rule-desc";
    desc.textContent = rule.description;

    const sw = document.createElement("span");
    sw.className = "switch";

    const input = document.createElement("input");
    input.type = "checkbox";
    input.checked = config.rules[rule.id] ?? rule.defaultOn;
    input.addEventListener("change", () => {
      config.rules[rule.id] = input.checked;
      void persistConfig();
    });

    const track = document.createElement("span");
    track.className = "switch-track";

    sw.append(input, track);
    wrap.append(title, desc);
    label.append(wrap, sw);
    el.rules.append(label);
  }
}

function renderLanguages(): void {
  if (!bootstrap) {
    return;
  }
  el.language.replaceChildren();
  for (const lang of bootstrap.languages) {
    const option = document.createElement("option");
    option.value = lang.code;
    option.textContent = lang.name;
    const isMatched =
      lang.code.toUpperCase() === config.language.toUpperCase() ||
      (lang.code === "GLOBAL" && ["JA", "EN", "DE", "FR", "GLOBAL"].includes(config.language.toUpperCase()));
    option.selected = isMatched;
    el.language.append(option);
  }
}

function setGameDir(dir: string): void {
  config.gameDir = dir;
  el.gamePath.value = dir;
  clientStatus = null;
  void persistConfig();
}

function createDiagItem(label: string, value: string, cls = ""): HTMLDivElement {
  const item = document.createElement("div");
  item.className = "diag-item";
  const k = document.createElement("span");
  k.className = "diag-k";
  k.textContent = label;
  const v = document.createElement("span");
  v.className = `diag-v ${cls}`.trim();
  v.textContent = value;
  v.title = value;
  item.append(k, v);
  return item;
}

function createClientStatusItem(): HTMLDivElement {
  const item = document.createElement("div");
  item.className = "diag-item diag-item-actionable";
  const k = document.createElement("span");
  k.className = "diag-k";
  k.textContent = "客户端环境";

  const row = document.createElement("div");
  row.className = "diag-v-row";

  const v = document.createElement("span");

  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "btn btn-quiet btn-xs diag-check-btn";

  if (checkingClient) {
    v.className = "diag-v";
    v.textContent = "校验哈希中…";
    btn.disabled = true;
    btn.textContent = "校验中";
  } else if (clientStatus) {
    let cls = "";
    if (clientStatus.status === "original") {
      cls = "ok";
    } else if (clientStatus.status === "patched") {
      cls = "patched";
    } else if (clientStatus.status === "modified") {
      cls = "warn";
    } else if (clientStatus.label === "文件损坏" || clientStatus.label === "文件缺失") {
      cls = "err";
    } else if (clientStatus.label === "未收录版本") {
      cls = "warn";
    }
    v.className = `diag-v ${cls}`.trim();
    v.textContent = clientStatus.label;
    v.title = clientStatus.description;
    btn.textContent = "重测";
    btn.title = "重新计算哈希并比对原版";
  } else {
    v.className = "diag-v";
    v.textContent = "未检测";
    btn.textContent = "检测";
    btn.title = "点击手动计算客户端关键数据包哈希并比对官方原版";
  }

  btn.addEventListener("click", (e) => {
    e.stopPropagation();
    void checkClientEnvironment();
  });

  row.append(v, btn);
  item.append(k, row);
  return item;
}

async function checkClientEnvironment(): Promise<void> {
  if (checkingClient || !config.gameDir) {
    return;
  }
  checkingClient = true;
  renderGameInfo();
  try {
    clientStatus = await call("CheckClientStatus", config.gameDir, config.language);
    let logKind: "ok" | "warn" | "error" | "patched" | "info" = "info";
    if (clientStatus.status === "original") {
      logKind = "ok";
    } else if (clientStatus.status === "patched") {
      logKind = "patched";
    } else if (clientStatus.status === "modified" || clientStatus.label === "未收录版本") {
      logKind = "warn";
    } else if (clientStatus.label === "文件损坏" || clientStatus.label === "文件缺失") {
      logKind = "error";
    }
    log(`客户端状态: ${clientStatus.label}`, logKind);
  } catch (err) {
    log(`客户端校验失败: ${describeError(err)}`, "error");
  } finally {
    checkingClient = false;
    renderGameInfo();
  }
}

function renderGameInfo(): void {
  el.gameInfo.replaceChildren();
  if (!config.gameDir) {
    el.gameInfo.append(createDiagItem("状态", "未选择游戏目录", "warn"));
    return;
  }
  if (!gameInfo) {
    el.gameInfo.append(createDiagItem("状态", "正在检测…"));
    return;
  }

  const backupText = gameInfo.backupExists ? "已就绪" : "未备份";
  const backupItem = createDiagItem("本地备份", backupText, gameInfo.backupExists ? "ok" : "");
  if (gameInfo.backupExists && gameInfo.backupVersion) {
    backupItem.title = `官方原版备份版本: v${gameInfo.backupVersion}`;
  }

  el.gameInfo.append(
    createDiagItem("客户端版本", gameInfo.version || "未知", ""),
    createClientStatusItem(),
    backupItem
  );
}

async function refreshGameInfo(): Promise<void> {
  if (!config.gameDir) {
    gameInfo = null;
    renderGameInfo();
    return;
  }
  try {
    gameInfo = await call("GetGameInfo", config.gameDir, config.language);
  } catch (error) {
    gameInfo = null;
    log(`读取游戏目录信息失败: ${describeError(error)}`, "error");
  }
  renderGameInfo();
}

function setSelected(key: string, value: boolean): void {
  if (value) {
    selected.add(key);
  } else {
    selected.delete(key);
  }
  updateScopeCount();
}

function applyPreset(name: string, rerender = true): void {
  if (!bootstrap) {
    return;
  }
  selected.clear();
  const all = bootstrap.tableList;
  if (name === "all") {
    for (const entry of all) {
      selected.add(entry.key);
    }
  } else {
    const excludedList = bootstrap.excluded || ["BNpcName"];
    for (const entry of all) {
      if (!excludedList.includes(entry.key)) {
        selected.add(entry.key);
      }
    }
    name = "recommended";
  }
  config.preset = name;
  el.preset.value = name;
  el.mainPreset.value = name;
  updateScopeCount();
  if (rerender) {
    renderTables();
  }
}

function updateScopeCount(): void {
  const total = bootstrap?.tableList.length ?? 0;
  const selCount = selected.size;
  const unselCount = total - selCount;

  el.countUnselected.textContent = String(unselCount);
  el.btnFilterUnselected.classList.toggle("has-alert", unselCount > 0);
}

function createTableDataRow(entry: TableEntry, isChild = false, folderChs = ""): HTMLTableRowElement {
  const isEntrySel = selected.has(entry.key);
  const tr = document.createElement("tr");
  tr.className = `table-data-row ${isEntrySel ? "selected" : "unselected"} ${isChild ? "is-child-row" : ""}`.trim();

  const tdCheck = document.createElement("td");
  tdCheck.className = "col-sel";
  const checkbox = document.createElement("input");
  checkbox.type = "checkbox";
  checkbox.checked = isEntrySel;
  checkbox.addEventListener("change", (e) => {
    e.stopPropagation();
    setSelected(entry.key, checkbox.checked);
    tr.classList.toggle("selected", checkbox.checked);
    tr.classList.toggle("unselected", !checkbox.checked);
    markCustomPreset();
    updateScopeCount();
  });
  tdCheck.append(checkbox);

  const tdKey = document.createElement("td");
  tdKey.className = `col-filename ${isChild ? "sub-filename" : ""}`.trim();
  tdKey.textContent = entry.key;
  tdKey.title = entry.key;

  const tdName = document.createElement("td");
  tdName.className = "col-chs";
  const hideRepeatName = isChild && (entry.name === folderChs || !entry.name || entry.name === "-");
  const displayName = hideRepeatName ? "" : (entry.name || "-");
  tdName.textContent = displayName;
  if (displayName && displayName !== "-") {
    tdName.title = displayName;
  }

  const tdStatus = document.createElement("td");
  tdStatus.className = "col-status";
  if (entry.warning) {
    const tag = document.createElement("span");
    tag.className = "status-tag-warn";
    tag.textContent = entry.warning;
    tag.title = entry.warning;
    tdStatus.append(tag);
  }

  tr.addEventListener("click", () => {
    const next = !selected.has(entry.key);
    checkbox.checked = next;
    setSelected(entry.key, next);
    tr.classList.toggle("selected", next);
    tr.classList.toggle("unselected", !next);
    markCustomPreset();
    updateScopeCount();
  });

  tr.append(tdCheck, tdKey, tdName, tdStatus);
  return tr;
}

function renderTables(): void {
  if (!bootstrap) {
    return;
  }
  const term = el.search.value.trim().toLowerCase();
  el.tableList.replaceChildren();

  const folderGroups = new Map<string, TableEntry[]>();
  const rootEntries: TableEntry[] = [];

  for (const entry of bootstrap.tableList) {
    const f = entry.folder ? entry.folder.trim() : "";
    if (!f || f === "root") {
      rootEntries.push(entry);
    } else {
      if (!folderGroups.has(f)) {
        folderGroups.set(f, []);
      }
      folderGroups.get(f)!.push(entry);
    }
  }

  const folderKeys = Array.from(folderGroups.keys()).sort((a, b) => a.localeCompare(b));
  rootEntries.sort((a, b) => a.key.localeCompare(b.key));

  const fragment = document.createDocumentFragment();

  // 1. 分类文件夹
  for (const fKey of folderKeys) {
    const allEntriesInFolder = folderGroups.get(fKey)!;

    const filteredEntries = allEntriesInFolder.filter((entry) => {
      const isSel = selected.has(entry.key);
      if (filterOnlyUnselected && isSel) {
        return false;
      }
      if (term) {
        const matchKey = entry.key.toLowerCase().includes(term);
        const matchName = entry.name.toLowerCase().includes(term);
        const matchFolder = entry.folder.toLowerCase().includes(term);
        if (!matchKey && !matchName && !matchFolder) {
          return false;
        }
      }
      return true;
    });

    if (filteredEntries.length === 0) {
      continue;
    }

    const totalInFolder = allEntriesInFolder.length;
    const selInFolder = allEntriesInFolder.filter((e) => selected.has(e.key)).length;
    const isAllSel = selInFolder === totalInFolder && totalInFolder > 0;
    const isPartSel = selInFolder > 0 && selInFolder < totalInFolder;

    const shouldAutoExpand = term.length > 0 || filterOnlyUnselected;
    const isExpanded = shouldAutoExpand || expandedFolders.has(fKey);

    const trGroup = document.createElement("tr");
    trGroup.className = `folder-header-row ${isExpanded ? "expanded" : ""}`;

    const tdSel = document.createElement("td");
    tdSel.className = "col-sel folder-sel-cell";
    const selWrap = document.createElement("div");
    selWrap.className = "folder-sel-wrap";

    const caret = document.createElement("span");
    caret.className = `folder-caret ${isExpanded ? "expanded" : ""}`;
    caret.innerHTML =
      '<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="9 18 15 12 9 6"/></svg>';

    const folderCheckbox = document.createElement("input");
    folderCheckbox.type = "checkbox";
    folderCheckbox.className = "folder-checkbox";
    folderCheckbox.checked = isAllSel;
    folderCheckbox.indeterminate = isPartSel;
    folderCheckbox.title = "全选/反选此类目下的所有表";
    folderCheckbox.addEventListener("click", (e) => {
      e.stopPropagation();
      const targetState = !isAllSel;
      for (const item of allEntriesInFolder) {
        setSelected(item.key, targetState);
      }
      markCustomPreset();
      renderTables();
    });

    selWrap.append(caret, folderCheckbox);
    tdSel.append(selWrap);

    const tdKey = document.createElement("td");
    tdKey.className = "col-filename folder-name-cell";
    tdKey.textContent = fKey;
    tdKey.title = fKey;

    const folderChs = bootstrap?.folders ? bootstrap.folders[fKey] || "" : "";
    const tdName = document.createElement("td");
    tdName.className = "col-chs folder-chs-cell";
    tdName.textContent = folderChs || "-";
    if (folderChs) {
      tdName.title = folderChs;
    }

    const tdStatus = document.createElement("td");
    tdStatus.className = "col-status folder-status-cell";

    trGroup.append(tdSel, tdKey, tdName, tdStatus);

    trGroup.addEventListener("click", () => {
      if (isExpanded) {
        expandedFolders.delete(fKey);
      } else {
        expandedFolders.add(fKey);
      }
      renderTables();
    });

    fragment.append(trGroup);

    if (isExpanded) {
      for (const entry of filteredEntries) {
        fragment.append(createTableDataRow(entry, true, folderChs));
      }
    }
  }

  // 2. 独立数据表
  const filteredRootEntries = rootEntries.filter((entry) => {
    const isSel = selected.has(entry.key);
    if (filterOnlyUnselected && isSel) {
      return false;
    }
    if (term) {
      const matchKey = entry.key.toLowerCase().includes(term);
      const matchName = entry.name.toLowerCase().includes(term);
      if (!matchKey && !matchName) {
        return false;
      }
    }
    return true;
  });

  for (const entry of filteredRootEntries) {
    fragment.append(createTableDataRow(entry));
  }

  el.tableList.append(fragment);
  updateScopeCount();
}

function markCustomPreset(): void {
  if (config.preset !== "custom") {
    config.preset = "custom";
    el.preset.value = "custom";
    el.mainPreset.value = "custom";
    void persistConfig();
  }
}

function wireEvents(): void {
  el.btnBrowse.addEventListener("click", async () => {
    if (busy) {
      return;
    }
    try {
      const dir = await call("SelectGameDirectory");
      if (dir) {
        const prevDir = config.gameDir;
        setGameDir(dir);
        log(`已选择游戏目录：${dir}`);
        await refreshGameInfo();
        if (dir !== prevDir) {
          void checkClientEnvironment();
        }
      }
    } catch (error) {
      log(`选择目录失败: ${describeError(error)}`, "error");
    }
  });

  el.btnDetect.addEventListener("click", async () => {
    if (busy) {
      return;
    }
    try {
      setBusy(true, "正在搜索本地客户端…");
      const list = await call("DetectGamePaths");
      if (list.length === 0) {
        log("未在常用路径中找到 FFXIV 客户端，请点击“浏览”手动指定。", "info");
        return;
      }
      const prevDir = config.gameDir;
      const detectedDir = list[0]!;
      setGameDir(detectedDir);
      log(`自动探测到客户端：${detectedDir}`, "ok");
      await refreshGameInfo();
      if (detectedDir !== prevDir) {
        void checkClientEnvironment();
      }
    } catch (error) {
      log(`探测游戏目录失败: ${describeError(error)}`, "error");
    } finally {
      setBusy(false);
    }
  });

  el.language.addEventListener("change", async () => {
    if (busy) {
      return;
    }
    config.language = el.language.value;
    void persistConfig();
    await refreshGameInfo();
    void checkClientEnvironment();
  });

  el.replaceText.addEventListener("change", () => {
    config.replaceText = el.replaceText.checked;
    updateTextScopeState();
    void persistConfig();
  });

  el.replaceFont.addEventListener("change", () => {
    config.replaceFont = el.replaceFont.checked;
    void persistConfig();
  });

  el.preset.addEventListener("change", () => {
    if (busy) {
      return;
    }
    applyPreset(el.preset.value, true);
    void persistConfig();
  });

  el.mainPreset.addEventListener("change", () => {
    if (busy) {
      return;
    }
    applyPreset(el.mainPreset.value, true);
    void persistConfig();
  });

  el.btnOpenTables.addEventListener("click", () => {
    switchView("tables");
  });

  el.btnBackWorkbench.addEventListener("click", () => {
    switchView("workbench");
  });

  el.search.addEventListener("input", () => {
    renderTables();
  });

  el.btnFilterUnselected.addEventListener("click", () => {
    filterOnlyUnselected = !filterOnlyUnselected;
    el.btnFilterUnselected.classList.toggle("active", filterOnlyUnselected);
    renderTables();
  });

  el.btnCollapseAll.addEventListener("click", () => {
    expandedFolders.clear();
    renderTables();
  });

  el.btnExpandAll.addEventListener("click", () => {
    if (!bootstrap) {
      return;
    }
    for (const entry of bootstrap.tableList) {
      if (entry.folder && entry.folder !== "root") {
        expandedFolders.add(entry.folder);
      }
    }
    renderTables();
  });

  el.btnSelectAll.addEventListener("click", () => {
    if (busy) {
      return;
    }
    applyPreset("all", true);
    markCustomPreset();
  });

  el.btnSelectNone.addEventListener("click", () => {
    if (busy) {
      return;
    }
    selected.clear();
    updateScopeCount();
    renderTables();
    markCustomPreset();
  });

  el.btnClearLog.addEventListener("click", () => {
    el.log.replaceChildren();
    logEntries = [];
    errorLogCount = 0;
    el.logErrorCount.textContent = "0";
    el.logErrorCount.classList.add("hidden");
  });

  el.btnCopyLog.addEventListener("click", () => {
    void copyLogs();
  });

  el.filterLogAll.addEventListener("click", () => setLogFilter("all"));
  el.filterLogNormal.addEventListener("click", () => setLogFilter("normal"));
  el.filterLogError.addEventListener("click", () => setLogFilter("error"));

  el.btnRestore.addEventListener("click", async () => {
    if (busy) {
      return;
    }
    if (!config.gameDir) {
      log("请先指定游戏目录。", "error");
      return;
    }
    const ok = await showConfirm(
      "还原官方原版文件",
      `确定将当前区服的本地原版备份文件恢复到游戏客户端目录吗？已有汉化修改将被撤销。`,
      "确认还原",
    );
    if (!ok) {
      return;
    }
    try {
      toggleConsole(true);
      setBusy(true, "正在还原官方原版文件…");
      const resp = await call("RestoreOriginal", config.gameDir, config.language);
      log(`已还原官方原版 (${(resp.restored ?? []).length} 个文件)`, "ok");
      try {
        clientStatus = await call("CheckClientStatus", config.gameDir, config.language);
      } catch {
        // 忽略静默更新失败
      }
      await refreshGameInfo();
    } catch (error) {
      log(`还原失败: ${describeError(error)}`, "error");
    } finally {
      setBusy(false);
    }
  });

  el.btnPatch.addEventListener("click", async () => {
    if (busy) {
      return;
    }
    if (!config.gameDir) {
      log("请先指定游戏目录。", "error");
      return;
    }
    if (!config.replaceText && !config.replaceFont) {
      log("请至少勾选“替换文本”或“替换游戏字体”。", "error");
      return;
    }
    if (config.replaceText && selected.size === 0) {
      log("已勾选“替换文本”，但汉化范围中未选择任何数据表。", "error");
      return;
    }

    try {
      toggleConsole(true);
      setBusy(true, "正在执行安全校验与汉化流程…");
      el.progressBar.style.width = "0%";
      log("开始汉化…");

      const req: PatchRequest = {
        gameDir: config.gameDir,
        language: config.language,
        replaceText: config.replaceText,
        replaceFont: config.replaceFont,
        rules: { ...config.rules },
        selectedTables: Array.from(selected),
        preset: config.preset,
      };

      const resp = await call("StartPatch", req);
      if (resp.cancelled) {
        log("汉化已取消", "info");
        return;
      }
      for (const w of resp.warnings ?? []) {
        log(`[警告] ${w}`, "warn");
      }
      if (resp.text.Files > 0) {
        log(`文本替换完成 (${resp.text.Tables} 个表, ${(resp.text.AppendedBytes / 1024 / 1024).toFixed(2)} MB)`, "ok");
      }
      if (resp.font.Files > 0) {
        log("字体贴图替换完成", "ok");
      }
      log("汉化完成", "ok");
      el.progressBar.style.width = "100%";
      try {
        clientStatus = await call("CheckClientStatus", config.gameDir, config.language);
      } catch {
        // 忽略静默更新失败
      }
      await refreshGameInfo();
    } catch (error) {
      log(`汉化失败: ${describeError(error)}`, "error");
    } finally {
      setBusy(false);
    }
  });
}

function wireRuntimeEvents(): void {
  const on = window.runtime?.EventsOn;
  if (!on) {
    return;
  }

  let pendingResourceProgress: PatchProgressEvent | null = null;
  let resourceRafId = 0;
  on("resource-progress", (data: unknown) => {
    const ev = asProgress(data);
    pendingResourceProgress = ev;
    if (!resourceRafId) {
      resourceRafId = requestAnimationFrame(() => {
        resourceRafId = 0;
        if (!pendingResourceProgress) return;
        const current = pendingResourceProgress;
        switchView("provisioning");
        el.provisionDetail.textContent = current.detail || current.stage || "正在准备汉化资源包…";
        if (current.total > 0) {
          const pct = Math.min(100, Math.max(0, (current.done / current.total) * 100));
          el.provisionProgressBar.style.width = `${pct.toFixed(1)}%`;
          const doneMb = (current.done / 1024 / 1024).toFixed(1);
          const totalMb = (current.total / 1024 / 1024).toFixed(1);
          el.provisionMeta.textContent = `${doneMb} MB / ${totalMb} MB (${pct.toFixed(0)}%)`;
        } else {
          el.provisionProgressBar.style.width = "100%";
          el.provisionMeta.textContent = "正在处理解压与哈希校验…";
        }
      });
    }
  });

  let lastDownloadDetail = "";
  let pendingPatchProgress: PatchProgressEvent | null = null;
  let patchRafId = 0;

  function renderPatchProgress(ev: PatchProgressEvent): void {
    if (ev.stage === "download") {
      if (ev.total > 0) {
        const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
        el.progressBar.style.width = `${pct.toFixed(1)}%`;
        const doneMb = (ev.done / 1024 / 1024).toFixed(1);
        const totalMb = (ev.total / 1024 / 1024).toFixed(1);
        el.busyIndicator.textContent = `下载中: ${ev.detail} (${doneMb} / ${totalMb} MB)`;
      } else {
        el.busyIndicator.textContent = `下载中: ${ev.detail}`;
      }
      return;
    }

    if (ev.total > 0) {
      const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
      el.progressBar.style.width = `${pct.toFixed(1)}%`;
      el.busyIndicator.textContent = `${ev.detail} (${ev.done}/${ev.total})`;
    } else {
      el.busyIndicator.textContent = ev.detail || ev.stage;
    }
  }

  on("patch-progress", (data: unknown) => {
    const ev = asProgress(data);
    pendingPatchProgress = ev;
    if (!patchRafId) {
      patchRafId = requestAnimationFrame(() => {
        patchRafId = 0;
        if (pendingPatchProgress) {
          renderPatchProgress(pendingPatchProgress);
        }
      });
    }

    if (ev.stage === "download") {
      if (ev.detail && ev.detail !== lastDownloadDetail) {
        lastDownloadDetail = ev.detail;
        const sizeInfo = ev.total > 0 ? ` (${(ev.total / 1024 / 1024).toFixed(1)} MB)` : "";
        log(`[下载] 开始下载 ${ev.detail}${sizeInfo}`);
      }
      return;
    }
  });

  on("patch-log", (msg: unknown) => {
    if (typeof msg === "string") {
      log(msg);
    }
  });

  on("font-confirm", async (prompt: unknown) => {
    const text = typeof prompt === "string" ? prompt : "当前游戏为官方原版字体，未选择替换字体将无法显示中文。是否确认继续？";
    const ok = await showConfirm("字体替换提示", text, "继续汉化（不替换字体）");
    try {
      await call("AnswerFontConfirm", ok);
    } catch (error) {
      log(`确认反馈失败: ${describeError(error)}`, "error");
    }
  });
}

void bootstrapApp();
