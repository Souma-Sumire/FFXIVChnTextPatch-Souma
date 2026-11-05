import "./style.css";

interface RuleMeta {
  id: string;
  name: string;
  description: string;
  category: string;
  defaultOn: boolean;
  status: string;
  statusDetail: string;
}

interface LanguageOption {
  code: string;
  name: string;
  region: string;
  backupUrl?: string;
}

interface RegionLanguage {
  code: string;
  name: string;
}

interface CSVDirLabel {
  title: string;
  desc: string;
}

interface CSVConfig {
  dirs: string[];
  selected: string;
  labels?: Record<string, CSVDirLabel>;
}

interface AppConfig {
  gameDirs: Record<string, string>;
  language: string;
  targetLanguages: Record<string, string[]>;
  replaceText: boolean;
  replaceFont: boolean;
  rules: Record<string, boolean>;
  preset: string;
  csvDirs?: Record<string, CSVConfig>;
}

interface TableEntry {
  key: string;
  name: string;
  folder: string;
  warning: string;
}

interface CSVDirOption {
  path: string;
  valid: boolean;
  title?: string;
  desc?: string;
  updatedAt?: string;
  canDeleteFiles?: boolean;
}

// CSVContext 由后端按区服给出：可选目录、当前选中的目录以及对应的表清单。
interface CSVContext {
  dirs: CSVDirOption[];
  selected: string;
  custom: boolean;
  defaultDir: string;
  tableList: TableEntry[];
  resourceReady: boolean;
  resourceUpdatedAt?: string;
  resourceHash?: string;
}

interface BootstrapInfo {
  version: string;
  resourceDir: string;
  resourceReady: boolean;
  rules: RuleMeta[];
  languages: LanguageOption[];
  regionLanguages?: Record<string, RegionLanguage[]>;
  config: AppConfig;
  folders: Record<string, string>;
  csv: CSVContext;
  excluded?: string[];
  resourceTime?: string;
  resourceHash?: string;
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
  detectedRegion?: string;
  regionMismatch?: boolean;
  resourceTime?: string;
  resourceHash?: string;
  uiAssetType?: number;
}

interface PatchRequest {
  gameDir: string;
  language: string;
  targetLanguages: string[];
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
  cancelled?: boolean;
}

interface PatchProgressEvent {
  stage: string;
  detail: string;
  done: number;
  total: number;
}

interface ClientStatusResult {
  status: "original" | "patched" | "modified" | "mismatch" | "unsupported" | "unknown";
  label: string;
  description: string;
}

interface AppUpdateInfo {
  hasUpdate: boolean;
  currentVersion: string;
  latestVersion: string;
  releaseNotes: string;
  releaseUrl: string;
  downloadUrl: string;
  downloadSha256?: string;
  mirrorUrl?: string;
  selfUpdateSupported: boolean;
}

