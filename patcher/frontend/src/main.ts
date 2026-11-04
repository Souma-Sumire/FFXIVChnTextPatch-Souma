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

interface AppConfig {
  gameDirs: Record<string, string>;
  language: string;
  targetLanguages: Record<string, string[]>;
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
  offlineReason?: string;
  rules: RuleMeta[];
  languages: LanguageOption[];
  regionLanguages?: Record<string, RegionLanguage[]>;
  config: AppConfig;
  folders: Record<string, string>;
  tableList: TableEntry[];
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

interface ImportResourceResult {
  success: boolean;
  cancelled: boolean;
  message: string;
  resourceTime?: string;
  resourceHash?: string;
  tableCount?: number;
}

interface GoApp {
  GetAppVersion(): Promise<string>;
  CheckAppUpdate(): Promise<AppUpdateInfo>;
  InstallAppUpdate(): Promise<void>;
  RestartApp(): Promise<void>;
  ImportOfflineResource(filePath?: string): Promise<ImportResourceResult>;
  GetBootstrap(): Promise<BootstrapInfo>;
  SaveConfig(config: AppConfig): Promise<void>;
  DetectGamePaths(language: string): Promise<string[]>;
  CancelDetect(): Promise<void>;
  SelectGameDirectory(language: string): Promise<string>;
  GetGameInfo(gameDir: string, language: string): Promise<GameInfo>;
  CheckClientStatus(gameDir: string, language: string): Promise<ClientStatusResult>;
  StartPatch(req: PatchRequest): Promise<PatchResponse>;
  CancelPatch(): Promise<void>;
  AnswerFontConfirm(ok: boolean): Promise<void>;
  AnswerLauncherConfirm(ok: boolean): Promise<void>;
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
let isDetecting = false;
let detectCanceled = false;
let latestUpdateInfo: AppUpdateInfo | null = null;
let checkingAppUpdate = false;

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
};

const el = {
  viewProvisioning: byId("view-provisioning"),
  viewWorkbench: byId("view-workbench"),
  viewTables: byId("view-tables"),

  provisionDetail: byId("provision-detail"),
  provisionProgressBar: byId("provision-progress-bar"),
  provisionMeta: byId("provision-meta"),
  provisionOfflineWrap: byId("provision-offline-wrap"),
  btnProvisionImportOffline: byId<HTMLButtonElement>("btn-provision-import-offline"),

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
  resourceBuildMeta: byId("resource-build-meta"),
  resourceBuildTime: byId("resource-build-time"),
  btnImportOffline: byId<HTMLButtonElement>("btn-import-offline"),
  rules: byId("rules"),

  btnOpenTables: byId<HTMLButtonElement>("btn-open-tables"),
  mainPreset: byId<HTMLSelectElement>("main-preset"),

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
  el.mainPreset.disabled = !isEnabled || busy;
  el.btnOpenTables.disabled = !isEnabled || busy;
  const scopeControl = el.mainPreset.closest(".scope-control");
  scopeControl?.classList.toggle("disabled", !isEnabled);
  el.resourceBuildMeta?.classList.toggle("disabled", !isEnabled);
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
    modalResolver = resolve;
    el.modalTitle.textContent = title;
    el.modalText.textContent = text;
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

function setOfflineState(offline: boolean, reason?: string): void {
  el.btnImportOffline.classList.toggle("hidden", !offline);
  el.provisionOfflineWrap?.classList.toggle("hidden", !offline);
  if (offline && !el.viewProvisioning.classList.contains("hidden")) {
    el.provisionDetail.textContent = reason
      ? `连接云端或下载资源失败：${reason}`
      : "连接云端或下载资源失败，已进入离线状态";
    el.provisionMeta.textContent = "可点击下方按钮选择本地离线资源包 (*.zip) 导入";
  }
}

async function handleImportOfflineResource(): Promise<void> {
  if (busy) return;
  try {
    setBusy(true, "正在导入离线资源包…");
    const res = await call("ImportOfflineResource", "");
    if (res.cancelled) {
      return;
    }
    if (res.success) {
      log(`[离线包] ${res.message}，汉化资源已就绪。`, "ok");
      setOfflineState(false);
      await loadBootstrap();
      if (currentGameDir()) {
        await refreshGameInfo();
        void checkClientEnvironment();
      }
      switchView("workbench");
      await showConfirm("导入成功", `成功导入离线资源包！\n共收录 ${res.tableCount ?? 0} 个数据表文件，汉化资源已就绪。`, "开始使用");
    }
  } catch (err) {
    log(`[错误] 导入离线资源包失败: ${describeError(err)}`, "error");
    await showConfirm("导入失败", `离线资源包解析失败: ${describeError(err)}`, "确定");
  } finally {
    setBusy(false);
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
    setOfflineState(true, describeError(error));
    switchView("workbench");
  }
}

function updateResourceBuildMeta(time?: string, _hash?: string): void {
  if (!el.resourceBuildTime) {
    return;
  }
  if (time) {
    el.resourceBuildTime.textContent = time;
  } else {
    el.resourceBuildTime.textContent = "--";
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
  setWindowTitle();
  if (el.versionBadge) {
    el.versionBadge.textContent = `v${info.version}`;
  }
  updateResourceBuildMeta(info.resourceTime, info.resourceHash);
  setOfflineState(info.offline, info.offlineReason);
  el.gamePath.value = currentGameDir();
  el.replaceText.checked = config.replaceText;
  el.replaceFont.checked = config.replaceFont;
  updateTextScopeState();
  updateFontUiAssetState();

  renderRules();
  renderLanguages();
  renderTargetLangs();

  const activePreset = config.preset && config.preset !== "custom" ? config.preset : "recommended";
  applyPreset(activePreset, false);

  renderTables();
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
    notice.textContent = `规则数据不可用，已停用：${unavailable.map((rule) => rule.name).join("、")}。请同步资源，或改用完整的离线资源包。`;
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

function createClientStatusItem(): HTMLDivElement {
  const isGlobal = isGlobalLanguage(config.language);
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
    } else if (clientStatus.status === "modified" || clientStatus.status === "mismatch") {
      cls = "warn";
    } else if (clientStatus.label === "文件损坏" || clientStatus.label === "文件缺失") {
      cls = "err";
    } else if (clientStatus.label === "未收录版本") {
      cls = "warn";
    } else if (clientStatus.status === "unsupported") {
      cls = "muted";
    }
    v.className = `diag-v ${cls}`.trim();
    v.textContent = clientStatus.label;
    v.title = clientStatus.description;
    btn.textContent = "重测";
    btn.style.display = clientStatus.status === "unsupported" ? "none" : "";
  } else if (!isGlobal) {
    v.className = "diag-v muted";
    v.textContent = "无法校验";
    btn.style.display = "none";
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

  if (gameInfo.resourceTime) {
    updateResourceBuildMeta(gameInfo.resourceTime, gameInfo.resourceHash);
  }

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

function markCustomPreset(): void {
  if (config.preset !== "custom") {
    config.preset = "custom";
    el.preset.value = "custom";
    el.mainPreset.value = "custom";
    void persistConfig();
  }
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
  el.btnImportOffline.addEventListener("click", () => void handleImportOfflineResource());
  el.btnProvisionImportOffline.addEventListener("click", () => void handleImportOfflineResource());

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

  document.addEventListener("click", () => {
    toggleTargetLangMenu(false);
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
      el.progressBar.style.width = "100%";
      try {
        clientStatus = await call("CheckClientStatus", currentGameDir(), config.language);
      } catch {
        // 忽略静默更新失败
      }
      await refreshGameInfo();
    } catch (error) {
      log(`汉化失败: ${describeError(error)}`, "error");
    } finally {
      isPatching = false;
      isStopping = false;
      el.btnPatch.classList.remove("is-patching", "is-stopping");
      if (el.btnPatchText) el.btnPatchText.textContent = "开始汉化";
      setBusy(false);
    }
  });
}

function wireRuntimeEvents(): void {
  const on = window.runtime?.EventsOn;
  if (!on) {
    return;
  }

  on("resource-offline", (offline: unknown, reason?: unknown) => {
    const isOff = typeof offline === "boolean" ? offline : true;
    const reasonStr = typeof reason === "string" && reason ? reason : undefined;
    setOfflineState(isOff, reasonStr);
  });

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

  let pendingResourceProgress: PatchProgressEvent | null = null;
  let resourceRafId = 0;
  on("resource-progress", (data: unknown) => {
    const ev = asProgress(data);
    if (ev.stage === "rawexd" || ev.stage === "font" || ev.stage === "meta" || ev.stage === "done") {
      setOfflineState(false);
    }
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