interface GoApp {
  GetAppVersion(): Promise<string>;
  CheckAppUpdate(): Promise<AppUpdateInfo>;
  InstallAppUpdate(): Promise<void>;
  RestartApp(): Promise<void>;
  GetBootstrap(): Promise<BootstrapInfo>;
  SaveConfig(config: AppConfig): Promise<void>;
  SyncBuiltinResource(): Promise<void>;
  EnsureTableDoc(): Promise<boolean>;
  GetCSVContext(language: string): Promise<CSVContext>;
  SetCSVSelection(language: string, path: string): Promise<CSVContext>;
  RemoveCSVDirectory(language: string, path: string, deleteFiles: boolean): Promise<CSVContext>;
  SetCSVDirectoryMeta(language: string, path: string, title: string, desc: string): Promise<CSVContext>;
  OpenCSVDirectory(language: string, path: string): Promise<void>;
  DetectGamePaths(language: string): Promise<string[]>;
  CancelDetect(): Promise<void>;
  SelectGameDirectory(language: string): Promise<string>;
  GetGameInfo(gameDir: string, language: string): Promise<GameInfo>;
  CheckClientStatus(gameDir: string, language: string): Promise<ClientStatusResult>;
  StartPatch(req: PatchRequest): Promise<PatchResponse>;
  CancelPatch(): Promise<void>;
  AnswerUnknownConfirm(ok: boolean): Promise<void>;
  AnswerFontConfirm(ok: boolean): Promise<void>;
  AnswerLauncherConfirm(ok: boolean): Promise<void>;
  CopyBuiltinCSVDirectory(language: string, convert: boolean): Promise<CSVContext>;
  KillLauncherProcesses(gameDir: string): Promise<string[]>;
  RestoreOriginal(gameDir: string, language: string): Promise<RestoreResponse>;
  EnsureHighResolutionUiAsset(): Promise<{
    configPath: string;
    backupPath: string;
    modified: boolean;
    previous: number;
    current: number;
  }>;
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
type ModalResult = "ok" | "alt" | "cancel";
let modalResolver: ((result: ModalResult) => void) | null = null;

let busy = false;
let isPatching = false;
let isStopping = false;
let syncingResource = false;
let tableDocReady = false;
let tableDocPending: Promise<void> = Promise.resolve();
let isDetecting = false;
let detectCanceled = false;
let latestUpdateInfo: AppUpdateInfo | null = null;
let checkingAppUpdate = false;
let invalidCSVNotified = "";
let csvBusyText = "";
// csvEditingPath 非空时，对应目录行在下拉里直接显示为可编辑输入框。
let csvEditingPath: string | null = null;
// 删除确认气泡挂在 body 上，避免被下拉菜单的滚动容器裁掉。
let csvDeletePopover: { element: HTMLElement; anchor: HTMLElement; dispose: () => void } | null = null;
// builtinSyncFailed 表示最近一次内置词条同步失败，仅用于状态提示；
// 内置词条开始汉化时后端会再试一次，不通过就中止。
let builtinSyncFailed = false;

let logEntries: LogEntry[] = [];
let nextLogId = 1;

const selected = new Set<string>();
const expandedFolders = new Set<string>();
let filterOnlyUnselected = false;
let targetLangs: string[] = [];

let config: AppConfig = {
  gameDirs: {},
  language: "GLOBAL",
  targetLanguages: {},
  replaceText: true,
  replaceFont: true,
  rules: {},
  preset: "recommended",
  csvDirs: {},
};

const el = {
  viewWorkbench: byId("view-workbench"),
  viewTables: byId("view-tables"),

  versionBadge: byId<HTMLButtonElement>("version-badge"),
  btnAppUpdate: byId<HTMLButtonElement>("btn-app-update"),

  gamePath: byId<HTMLInputElement>("game-path"),
  gameInfo: byId("game-info"),
  language: byId<HTMLSelectElement>("language"),
  targetLangTrigger: byId<HTMLButtonElement>("target-lang-trigger"),
  targetLangSummary: byId("target-lang-summary"),
  targetLangMenu: byId("target-lang-menu"),
  btnBrowse: byId<HTMLButtonElement>("btn-browse"),
  btnDetect: byId<HTMLButtonElement>("btn-detect"),
  replaceText: byId<HTMLInputElement>("replace-text"),
  replaceFont: byId<HTMLInputElement>("replace-font"),
  fontUiAssetMeta: byId("font-ui-asset-meta"),
  fontUiAssetStatus: byId("font-ui-asset-status"),
  csvPicker: byId("csv-picker"),
  csvTrigger: byId<HTMLButtonElement>("csv-select-trigger"),
  csvSummary: byId("csv-select-summary"),
  csvSyncBar: byId("csv-sync-bar"),
  csvMenu: byId("csv-select-menu"),
  extendedSection: byId("extended-section"),
  rules: byId("rules"),

  btnOpenTables: byId<HTMLButtonElement>("btn-open-tables"),
  scopeSummary: byId("scope-summary"),

  btnPatch: byId<HTMLButtonElement>("btn-patch"),
  btnPatchText: byId("btn-patch-text"),
  btnRestore: byId<HTMLButtonElement>("btn-restore"),

  progressBar: byId("progress-bar"),
  actionProgressWrap: byId("action-progress-wrap"),
  actionProgressPercent: byId("action-progress-percent"),
  busyIndicator: byId("busy-indicator"),
  workbenchBody: document.querySelector<HTMLElement>(".workbench-body")!,
  logPane: byId("log-pane"),
  logResizer: byId("log-resizer"),
  btnToggleLog: byId<HTMLButtonElement>("btn-toggle-log"),
  btnToggleLogText: byId<HTMLElement>("btn-toggle-log-text"),
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
  modalAlt: byId<HTMLButtonElement>("modal-alt"),
};

function switchView(view: "workbench" | "tables"): void {
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
  if (message.includes("成功") || message.includes("已是最新") || message.includes("已还原") || message.includes("已更新") || message.includes("[完成]")) {
    return "ok";
  }
  return "info";
}


function log(message: string, kind?: "info" | "ok" | "warn" | "error" | "patched"): void {
  const actualKind = parseLogKind(message, kind);
  const timeStr = new Date().toLocaleTimeString("zh-CN", { hour12: false });

  const entry: LogEntry = {
    id: nextLogId++,
    time: timeStr,
    level: actualKind,
    tag: "",
    message: message,
  };
  logEntries.push(entry);
  if (logEntries.length > 500) {
    logEntries.shift();
  }

  const line = document.createElement("div");
  line.className = `log-line ${actualKind === "info" ? "" : actualKind}`.trim();
  line.dataset.level = actualKind;

  const timeEl = document.createElement("span");
  timeEl.className = "time";
  timeEl.textContent = timeStr;
  line.append(timeEl);

  const textEl = document.createElement("span");
  textEl.className = "log-text";
  textEl.textContent = message;
  line.append(textEl);

  el.log.append(line);
  while (el.log.childElementCount > 500) {
    el.log.firstElementChild?.remove();
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

const STORAGE_KEY_LOG_HEIGHT = "ffxiv_patcher_log_height";
const DEFAULT_LOG_HEIGHT = 130;
const MIN_LOG_HEIGHT = 70;
const COLLAPSE_THRESHOLD = 50;

let currentLogHeight = DEFAULT_LOG_HEIGHT;

function getMaxLogHeight(): number {
  const bodyHeight = el.workbenchBody?.clientHeight || 0;
  return bodyHeight > 220 ? Math.floor(bodyHeight - 120) : 600;
}

function applyLogHeight(height: number, save = false): void {
  const max = getMaxLogHeight();
  const clamped = Math.min(Math.max(height, MIN_LOG_HEIGHT), max);
  currentLogHeight = clamped;
  el.logPane.style.height = `${clamped}px`;
  el.logPane.style.flexBasis = `${clamped}px`;
  if (save) {
    try {
      localStorage.setItem(STORAGE_KEY_LOG_HEIGHT, String(clamped));
    } catch {
      // 存储写入兼容防御
    }
  }
}

function initLogResizer(): void {
  const resizer = el.logResizer;
  if (!resizer) {
    return;
  }

  try {
    const saved = localStorage.getItem(STORAGE_KEY_LOG_HEIGHT);
    if (saved) {
      const parsed = parseInt(saved, 10);
      if (!Number.isNaN(parsed) && parsed >= MIN_LOG_HEIGHT) {
        applyLogHeight(parsed, false);
      }
    }
  } catch {
    // 存储读取兼容防御
  }

  let isDragging = false;

  const onPointerDown = (e: PointerEvent) => {
    if (e.button !== 0) {
      return;
    }
    isDragging = true;
    resizer.setPointerCapture(e.pointerId);
    document.body.classList.add("is-resizing-log");
    el.logPane.classList.add("is-resizing");
    e.preventDefault();
  };

  const onPointerMove = (e: PointerEvent) => {
    if (!isDragging) {
      return;
    }
    const containerRect = el.workbenchBody?.getBoundingClientRect();
    if (!containerRect) {
      return;
    }
    const targetHeight = Math.round(containerRect.bottom - e.clientY);

    if (targetHeight < COLLAPSE_THRESHOLD) {
      if (!el.logPane.classList.contains("collapsed")) {
        el.logPane.classList.add("collapsed");
        if (el.btnToggleLogText) {
          el.btnToggleLogText.textContent = "展开";
        }
      }
    } else {
      if (el.logPane.classList.contains("collapsed")) {
        el.logPane.classList.remove("collapsed");
        if (el.btnToggleLogText) {
          el.btnToggleLogText.textContent = "折叠";
        }
      }
      applyLogHeight(targetHeight, false);
    }
  };

  const onPointerUp = (e: PointerEvent) => {
    if (!isDragging) {
      return;
    }
    isDragging = false;
    try {
      if (resizer.hasPointerCapture(e.pointerId)) {
        resizer.releasePointerCapture(e.pointerId);
      }
    } catch {
      // 指针捕获释放兼容防御
    }
    document.body.classList.remove("is-resizing-log");
    el.logPane.classList.remove("is-resizing");

    if (!el.logPane.classList.contains("collapsed")) {
      try {
        localStorage.setItem(STORAGE_KEY_LOG_HEIGHT, String(currentLogHeight));
      } catch {
        // 存储写入兼容防御
      }
    }
  };

  resizer.addEventListener("pointerdown", onPointerDown);
  resizer.addEventListener("pointermove", onPointerMove);
  resizer.addEventListener("pointerup", onPointerUp);
  resizer.addEventListener("pointercancel", onPointerUp);

  resizer.addEventListener("dblclick", (e) => {
    e.stopPropagation();
    toggleConsole();
  });

  window.addEventListener("resize", () => {
    if (!el.logPane.classList.contains("collapsed") && el.logPane.style.height) {
      applyLogHeight(currentLogHeight, false);
    }
  });
}

function toggleConsole(open?: boolean): void {
  const isCollapsed = el.logPane.classList.contains("collapsed");
  const shouldOpen = open !== undefined ? open : isCollapsed;
  el.logPane.classList.toggle("collapsed", !shouldOpen);
  if (el.btnToggleLogText) {
    el.btnToggleLogText.textContent = shouldOpen ? "折叠" : "展开";
  }
  if (shouldOpen) {
    if (!el.logPane.style.height && currentLogHeight) {
      applyLogHeight(currentLogHeight, false);
    }
    el.log.scrollTop = el.log.scrollHeight;
  }
}

function updateTextScopeState(): void {
  const isEnabled = el.replaceText.checked;
  el.btnOpenTables.disabled = !isEnabled || busy;
  const scopeControl = el.btnOpenTables.closest(".scope-control");
  scopeControl?.classList.toggle("disabled", !isEnabled);
  el.csvPicker?.classList.toggle("disabled", !isEnabled);
}

function updateRestoreButtonState(): void {
  if (gameInfo?.regionMismatch || clientStatus?.status === "mismatch") {
    el.btnRestore.disabled = true;
    el.btnRestore.title = "客户端区服与当前选择的目标区服不匹配，无法执行还原";
    return;
  }
  const isGlobal = isGlobalLanguage(config.language);
  const versionMismatch = Boolean(
    gameInfo?.version &&
    gameInfo?.backupVersion &&
    gameInfo.version !== gameInfo.backupVersion
  );

  if (!isGlobal) {
    if (!gameInfo?.backupExists) {
      el.btnRestore.disabled = true;
      el.btnRestore.title = "本地还没有原版备份，当前区服也没有云端原版文件（首次汉化会自动备份）";
    } else if (versionMismatch) {
      el.btnRestore.disabled = true;
      el.btnRestore.title = `游戏已更新至 v${gameInfo.version}，本地备份为旧版 v${gameInfo.backupVersion}，无法还原`;
    } else {
      el.btnRestore.disabled = busy;
      el.btnRestore.removeAttribute("title");
    }
  } else {
    el.btnRestore.disabled = busy;
    el.btnRestore.removeAttribute("title");
  }
}

// detectGameDir 请求后端探测客户端目录；silent 为 true 时用于界面就绪后的后台扫描，
// 不占用全局忙碌态，扫描期间探测按钮同样可点“停止”。
async function detectGameDir(silent: boolean): Promise<void> {
  isDetecting = true;
  detectCanceled = false;
  if (!silent) {
    setBusy(true, "正在搜索本地客户端…");
  }
  renderDetectButton(true);
  try {
    const list = await call("DetectGamePaths", config.language);
    if (detectCanceled) {
      log("[探测] 已停止搜索。", "info");
      return;
    }
    const prevDir = currentGameDir();
    if (list.length === 0) {
      if (!silent) {
        log("[探测] 未在常用路径及磁盘 3 层目录内找到匹配该区服的客户端，请点击“浏览”手动指定。", "info");
      }
      return;
    }
    if (silent && prevDir) {
      return;
    }
    const detectedDir = list[0]!;
    setGameDir(detectedDir);
    log(silent ? `[配置] 自动探测并定位到游戏目录：${detectedDir}` : `[探测] 自动探测到客户端：${detectedDir}`);
    await refreshGameInfo();
    if (detectedDir !== prevDir) {
      void checkClientEnvironment();
    }
  } catch (error) {
    log(`探测游戏目录失败: ${describeError(error)}`, "error");
  } finally {
    isDetecting = false;
    renderDetectButton(false);
    if (!silent) {
      setBusy(false);
    }
  }
}

// renderDetectButton 在扫描期间把探测按钮切换为可点击的“停止”。
function renderDetectButton(scanning: boolean): void {
  el.btnDetect.textContent = scanning ? "停止" : "自动探测";
  el.btnDetect.classList.toggle("is-detecting", scanning);
  if (scanning) {
    el.btnDetect.disabled = false;
  }
}

function setBusy(value: boolean, label = ""): void {
  busy = value;
  el.busyIndicator.textContent = value ? label || "执行中…" : "准备就绪";
  el.actionProgressWrap.classList.toggle("hidden", !value);
  el.actionProgressPercent.classList.toggle("hidden", !value);
  if (!value) {
    el.progressBar.style.width = "0%";
    el.actionProgressPercent.textContent = "0%";
  }
  if (!isPatching) {
    el.btnPatch.disabled = value;
  }
  el.btnBrowse.disabled = value;
  el.btnDetect.disabled = value;
  el.preset.disabled = value;
  el.language.disabled = value;
  el.targetLangTrigger.disabled = value;
  el.replaceText.disabled = value;
  el.replaceFont.disabled = value;
  el.csvTrigger.disabled = value;
  el.rules.querySelectorAll<HTMLInputElement>("input[type='checkbox']").forEach((cb) => {
    cb.disabled = value;
  });
  el.rules.querySelectorAll(".rule-row").forEach((row) => {
    row.classList.toggle("disabled", value);
  });
  updateTextScopeState();
  updateFontUiAssetState();
  updateRestoreButtonState();
  refreshUpdateAffordance();
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
  return openModal(title, text, okText, "").then((result) => result === "ok");
}

// openModal 支持一个可选的次要操作按钮（如「手动下载」），确认/次要/取消三态返回。
// hideCancel 用于两个选项已覆盖全部去向的场合，避免出现语义重复的第三个按钮。
function openModal(
  title: string,
  text: string,
  okText: string,
  altText: string,
  hideCancel = false,
): Promise<ModalResult> {
  return new Promise<ModalResult>((resolve) => {
    // 只有一个弹窗槽位：新弹窗顶掉旧弹窗时先把旧 Promise 按「取消」结算，
    // 否则等待旧弹窗答案的后端流程（如启动器确认）会一直挂到超时。
    modalResolver?.("cancel");
    modalResolver = resolve;
    el.modalTitle.textContent = title;
    el.modalText.textContent = text;
    el.modalText.classList.remove("hidden");
    el.modalOk.textContent = okText;
    if (altText) {
      el.modalAlt.textContent = altText;
      el.modalAlt.classList.remove("hidden");
    } else {
      el.modalAlt.classList.add("hidden");
    }
    el.modalCancel.classList.toggle("hidden", hideCancel);
    el.modal.classList.remove("hidden");
  });
}

function closeModal(result: ModalResult): void {
  el.modal.classList.add("hidden");
  el.modalAlt.classList.add("hidden");
  const resolver = modalResolver;
  modalResolver = null;
  resolver?.(result);

  el.modalText.classList.remove("hidden");
}

el.modalOk.addEventListener("click", () => closeModal("ok"));
el.modalCancel.addEventListener("click", () => closeModal("cancel"));
el.modalAlt.addEventListener("click", () => closeModal("alt"));

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

// updateLocked 表示当前禁止检查或安装主程序更新：汉化、还原等占用客户端的任务期间一律不允许。
function updateLocked(): boolean {
  return isPatching || busy;
}

// refreshUpdateAffordance 依据任务状态与已知更新结果同步版本区入口的可见性。
function refreshUpdateAffordance(): void {
  const locked = updateLocked();
  el.btnAppUpdate.classList.toggle("hidden", !latestUpdateInfo?.hasUpdate || locked);
  el.versionBadge.disabled = locked;
  el.versionBadge.title = locked ? "汉化进行中，完成后可检查更新" : "点击检查主程序更新";
  el.btnAppUpdate.title = latestUpdateInfo
    ? `发现新版本 v${latestUpdateInfo.latestVersion}，点击更新`
    : "发现新版本，点击查看";
}

type UpdateCheckMode = { interactive?: boolean; prompt?: boolean };

async function checkAppUpdate(mode: UpdateCheckMode = {}): Promise<void> {
  const { interactive = false, prompt = false } = mode;
  if (checkingAppUpdate) return;
  if (updateLocked()) {
    if (interactive) {
      log("[提示] 汉化进行中，暂不能检查更新。", "warn");
    }
    return;
  }
  checkingAppUpdate = true;
  if (interactive) {
    log("[检查] 正在检查汉化器主程序更新...");
  }
  try {
    const info = await call("CheckAppUpdate");
    latestUpdateInfo = info;
    refreshUpdateAffordance();
    if (info.hasUpdate) {
      log(`[更新] 发现新版本 v${info.latestVersion}。`, "ok");
      // 结果返回时可能已开始汉化，此时不弹窗，只保留版本入口供任务结束后点击。
      if ((interactive || prompt) && !updateLocked()) {
        showUpdateDialog(info);
      }
    } else if (interactive) {
      log("[检查] 当前汉化器已是最新版本。", "ok");
      await showConfirm("检查更新", `当前已是最新版本 (v${info.currentVersion})。`, "确定");
    }
  } catch (err) {
    if (interactive) {
      log(`[错误] 检查主程序更新失败: ${describeError(err)}`, "error");
      await showConfirm("检查更新失败", `无法连接到更新服务器: ${describeError(err)}`, "确定");
    }
  } finally {
    checkingAppUpdate = false;
  }
}

function showUpdateDialog(info: AppUpdateInfo): void {
  const notes = info.releaseNotes ? `\n\n更新说明：\n${info.releaseNotes}` : "";
  const canSelfUpdate = info.selfUpdateSupported === true;
  const tail = canSelfUpdate ? "" : "\n\n当前平台不支持自动更新，请手动下载后覆盖原程序。";
  const text = `发现新版本 v${info.latestVersion}（当前为 v${info.currentVersion}）${notes}${tail}`;
  // 次要入口只是「不走自动更新，去浏览器里下载」，镜像与否是实现细节，不出现在界面上。
  const downloadUrl = manualDownloadUrl(info);
  const altText = "手动下载";
  const okText = canSelfUpdate ? "立即更新" : "手动下载";
  void openModal("发现新版本", text, okText, canSelfUpdate ? altText : "").then((result) => {
    if (result === "ok") {
      if (canSelfUpdate) {
        void installAppUpdate(info);
      } else {
        void call("OpenLink", downloadUrl);
      }
    } else if (result === "alt") {
      void call("OpenLink", downloadUrl);
    }
  });
}

// manualDownloadUrl 优先给出加速镜像地址：直连 GitHub 在部分网络下不可用。
function manualDownloadUrl(info: AppUpdateInfo): string {
  return info.mirrorUrl || info.downloadUrl || info.releaseUrl;
}

// installAppUpdate 下载并替换当前程序。新版本启动后由后端退出当前进程。
async function installAppUpdate(info: AppUpdateInfo): Promise<void> {
  if (updateLocked()) {
    log("[提示] 汉化进行中，暂不能更新主程序。", "warn");
    return;
  }
  try {
    setBusy(true, `正在下载新版本 v${info.latestVersion}…`);
    refreshUpdateAffordance();
    log(`[更新] 开始下载 v${info.latestVersion}…`);
    await call("InstallAppUpdate");
  } catch (err) {
    setBusy(false);
    refreshUpdateAffordance();
    log(`[错误] 自动更新失败: ${describeError(err)}`, "error");
    const goManual = await showConfirm(
      "自动更新失败",
      `${describeError(err)}\n\n是否手动下载最新版本覆盖当前程序？`,
      "手动下载",
    );
    if (goManual) {
      void call("OpenLink", manualDownloadUrl(info));
    }
    return;
  }

  setBusy(false);
  latestUpdateInfo = null;
  refreshUpdateAffordance();

  const restart = await openModal(
    "更新已安装",
    `新版本 v${info.latestVersion} 已安装完成，重启程序后生效。`,
    "立即重启",
    "稍后重启",
    true,
  );
  if (restart !== "ok") {
    log("[更新] 已推迟重启，下次启动时生效。");
    return;
  }
  log("[更新] 正在重启…");
  try {
    await call("RestartApp");
  } catch (err) {
    log(`[错误] 重启失败: ${describeError(err)}`, "error");
  }
}

type IdleScheduler = { requestIdleCallback?: (cb: () => void, opts: { timeout: number }) => number };

// runWhenIdle 把磁盘扫描等非紧急任务推迟到界面渲染完成后的空闲时段执行。
function runWhenIdle(task: () => void, fallbackDelay = 300): void {
  const schedule = (window as unknown as IdleScheduler).requestIdleCallback;
  if (typeof schedule === "function") {
    schedule(task, { timeout: 1500 });
    return;
  }
  window.setTimeout(task, fallbackDelay);
}

async function bootstrapApp(): Promise<void> {
  try {
    await waitForBackend(8000);
    wireRuntimeEvents();
    wireEvents();

    try {
      const ver = await call("GetAppVersion");
      if (ver && el.versionBadge) {
        el.versionBadge.textContent = `v${ver}`;
      }
    } catch {
      // 容错防御
    }

    await loadBootstrap();
    switchView("workbench");
    void checkAppUpdate({ prompt: true });
    void syncBuiltinOnStart();

    if (currentGameDir()) {
      await refreshGameInfo();
      void checkClientEnvironment();
    } else {
      renderGameInfo();
      runWhenIdle(() => {
        void detectGameDir(true);
      });
    }
  } catch (error) {
    try {
      const ver = await call("GetAppVersion");
      if (ver && el.versionBadge) {
        el.versionBadge.textContent = `v${ver}`;
      }
    } catch {
      // 容错防御
    }
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
  if (!config.gameDirs) {
    config.gameDirs = {};
  }
  if (!config.targetLanguages) {
    config.targetLanguages = {};
  }
  if (!config.csvDirs) {
    config.csvDirs = {};
  }
  setWindowTitle();
  if (el.versionBadge) {
    el.versionBadge.textContent = `v${info.version}`;
  }
  el.gamePath.value = currentGameDir();
  el.replaceText.checked = config.replaceText;
  el.replaceFont.checked = config.replaceFont;
  updateTextScopeState();
  updateFontUiAssetState();

  renderRules();
  renderLanguages();
  renderTargetLangs();

  const activePreset = config.preset && config.preset !== "custom" ? config.preset : "recommended";
  config.preset = activePreset;
  applyCSVContext(info.csv, "");
  tableDocPending = ensureTableDoc();
}

// syncBuiltinOnStart 启动后立刻同步一次内置资源，与当前选中的词条目录无关。
async function syncBuiltinOnStart(): Promise<void> {
  await tableDocPending;
  if (bootstrap?.csv.selected && !csvSelectionValid()) {
    notifyInvalidCSV();
  }
  await syncBuiltinResource();
}

// ensureTableDoc 在本地表清单不可用时只补这一份清单，不触发整包同步。
async function ensureTableDoc(): Promise<void> {
  tableDocReady = true;
  try {
    const updated = await call("EnsureTableDoc");
    if (updated) {
      log("[资源] 已更新表清单 tables.json");
      await reloadCSVContext();
    }
  } catch (err) {
    log(`[警告] 表清单同步失败: ${describeError(err)}`, "warn");
  }
  // 内置词条随后会走整包同步，这里只处理自定义目录，避免同步前误报缺清单。
  if (bootstrap?.csv.selected && !csvSelectionValid()) {
    notifyInvalidCSV();
  }
}

function renderRules(): void {
  if (!bootstrap) {
    return;
  }
  el.rules.replaceChildren();

  const unavailable = bootstrap.rules.filter((rule) => rule.status === "unavailable");
  if (unavailable.length > 0) {
    const notice = document.createElement("div");
    notice.className = "rule-notice";
    notice.textContent = `规则数据不可用，已停用：${unavailable.map((rule) => rule.name).join("、")}。请同步资源后重试。`;
    el.rules.append(notice);
  }

  for (const rule of bootstrap.rules) {
    const blocked = rule.status === "unavailable";

    const label = document.createElement("label");
    label.className = "form-row rule-row";
    if (blocked) {
      label.classList.add("disabled");
    }

    const wrap = document.createElement("span");
    wrap.className = "rule-text";

    const title = document.createElement("span");
    title.className = "rule-name";
    title.textContent = rule.name;
    if (blocked) {
      const badge = document.createElement("span");
      badge.className = "rule-badge";
      badge.textContent = "不可用";
      title.append(badge);
    }

    const desc = document.createElement("span");
    desc.className = "rule-desc";
    desc.textContent = blocked && rule.statusDetail ? rule.statusDetail : rule.description;
    if (blocked) {
      desc.classList.add("rule-desc-blocked");
    }

    const sw = document.createElement("span");
    sw.className = "switch";

    const input = document.createElement("input");
    input.type = "checkbox";
    input.disabled = blocked;
    input.checked = blocked ? false : (config.rules[rule.id] ?? rule.defaultOn);
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

// currentGameDir 返回当前选中区服的客户端目录；配置只按区服保存，切换语言即切换目录。
function currentGameDir(): string {
  return config.gameDirs?.[config.language] ?? "";
}

function setGameDir(dir: string): void {
  if (!config.gameDirs) {
    config.gameDirs = {};
  }
  config.gameDirs[config.language] = dir;
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
  item.append(k, v);
  return item;
}

function isGlobalLanguage(langCode: string): boolean {
  const opt = bootstrap?.languages?.find((l) => l.code === langCode);
  if (opt) {
    return opt.region === "global";
  }
  const upper = (langCode || "").trim().toUpperCase();
  return upper === "GLOBAL" || upper === "GL" || upper === "JA" || upper === "EN" || upper === "DE" || upper === "FR";
}

// ---------- 汉化语言多选框（始终显示；仅国际服可多选） ----------

function currentRegionCode(): string {
  const opt = bootstrap?.languages?.find((l) => l.code.toUpperCase() === config.language.toUpperCase());
  if (opt) {
    return opt.region;
  }
  return isGlobalLanguage(config.language) ? "global" : "";
}

function regionLanguageOptions(): RegionLanguage[] {
  const region = currentRegionCode();
  return bootstrap?.regionLanguages?.[region] ?? [];
}

// syncTargetLangConfig 只更新当前区服的勾选，其它区服的记录保持原样。
function syncTargetLangConfig(): void {
  if (!config.targetLanguages) {
    config.targetLanguages = {};
  }
  config.targetLanguages[config.language] = [...targetLangs];
  void persistConfig();
}

function resolveTargetLanguages(): void {
  const opts = regionLanguageOptions();
  const saved = (config.targetLanguages?.[config.language] ?? []).filter((c) => opts.some((o) => o.code === c));
  const active = targetLangs.filter((c) => opts.some((o) => o.code === c));
  const source = active.length > 0 ? active : (saved.length > 0 ? saved : ["JA", "EN"]);
  targetLangs = opts.map((o) => o.code).filter((c) => source.includes(c));
}

function updateTargetLangView(): void {
  const opts = regionLanguageOptions();
  // 单语言区服的选项固定为唯一语言，不受 targetLangs（仅国际服有效）影响
  const selected = opts.length === 1 ? opts.map((o) => o.code) : targetLangs;
  const nameOf = (code: string) => opts.find((o) => o.code === code)?.name || code;
  el.targetLangSummary.textContent = selected.length === 0
    ? "—"
    : opts.length > 1 && selected.length === opts.length
      ? "全部语言"
      : selected.map(nameOf).join("、");

  el.targetLangMenu.querySelectorAll<HTMLInputElement>("input[type='checkbox']").forEach((cb) => {
    const code = cb.dataset.langCode;
    if (code) {
      cb.checked = selected.includes(code);
    }
  });
}

// 汉化语言选择器始终显示，避免切换区服时布局跳动；
// 单语言区服只展示唯一语言，勾选项禁止取消。
function renderTargetLangs(): void {
  const opts = regionLanguageOptions();
  const locked = opts.length <= 1;

  if (locked) {
    targetLangs = [];
    // 单语言区服的目标语言由区服决定，仍写入配置以便直接阅读
    if (opts.length === 1) {
      if (!config.targetLanguages) {
        config.targetLanguages = {};
      }
      config.targetLanguages[config.language] = [opts[0].code];
    }
  } else {
    resolveTargetLanguages();
  }

  el.targetLangMenu.replaceChildren();
  el.targetLangMenu.classList.add("hidden");
  el.targetLangTrigger.setAttribute("aria-expanded", "false");
  if (locked && opts.length === 1) {
    el.targetLangTrigger.title = "该区服只有一种语言，无需选择";
  } else {
    el.targetLangTrigger.removeAttribute("title");
  }

  for (const opt of opts) {
    const item = document.createElement("label");
    item.className = locked ? "multiselect-item is-locked" : "multiselect-item";

    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.dataset.langCode = opt.code;
    cb.disabled = locked;
    if (!locked) {
      cb.addEventListener("change", () => {
        if (cb.checked) {
          if (!targetLangs.includes(opt.code)) {
            targetLangs.push(opt.code);
          }
        } else {
          targetLangs = targetLangs.filter((c) => c !== opt.code);
        }
        if (targetLangs.length === 0) {
          targetLangs = [opt.code];
          cb.checked = true;
          log("[提示] 至少需要勾选一种汉化语言。", "warn");
        }
        targetLangs = opts.map((o) => o.code).filter((c) => targetLangs.includes(c));
        updateTargetLangView();
        syncTargetLangConfig();
      });
    }

    const text = document.createElement("span");
    text.textContent = opt.name;
    item.append(cb, text);
    el.targetLangMenu.append(item);
  }

  updateTargetLangView();
}

function toggleTargetLangMenu(open?: boolean): void {
  const shouldOpen = open !== undefined ? open : el.targetLangMenu.classList.contains("hidden");
  el.targetLangMenu.classList.toggle("hidden", !shouldOpen);
  el.targetLangTrigger.setAttribute("aria-expanded", String(shouldOpen));
}

// tableList 返回当前词条目录对应的表清单（内置词条为全量表，自定义目录只含其收录的表）。
function tableList(): TableEntry[] {
  return bootstrap?.csv?.tableList ?? [];
}

function csvDirName(path: string): string {
  const parts = path.split(/[\\/]/).filter((part) => part.length > 0);
  return parts.length > 0 ? parts[parts.length - 1]! : path;
}

// csvDisplayName 优先使用用户设置的名词，未设置时退回目录名。
function csvDisplayName(path: string): string {
  if (!path) {
    return "内置词条";
  }
  const title = bootstrap?.csv?.dirs.find((dir) => dir.path === path)?.title;
  return title ? title : csvDirName(path);
}

function csvSelectionValid(): boolean {
  const ctx = bootstrap?.csv;
  if (!ctx || !ctx.selected) {
    return true;
  }
  return ctx.dirs.some((dir) => dir.path === ctx.selected && dir.valid);
}

// beginCSVBusy 在切换/复制词条目录时给选择器加等待状态，立即生效。
function beginCSVBusy(text: string): void {
  csvBusyText = text;
  el.csvTrigger.disabled = true;
  el.csvTrigger.classList.remove("is-invalid");
  el.csvTrigger.classList.add("is-syncing");
  el.csvSyncBar.classList.add("is-pending");
  el.csvSyncBar.style.width = "100%";
  el.csvSummary.textContent = text;
}

function endCSVBusy(): void {
  if (!csvBusyText) {
    return;
  }
  csvBusyText = "";
  el.csvTrigger.disabled = !el.replaceText.checked;
  el.csvTrigger.classList.remove("is-syncing");
  el.csvSyncBar.classList.remove("is-pending");
  el.csvSyncBar.style.width = "0%";
  renderCSVTrigger();
}

function renderCSVTrigger(): void {
  const ctx = bootstrap?.csv;
  if (!ctx || csvBusyText) {
    return;
  }
  const invalid = !csvSelectionValid();
  const pending = !ctx.selected && !ctx.resourceReady;
  const syncFailed = !ctx.selected && builtinSyncFailed;
  el.csvSummary.textContent = ctx.selected
    ? `${csvDisplayName(ctx.selected)}${invalid ? "（已失效）" : ""}`
    : syncFailed
      ? "内置词条（同步失败）"
      : pending
        ? "内置词条（未同步）"
        : "内置词条";
  el.csvTrigger.title = ctx.selected
    ? ctx.selected
    : syncFailed
      ? "上次同步内置词条失败：开始汉化时会再试一次，或改用自定义词条目录"
      : pending
        ? "内置词条尚未同步完成"
        : "";
  el.csvTrigger.classList.toggle("is-invalid", invalid || syncFailed);
}

function clearCSVSyncState(): void {
  el.csvTrigger.classList.remove("is-syncing");
  el.csvSyncBar.classList.remove("is-pending");
  el.csvSyncBar.style.width = "0%";
  renderCSVTrigger();
}

function renderCSVMenu(): void {
  const ctx = bootstrap?.csv;
  if (!ctx) {
    return;
  }
  closeCSVDeletePopover();
  el.csvMenu.replaceChildren();

  const builtinDetail = builtinSyncFailed
    ? "使用云端资源确保最新，用户无法修改（上次同步失败，开始汉化时会重试）"
    : "使用云端资源确保最新，用户无法修改";
  const options: {
    path: string;
    label: string;
    detail: string;
    title: string;
    desc: string;
    updatedAt: string;
    hash: string;
    valid: boolean;
    editable: boolean;
    removable: boolean;
    canDeleteFiles: boolean;
  }[] = [
    {
      path: "",
      label: "内置词条",
      detail: builtinDetail,
      title: "",
      desc: "",
      updatedAt: ctx.resourceUpdatedAt ?? "",
      hash: ctx.resourceHash ?? "",
      valid: true,
      editable: false,
      removable: false,
      canDeleteFiles: false,
    },
    ...ctx.dirs.map((dir) => ({
      path: dir.path,
      label: dir.title || csvDirName(dir.path),
      detail: dir.desc || dir.path,
      title: dir.title ?? "",
      desc: dir.desc ?? "",
      updatedAt: dir.updatedAt ?? "",
      hash: "",
      valid: dir.valid,
      editable: true,
      removable: true,
      canDeleteFiles: dir.canDeleteFiles === true,
    })),
  ];

  for (const opt of options) {
    const row = document.createElement("div");
    row.className = "csv-option";
    if (opt.path === ctx.selected) {
      row.classList.add("is-active");
    }
    if (!opt.valid) {
      row.classList.add("is-invalid");
    }

    // 正在编辑的目录行整行换成输入框，名称与描述都在原位改。
    if (opt.editable && csvEditingPath === opt.path) {
      row.classList.add("is-editing");
      row.append(buildCSVMetaEditor(opt.path, opt.title, opt.desc));
      el.csvMenu.append(row);
      continue;
    }

    const main = document.createElement("button");
    main.type = "button";
    main.className = "csv-option-main";
    if (opt.path) {
      main.title = opt.path;
    }

    // 名字后跟一小段时间，标明这份词条最后更新的时刻。
    const head = document.createElement("span");
    head.className = "csv-option-head";
    const name = document.createElement("span");
    name.className = "csv-option-name";
    name.textContent = opt.valid ? opt.label : `${opt.label}（已失效）`;
    head.append(name);
    if (opt.updatedAt) {
      const time = document.createElement("span");
      time.className = "csv-option-time";
      time.textContent = opt.updatedAt;
      head.append(time);
    }
    // 内置词条额外标出资源包短哈希，便于确认副本基于哪份快照。
    if (opt.hash) {
      const hash = document.createElement("span");
      hash.className = "csv-option-hash";
      hash.textContent = `#${opt.hash.slice(0, 8)}`;
      hash.title = `内置词条哈希 ${opt.hash}`;
      head.append(hash);
    }

    const detail = document.createElement("span");
    detail.className = "csv-option-path";
    detail.textContent = opt.valid ? opt.detail : opt.path;

    main.append(head, detail);
    if (opt.valid) {
      main.addEventListener("click", () => {
        void chooseCSV(opt.path);
      });
    } else {
      main.disabled = true;
      main.title = `${opt.path}（目录已失效，可打开目录检查或删除）`;
    }
    row.append(main);

    const actions = document.createElement("div");
    actions.className = "csv-option-actions";
    // 内置词条由云端维护，不提供打开本地目录的入口。
    if (opt.path) {
      actions.append(
        csvIconButton(CSV_ICON_FOLDER, "在文件管理器中打开", () => void openCSVDir(opt.path)),
      );
    }
    if (opt.editable) {
      actions.append(
        csvIconButton(CSV_ICON_PENCIL, "编辑名称与描述", () => startCSVMetaEdit(opt.path)),
      );
    }
    if (opt.removable) {
      let trashBtn: HTMLButtonElement | null = null;
      trashBtn = csvIconButton(
        CSV_ICON_TRASH,
        "移除该词条目录",
        () => {
          if (trashBtn) {
            openCSVDeletePopover(opt.path, opt.label, opt.canDeleteFiles, trashBtn);
          }
        },
        true,
      );
      actions.append(trashBtn);
    }
    row.append(actions);

    el.csvMenu.append(row);
  }

  el.csvMenu.append(buildCSVCopyPanel());
}

// buildCSVMetaEditor 把一行换成两个输入框：失焦即保存，Enter 保存，Esc 放弃。
function buildCSVMetaEditor(path: string, title: string, desc: string): HTMLElement {
  const wrap = document.createElement("div");
  wrap.className = "csv-option-editor";

  const titleInput = document.createElement("input");
  titleInput.type = "text";
  titleInput.className = "csv-option-input is-title";
  titleInput.maxLength = 40;
  titleInput.placeholder = "名称";
  titleInput.value = title || csvDirName(path);

  const descInput = document.createElement("input");
  descInput.type = "text";
  descInput.className = "csv-option-input";
  descInput.maxLength = 120;
  descInput.placeholder = "描述（可选）";
  descInput.value = desc;

  wrap.append(titleInput, descInput);

  let finished = false;
  const finish = (save: boolean): void => {
    if (finished) {
      return;
    }
    finished = true;
    const nextTitle = titleInput.value.trim();
    const nextDesc = descInput.value.trim();
    csvEditingPath = null;
    if (save && (nextTitle !== title || nextDesc !== desc)) {
      void saveCSVMeta(path, nextTitle, nextDesc);
      return;
    }
    renderCSVMenu();
  };

  wrap.addEventListener("focusout", (e) => {
    // 焦点在两个输入框之间移动时不算离开，不触发保存。
    if (!wrap.contains(e.relatedTarget as Node | null)) {
      finish(true);
    }
  });
  for (const input of [titleInput, descInput]) {
    input.addEventListener("keydown", (e) => {
      if (e.key === "Enter") {
        e.preventDefault();
        finish(true);
      } else if (e.key === "Escape") {
        e.preventDefault();
        finish(false);
      }
    });
  }

  requestAnimationFrame(() => {
    titleInput.focus();
    titleInput.select();
  });
  return wrap;
}

const CSV_ICON_FOLDER =
  '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h5a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"/>';
const CSV_ICON_PENCIL = '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4Z"/>';
const CSV_ICON_TRASH =
  '<polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>';

// csvIconButton 生成词条列表行尾的图标按钮。
function csvIconButton(
  icon: string,
  title: string,
  onClick: () => void,
  danger = false,
): HTMLButtonElement {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = danger ? "csv-icon-btn danger" : "csv-icon-btn";
  btn.title = title;
  btn.innerHTML = `<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icon}</svg>`;
  btn.addEventListener("click", (e) => {
    e.stopPropagation();
    onClick();
  });
  return btn;
}

// openCSVDeletePopover 在删除按钮下方弹出确认气泡：可删文件时会默认勾选
// 「同时删除目录文件」（后端仅对程序生成的副本放行），否则只能从列表移除。
function openCSVDeletePopover(
  path: string,
  label: string,
  canDeleteFiles: boolean,
  anchor: HTMLElement,
): void {
  // 再次点同一个删除按钮即收起气泡。
  if (csvDeletePopover?.anchor === anchor) {
    closeCSVDeletePopover();
    return;
  }
  closeCSVDeletePopover();

  const bubble = document.createElement("div");
  bubble.className = "popconfirm";
  bubble.setAttribute("role", "dialog");
  bubble.addEventListener("click", (e) => e.stopPropagation());

  const title = document.createElement("div");
  title.className = "popconfirm-title";
  title.textContent = `删除词条目录「${label}」？`;

  const hint = document.createElement("div");
  hint.className = "popconfirm-hint";

  let deleteFiles = canDeleteFiles;
  const syncHint = (): void => {
    hint.textContent = deleteFiles
      ? "目录文件会被永久删除，无法恢复。"
      : "仅从列表中移除，目录文件保留在磁盘上。";
  };
  syncHint();

  bubble.append(title);
  if (canDeleteFiles) {
    const checkLabel = document.createElement("label");
    checkLabel.className = "popconfirm-check";
    const check = document.createElement("input");
    check.type = "checkbox";
    check.checked = true;
    check.addEventListener("change", () => {
      deleteFiles = check.checked;
      syncHint();
    });
    const checkText = document.createElement("span");
    checkText.textContent = "同时删除目录文件";
    checkLabel.append(check, checkText);
    bubble.append(checkLabel);
  }
  bubble.append(hint);

  const actions = document.createElement("div");
  actions.className = "popconfirm-actions";
  const okBtn = document.createElement("button");
  okBtn.type = "button";
  okBtn.className = "btn btn-xs btn-danger";
  okBtn.textContent = "删除";
  okBtn.addEventListener("click", () => void removeCSV(path, label, deleteFiles));
  const cancelBtn = document.createElement("button");
  cancelBtn.type = "button";
  cancelBtn.className = "btn btn-xs btn-quiet";
  cancelBtn.textContent = "取消";
  cancelBtn.addEventListener("click", () => closeCSVDeletePopover());
  actions.append(okBtn, cancelBtn);
  bubble.append(actions);

  document.body.append(bubble);
  positionPopover(bubble, anchor);

  const onPointerDown = (e: PointerEvent): void => {
    const target = e.target as Node | null;
    if (target && (bubble.contains(target) || anchor.contains(target))) {
      return;
    }
    closeCSVDeletePopover();
  };
  const onKeyDown = (e: KeyboardEvent): void => {
    if (e.key === "Escape") {
      e.stopPropagation();
      closeCSVDeletePopover();
    }
  };
  // 菜单滚动或窗口尺寸变化会让气泡脱离锚点，直接收起。
  const onViewportChange = (): void => closeCSVDeletePopover();

  document.addEventListener("pointerdown", onPointerDown, true);
  document.addEventListener("keydown", onKeyDown, true);
  window.addEventListener("resize", onViewportChange);
  document.addEventListener("scroll", onViewportChange, true);

  cancelBtn.focus();

  csvDeletePopover = {
    element: bubble,
    anchor,
    dispose: () => {
      document.removeEventListener("pointerdown", onPointerDown, true);
      document.removeEventListener("keydown", onKeyDown, true);
      window.removeEventListener("resize", onViewportChange);
      document.removeEventListener("scroll", onViewportChange, true);
    },
  };
}

function positionPopover(bubble: HTMLElement, anchor: HTMLElement): void {
  const margin = 8;
  const rect = anchor.getBoundingClientRect();
  const width = bubble.offsetWidth;
  const height = bubble.offsetHeight;
  const left = Math.min(Math.max(margin, rect.right - width), window.innerWidth - width - margin);
  let top = rect.bottom + 6;
  if (top + height > window.innerHeight - margin) {
    top = Math.max(margin, rect.top - height - 6);
  }
  bubble.style.left = `${Math.round(left)}px`;
  bubble.style.top = `${Math.round(top)}px`;
}

function closeCSVDeletePopover(): void {
  const current = csvDeletePopover;
  if (!current) {
    return;
  }
  csvDeletePopover = null;
  current.dispose();
  current.element.remove();
}

// buildCSVCopyPanel 是菜单底部的副本入口，用卡片样式与目录列表区分层级。
function buildCSVCopyPanel(): HTMLElement {
  const panel = document.createElement("div");
  panel.className = "csv-copy-panel";
  panel.innerHTML =
    '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' +
    '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>';

  const text = document.createElement("div");
  text.className = "csv-copy-text";
  const title = document.createElement("span");
  title.className = "csv-copy-title";
  title.textContent = "想自己改 CSV？";
  const desc = document.createElement("span");
  desc.className = "csv-copy-desc";
  desc.textContent = "制作一份可自由编辑的内置词条副本";
  text.append(title, desc);

  // 已有副本时点按钮只会切换过去，转换选项没有意义，直接不显示。
  const hasCopy = (bootstrap?.csv.dirs ?? []).some((dir) => dir.canDeleteFiles);
  if (!hasCopy) {
    const checkLabel = document.createElement("label");
    checkLabel.className = "csv-copy-check";
    checkLabel.title = "先按内置的简繁转换规则（OpenCC 台湾正体）改写副本内容";
    const check = document.createElement("input");
    check.type = "checkbox";
    const checkText = document.createElement("span");
    checkText.textContent = "先套用简繁转换";
    checkLabel.append(check, checkText);
    text.append(checkLabel);
  }

  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "btn btn-quiet btn-xs csv-copy-btn";
  btn.textContent = "制作副本";
  btn.addEventListener("click", () => void copyBuiltinCSV());

  panel.append(text, btn);
  return panel;
}

// copyBuiltinCSV 制作内置词条副本并切换过去。复制期间列表保持展开，
// 完成后新目录即为当前项，并直接进入名称与描述的编辑。
async function copyBuiltinCSV(): Promise<void> {
  if (busy || syncingResource || csvBusyText) {
    return;
  }
  const copyBtn = el.csvMenu.querySelector<HTMLButtonElement>(".csv-copy-btn");
  const convert = el.csvMenu.querySelector<HTMLInputElement>(".csv-copy-check input")?.checked ?? false;
  if (copyBtn) {
    copyBtn.disabled = true;
    copyBtn.textContent = convert ? "正在转换…" : "正在制作…";
  }
  beginCSVBusy(convert ? "正在转换并制作副本…" : "正在制作副本…");
  try {
    const lang = config.language;
    const ctx = await call("CopyBuiltinCSVDirectory", lang, convert);
    endCSVBusy();
    applyCSVContextForLanguage(lang, ctx);
    // 副本落在列表中并直接进入内联编辑，名字默认是文件夹名 custom-csv。
    startCSVMetaEdit(ctx.selected);
  } catch (err) {
    endCSVBusy();
    log(`[错误] 制作内置词条副本失败: ${describeError(err)}`, "error");
    if (copyBtn?.isConnected) {
      copyBtn.disabled = false;
      copyBtn.textContent = "制作副本";
    }
  }
}

// openCSVDir 在系统文件管理器中打开指定词条目录，空路径表示内置词条目录。
async function openCSVDir(path: string): Promise<void> {
  try {
    await call("OpenCSVDirectory", config.language, path);
  } catch (err) {
    log(`[错误] 打开词条目录失败: ${describeError(err)}`, "error");
  }
}

// startCSVMetaEdit 让指定目录行进入内联编辑状态，列表保持展开。
function startCSVMetaEdit(path: string): void {
  if (csvBusyText) {
    return;
  }
  csvEditingPath = path;
  toggleCSVMenu(true);
  renderCSVMenu();
}

// saveCSVMeta 保存内联编辑的名称与描述，成功后按后端返回的上下文刷新列表。
async function saveCSVMeta(path: string, title: string, desc: string): Promise<void> {
  try {
    const lang = config.language;
    const ctx = await call("SetCSVDirectoryMeta", lang, path, title, desc);
    applyCSVContextForLanguage(lang, ctx);
    log(`[词条] 已更新词条目录信息：${title || csvDirName(path)}`);
  } catch (err) {
    log(`[错误] 更新词条目录信息失败: ${describeError(err)}`, "error");
    renderCSVMenu();
  }
}

function toggleCSVMenu(open?: boolean): void {
  if (csvBusyText) {
    return;
  }
  const shouldOpen = open !== undefined ? open : el.csvMenu.classList.contains("hidden");
  if (!shouldOpen) {
    closeCSVDeletePopover();
  }
  el.csvMenu.classList.toggle("hidden", !shouldOpen);
  el.csvTrigger.setAttribute("aria-expanded", String(shouldOpen));
}

// applyCSVContext 应用后端返回的词条目录上下文；表清单变化时同步勾选范围。
function applyCSVContext(ctx: CSVContext, previousKeys?: string): void {
  applyCSVContextForLanguage(config.language, ctx, previousKeys);
}

// applyCSVContextForLanguage 按「请求发出时的区服」落盘，
// 而不是应用时的当前区服：乱序返回时二者可能已经不同。
function applyCSVContextForLanguage(lang: string, ctx: CSVContext, previousKeys?: string): void {
  if (!bootstrap) {
    return;
  }
  const prevKeys = previousKeys ?? tableList().map((entry) => entry.key).join("\n");
  bootstrap.csv = ctx;
  bootstrap.resourceReady = ctx.resourceReady;

  if (!config.csvDirs) {
    config.csvDirs = {};
  }
  const labels: Record<string, CSVDirLabel> = {};
  for (const dir of ctx.dirs) {
    if (dir.title || dir.desc) {
      labels[dir.path] = { title: dir.title ?? "", desc: dir.desc ?? "" };
    }
  }
  config.csvDirs[lang] = {
    dirs: ctx.dirs.map((dir) => dir.path),
    selected: ctx.selected,
    labels,
  };

  renderCSVTrigger();
  renderCSVMenu();
  el.extendedSection.classList.toggle("hidden", ctx.custom);

  const nextKeys = ctx.tableList.map((entry) => entry.key).join("\n");
  if (nextKeys !== prevKeys) {
    const available = new Set(ctx.tableList.map((entry) => entry.key));
    for (const key of [...selected]) {
      if (!available.has(key)) {
        selected.delete(key);
      }
    }
    if (config.preset === "recommended" || config.preset === "all") {
      applyPreset(config.preset, false);
    }
  }
  syncPresetWithSelection();
  renderTables();

  if (bootstrap.csv.tableList.length === 0 || !csvSelectionValid()) {
    if (tableDocReady) {
      notifyInvalidCSV();
    }
  } else {
    invalidCSVNotified = "";
  }
}

function notifyInvalidCSV(): void {
  const ctx = bootstrap?.csv;
  if (!ctx || invalidCSVNotified === ctx.selected) {
    return;
  }
  invalidCSVNotified = ctx.selected;
  if (ctx.tableList.length === 0) {
    log("[警告] 本地表清单不可用，联网获取也失败了，请检查网络后重启程序。", "warn");
    return;
  }
  log(`[警告] 词条目录已失效：${ctx.selected}。请重新选择或删除该目录。`, "warn");
}

// reloadCSVContext 按当前区服拉取词条目录上下文。
// generation 校验：快速连切区服时丢弃乱序返回的旧响应，
// 避免旧区服的目录列表被写到当前（新）区服名下并存盘。
let csvContextGeneration = 0;

async function reloadCSVContext(): Promise<void> {
  const gen = ++csvContextGeneration;
  const langAtRequest = config.language;
  try {
    const ctx = await call("GetCSVContext", langAtRequest);
    if (gen !== csvContextGeneration) {
      return;
    }
    applyCSVContextForLanguage(langAtRequest, ctx);
  } catch (err) {
    log(`[警告] 读取词条目录配置失败: ${describeError(err)}`, "warn");
  }
}

async function chooseCSV(path: string): Promise<void> {
  if (busy || !bootstrap || csvBusyText) {
    return;
  }
  toggleCSVMenu(false);
  if (path === bootstrap.csv.selected) {
    return;
  }
  beginCSVBusy("正在切换…");
  try {
    const lang = config.language;
    const ctx = await call("SetCSVSelection", lang, path);
    endCSVBusy();
    applyCSVContextForLanguage(lang, ctx);
    if (path) {
      log(`[词条] 已切换到自定义词条目录：${csvDisplayName(path)}`);
    } else {
      log("[词条] 已切换到内置词条");
    }
  } catch (err) {
    endCSVBusy();
    log(`[错误] 切换词条目录失败: ${describeError(err)}`, "error");
    await reloadCSVContext();
  }
}

async function removeCSV(path: string, label: string, deleteFiles: boolean): Promise<void> {
  if (busy || !bootstrap || csvBusyText) {
    return;
  }
  closeCSVDeletePopover();
  if (csvEditingPath === path) {
    csvEditingPath = null;
  }
  beginCSVBusy(deleteFiles ? "正在删除目录文件…" : "正在移除…");
  try {
    const lang = config.language;
    const ctx = await call("RemoveCSVDirectory", lang, path, deleteFiles);
    endCSVBusy();
    applyCSVContextForLanguage(lang, ctx);
    log(
      deleteFiles
        ? `[词条] 已移除词条目录并删除其中的文件：${label}`
        : `[词条] 已移除词条目录（目录文件保留）：${label}`,
    );
  } catch (err) {
    endCSVBusy();
    log(`[错误] 移除词条目录失败: ${describeError(err)}`, "error");
  }
}

// setResourceSyncing 标记内置词条同步中；期间禁止开始汉化，避免后端的忙碌互斥报错。
function setResourceSyncing(value: boolean): void {
  syncingResource = value;
  if (isPatching) {
    return;
  }
  el.btnPatch.disabled = value || busy;
  if (el.btnPatchText) {
    el.btnPatchText.textContent = value ? "同步词条中…" : "开始汉化";
  }
}

// syncBuiltinResource 同步并校验内置资源，进度压在选择器上；
// 当前用的是自定义词条时只在后台进行，不占用选择器文字。
async function syncBuiltinResource(): Promise<void> {
  if (busy || syncingResource || csvBusyText) {
    return;
  }
  const showOnPicker = !bootstrap?.csv.selected;
  setResourceSyncing(true);
  if (showOnPicker) {
    beginCSVBusy("内置词条 同步中…");
  }
  log("[资源] 正在同步内置词条资源…");
  try {
    await call("SyncBuiltinResource");
    builtinSyncFailed = false;
    await reloadCSVContext();
  } catch (err) {
    builtinSyncFailed = true;
    log(`[警告] 内置词条同步失败: ${describeError(err)}`, "warn");
    if (!showOnPicker) {
      log("[提示] 当前使用自定义词条目录，不受影响；改用内置词条时会在开始汉化前重试", "warn");
    }
  } finally {
    setResourceSyncing(false);
    if (showOnPicker) {
      endCSVBusy();
    }
    clearCSVSyncState();
    renderCSVMenu();
  }
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
    } else if (clientStatus.status === "modified" || clientStatus.status === "mismatch") {
      cls = "warn";
    } else if (clientStatus.label === "文件损坏" || clientStatus.label === "文件缺失") {
      cls = "err";
    } else if (clientStatus.status === "unknown") {
      cls = "warn";
    }
    v.className = `diag-v ${cls}`.trim();
    v.textContent = clientStatus.label;
    v.title = clientStatus.description;
    btn.textContent = "重测";
    btn.style.display = "";
  } else {
    v.className = "diag-v";
    v.textContent = "未检测";
    btn.textContent = "检测";
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
  if (checkingClient || !currentGameDir()) {
    return;
  }
  checkingClient = true;
  renderGameInfo();
  try {
    clientStatus = await call("CheckClientStatus", currentGameDir(), config.language);
    let logKind: "ok" | "warn" | "error" | "patched" | "info" = "info";
    if (clientStatus.status === "original") {
      logKind = "ok";
    } else if (clientStatus.status === "patched") {
      logKind = "patched";
    } else if (clientStatus.status === "modified" || clientStatus.status === "mismatch" || clientStatus.label === "未收录版本") {
      logKind = "warn";
    } else if (clientStatus.label === "文件损坏" || clientStatus.label === "文件缺失") {
      logKind = "error";
    }
    const detail = clientStatus.description ? `（${clientStatus.description}）` : "";
    log(`[状态] 客户端：${clientStatus.label}${detail}`, logKind);
  } catch (err) {
    log(`[错误] 客户端校验失败: ${describeError(err)}`, "error");
  } finally {
    checkingClient = false;
    renderGameInfo();
  }
}

function updateFontUiAssetState(): void {
  const isEnabled = el.replaceFont.checked && !busy;
  el.fontUiAssetMeta?.classList.toggle("disabled", !isEnabled);

  if (!el.fontUiAssetStatus) return;

  if (!el.replaceFont.checked) {
    el.fontUiAssetStatus.className = "meta-tag muted";
    el.fontUiAssetStatus.textContent = "未启用";
    return;
  }

  if (gameInfo?.uiAssetType === 1) {
    el.fontUiAssetStatus.className = "meta-tag ok";
    el.fontUiAssetStatus.textContent = "已开启高分辨率";
  } else if (gameInfo?.uiAssetType === 0) {
    el.fontUiAssetStatus.className = "meta-tag warn";
    el.fontUiAssetStatus.textContent = "未开启高分辨率（汉化将自动开启）";
  } else {
    el.fontUiAssetStatus.className = "meta-tag muted";
    el.fontUiAssetStatus.textContent = "未找到配置";
  }
}

function renderGameInfo(): void {
  updateRestoreButtonState();
  updateFontUiAssetState();
  el.gameInfo.replaceChildren();
  if (!currentGameDir()) {
    el.gameInfo.append(createDiagItem("状态", "未选择游戏目录", "warn"));
    return;
  }
  if (!gameInfo) {
    el.gameInfo.append(createDiagItem("状态", "正在检测…"));
    return;
  }

  const isGlobal = isGlobalLanguage(config.language);
  const versionMismatch = Boolean(
    gameInfo.version &&
    gameInfo.backupVersion &&
    gameInfo.version !== gameInfo.backupVersion
  );

  let backupText = "未备份";
  let backupCls = "";
  if (gameInfo.backupExists) {
    if (!isGlobal && versionMismatch) {
      backupText = "已失效";
      backupCls = "warn";
    } else {
      backupText = "已就绪";
      backupCls = "ok";
    }
  }

  const backupItem = createDiagItem("本地备份", backupText, backupCls);

  const items = [
    createDiagItem("客户端版本", gameInfo.version || "未知", ""),
    createClientStatusItem(),
    backupItem,
  ];

  el.gameInfo.append(...items);
}

async function refreshGameInfo(): Promise<void> {
  if (!currentGameDir()) {
    gameInfo = null;
    renderGameInfo();
    return;
  }
  try {
    gameInfo = await call("GetGameInfo", currentGameDir(), config.language);
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
  // 自定义范围保留当前勾选，只切换预设状态，具体表在详情页里挑。
  if (name === "custom") {
    config.preset = "custom";
    el.preset.value = "custom";
    updateScopeCount();
    if (rerender) {
      renderTables();
    }
    return;
  }
  selected.clear();
  const all = tableList();
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
  updateScopeCount();
  if (rerender) {
    renderTables();
  }
}

// renderScopeSummary 让主界面的「选表范围」只呈现当前状态，
// 进入详情页统一交给旁边的按钮，避免下拉项同时承担导航职责。
function renderScopeSummary(): void {
  const label =
    config.preset === "all"
      ? "全量汉化（全部数据表）"
      : config.preset === "custom"
        ? `自定义（已选 ${selected.size} 张）`
        : "ACT兼容版（不汉化敌人名称）";
  el.scopeSummary.textContent = label;
  el.btnOpenTables.setAttribute("aria-label", `打开数据表清单，当前为${label}`);
}

function updateScopeCount(): void {
  const total = tableList().length;
  const selCount = selected.size;
  const unselCount = total - selCount;

  el.countUnselected.textContent = String(unselCount);
  el.btnFilterUnselected.classList.toggle("has-alert", unselCount > 0);
  renderScopeSummary();
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
    syncPresetWithSelection();
    updateScopeCount();
  });
  tdCheck.append(checkbox);

  const tdKey = document.createElement("td");
  tdKey.className = `col-filename ${isChild ? "sub-filename" : ""}`.trim();
  tdKey.textContent = entry.key;

  const tdName = document.createElement("td");
  tdName.className = "col-chs";
  const hideRepeatName = isChild && (entry.name === folderChs || !entry.name || entry.name === "-");
  const displayName = hideRepeatName ? "" : (entry.name || "-");
  tdName.textContent = displayName;

  const tdStatus = document.createElement("td");
  tdStatus.className = "col-status";
  if (entry.warning) {
    const tag = document.createElement("span");
    tag.className = "status-tag-warn";
    tag.textContent = entry.warning;
    tdStatus.append(tag);
  }

  tr.addEventListener("click", () => {
    const next = !selected.has(entry.key);
    checkbox.checked = next;
    setSelected(entry.key, next);
    tr.classList.toggle("selected", next);
    tr.classList.toggle("unselected", !next);
    syncPresetWithSelection();
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

  for (const entry of tableList()) {
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
    folderCheckbox.addEventListener("click", (e) => {
      e.stopPropagation();
      const targetState = !isAllSel;
      for (const item of allEntriesInFolder) {
        setSelected(item.key, targetState);
      }
      syncPresetWithSelection();
      renderTables();
    });

    selWrap.append(caret, folderCheckbox);
    tdSel.append(selWrap);

    const tdKey = document.createElement("td");
    tdKey.className = "col-filename folder-name-cell";
    tdKey.textContent = fKey;

    const folderChs = bootstrap?.folders ? bootstrap.folders[fKey] || "" : "";
    const tdName = document.createElement("td");
    tdName.className = "col-chs folder-chs-cell";
    tdName.textContent = folderChs || "-";

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

// syncPresetWithSelection 让预设跟随实际勾选结果：
// 勾选恰好等于某个预设时归到该预设，否则标记为自定义。
function syncPresetWithSelection(): void {
  const all = tableList();
  if (all.length === 0) {
    return;
  }
  const excluded = bootstrap?.excluded || ["BNpcName"];
  let next = "custom";
  if (all.every((entry) => selected.has(entry.key))) {
    next = "all";
  } else if (all.every((entry) => selected.has(entry.key) === !excluded.includes(entry.key))) {
    next = "recommended";
  }
  if (config.preset === next) {
    return;
  }
  config.preset = next;
  el.preset.value = next;
  updateScopeCount();
  void persistConfig();
}

function wireEvents(): void {
  el.versionBadge.addEventListener("click", () => {
    if (updateLocked()) {
      log("[提示] 汉化进行中，暂不能检查更新。", "warn");
      return;
    }
    void checkAppUpdate({ interactive: true });
  });
  el.btnAppUpdate.addEventListener("click", () => {
    if (updateLocked()) {
      return;
    }
    if (latestUpdateInfo?.hasUpdate) {
      showUpdateDialog(latestUpdateInfo);
    } else {
      void checkAppUpdate({ interactive: true });
    }
  });

  el.btnBrowse.addEventListener("click", async () => {
    if (busy) {
      return;
    }
    try {
      const dir = await call("SelectGameDirectory", config.language);
      if (dir) {
        const prevDir = currentGameDir();
        setGameDir(dir);
        log(`[配置] 已选择游戏目录：${dir}`);
        await refreshGameInfo();
        if (dir !== prevDir) {
          void checkClientEnvironment();
        }
      }
    } catch (error) {
      log(`[错误] 选择目录失败: ${describeError(error)}`, "error");
    }
  });

  el.btnDetect.addEventListener("click", () => {
    if (isDetecting) {
      detectCanceled = true;
      void call("CancelDetect").catch(() => {
        // 忽略停止失败：后端会在扫描结束后自然返回
      });
      log("[探测] 正在停止搜索…", "info");
      return;
    }
    if (busy) {
      return;
    }
    void detectGameDir(false);
  });

  el.language.addEventListener("change", async () => {
    if (busy) {
      return;
    }
    const oldLang = config.language;
    const newLang = el.language.value;
    if (oldLang === newLang) {
      return;
    }

    config.language = newLang;
    const rememberedDir = config.gameDirs?.[newLang] ?? "";
    el.gamePath.value = rememberedDir;
    clientStatus = null;

    // 切换区服后刷新汉化语言多选框（国际服恢复已保存勾选或默认日英，单语言区服锁定唯一语言）
    targetLangs = [];
    renderTargetLangs();

    if (!rememberedDir) {
      try {
        const detected = await call("DetectGamePaths", newLang);
        if (detected.length > 0 && detected[0] && !currentGameDir()) {
          config.gameDirs[newLang] = detected[0];
          el.gamePath.value = detected[0];
          log(`[探测] 自动定位到该区服客户端目录：${detected[0]}`);
        }
      } catch (err) {
        log(`[警告] 自动探测游戏目录失败: ${describeError(err)}`, "warn");
      }
    }

    void persistConfig();
    // 词条目录按区服保存，切换区服后同步该区服的目录列表与表清单
    await reloadCSVContext();
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
    updateFontUiAssetState();
    void persistConfig();
  });

  el.preset.addEventListener("change", () => {
    if (busy) {
      return;
    }
    applyPreset(el.preset.value, true);
    void persistConfig();
  });

  el.btnOpenTables.addEventListener("click", () => {
    if (busy) {
      return;
    }
    switchView("tables");
  });

  el.targetLangTrigger.addEventListener("click", (e) => {
    e.stopPropagation();
    if (busy) {
      return;
    }
    toggleTargetLangMenu();
  });

  el.targetLangMenu.addEventListener("click", (e) => {
    e.stopPropagation();
  });

  el.csvTrigger.addEventListener("click", (e) => {
    e.stopPropagation();
    if (busy) {
      return;
    }
    toggleCSVMenu();
  });

  el.csvMenu.addEventListener("click", (e) => {
    e.stopPropagation();
  });

  document.addEventListener("click", () => {
    toggleTargetLangMenu(false);
    toggleCSVMenu(false);
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
    for (const entry of tableList()) {
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
  });

  el.btnSelectNone.addEventListener("click", () => {
    if (busy) {
      return;
    }
    selected.clear();
    syncPresetWithSelection();
    renderTables();
  });

  el.btnClearLog.addEventListener("click", () => {
    el.log.replaceChildren();
    logEntries = [];
  });

  el.btnCopyLog.addEventListener("click", () => {
    void copyLogs();
  });

  initLogResizer();

  el.btnToggleLog?.addEventListener("click", (e) => {
    e.stopPropagation();
    toggleConsole();
  });

  el.logPane?.querySelector(".log-head")?.addEventListener("click", () => {
    if (el.logPane.classList.contains("collapsed")) {
      toggleConsole(true);
    }
  });

  el.btnRestore.addEventListener("click", async () => {
    if (busy) {
      return;
    }
    if (gameInfo?.regionMismatch || clientStatus?.status === "mismatch") {
      log("[错误] 客户端区服与当前选择的目标区服不匹配，无法执行还原。", "error");
      return;
    }
    const isGlobal = isGlobalLanguage(config.language);
    const versionMismatch = Boolean(
      gameInfo?.version &&
      gameInfo?.backupVersion &&
      gameInfo.version !== gameInfo.backupVersion
    );
    if (!isGlobal && !gameInfo?.backupExists) {
      log("[警告] 本地还没有原版备份，当前区服也没有云端原版文件；请用官方启动器修复客户端。", "warn");
      return;
    }
    if (!isGlobal && versionMismatch) {
      log(`[警告] 游戏已更新到 ${gameInfo?.version}，本地备份还是 ${gameInfo?.backupVersion}，旧备份不能用于还原；请用官方启动器修复客户端。`, "warn");
      return;
    }
    if (!currentGameDir()) {
      log("[错误] 请先指定游戏目录。", "error");
      return;
    }
    const confirmMsg = isGlobal && !gameInfo?.backupExists
      ? "本地暂无原版备份，将自动从云端下载官方原版文件恢复到客户端，已有汉化修改将被撤销。"
      : "确定将本地原版备份文件恢复到游戏客户端目录吗？已有汉化修改将被撤销。";
    const ok = await showConfirm(
      "还原官方原版文件",
      confirmMsg,
      "确认还原",
    );
    if (!ok) {
      return;
    }
    try {
      toggleConsole(true);
      setBusy(true, "正在还原官方原版文件…");
      const resp = await call("RestoreOriginal", currentGameDir(), config.language);
      if (resp.cancelled) {
        log("[提示] 还原已取消", "info");
        return;
      }
      // 完成结果由后端日志给出（[完成] …），此处不再重复输出。
      try {
        clientStatus = await call("CheckClientStatus", currentGameDir(), config.language);
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
    if (isPatching) {
      if (isStopping) return;
      isStopping = true;
      if (el.btnPatchText) el.btnPatchText.textContent = "正在停止…";
      el.btnPatch.classList.add("is-stopping");
      el.btnPatch.disabled = true;
      try {
        await call("CancelPatch");
      } catch {
        // 忽略中止调用异常
      }
      return;
    }

    if (busy) {
      return;
    }
    if (gameInfo?.regionMismatch || clientStatus?.status === "mismatch") {
      log("[错误] 客户端区服与当前选择的目标区服不匹配，请核对后重试。", "error");
      return;
    }
    if (!currentGameDir()) {
      log("[错误] 请先指定游戏目录。", "error");
      return;
    }
    if (!config.replaceText && !config.replaceFont) {
      log("[错误] 请至少勾选“替换文本”或“替换游戏字体”。", "error");
      return;
    }
    if (config.replaceText && !csvSelectionValid()) {
      if ((bootstrap?.csv.tableList.length ?? 0) === 0) {
        log("[错误] 本地缺少表清单（tables.json），请联网后重试。", "error");
      } else {
        log("[错误] 当前词条目录已失效，请重新选择或删除后再开始汉化。", "error");
      }
      return;
    }
    if (config.replaceText && selected.size === 0) {
      log("[错误] 已勾选“替换文本”，但汉化范围中未选择任何数据表。", "error");
      return;
    }
    if (config.replaceText && regionLanguageOptions().length > 1 && targetLangs.length === 0) {
      log("[错误] 请至少勾选一种汉化语言。", "error");
      return;
    }

    isPatching = true;
    isStopping = false;
    el.btnPatch.classList.add("is-patching");
    if (el.btnPatchText) el.btnPatchText.textContent = "停止汉化";
    el.btnPatch.disabled = false;

    try {
      toggleConsole(true);
      setBusy(true, "正在执行安全校验与汉化流程…");
      el.progressBar.style.width = "0%";
      log("[补丁] 开始汉化…");

      const req: PatchRequest = {
        gameDir: currentGameDir(),
        language: config.language,
        targetLanguages: [...targetLangs],
        replaceText: config.replaceText,
        replaceFont: config.replaceFont,
        rules: { ...config.rules },
        selectedTables: Array.from(selected),
        preset: config.preset,
      };

      const resp = await call("StartPatch", req);
      if (resp.cancelled) {
        log("[提示] 汉化已中止", "info");
        return;
      }
      for (const w of resp.warnings ?? []) {
        log(`[警告] ${w}`, "warn");
      }
      // 之前同步失败过的话，这次开始汉化前已经重试通过。
      builtinSyncFailed = false;
      el.progressBar.style.width = "100%";
      try {
        clientStatus = await call("CheckClientStatus", currentGameDir(), config.language);
      } catch {
        // 忽略静默更新失败
      }
      await refreshGameInfo();
      await reloadCSVContext();
    } catch (error) {
      log(`汉化失败: ${describeError(error)}`, "error");
    } finally {
      isPatching = false;
      isStopping = false;
      el.btnPatch.classList.remove("is-patching", "is-stopping");
      if (el.btnPatchText) el.btnPatchText.textContent = "开始汉化";
      clearCSVSyncState();
      setBusy(false);
    }
  });
}

function wireRuntimeEvents(): void {
  const on = window.runtime?.EventsOn;
  if (!on) {
    return;
  }

  // 自更新进度：下载阶段显示百分比，替换与重启阶段只更新提示文案。
  on("app-update-progress", (data: unknown) => {
    const ev = asProgress(data);
    if (!busy) return;
    if (ev.total > 0) {
      const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
      el.busyIndicator.textContent = `${ev.detail}… ${pct.toFixed(0)}%`;
      el.progressBar.style.width = `${pct.toFixed(1)}%`;
      el.actionProgressPercent.textContent = `${pct.toFixed(0)}%`;
      return;
    }
    el.busyIndicator.textContent = ev.detail ? `${ev.detail}…` : "正在更新…";
  });

  // 制作副本要逐个创建 7114 个 CSV（约 2.7 秒），进度同样压在选择器上。
  on("csv-copy-progress", (data: unknown) => {
    const ev = asProgress(data);
    if (!csvBusyText || ev.total <= 0) return;
    const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
    el.csvSummary.textContent = `${ev.detail || "制作副本"} ${pct.toFixed(0)}%`;
    el.csvSyncBar.classList.remove("is-pending");
    el.csvSyncBar.style.width = `${pct.toFixed(1)}%`;
  });

  // 资源同步的进度直接压在内置词条选择器上；汉化期间的进度同时走顶部进度条。
  let pendingResourceProgress: PatchProgressEvent | null = null;
  let resourceRafId = 0;
  on("resource-progress", (data: unknown) => {
    const ev = asProgress(data);
    pendingResourceProgress = ev;
    if (!resourceRafId) {
      resourceRafId = requestAnimationFrame(() => {
        resourceRafId = 0;
        const current = pendingResourceProgress;
        if (!current) return;
        if (!isPatching && !syncingResource) return;
        // 内置词条的同步进度直接压在选择器文字上，文案用后端给的当前阶段。
        if (!bootstrap?.csv.selected) {
          el.csvTrigger.classList.add("is-syncing");
          const detail = current.detail || "同步词条";
          if (current.total > 0) {
            const pct = Math.min(100, Math.max(0, (current.done / current.total) * 100));
            el.csvSummary.textContent = `${detail} ${pct.toFixed(0)}%`;
            el.csvSyncBar.classList.remove("is-pending");
            el.csvSyncBar.style.width = `${pct.toFixed(1)}%`;
          } else {
            el.csvSummary.textContent = `${detail}…`;
            el.csvSyncBar.classList.add("is-pending");
          }
        }
        if (!isPatching) return;
        if (current.total > 0) {
          const pct = Math.min(100, Math.max(0, (current.done / current.total) * 100));
          el.progressBar.style.width = `${pct.toFixed(1)}%`;
          el.busyIndicator.textContent = `同步资源：${current.detail || "请稍候"} ${pct.toFixed(0)}%`;
          el.actionProgressPercent.textContent = `${pct.toFixed(0)}%`;
        } else {
          el.busyIndicator.textContent = `同步资源：${current.detail || "请稍候…"}`;
        }
      });
    }
  });

  let lastDownloadDetail = "";
  let pendingPatchProgress: PatchProgressEvent | null = null;
  let patchRafId = 0;

  function formatStageTitle(stage: string): string {
    switch (stage) {
      case "verify":
        return "校验原版";
      case "backup":
        return "建立备份";
      case "restore":
        return "还原原版";
      case "download":
        return "下载备份";
      case "text":
        return "汉化文本";
      case "font":
        return "写入字体";
      default:
        return "正在处理";
    }
  }

  function renderPatchProgress(ev: PatchProgressEvent): void {
    const stageTitle = formatStageTitle(ev.stage);
    if (ev.stage === "download") {
      if (ev.total > 0) {
        const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
        el.progressBar.style.width = `${pct.toFixed(1)}%`;
        const doneMb = (ev.done / 1024 / 1024).toFixed(1);
        const totalMb = (ev.total / 1024 / 1024).toFixed(1);
        el.busyIndicator.textContent = `${stageTitle}：${ev.detail || "官方数据包"}`;
        el.actionProgressPercent.textContent = `${doneMb} / ${totalMb} MB (${pct.toFixed(1)}%)`;
      } else {
        el.busyIndicator.textContent = `${stageTitle}：${ev.detail || "官方数据包"}`;
        el.actionProgressPercent.textContent = "";
      }
      return;
    }

    if (ev.total > 0) {
      const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
      el.progressBar.style.width = `${pct.toFixed(1)}%`;
      el.busyIndicator.textContent = `${stageTitle}：${ev.detail || "正在处理数据"}`;
      el.actionProgressPercent.textContent = `${ev.done} / ${ev.total} (${pct.toFixed(1)}%)`;
    } else {
      el.busyIndicator.textContent = `${stageTitle}：${ev.detail || "请稍候…"}`;
      el.actionProgressPercent.textContent = "";
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

  on("unknown-confirm", async (prompt: unknown) => {
    const text = typeof prompt === "string" ? prompt : "当前游戏版本在云端尚未收录原版备份，无法保证当前客户端文件是官方原版，继续汉化可能导致异常或封号风险。是否仍要继续汉化？";
    const ok = await showConfirm("未知版本提示", text, "仍要继续汉化");
    try {
      await call("AnswerUnknownConfirm", ok);
    } catch (error) {
      log(`确认反馈失败: ${describeError(error)}`, "error");
    }
  });

  on("font-confirm", async (prompt: unknown) => {
    const text = typeof prompt === "string" ? prompt : "当前字体是官方原版，需要替换字体才能显示中文。是否继续？";
    const ok = await showConfirm("字体替换提示", text, "继续汉化（不替换字体）");
    try {
      await call("AnswerFontConfirm", ok);
    } catch (error) {
      log(`确认反馈失败: ${describeError(error)}`, "error");
    }
  });

  on("launcher-confirm", async (prompt: unknown) => {
    const text = typeof prompt === "string" ? prompt : "检测到游戏启动器正在运行，是否强制结束启动器并继续？";
    const ok = await showConfirm("启动器运行提示", text, "强制结束启动器并继续");
    try {
      await call("AnswerLauncherConfirm", ok);
    } catch (error) {
      log(`确认反馈失败: ${describeError(error)}`, "error");
    }
  });
}

void bootstrapApp();
