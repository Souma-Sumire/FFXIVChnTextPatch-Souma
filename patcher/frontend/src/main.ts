import { Call, Events, Window as WailsWindow } from "@wailsio/runtime";
import "./style.css";
import {
  applyReplacementRowEdit,
  formatReplacementKeyHex,
  formatReplacementsToText,
  nextReplacementKey,
  parseReplacementKey,
  parseTextToReplacements,
} from "./codepoint_replacements";
import { GAME_FONT_FACES, GAME_FONT_METRICS } from "./font_data";
import { initTooltips, setTip } from "./tooltip";

const IS_ELEMENT_EDITOR_SUBWINDOW = new URLSearchParams(window.location.search).get("window") === "element-editor";
const WAILS_APP_SERVICE = "main.App";

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
  fontPresets?: Record<string, string>;
  fontSource?: "package" | "compile";
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

interface PresetSummary {
  id: string;
  name: string;
  description: string;
  builtin: boolean;
  /** 仅自定义方案有值：文件大小与修改时间。 */
  size?: number;
  updatedAt?: string;
}

interface FontLookup {
  name?: string;
  familyName?: string;
  weight?: number;
  stretch?: number;
  style?: number;
  features?: string[];
  filename?: string;
  [key: string]: unknown;
}

interface FontTransform {
  rotation?: number;
  scaleX?: number;
  scaleY?: number;
  skew?: number;
  [key: string]: unknown;
}

interface FontWrapModifiers {
  baselineShift?: number;
  verticalOffset?: number;
  horizontalOffset?: number;
  letterSpacing?: number;
  codepoints?: number[][];
  codepointReplacements?: Record<string, string>;
  [key: string]: unknown;
}

interface DirectWriteSpecific {
  gridFitMode?: number;
  measureMode?: number;
  renderMode?: number;
  [key: string]: unknown;
}

interface FreeTypeSpecific {
  forceAutohint?: boolean;
  noAutohint?: boolean;
  noBitmap?: boolean;
  noHinting?: boolean;
  renderMode?: number;
  [key: string]: unknown;
}

interface EmptySpecific {
  ascent?: number;
  lineHeight?: number;
  [key: string]: unknown;
}

interface FontRenderSpecific {
  directwrite?: DirectWriteSpecific;
  freetype?: FreeTypeSpecific;
  empty?: EmptySpecific;
  [key: string]: unknown;
}

interface FontElement {
  size?: number;
  gamma?: number;
  renderer?: number;
  source?: number;
  mergeMode?: number;
  lookup?: FontLookup;
  transform?: FontTransform;
  transformationMatrix?: number[];
  wrapModifiers?: FontWrapModifiers;
  renderSpecific?: FontRenderSpecific;
  [key: string]: unknown;
}

interface FontFace {
  name: string;
  previewText?: string;
  verticalAlignment?: string;
  elements: FontElement[];
  [key: string]: unknown;
}

interface FontSet {
  discardStep?: number;
  expectedTexCount?: number;
  sideLength?: number;
  texFilenameFormat?: string;
  faces?: FontFace[];
  fonts?: FontFace[];
  [key: string]: unknown;
}

interface MultiFontSet extends FontSet {
  exportMapChnAxisToFont?: boolean;
  exportMapFontLobbyToFont?: boolean;
  exportMapKrnAxisToFont?: boolean;
  exportMapTcAxisToFont?: boolean;
  fontSets?: FontSet[];
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
  // devMode 为 true 时是本地开发构建：内置词条直接读工作区 resource/，不联网同步。
  devMode: boolean;
  rules: RuleMeta[];
  languages: LanguageOption[];
  regionLanguages?: Record<string, RegionLanguage[]>;
  config: AppConfig;
  folders: Record<string, string>;
  csv: CSVContext;
  excluded?: string[];
  resourceTime?: string;
  resourceHash?: string;
  fontCompilerReady?: boolean;
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

interface ClientPartStatus {
  status: string;
  label: string;
  description?: string;
}

interface ClientStatusResult {
  status: "original" | "modified" | "mismatch" | "unknown";
  label: string;
  description: string;
  text?: ClientPartStatus;
  font?: ClientPartStatus;
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

// FontBuildInfo 是「编译」按钮与开始汉化的放行依据：ready 为假时说明当前方案
// 还没有可用的贴图，reason 里是给用户看的原因。
interface FontBuildInfo {
  ready: boolean;
  reason: string;
  presetId: string;
  texCount: number;
  sideLength: number;
  compiledAt: string;
}

interface GoApp {
  GetAppVersion(): Promise<string>;
  GetDiagnostics(): Promise<string[]>;
  CheckAppUpdate(): Promise<AppUpdateInfo>;
  InstallAppUpdate(): Promise<void>;
  RestartApp(): Promise<void>;
  GetBootstrap(): Promise<BootstrapInfo>;
  GetRules(): Promise<RuleMeta[]>;
  SaveConfig(config: AppConfig): Promise<void>;
  SyncBuiltinResource(): Promise<void>;
  EnsureTableDoc(): Promise<boolean>;
  GetCSVContext(language: string): Promise<CSVContext>;
  SetCSVSelection(language: string, path: string): Promise<CSVContext>;
  RemoveCSVDirectory(language: string, path: string, deleteFiles: boolean): Promise<CSVContext>;
  SetCSVDirectoryMeta(language: string, path: string, title: string, desc: string): Promise<CSVContext>;
  OpenCSVDirectory(language: string, path: string): Promise<void>;
  OpenBackupDirectory(language: string): Promise<void>;
  DetectGamePaths(language: string): Promise<string[]>;
  CancelDetect(): Promise<void>;
  SelectGameDirectory(language: string): Promise<string>;
  GetGameInfo(gameDir: string, language: string): Promise<GameInfo>;
  CheckClientStatus(gameDir: string, language: string): Promise<ClientStatusResult>;
  StartPatch(req: PatchRequest): Promise<PatchResponse>;
  CancelPatch(): Promise<void>;
  AnswerUnknownConfirm(ok: boolean): Promise<void>;
  AnswerLauncherConfirm(ok: boolean): Promise<void>;
  CopyBuiltinCSVDirectory(language: string, convert: boolean): Promise<CSVContext>;
  RestoreOriginal(gameDir: string, language: string, restoreText: boolean, restoreFont: boolean): Promise<RestoreResponse>;
  OpenLink(url: string): Promise<void>;
  GetFontPresets(language: string): Promise<PresetSummary[]>;
  GetFontPreset(presetId: string): Promise<string>;
  SaveFontPreset(language: string, name: string, configJson: string): Promise<PresetSummary>;
  SelectFontPreset(language: string, presetId: string): Promise<void>;
  RenameFontPreset(language: string, presetId: string, newName: string): Promise<PresetSummary>;
  DeleteFontPreset(language: string, presetId: string): Promise<void>;
  GetSystemFonts(): Promise<string[]>;
  EnsureFontCompiler(): Promise<void>;
  GetFontBuildInfo(): Promise<FontBuildInfo>;
  CompileFontPreset(): Promise<FontBuildInfo>;
  RenderFontPreview(configJson: string, fontName: string, text: string, maxWidth: number): Promise<string>;
  AutoFitFontPreset(configJson: string, fontName: string, elementIndices: number[]): Promise<string>;
  ExportFontTTMP(configJson: string, savePath: string): Promise<void>;
  ExportFontRaw(configJson: string, outDir: string): Promise<void>;
  SelectRawExportDir(): Promise<string>;
  GetFontTexture16Bit(): Promise<boolean>;
  SetFontTexture16Bit(enable: boolean): Promise<void>;
  SelectFontFile(): Promise<string>;
  SelectFontJsonFile(): Promise<string>;
  SelectFontJsonSavePath(defaultName: string): Promise<string>;
  SelectTTMPSavePath(): Promise<string>;
  ReadTextFile(filePath: string): Promise<string>;
  WriteTextFile(filePath: string, content: string): Promise<void>;
  OpenElementEditorWindow(title: string, stateJson: string): Promise<void>;
  SyncElementEditorWindow(title: string, stateJson: string): Promise<void>;
  HideElementEditorWindow(): Promise<void>;
  GetElementEditorState(): Promise<string>;
}

// 启动握手会在后端就绪前反复失败，逐次记录只会刷屏；最终结果由 waitForBackend 抛出。
const quietCallFailures = new Set<string>(["GetAppVersion"]);

async function call<K extends keyof GoApp>(
  name: K,
  ...args: Parameters<GoApp[K]>
): Promise<Awaited<ReturnType<GoApp[K]>>> {
  try {
    return (await Call.ByName(`${WAILS_APP_SERVICE}.${String(name)}`, ...args)) as Awaited<ReturnType<GoApp[K]>>;
  } catch (error) {
    if (!quietCallFailures.has(String(name))) {
      log(`${DIAG_PREFIX}调用 ${String(name)} 失败：${describeError(error)}；参数=${summarizeCallArgs(args)}`, "diag");
    }
    throw error;
  }
}

function summarizeCallArgs(args: unknown[]): string {
  try {
    const text = JSON.stringify(args);
    if (text.length <= 2000) return text;
    return `${text.slice(0, 2000)}…（共 ${text.length} 字符）`;
  } catch {
    return "无法序列化";
  }
}

function byId<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id);
  if (!node) {
    throw new Error(`页面缺少关键节点 #${id}`);
  }
  return node as T;
}

type LogLevel = "info" | "ok" | "warn" | "error" | "diag";

// 与后端 verifier.DiagPrefix 一致，改一处要同步另一处。
const DIAG_PREFIX = "[诊断] ";

interface LogEntry {
  id: number;
  // 面板显示用（时:分:秒）；stamp 带日期，只在复制时使用。
  time: string;
  stamp: string;
  level: LogLevel;
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

// 会话头不参与裁剪，复制日志时始终带上；DOM 只保留有限行以免拖慢界面。
const LOG_DOM_LIMIT = 500;
const LOG_HISTORY_LIMIT = 10000;
const headerEntries: LogEntry[] = [];
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
  btnOpenFontEditor: byId<HTMLButtonElement>("btn-open-font-editor"),
  btnOpenFontEditorText: byId("btn-open-font-editor-text"),
  fontCompilerBar: byId("font-compiler-bar"),
  fontCompileGroup: byId("font-compile-group"),
  btnCompileFont: byId<HTMLButtonElement>("btn-compile-font"),
  btnCompileFontText: byId("btn-compile-font-text"),
  fontBuildProgress: byId("font-build-progress"),
  fontBuildBar: byId("font-build-bar"),
  fontBuildStatus: byId("font-build-status"),
  fontPresetSummary: byId("font-preset-summary"),
  fontSource: byId<HTMLSelectElement>("font-source"),
  fontEditorAlert: byId("font-editor-alert"),
  fontEditorAlertText: byId("font-editor-alert-text"),
  csvPicker: byId("csv-picker"),
  csvTrigger: byId<HTMLButtonElement>("csv-select-trigger"),
  csvSummary: byId("csv-select-summary"),
  csvSyncBar: byId("csv-sync-bar"),
  csvMenu: byId("csv-select-menu"),
  contentSection: byId("content-section"),
  rulesPicker: byId("rules-picker"),
  rulesTrigger: byId<HTMLButtonElement>("rules-trigger"),
  rulesSummary: byId("rules-summary"),
  rulesMenu: byId("rules-menu"),
  rules: byId("rules"),

  btnOpenTables: byId<HTMLButtonElement>("btn-open-tables"),
  scopeSummary: byId("scope-summary"),

  btnPatch: byId<HTMLButtonElement>("btn-patch"),
  btnPatchText: byId("btn-patch-text"),
  btnRestore: byId<HTMLButtonElement>("btn-restore"),
  restoreReason: byId("restore-reason"),

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

  // 字体定制工作台节点 (复刻 FFXIV-FontChanger)
  viewFontEditor: byId("view-font-editor"),
  btnBackWorkbenchFont: byId<HTMLButtonElement>("btn-back-workbench-font"),
  fcPresetSelect: byId<HTMLSelectElement>("fc-preset-select"),
  fontRegionBadge: byId("font-region-badge"),
  fontRegionWarning: byId("font-region-warning"),
  fcCheck16bit: byId("fc-check-16bit"),
  fontFaceTabs: byId("font-face-tabs"),
  elementsTableViewport: byId("elements-table-viewport"),
  fontElementList: byId("font-element-list"),
  previewCustomText: byId<HTMLTextAreaElement>("preview-custom-text"),
  previewCanvasBox: byId("preview-canvas-box"),
  previewStage: byId("fc-preview-stage"),
  previewImage: byId<HTMLImageElement>("fc-preview-image"),
  previewHint: byId("fc-preview-hint"),
  previewLoadingOverlay: byId("preview-loading-overlay"),
  fontPresetImportPicker: byId<HTMLInputElement>("font-preset-import-picker"),
  presetModal: byId("preset-modal"),
  presetManagerList: byId("preset-manager-list"),
  presetModalRegion: byId("preset-modal-region"),
  btnPresetClose: byId<HTMLButtonElement>("btn-preset-close"),
  btnPresetImport: byId<HTMLButtonElement>("btn-preset-import"),
  btnPresetNewEmpty: byId<HTMLButtonElement>("btn-preset-new-empty"),
  btnPresetCloseFoot: byId<HTMLButtonElement>("btn-preset-close-foot"),

  // FFXIV-FontChanger 分割条与菜单/右键菜单
  fcSplitterV: byId("fc-splitter-v"),
  fcFontListContainer: byId("fc-font-list-container"),
  fcElementsSection: byId("fc-elements-section"),
  fcSplitter1: byId("fc-splitter-1"),
  fcPreviewEditWrap: byId("fc-preview-edit-wrap"),
  fcSplitter2: byId("fc-splitter-2"),
  fcFontListContextMenu: byId("fc-fontlist-context-menu"),
  fcElementContextMenu: byId("fc-element-context-menu"),

  // 字体元素编辑器模态框节点 (IDD_FONTELEMENTEDITOR)
  fontElementModal: byId("font-element-modal"),
  elemTabFont: byId("elem-tab-font"),
  elemTabChars: byId("elem-tab-chars"),
  elemTabTypo: byId("elem-tab-typo"),
  elemTabMerge: byId("elem-tab-merge"),
  propsGameFontNotice: byId("props-game-font-notice"),
  propsEmptyFontNotice: byId("props-empty-font-notice"),
  propsExternalFontWrap: byId("props-external-font-wrap"),
  propFontFamily: byId<HTMLSelectElement>("prop-font-family"),
  propFontFile: byId<HTMLInputElement>("prop-font-file"),
  btnBrowseFontFile: byId<HTMLButtonElement>("btn-browse-font-file"),
  propRenderer: byId<HTMLSelectElement>("prop-renderer"),
  propMergeMode: byId<HTMLSelectElement>("prop-merge-mode"),
  propSize: byId<HTMLInputElement>("prop-size"),
  propWeight: byId<HTMLSelectElement>("prop-weight"),
  propStyle: byId<HTMLSelectElement>("prop-style"),
  propStretch: byId<HTMLSelectElement>("prop-stretch"),
  propGamma: byId<HTMLInputElement>("prop-gamma"),
  propBaselineShift: byId<HTMLInputElement>("prop-baseline-shift"),
  propHorizontalOffset: byId<HTMLInputElement>("prop-horizontal-offset"),
  propLetterSpacing: byId<HTMLInputElement>("prop-letter-spacing"),
  propScaleX: byId<HTMLInputElement>("prop-scale-x"),
  propScaleY: byId<HTMLInputElement>("prop-scale-y"),
  propSkew: byId<HTMLInputElement>("prop-skew"),
  propRotation: byId<HTMLInputElement>("prop-rotation"),
  propsDirectwriteGroup: byId("props-directwrite-group"),
  propDwGridfit: byId<HTMLSelectElement>("prop-dw-gridfit"),
  propDwMeasure: byId<HTMLSelectElement>("prop-dw-measure"),
  propDwRender: byId<HTMLSelectElement>("prop-dw-render"),
  propsFreetypeGroup: byId("props-freetype-group"),
  propFtRender: byId<HTMLSelectElement>("prop-ft-render"),
  propFtNohinting: byId<HTMLInputElement>("prop-ft-nohinting"),
  propFtAutohint: byId<HTMLInputElement>("prop-ft-autohint"),
  propFtNobitmap: byId<HTMLInputElement>("prop-ft-nobitmap"),
  propsEmptyGroup: byId("props-empty-group"),
  propEmptyAscent: byId<HTMLInputElement>("prop-empty-ascent"),
  propEmptyLineheight: byId<HTMLInputElement>("prop-empty-lineheight"),
  cpTotalSummary: byId("cp-total-summary"),
  btnCpClear: byId<HTMLButtonElement>("btn-cp-clear"),
  cpIntervalsBox: byId("cp-intervals-box"),
  propCodepointsText: byId<HTMLInputElement>("prop-codepoints-text"),
  cpCustomInput: byId<HTMLInputElement>("cp-custom-input"),
  btnCpCustomAdd: byId<HTMLButtonElement>("btn-cp-custom-add"),
  btnCpCustomSub: byId<HTMLButtonElement>("btn-cp-custom-sub"),
  otFeaturesGrid: byId("ot-features-grid"),
  propOtCustom: byId<HTMLInputElement>("prop-ot-custom"),
  btnMergePresetChsTime: byId<HTMLButtonElement>("btn-merge-preset-chs-time"),
  btnMergePresetEnTime: byId<HTMLButtonElement>("btn-merge-preset-en-time"),
  btnMergeClear: byId<HTMLButtonElement>("btn-merge-clear"),
  mergeReplacementsList: byId("merge-replacements-list"),
  btnMergeAddRule: byId<HTMLButtonElement>("btn-merge-add-rule"),
  btnMergeToggleRaw: byId<HTMLButtonElement>("btn-merge-toggle-raw"),
  mergeRawContainer: byId("merge-raw-container"),
  propReplacementsJson: byId<HTMLTextAreaElement>("prop-replacements-json"),
  btnLeverBypass: byId<HTMLButtonElement>("btn-lever-bypass"),
  btnLeverCompare: byId<HTMLButtonElement>("btn-lever-compare"),
  btnElemModalCancel: byId<HTMLButtonElement>("btn-elem-modal-cancel"),
  btnElemModalOk: byId<HTMLButtonElement>("btn-elem-modal-ok"),

  modal: byId("modal"),
  modalTitle: byId("modal-title"),
  modalText: byId("modal-text"),
  modalOk: byId<HTMLButtonElement>("modal-ok"),
  modalCancel: byId<HTMLButtonElement>("modal-cancel"),
  modalAlt: byId<HTMLButtonElement>("modal-alt"),
  modalOptions: byId("modal-options"),
  restoreTextCheck: byId<HTMLInputElement>("restore-text-check"),
  restoreFontCheck: byId<HTMLInputElement>("restore-font-check"),

  toast: byId("app-toast"),
  toastTitle: byId("app-toast-title"),
  toastDesc: byId("app-toast-desc"),
  toastAction: byId<HTMLButtonElement>("app-toast-action"),
  toastClose: byId<HTMLButtonElement>("app-toast-close"),
};

const FONT_EDITOR_MIN_WIDTH = 1160;
const FONT_EDITOR_MIN_HEIGHT = 760;
let savedWorkbenchWindowSize: { w: number; h: number } | null = null;

async function adjustWindowForView(view: "workbench" | "tables" | "font-editor"): Promise<void> {
  try {
    const [isMax, isFull] = await Promise.all([WailsWindow.IsMaximised(), WailsWindow.IsFullscreen()]);
    if (isMax || isFull) return;

    if (view === "font-editor") {
      const cur = await WailsWindow.Size();
      const availW = window.screen?.availWidth ? Math.max(900, window.screen.availWidth - 40) : FONT_EDITOR_MIN_WIDTH;
      const availH = window.screen?.availHeight ? Math.max(640, window.screen.availHeight - 48) : FONT_EDITOR_MIN_HEIGHT;
      const targetW = Math.min(FONT_EDITOR_MIN_WIDTH, availW);
      const targetH = Math.min(FONT_EDITOR_MIN_HEIGHT, availH);
      if (cur.width < targetW || cur.height < targetH) {
        if (!savedWorkbenchWindowSize) {
          savedWorkbenchWindowSize = { w: cur.width, h: cur.height };
        }
        const nextW = Math.max(cur.width, targetW);
        const nextH = Math.max(cur.height, targetH);
        await WailsWindow.SetSize(nextW, nextH);
        if (
          window.screen?.availWidth &&
          window.screen?.availHeight &&
          (window.screenX + nextW > window.screen.availWidth || window.screenY + nextH > window.screen.availHeight)
        ) {
          await WailsWindow.Center();
        }
      }
    } else if (view === "workbench" && savedWorkbenchWindowSize) {
      const prev = savedWorkbenchWindowSize;
      savedWorkbenchWindowSize = null;
      await WailsWindow.SetSize(prev.w, prev.h);
    }
  } catch {
    // 忽略窗口尺寸调整异常
  }
}

function switchView(view: "workbench" | "tables" | "font-editor"): void {
  el.viewWorkbench.classList.toggle("hidden", view !== "workbench");
  el.viewTables.classList.toggle("hidden", view !== "tables");
  el.viewFontEditor.classList.toggle("hidden", view !== "font-editor");
  void adjustWindowForView(view);
}

function parseLogKind(message: string, explicitKind?: LogLevel): LogLevel {
  if (message.startsWith(DIAG_PREFIX)) {
    return "diag";
  }
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
  if (message.includes("成功") || message.includes("已是最新") || message.includes("已还原") || message.includes("已更新") || message.includes("编译完成") || message.includes("已就绪") || message.includes("[完成]")) {
    return "ok";
  }
  return "info";
}

// 面板只显示时分秒：日期会把宽约 90px 的时间列占掉，正文跟着变窄；
// 复制时用带日期的 logStamp，贴出去还能对时。
function logClock(date = new Date()): string {
  return date.toLocaleTimeString("zh-CN", { hour12: false });
}

function logStamp(date = new Date()): string {
  return date.toLocaleString("zh-CN", {
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false,
  });
}

function makeLogEntry(message: string, kind?: LogLevel): LogEntry {
  const now = new Date();
  return {
    id: nextLogId++,
    time: logClock(now),
    stamp: logStamp(now),
    level: parseLogKind(message, kind),
    tag: "",
    message,
  };
}

function appendLogLine(entry: LogEntry): void {
  const line = document.createElement("div");
  line.className = `log-line ${entry.level === "info" ? "" : entry.level}`.trim();
  line.dataset.level = entry.level;

  const timeEl = document.createElement("span");
  timeEl.className = "time";
  timeEl.textContent = entry.time;
  line.append(timeEl);

  const textEl = document.createElement("span");
  textEl.className = "log-text";
  textEl.textContent = entry.message;
  line.append(textEl);

  el.log.append(line);
  while (el.log.childElementCount > LOG_DOM_LIMIT) {
    el.log.firstElementChild?.remove();
  }
  el.log.scrollTop = el.log.scrollHeight;
}

function log(message: string, kind?: LogLevel): void {
  const entry = makeLogEntry(message, kind);
  logEntries.push(entry);
  if (logEntries.length > LOG_HISTORY_LIMIT) {
    logEntries.splice(0, logEntries.length - LOG_HISTORY_LIMIT);
  }
  // 诊断行只进复制内容，平时不占日志窗口。
  if (entry.level !== "diag") {
    appendLogLine(entry);
  }
}

// 会话快照只进复制内容：面板上不显示，否则启动就把日志窗口填满。
function logHeader(lines: string[]): void {
  for (const line of lines) {
    headerEntries.push(makeLogEntry(line, "diag"));
  }
}

const APPENDIX_TABLE_SAMPLE = 8;

// 后端已把路径里的用户名换成占位符，这里兜底界面日志：用户选的目录、
// 探测到的路径都不经过后端。两种写法各处理一次，已替换过的不会再命中。
function redactText(text: string): string {
  return text
    .replace(/[a-z]:[\\/]+(?:users|documents and settings)[\\/]+[^\\/\s"',|]+/gi, "%USERPROFILE%")
    .replace(/(^|[\s"'(=])\/(?:home|users)\/[^/\s"',|]+/gi, "$1~");
}

// 反馈页地址：模板里第一栏写现象，第二栏粘调试信息。
const FEEDBACK_ISSUE_URL = "https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/issues/new?template=bug_report.yml";

let lastClientCheckLog = "";
let copyFlashTimer = 0;
let toastTimer = 0;
let toastActionHandler: (() => void) | null = null;
let toastHovered = false;
let toastDuration = 0;

function partStatusText(part?: ClientPartStatus): string {
  if (!part) return "无";
  return `${part.label}(${part.status})`;
}

function formatClientStatusSummary(status: ClientStatusResult): string {
  const detail = status.description ? ` | 说明=${status.description}` : "";
  return `${status.label}(${status.status}) | 文本=${partStatusText(status.text)} | 字体=${partStatusText(status.font)}${detail}`;
}

function formatReplaceState(): string {
  if (config.replaceText && config.replaceFont) return "文本+字体";
  if (config.replaceText) return "仅文本";
  if (config.replaceFont) return "仅字体";
  return "全关";
}

function formatEnabledRules(rules?: Record<string, boolean>): string {
  if (!rules) return "默认";
  const enabled = Object.keys(rules)
    .filter((k) => Boolean(rules[k]))
    .sort();
  return enabled.length > 0 ? enabled.join(",") : "全关";
}

function diagnosticAppendix(): string[] {
  const fontReg = getCurrentFontRegion();
  const fontPreset = config.fontPresets?.[fontReg] || config.fontPresets?.[fontReg.toLowerCase()] || "默认";
  const presetPart = config.preset === "custom" && selected.size > 0
    ? `custom(${selected.size}表, 示例=${Array.from(selected).sort().slice(0, APPENDIX_TABLE_SAMPLE).join(",")})`
    : `${config.preset}(${selected.size}表)`;
  const lines: string[] = [
    `当前界面：区服=${config.language} | 替换=${formatReplaceState()} | 预设=${presetPart} | 语言=${targetLangs.join(",") || "默认"} | 词条=${bootstrap?.csv?.selected || "内置"} | 字体=${config.fontSource || "package"}/${fontPreset}/16位=${fontTexture16BitEnabled} | 规则=${formatEnabledRules(config.rules)}`,
  ];
  if (!clientStatus) {
    lines.push("当前客户端：尚未校验");
  } else {
    const summary = formatClientStatusSummary(clientStatus);
    if (summary !== lastClientCheckLog) {
      lines.push(`当前客户端：${summary}`);
    }
  }
  return lines;
}

function formatLogEntry(entry: LogEntry): string {
  let message = entry.level === "diag" && entry.message.startsWith(DIAG_PREFIX)
    ? entry.message.slice(DIAG_PREFIX.length)
    : entry.message;
  if (entry.level === "warn" && message.startsWith("[警告] ")) {
    message = message.slice("[警告] ".length);
  } else if (entry.level === "error" && message.startsWith("[错误] ")) {
    message = message.slice("[错误] ".length);
  }
  return `[${entry.stamp}] [${entry.level.toUpperCase()}] ${entry.tag ? `[${entry.tag}] ` : ""}${message}`;
}

function buildLogText(): string {
  const lines = [...headerEntries, ...logEntries].map((entry) => redactText(formatLogEntry(entry)));
  const stamp = logStamp();
  for (const line of diagnosticAppendix()) {
    lines.push(`[${stamp}] [DIAG] ${redactText(line)}`);
  }
  const safeLines = lines.map((line) => line.replace(/```/g, "` ` `"));
  return ["```text", ...safeLines, "```"].join("\n");
}

function flashCopyButton(): void {
  window.clearTimeout(copyFlashTimer);
  el.btnCopyLog.classList.add("is-copied");
  if (el.btnCopyLogText) {
    el.btnCopyLogText.textContent = "已复制";
  }
  copyFlashTimer = window.setTimeout(() => {
    el.btnCopyLog.classList.remove("is-copied");
    if (el.btnCopyLogText) {
      el.btnCopyLogText.textContent = "复制调试信息";
    }
    copyFlashTimer = 0;
  }, 2500);
}

function hideToast(): void {
  window.clearTimeout(toastTimer);
  toastTimer = 0;
  toastActionHandler = null;
  el.toast.classList.add("hidden");
}

function scheduleToastHide(durationMs: number): void {
  window.clearTimeout(toastTimer);
  if (durationMs <= 0 || toastHovered) return;
  toastTimer = window.setTimeout(() => hideToast(), durationMs);
}

function showToast(opts: {
  kind: "ok" | "error";
  title: string;
  description?: string;
  actionText?: string;
  onAction?: () => void;
  durationMs?: number;
}): void {
  el.toast.classList.toggle("is-error", opts.kind === "error");
  el.toastTitle.textContent = opts.title;
  el.toastDesc.textContent = opts.description ?? "";
  el.toastDesc.classList.toggle("hidden", !opts.description);
  if (opts.actionText && opts.onAction) {
    toastActionHandler = opts.onAction;
    el.toastAction.textContent = opts.actionText;
    el.toastAction.classList.remove("hidden");
  } else {
    toastActionHandler = null;
    el.toastAction.classList.add("hidden");
  }
  toastDuration = opts.durationMs ?? 4500;
  el.toast.classList.remove("hidden");
  scheduleToastHide(toastDuration);
}

async function openFeedbackPage(): Promise<void> {
  try {
    await call("OpenLink", FEEDBACK_ISSUE_URL);
  } catch (error) {
    log(`[错误] 打开反馈页失败: ${describeError(error)}；可手动访问 ${FEEDBACK_ISSUE_URL}`, "error");
  }
}

async function copyDiagnostics(): Promise<void> {
  let text = "";
  try {
    text = buildLogText();
  } catch (error) {
    const msg = describeError(error);
    log(`[错误] 生成调试信息失败: ${msg}`, "error");
    showToast({ kind: "error", title: "生成调试信息失败", description: msg });
    return;
  }
  try {
    await navigator.clipboard.writeText(text);
  } catch {
    log("[错误] 复制调试信息失败：剪贴板不可用", "error");
    showToast({ kind: "error", title: "复制失败", description: "剪贴板不可用，请检查系统权限" });
    return;
  }
  flashCopyButton();
  showToast({
    kind: "ok",
    title: "已复制调试信息",
    description: "可直接粘贴到反馈页面",
    actionText: "打开反馈页",
    onAction: () => {
      hideToast();
      void openFeedbackPage();
    },
    durationMs: 5000,
  });
}

function wireErrorCapture(): void {
  window.addEventListener("error", (event) => {
    log(`${DIAG_PREFIX}界面异常：${event.message}（${event.filename}:${event.lineno}:${event.colno}）`, "diag");
  });
  window.addEventListener("unhandledrejection", (event) => {
    log(`${DIAG_PREFIX}未处理的异步异常：${describeError(event.reason)}`, "diag");
  });
}

function extractBrowserTag(ua: string): string {
  const edge = ua.match(/Edg\/(\d+)/);
  if (edge) return `Edge/${edge[1]}`;
  const chrome = ua.match(/Chrome\/(\d+)/);
  if (chrome) return `Chrome/${chrome[1]}`;
  return ua;
}

function frontendDiagnostics(): string[] {
  const offset = -new Date().getTimezoneOffset();
  const sign = offset >= 0 ? "+" : "-";
  const hours = String(Math.floor(Math.abs(offset) / 60)).padStart(2, "0");
  const mins = String(Math.abs(offset) % 60).padStart(2, "0");
  const tz = `${Intl.DateTimeFormat().resolvedOptions().timeZone} (UTC${sign}${hours}:${mins})`;
  return [
    `${DIAG_PREFIX}界面：${extractBrowserTag(navigator.userAgent)} | 窗口=${window.innerWidth}×${window.innerHeight}@${window.devicePixelRatio}x | 时区=${tz}`,
  ];
}

const STORAGE_KEY_LOG_HEIGHT = "ffxiv_patcher_log_height_v2";
const DEFAULT_LOG_HEIGHT = 168;
const MIN_LOG_HEIGHT = 70;
const MAX_LOG_HEIGHT_HARD = 240;
const COLLAPSE_THRESHOLD = 50;

let currentLogHeight = DEFAULT_LOG_HEIGHT;

function getMaxLogHeight(): number {
  const bodyHeight = el.workbenchBody?.clientHeight || 0;
  if (bodyHeight > 300) {
    return Math.min(MAX_LOG_HEIGHT_HARD, Math.floor(bodyHeight * 0.38));
  }
  return 168;
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
      if (!Number.isNaN(parsed) && parsed >= MIN_LOG_HEIGHT && parsed <= MAX_LOG_HEIGHT_HARD) {
        applyLogHeight(parsed, false);
      } else {
        applyLogHeight(DEFAULT_LOG_HEIGHT, true);
      }
    } else {
      applyLogHeight(DEFAULT_LOG_HEIGHT, false);
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

// 「词条目录」「选表范围」跟「文本数据」开关联动：关掉时没有意义，禁用；打开立刻恢复。
// 之前只有 endCSVBusy() 里按开关算过一次 disabled，之后开开关没有任何地方清掉它，
// 于是启动时如果是关的，后面怎么开都点不动。
function updateTextScopeState(): void {
  const disabled = busy || Boolean(csvBusyText) || !el.replaceText.checked;
  el.btnOpenTables.disabled = disabled;
  el.csvTrigger.disabled = disabled;
  el.btnOpenTables.closest(".scope-control")?.classList.toggle("disabled", disabled);
  el.csvPicker?.classList.toggle("disabled", disabled);
  updateWorkbenchState();
}

function updateRestoreButtonState(): void {
  let reason = "";
  if (gameInfo?.regionMismatch || clientStatus?.status === "mismatch") {
    reason = "区服不匹配，无法还原";
  } else if (!isGlobalLanguage(config.language)) {
    if (!gameInfo?.backupExists) {
      reason = "没有可用的原版备份";
    } else if (gameInfo.version && gameInfo.backupVersion && gameInfo.version !== gameInfo.backupVersion) {
      reason = `游戏版本 v${gameInfo.version} 与备份版本 v${gameInfo.backupVersion} 不一致，无法还原`;
    }
  }
  el.btnRestore.disabled = busy || Boolean(reason);
  setTip(el.btnRestore, null);
  el.restoreReason.textContent = reason;
  el.restoreReason.classList.toggle("hidden", !reason);
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
      return;
    }
    const prevDir = currentGameDir();
    if (list.length === 0) {
      if (!silent) {
        log("[探测] 未找到客户端目录，请手动「浏览」选择", "info");
      }
      return;
    }
    if (silent && prevDir) {
      return;
    }
    const detectedDir = list[0]!;
    setGameDir(detectedDir);
    log(`[探测] 已定位游戏目录：${detectedDir}`);
    await refreshGameInfo();
    if (detectedDir !== prevDir) {
      void checkClientEnvironment();
      if (isGlobalLanguage(config.language) && !config.gameDirs?.["CHS"]) {
        runWhenIdle(() => {
          void detectChsGameDirForFontInBackground();
        });
      }
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
  void WailsWindow.SetTitle(title).catch(() => {
    // 忽略非桌面环境下的调用失败
  });
}

function showConfirm(title: string, text: string, okText = "确定继续"): Promise<boolean> {
  return openModal(title, text, okText, "").then((result) => result === "ok");
}

function showAlert(title: string, text: string, okText = "知道了"): Promise<void> {
  return openModal(title, text, okText, "", true).then(() => undefined);
}

// openModal 支持一个可选的次要操作按钮（如「手动下载」），确认/次要/取消三态返回。
// hideCancel 用于两个选项已覆盖全部去向的场合，避免出现语义重复的第三个按钮。
// showOptions 为真时在正文下方显示「文本 / 字体」两个还原范围勾选。
function openModal(
  title: string,
  text: string,
  okText: string,
  altText: string,
  hideCancel = false,
  showOptions = false,
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
    el.modalCancel.textContent = "取消";
    if (showOptions) {
      el.restoreTextCheck.checked = true;
      el.restoreFontCheck.checked = true;
      el.modalOptions.classList.remove("hidden");
    } else {
      el.modalOptions.classList.add("hidden");
    }
    el.modal.classList.remove("hidden");
  });
}

function closeModal(result: ModalResult): void {
  el.modal.classList.add("hidden");
  el.modalAlt.classList.add("hidden");
  el.modalOptions.classList.add("hidden");
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
  let lastError: unknown;
  while (true) {
    try {
      await call("GetAppVersion");
      return;
    } catch (error) {
      lastError = error;
      if (Date.now() - start > timeoutMs) {
        const reason = lastError ? ` (${describeError(lastError)})` : "";
        throw new Error(`后端服务连接超时，请检查客户端启动状态${reason}`);
      }
      await new Promise((resolve) => window.setTimeout(resolve, 80));
    }
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
  setTip(el.versionBadge, locked ? "汉化进行中，稍后再试" : "检查更新");
  setTip(
    el.btnAppUpdate,
    latestUpdateInfo ? `更新到 v${latestUpdateInfo.latestVersion}` : "有新版本，点击查看",
  );
}

type UpdateCheckMode = { interactive?: boolean; prompt?: boolean };

async function checkAppUpdate(mode: UpdateCheckMode = {}): Promise<void> {
  const { interactive = false, prompt = false } = mode;
  if (bootstrap?.devMode) {
    if (interactive) {
      log("[提示] 本地开发构建不检查主程序更新。", "warn");
    }
    return;
  }
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
      await showAlert("检查更新", `当前已是最新版本 (v${info.currentVersion})。`);
    }
  } catch (err) {
    if (interactive) {
      log(`[错误] 检查主程序更新失败: ${describeError(err)}`, "error");
      await showAlert(
        "检查更新失败",
        `无法连接到更新服务器，请检查网络或代理设置后重试。\n\n详情：${describeError(err)}`,
      );
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

const PRESET_ID_CHNAXIS = "chnaxis";
const PRESET_ID_CHNAXIS_SYSTEM = "chnaxis_system";

function isSystemFallbackGlobalPreset(id?: string): boolean {
  return !id || id === PRESET_ID_CHNAXIS_SYSTEM || id === "builtin:global:chnaxis_system";
}

async function detectChsGameDirForFontInBackground(): Promise<void> {
  if (config.gameDirs?.["CHS"]) return;
  try {
    const list = await call("DetectGamePaths", "CHS");
    if (list.length > 0 && list[0] && !config.gameDirs?.["CHS"]) {
      if (!config.gameDirs) config.gameDirs = {};
      config.gameDirs["CHS"] = list[0];
      if (isSystemFallbackGlobalPreset(config.fontPresets?.["GLOBAL"])) {
        if (!config.fontPresets) config.fontPresets = {};
        config.fontPresets["GLOBAL"] = PRESET_ID_CHNAXIS;
      }
      await persistConfig();
      log(`${DIAG_PREFIX}后台探测到国服客户端目录：${list[0]}`, "diag");
      if (isGlobalLanguage(config.language)) {
        await loadFontPresetsForCurrentRegion();
      }
    }
  } catch {
    // 后台静默探测国服路径失败不打扰用户
  }
}

async function bootstrapElementEditorSubWindow(): Promise<void> {
  document.body.style.margin = "0";
  document.body.style.padding = "0";
  document.body.style.backgroundColor = "#16171a";
  document.body.style.color = "#f0f2f5";
  document.body.style.height = "100vh";
  document.body.style.width = "100vw";
  document.body.style.display = "flex";
  document.body.style.flexDirection = "column";
  document.body.style.overflow = "hidden";

  const winEl = document.querySelector<HTMLElement>(".elem-modal-window");
  const appEl = document.getElementById("app");
  if (winEl) {
    winEl.classList.add("subwindow-mode");
    document.body.appendChild(winEl);
  }
  if (appEl) {
    appEl.style.display = "none";
  }
  document.getElementById("preset-modal")?.classList.add("hidden");
  document.getElementById("modal")?.classList.add("hidden");

  const applyPayload = (rawJson: unknown, resetTab: boolean) => {
    if (typeof rawJson !== "string" || !rawJson) return;
    try {
      const payload = JSON.parse(rawJson) as SubEditorStatePayload;
      releaseSubEditorLevers(false);
      activeSubEditorSessionId = payload.sessionId;
      subEditorFaceName = payload.faceName;
      subEditorElementIndex = payload.elementIndex;
      systemFontsList = Array.isArray(payload.systemFonts) ? payload.systemFonts : [];
      currentMultiFont = {
        fontSets: [{ name: "sub", faces: [{ name: payload.faceName, elements: [payload.element] }] }],
      };
      activeFaceName = payload.faceName;
      activeElementIndex = 0;
      document.title = `字体图层属性 - ${payload.faceName} · 图层 #${payload.elementIndex + 1}`;
      renderPropsPanel();
      if (resetTab) {
        switchElementModalTab("font");
      }
    } catch {
      // 忽略无效状态数据
    }
  };

  Events.On("elem-editor:open", (event) => applyPayload(event.data, true));
  Events.On("elem-editor:sync", (event) => applyPayload(event.data, false));

  wireElementEditorEvents();

  try {
    await waitForBackend(8000);
    const initialJson = await call("GetElementEditorState");
    if (initialJson) {
      applyPayload(initialJson, true);
    }
  } catch (err) {
    console.error("初始化图层属性子窗口失败:", err);
  }
}

async function bootstrapApp(): Promise<void> {
  wireErrorCapture();
  if (IS_ELEMENT_EDITOR_SUBWINDOW) {
    await bootstrapElementEditorSubWindow();
    return;
  }
  try {
    await waitForBackend(8000);
    wireRuntimeEvents();
    wireEvents();

    try {
      const diagnostics = await call("GetDiagnostics");
      logHeader([...diagnostics, ...frontendDiagnostics()]);
    } catch (error) {
      log(`[警告] 读取诊断信息失败: ${describeError(error)}`, "warn");
      logHeader(frontendDiagnostics());
    }

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
    updateWorkbenchState();
    void refreshFontBuild();
    void checkAppUpdate({ prompt: true });
    void syncBuiltinOnStart();

    if (currentGameDir()) {
      await refreshGameInfo();
      if (gameInfo?.valid) {
        log(`[环境] 已加载${getRegionShortName(getCurrentFontRegion())}客户端（v${gameInfo.version || "未知"}）`);
      }
      void checkClientEnvironment();
      if (isGlobalLanguage(config.language) && !config.gameDirs?.["CHS"]) {
        runWhenIdle(() => {
          void detectChsGameDirForFontInBackground();
        });
      }
    } else {
      renderGameInfo();
      runWhenIdle(() => {
        void (async () => {
          await detectGameDir(true);
          if (isGlobalLanguage(config.language) && !config.gameDirs?.["CHS"]) {
            await detectChsGameDirForFontInBackground();
          }
        })();
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
  fontCompilerReady = Boolean(info.fontCompilerReady);
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
  el.fontSource.value = config.fontSource === "compile" ? "compile" : "package";
  updateTextScopeState();
  updateFontUiAssetState();


  renderRules();
  renderLanguages();
  renderTargetLangs();

  const activePreset = config.preset && config.preset !== "custom" ? config.preset : "recommended";
  config.preset = activePreset;
  applyCSVContext(info.csv, "");
  tableDocPending = ensureTableDoc();
  void loadFontPresetsForCurrentRegion();
}

// syncBuiltinOnStart 启动后立刻同步一次内置资源，与当前选中的词条目录无关。
// 本地开发构建只校验工作区 resource/，不联网，也不在选择器上显示同步态。
async function syncBuiltinOnStart(): Promise<void> {
  await tableDocPending;
  if (bootstrap?.csv.selected && !csvSelectionValid()) {
    notifyInvalidCSV();
  }
  if (bootstrap?.devMode) {
    try {
      await call("SyncBuiltinResource");
      await reloadRules();
    } catch (err) {
      log(`[警告] 本地内置词条不可用: ${describeError(err)}`, "warn");
    }
    return;
  }
  await syncBuiltinResource();
}

// ensureTableDoc 在本地表清单不可用时只补这一份清单，不触发整包同步。
async function ensureTableDoc(): Promise<void> {
  tableDocReady = true;
  try {
    const updated = await call("EnsureTableDoc");
    if (updated) {
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

function updateRulesSummary(): void {
  if (!bootstrap?.rules) {
    el.rulesSummary.textContent = "附加规则(0)";
    return;
  }
  let activeCount = 0;
  for (const rule of bootstrap.rules) {
    if (rule.status === "unavailable") continue;
    const isChecked = config.rules[rule.id] ?? rule.defaultOn;
    if (isChecked) {
      activeCount++;
    }
  }
  el.rulesSummary.textContent = `附加规则(${activeCount})`;
}

function toggleRulesMenu(open?: boolean): void {
  if (busy || Boolean(csvBusyText)) {
    return;
  }
  const shouldOpen = open !== undefined ? open : el.rulesMenu.classList.contains("hidden");
  if (shouldOpen) {
    const rect = el.rulesTrigger.getBoundingClientRect();
    const spaceBelow = window.innerHeight - rect.bottom;
    const spaceAbove = rect.top;
    const dropUp = spaceBelow < 140 && spaceAbove > spaceBelow;
    el.rulesMenu.classList.toggle("dropup", dropUp);
  }
  el.rulesMenu.classList.toggle("hidden", !shouldOpen);
  el.rulesTrigger.setAttribute("aria-expanded", String(shouldOpen));
}

function hasClientDir(): boolean {
  if (gameInfo && gameInfo.version) {
    return true;
  }
  const inputVal = (el.gamePath?.value || "").trim();
  if (inputVal) {
    return true;
  }
  const dir = currentGameDir();
  return Boolean(dir && dir.trim());
}

function updateWorkbenchState(): void {
  const hasClient = hasClientDir();
  el.contentSection.classList.toggle("hidden", !hasClient);

  const isBuiltinCSV = !bootstrap?.csv?.selected;
  const showRules = hasClient && el.replaceText.checked && isBuiltinCSV;
  el.rulesPicker.classList.toggle("hidden", !showRules);
  if (!showRules) {
    toggleRulesMenu(false);
  }

  el.rulesTrigger.disabled = busy || Boolean(csvBusyText);
  updateRulesSummary();
}

async function reloadRules(): Promise<void> {
  if (!bootstrap) {
    return;
  }
  try {
    const rules = await call("GetRules");
    bootstrap.rules = rules;
    renderRules();
  } catch (err) {
    log(`[警告] 刷新附加规则失败: ${describeError(err)}`, "warn");
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
    if (syncingResource) {
      notice.textContent = `规则数据同步中（${unavailable.map((rule) => rule.name).join("、")}），请稍候…`;
    } else {
      notice.textContent = `当前有规则不可用（${unavailable.map((rule) => rule.name).join("、")}），请同步内置资源后重试`;
    }
    el.rules.append(notice);
  }

  for (const rule of bootstrap.rules) {
    const blocked = rule.status === "unavailable";

    const label = document.createElement("label");
    label.className = "rule-row";
    if (blocked) {
      label.classList.add("disabled");
    }
    const tooltip = blocked && rule.statusDetail ? rule.statusDetail : rule.description;
    setTip(label, tooltip);

    const input = document.createElement("input");
    input.type = "checkbox";
    input.className = "rule-check";
    input.disabled = blocked;
    input.checked = blocked ? false : (config.rules[rule.id] ?? rule.defaultOn);
    input.addEventListener("change", () => {
      config.rules[rule.id] = input.checked;
      updateRulesSummary();
      void persistConfig();
    });

    const title = document.createElement("span");
    title.className = "rule-name";
    title.textContent = rule.name;
    if (blocked) {
      const badge = document.createElement("span");
      badge.className = "rule-badge";
      badge.textContent = syncingResource ? "同步中" : "不可用";
      title.append(badge);
    }

    label.append(input, title);
    el.rules.append(label);
  }
  updateRulesSummary();
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
  if (!config.gameDirs) {
    return "";
  }
  if (config.gameDirs[config.language]) {
    return config.gameDirs[config.language];
  }
  const targetKey = (config.language || "").toLowerCase();
  for (const [key, val] of Object.entries(config.gameDirs)) {
    if (key.toLowerCase() === targetKey && val) {
      return val;
    }
  }
  return "";
}

function setGameDir(dir: string): void {
  if (!config.gameDirs) {
    config.gameDirs = {};
  }
  config.gameDirs[config.language] = dir;
  el.gamePath.value = dir;
  clientStatus = null;
  gameInfo = null;
  renderGameInfo();
  updateWorkbenchState();
  void persistConfig();
}

const DIAG_ICON_REFRESH =
  '<path d="M21 12a9 9 0 1 1-2.64-6.36"/><polyline points="21 3 21 9 15 9"/>';

function createDiagActionButton(
  icon: string,
  label: string,
  onClick: () => void,
  tip = "",
): HTMLButtonElement {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = "btn-diag-action";
  btn.innerHTML = `<svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icon}</svg><span>${label}</span>`;
  if (tip) {
    setTip(btn, tip);
  }
  btn.addEventListener("click", (e) => {
    e.stopPropagation();
    onClick();
  });
  return btn;
}

function createDiagItem(label: string, value: string, cls = "", action?: HTMLElement): HTMLDivElement {
  const item = document.createElement("div");
  item.className = "diag-item";
  const head = document.createElement("div");
  head.className = "diag-head";
  const k = document.createElement("span");
  k.className = "diag-k";
  k.textContent = label;
  head.append(k);
  if (action) {
    head.append(action);
  }
  const v = document.createElement("span");
  v.className = `diag-v ${cls}`.trim();
  v.textContent = value;
  item.append(head, v);
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
  setTip(el.targetLangTrigger, null);

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
  // 同步结束按「文本数据」开关重新算一遍禁用状态，而不是写死 false。
  updateTextScopeState();
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
  setTip(
    el.csvTrigger,
    !ctx.selected && syncFailed ? "上次同步失败，点击「开始汉化」时将自动重试" : null,
  );
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

  const builtinDetail = bootstrap?.devMode
    ? "开发构建：直接读取 resource/rawexd，不联网"
    : builtinSyncFailed
      ? "云端维护，每次启动自动同步，不可直接修改。上次同步失败，开始汉化时将重试"
      : "云端维护，每次启动自动同步，不可直接修改";
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

// buildCSVMetaEditor 把一行换成输入框与确定按钮：Enter/点确定保存，Esc 放弃。
function buildCSVMetaEditor(path: string, title: string, desc: string): HTMLElement {
  const wrap = document.createElement("div");
  wrap.className = "csv-option-editor";

  const fields = document.createElement("div");
  fields.className = "csv-option-editor-fields";

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

  fields.append(titleInput, descInput);

  const confirmBtn = document.createElement("button");
  confirmBtn.type = "button";
  confirmBtn.className = "btn btn-primary btn-xs csv-option-confirm-btn";
  confirmBtn.textContent = "确定";

  wrap.append(fields, confirmBtn);

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

  confirmBtn.addEventListener("mousedown", (e) => {
    e.preventDefault();
  });
  confirmBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    finish(true);
  });

  wrap.addEventListener("focusout", (e) => {
    // 焦点在两个输入框或确定按钮之间移动时不算离开，不触发保存。
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
  setTip(btn, title);
  btn.innerHTML = `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icon}</svg>`;
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

  const hint = document.createElement("div");
  hint.className = "popconfirm-hint";

  const actions = document.createElement("div");
  actions.className = "popconfirm-actions";
  const okBtn = document.createElement("button");
  okBtn.type = "button";
  okBtn.className = "btn btn-xs btn-danger";

  let deleteFiles = canDeleteFiles;
  const syncHint = (): void => {
    title.textContent = deleteFiles ? `永久删除词条目录「${label}」？` : `移除词条目录「${label}」？`;
    hint.textContent = deleteFiles
      ? "目录及其中所有文件会被永久删除，无法恢复。"
      : "仅从列表中移除，磁盘上的目录与文件会保留。";
    okBtn.textContent = deleteFiles ? "永久删除" : "移出列表";
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
    checkText.textContent = "同时删除磁盘上的目录文件";
    checkLabel.append(check, checkText);
    bubble.append(checkLabel);
  }
  bubble.append(hint);

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
    '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' +
    '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>';

  const text = document.createElement("div");
  text.className = "csv-copy-text";
  const title = document.createElement("span");
  title.className = "csv-copy-title";
  title.textContent = "自定义词条";
  const desc = document.createElement("span");
  desc.className = "csv-copy-desc";
  desc.textContent = "制作一份可自由编辑的内置词条副本";
  text.append(title, desc);

  // 已有副本时点按钮只会切换过去，转换选项没有意义，直接不显示。
  const hasCopy = (bootstrap?.csv.dirs ?? []).some((dir) => dir.canDeleteFiles);
  if (!hasCopy) {
    const checkLabel = document.createElement("label");
    checkLabel.className = "csv-copy-check";
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
  updateWorkbenchState();

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
  const hasUnavailable = bootstrap?.rules?.some((rule) => rule.status === "unavailable");
  if (hasUnavailable) {
    renderRules();
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
  try {
    await call("SyncBuiltinResource");
    builtinSyncFailed = false;
    await reloadCSVContext();
    await reloadRules();
  } catch (err) {
    builtinSyncFailed = true;
    log(`[警告] 内置词条同步失败: ${describeError(err)}`, "warn");
    if (!showOnPicker) {
      log("[提示] 当前使用自定义词条，不受影响；改回内置词条时会重试同步", "warn");
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

function partStatusClass(status: string | undefined): string {
  if (status === "original") {
    return "ok";
  }
  if (status === "modified" || status === "unknown") {
    return "warn";
  }
  if (status === "missing") {
    return "err";
  }
  return "";
}

function createClientStatusItem(): HTMLDivElement {
  const item = document.createElement("div");
  item.className = "diag-item diag-item-actionable";

  const head = document.createElement("div");
  head.className = "diag-head";
  const k = document.createElement("span");
  k.className = "diag-k";
  k.textContent = "客户端状态";
  const btn = createDiagActionButton(
    DIAG_ICON_REFRESH,
    checkingClient ? "检测中" : clientStatus ? "重新检测" : "检测",
    () => void checkClientEnvironment(),
  );
  btn.classList.toggle("is-busy", checkingClient);
  btn.disabled = checkingClient;
  head.append(k, btn);

  const makePair = (label: string, part: ClientPartStatus | undefined): HTMLSpanElement => {
    const pair = document.createElement("span");
    pair.className = "diag-pair";
    const pk = document.createElement("span");
    pk.className = "diag-k";
    pk.textContent = label;
    const pv = document.createElement("span");
    pv.className = `diag-v ${checkingClient ? "" : partStatusClass(part?.status)}`.trim();
    pv.textContent = checkingClient ? "检测中…" : part?.label ?? "未检测";
    pair.append(pk, pv);
    if (part?.description && (part.status === "missing" || part.status === "unknown")) {
      setTip(pair, part.description, "bottom");
    }
    return pair;
  };

  const sub = document.createElement("div");
  sub.className = "diag-line diag-sub";
  const sep = document.createElement("span");
  sep.className = "diag-sep";
  sub.append(makePair("文本", clientStatus?.text), sep, makePair("字体", clientStatus?.font));

  item.append(head, sub);
  return item;
}
let checkClientGeneration = 0;

async function checkClientEnvironment(): Promise<void> {
  const gen = ++checkClientGeneration;
  const currentDir = currentGameDir();
  const currentLang = config.language;
  if (!currentDir) {
    clientStatus = null;
    checkingClient = false;
    renderGameInfo();
    return;
  }
  checkingClient = true;
  renderGameInfo();
  try {
    const status = await call("CheckClientStatus", currentDir, currentLang);
    if (gen !== checkClientGeneration || config.language !== currentLang || currentGameDir() !== currentDir) {
      return;
    }
    clientStatus = status;
    const summary = formatClientStatusSummary(status);
    const changed = summary !== lastClientCheckLog;
    lastClientCheckLog = summary;
    if (changed) {
      const kind: LogLevel =
        status.status === "original" ? "ok" : status.status === "mismatch" ? "warn" : "info";
      const parts: string[] = [];
      if (status.text?.label) parts.push(`文本：${status.text.label}`);
      if (status.font?.label) parts.push(`字体：${status.font.label}`);
      const detail = parts.length > 0 ? `（${parts.join("，")}）` : "";
      log(`[校验] 客户端状态：${status.label}${detail}`, kind);
    }
  } catch (err) {
    if (gen !== checkClientGeneration || config.language !== currentLang || currentGameDir() !== currentDir) {
      return;
    }
    log(`[错误] 客户端校验失败: ${describeError(err)}`, "error");
  } finally {
    if (gen === checkClientGeneration) {
      checkingClient = false;
      renderGameInfo();
    }
  }
}

function fontSourceMode(): "package" | "compile" {
  return config.fontSource === "compile" ? "compile" : "package";
}

// ---------- 字体编译状态 ----------
// 自定义方案要现编译贴图。编译独立成一个动作：没过编译就不放行开始汉化，
// 避免文本汉化进行到一半才在字体步骤失败。

let fontBuild: FontBuildInfo | null = null;
let fontCompiling = false;
let fontCompilerReady = false;
let openingFontEditor = false;
let fontEditorHint = "";

function resetFontCompilerButtonState(text = "编辑方案"): void {
  el.btnOpenFontEditor.classList.remove("is-syncing");
  el.fontCompilerBar.classList.remove("is-pending");
  el.fontCompilerBar.style.width = "0%";
  el.btnOpenFontEditorText.textContent = text;
  updateFontUiAssetState();
}

// 按钮点不了的原因统一摆在这一行下面，悬停提示只作为补充。
function renderFontAlert(): void {
  const reasons: string[] = [];
  if (fontSourceMode() === "compile") {
    if (fontEditorHint) reasons.push(fontEditorHint);
    if (fontCompilerReady && !busy && !fontCompiling && fontBuildNeeded() && !fontBuildReady() && fontBuild?.reason) {
      const msg = fontBuild.reason.includes("请重新编译")
        ? `${fontBuild.reason}。`
        : `${fontBuild.reason}，请先执行「编译」生成字体贴图。`;
      reasons.push(msg);
    }
  }
  const text = reasons.join("；");
  if (el.fontEditorAlertText.textContent !== text) {
    el.fontEditorAlertText.textContent = text;
  }
  el.fontEditorAlert.classList.toggle("hidden", !text || !fontCompilerReady);
}

function fontBuildNeeded(): boolean {
  return config.replaceFont && fontSourceMode() === "compile";
}

function fontBuildReady(): boolean {
  return fontBuild?.ready === true;
}

async function refreshFontBuild(): Promise<void> {
  if (!fontBuildNeeded()) {
    fontBuild = null;
    renderFontBuildState();
    return;
  }
  try {
    fontBuild = await call("GetFontBuildInfo");
  } catch {
    fontBuild = null;
  }
  renderFontBuildState();
}

function renderFontBuildState(): void {
  const ready = fontBuildReady();
  const isEnabled = el.replaceFont.checked && !busy;
  const presetEnabled = isEnabled && !fontEditorHint;
  const needsCompile = fontCompilerReady && presetEnabled && fontBuildNeeded() && !ready && !fontCompiling;
  const showReadyBadge = fontCompilerReady && presetEnabled && ready && !fontCompiling;

  el.btnCompileFontText.textContent = fontCompiling ? "编译中…" : ready ? "重新编译" : "编译";
  el.fontCompileGroup.classList.toggle("hidden", !fontCompilerReady);
  el.btnCompileFont.classList.toggle("is-compile-needed", needsCompile);
  el.btnCompileFont.classList.toggle("is-compiling", fontCompiling);
  el.btnCompileFont.classList.toggle("btn-quiet", ready && !fontCompiling);
  el.fontBuildProgress.classList.toggle("hidden", !fontCompiling);
  el.fontBuildStatus.classList.toggle("hidden", !showReadyBadge);
  if (!fontCompiling) {
    el.fontBuildBar.style.width = "0%";
  }
  setTip(el.btnCompileFont, null);
  renderFontAlert();
  refreshPatchButtonGate();
}

// 只负责「编译没过」这一条禁用理由，忙碌与词条同步由各自的逻辑处理，
// 因此解除限制前要确认没有别的任务在跑。
function refreshPatchButtonGate(): void {
  if (isPatching) {
    return;
  }
  if (fontBuildNeeded()) {
    if (!fontCompilerReady) {
      el.btnPatch.disabled = true;
      setTip(el.btnPatch, busy ? null : "请先进入「编辑方案」准备字体编译组件");
      return;
    }
    if (!fontBuildReady()) {
      el.btnPatch.disabled = true;
      setTip(el.btnPatch, busy ? null : "请先点击「编译」生成字体贴图");
      return;
    }
  }
  setTip(el.btnPatch, null);
  if (!busy && !syncingResource && !fontCompiling) {
    el.btnPatch.disabled = false;
  }
}

function updateFontUiAssetState(): void {
  const isEnabled = el.replaceFont.checked && !busy;
  const hasGameDir = Boolean(currentGameDir());
  const isGlobal = isGlobalLanguage(config.language);
  const isFontModified = isGlobal && clientStatus?.font?.status === "modified";
  const isFontMissing = clientStatus?.font?.status === "missing";
  const isMismatch = clientStatus?.status === "mismatch";
  const isCompile = fontSourceMode() === "compile";
  // 内置字体包模式下，编辑器里的方案不会被使用，相关入口一并禁用；且必须有游戏安装目录，字体未被修改或缺失。
  const presetEnabled =
    isEnabled && isCompile && hasGameDir && !isFontModified && !isFontMissing && !isMismatch;
  el.btnOpenFontEditor.disabled = !presetEnabled || fontCompiling;
  el.btnCompileFont.disabled = !presetEnabled || fontCompiling;

  let hint = "";
  if (busy) {
    hint = "任务执行中，请等待完成后再编辑方案";
  } else if (fontCompiling) {
    hint = "正在编译字体贴图，请等待编译完成后再编辑方案";
  } else if (!el.replaceFont.checked) {
    hint = "请先开启「游戏字体」，再编辑自定义方案";
  } else if (!hasGameDir) {
    hint = "请先选择当前区服有效的游戏安装目录";
  } else if (isMismatch) {
    hint = "客户端区服不匹配，请切换区服或重新选择游戏目录";
  } else if (isFontModified) {
    hint = "客户端字体文件已被修改，请先用「还原原版」恢复官方字体，再编辑方案";
  } else if (isFontMissing) {
    hint = "客户端缺少原版字体文件，请先点击「还原原版」或通过官方启动器修复客户端";
  }

  fontEditorHint = hint;
  setTip(el.btnOpenFontEditor, null);
  const presetControl = el.btnOpenFontEditor.closest(".font-preset-control") as HTMLElement | null;
  if (presetControl) {
    presetControl.classList.toggle("disabled", !isEnabled || !presetEnabled);
    setTip(presetControl, null);
    presetControl.classList.toggle("hidden", !isCompile);
  }
  renderFontAlert();
  el.fontSource.disabled = !isEnabled;
  // 字体来源的标签跟下拉一起变灰，和上面的「词条目录」保持同一种禁用外观。
  el.fontSource.closest(".font-source-inline")?.classList.toggle("disabled", !isEnabled);
  renderFontBuildState();
}

function getCurrentFontRegion(): string {
  const lang = (config.language || "GLOBAL").toUpperCase();
  if (lang === "CHS" || lang === "ZH-CN" || lang === "CN" || lang === "ZH") {
    return "CHS";
  }
  if (lang === "KO" || lang === "KR") {
    return "KO";
  }
  if (lang === "TC" || lang === "CHT") {
    return "TC";
  }
  return "GLOBAL";
}

function getRegionDisplayName(reg: string): string {
  switch (reg) {
    case "CHS":
      return "国服 (CN)";
    case "KO":
      return "韩服 (KR)";
    case "TC":
      return "繁中服 (TC)";
    default:
      return "国际服 (Global)";
  }
}

// 编辑器顶栏与方案弹窗只显示区服中文名：括号里的英文码在同一个应用里重复出现，属于噪音。
function getRegionShortName(reg: string): string {
  return getRegionDisplayName(reg).replace(/\s*\([^)]*\)\s*$/, "");
}

function promptModalInput(title: string, description: string, defaultValue: string): Promise<string | null> {
  return new Promise<string | null>((resolve) => {
    modalResolver?.("cancel");
    el.modalTitle.textContent = title;
    el.modalText.replaceChildren();
    el.modalText.classList.remove("hidden");

    const descEl = document.createElement("div");
    descEl.style.marginBottom = "8px";
    descEl.textContent = description;

    const inputEl = document.createElement("input");
    inputEl.type = "text";
    inputEl.className = "input";
    inputEl.style.width = "100%";
    inputEl.value = defaultValue;

    inputEl.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") {
        ev.preventDefault();
        closeModal("ok");
      } else if (ev.key === "Escape") {
        ev.preventDefault();
        closeModal("cancel");
      }
    });

    el.modalText.append(descEl, inputEl);
    el.modalOk.textContent = "确定";
    el.modalAlt.classList.add("hidden");
    el.modalCancel.classList.remove("hidden");
    el.modal.classList.remove("hidden");
    window.setTimeout(() => {
      inputEl.focus();
      inputEl.select();
    }, 20);

    modalResolver = (res: ModalResult) => {
      const val = inputEl.value.trim();
      el.modalText.replaceChildren();
      if (res === "ok" && val) {
        resolve(val);
      } else {
        resolve(null);
      }
    };
  });
}

// promptModalSelect 与 promptModalInput 同一套弹窗外壳，只是把输入框换成下拉。
function promptModalSelect(
  title: string,
  description: string,
  items: string[],
  defaultValue: string,
): Promise<string | null> {
  return new Promise<string | null>((resolve) => {
    modalResolver?.("cancel");
    el.modalTitle.textContent = title;
    el.modalText.replaceChildren();
    el.modalText.classList.remove("hidden");

    const descEl = document.createElement("div");
    descEl.style.marginBottom = "8px";
    descEl.textContent = description;

    const selectEl = document.createElement("select");
    selectEl.className = "select";
    selectEl.style.width = "100%";
    for (const item of items) {
      const opt = document.createElement("option");
      opt.value = item;
      opt.textContent = item;
      selectEl.append(opt);
    }
    if (defaultValue && items.includes(defaultValue)) selectEl.value = defaultValue;

    el.modalText.append(descEl, selectEl);
    el.modalOk.textContent = "确定";
    el.modalAlt.classList.add("hidden");
    el.modalCancel.classList.remove("hidden");
    el.modal.classList.remove("hidden");
    window.setTimeout(() => selectEl.focus(), 20);

    modalResolver = (res: ModalResult) => {
      const val = selectEl.value;
      el.modalText.replaceChildren();
      resolve(res === "ok" && val ? val : null);
    };
  });
}


// ---------- 字体编辑器状态与逻辑 (复刻 FFXIV-FontChanger 架构) ----------

let availableFontPresets: PresetSummary[] = [];
let activePresetId = "";
let currentMultiFont: MultiFontSet | null = null;
let activeFaceName = "";
let activeElementIndex = 0;
let systemFontsList: string[] = [];
let previewWrapEnabled = true;
let previewDebounceTimer: number | null = null;
let previewRenderToken = 0;
let currentZoom = 1.0;
let previewPanX = 0;
let previewPanY = 0;
// 复制/剪切的内容放在内存里；同时尽力写一份到系统剪贴板，便于跨实例粘贴。
let copiedFontElements: FontElement[] = [];
// 元素列表支持 Ctrl/Shift 多选：菜单与右键命令都作用在这组下标上，activeElementIndex 仍是"当前详情行"。
const selectedElementIndices = new Set<number>([0]);
let elementModalSnapshot: string | null = null;

function hideAllContextMenus(): void {
  el.fcFontListContextMenu?.classList.add("hidden");
  el.fcElementContextMenu?.classList.add("hidden");
  closeFontMenus();
}

function showFontListContextMenu(x: number, y: number): void {
  hideAllContextMenus();
  if (!el.fcFontListContextMenu) return;
  el.fcFontListContextMenu.style.left = `${Math.min(window.innerWidth - 200, x)}px`;
  el.fcFontListContextMenu.style.top = `${Math.min(window.innerHeight - 150, y)}px`;
  el.fcFontListContextMenu.classList.remove("hidden");
}

function showElementContextMenu(x: number, y: number): void {
  hideAllContextMenus();
  if (!el.fcElementContextMenu) return;
  el.fcElementContextMenu.style.left = `${Math.min(window.innerWidth - 220, x)}px`;
  el.fcElementContextMenu.style.top = `${Math.min(window.innerHeight - 340, y)}px`;
  el.fcElementContextMenu.classList.remove("hidden");
}

// ---------- 菜单栏 / 右键菜单 / 快捷键 (复刻 FFXIV-FontChanger IDR_FONTEDITOR) ----------

let openFontMenuItemId: string | null = null;
let fontTexture16BitEnabled = true;

function setMenuCheck(id: string, checked: boolean): void {
  const node = document.getElementById(id);
  if (!node) return;
  node.textContent = "";
  node.classList.toggle("is-checked", checked);
}

// 把预览与导出开关的当前值写回菜单的勾选标记。
function syncFontMenuChecks(): void {
  setMenuCheck("fc-check-wrap", previewWrapEnabled);
  setMenuCheck("fc-check-16bit", fontTexture16BitEnabled);
}

function closeFontMenus(): void {
  el.viewFontEditor.querySelectorAll<HTMLElement>(".fc-menu-item").forEach((item) => item.classList.remove("open"));
  el.viewFontEditor.querySelectorAll<HTMLElement>(".fc-dropdown-menu").forEach((menu) => menu.classList.add("hidden"));
  openFontMenuItemId = null;
}

function toggleFontMenu(item: HTMLElement): void {
  const menu = item.querySelector<HTMLElement>(":scope > .fc-dropdown-menu");
  if (!menu) return;
  const wasOpen = openFontMenuItemId === item.id;
  closeFontMenus();
  if (wasOpen) return;
  syncFontMenuChecks();
  item.classList.add("open");
  menu.classList.remove("hidden");
  openFontMenuItemId = item.id;
}

// 元素列表的选择：selectedElementIndices 是命令的作用对象，activeElementIndex 是详情行。
function getSelectedElementIndices(): number[] {
  const face = getCurrentFace();
  if (!face) return [];
  const list = [...selectedElementIndices].filter((i) => i >= 0 && i < face.elements.length).sort((a, b) => a - b);
  if (list.length > 0) return list;
  if (face.elements.length === 0) return [];
  return [Math.min(Math.max(activeElementIndex, 0), face.elements.length - 1)];
}

function setElementSelection(indices: number[], active?: number): void {
  const face = getCurrentFace();
  const max = face ? face.elements.length - 1 : -1;
  selectedElementIndices.clear();
  for (const i of indices) {
    if (i >= 0 && i <= max) selectedElementIndices.add(i);
  }
  const target = active ?? indices[indices.length - 1] ?? 0;
  activeElementIndex = Math.max(0, Math.min(target, Math.max(max, 0)));
}

function refreshAfterElementChange(): void {
  renderElementList();
  renderPropsPanel();
  syncOpenSubEditorToSelection();
  schedulePreviewRender();
}

// 微调命令：对每个选中图层套用同一步进（菜单里的 W/S/A/D/Q/E 与 [/] 都走这里）。
function applyQuickStep(stepType: string): void {
  const face = getCurrentFace();
  if (!face) return;
  let applied = 0;
  for (const index of getSelectedElementIndices()) {
    const elem = face.elements[index];
    if (!elem) continue;
    ensureElementDefaults(elem);
    switch (stepType) {
      case "vo-up":
        setElementBaselineShift(elem, getElementBaselineShift(elem) - 1);
        break;
      case "vo-down":
        setElementBaselineShift(elem, getElementBaselineShift(elem) + 1);
        break;
      case "ho-left":
        elem.wrapModifiers!.horizontalOffset = (elem.wrapModifiers!.horizontalOffset ?? 0) - 1;
        break;
      case "ho-right":
        elem.wrapModifiers!.horizontalOffset = (elem.wrapModifiers!.horizontalOffset ?? 0) + 1;
        break;
      case "ls-dec":
        elem.wrapModifiers!.letterSpacing = (elem.wrapModifiers!.letterSpacing ?? 0) - 1;
        break;
      case "ls-inc":
        elem.wrapModifiers!.letterSpacing = (elem.wrapModifiers!.letterSpacing ?? 0) + 1;
        break;
      case "sz-dec":
        elem.size = Math.max(4, Number(((elem.size ?? 14) - 0.5).toFixed(2)));
        break;
      case "sz-inc":
        elem.size = Number(((elem.size ?? 14) + 0.5).toFixed(2));
        break;
      case "merge-cycle":
        elem.mergeMode = ((elem.mergeMode ?? 0) + 1) % 3;
        break;
      default:
        return;
    }
    applied++;
  }
  if (applied === 0) return;
  refreshAfterElementChange();
}

function defaultElementSize(face: FontFace): number {
  // 新图层的默认字号优先用上游给的权威值，其次才是预览用的近似快照。
  return parseFaceNominalSize(face.name) || (GAME_FONT_METRICS[face.name]?.size ?? 14);
}

// 新图层沿用"系统字体 + CJK 码点区间"的默认配置，与工具箱的"新增图层"一致。
function makeDefaultFontElement(baseSize: number): FontElement {
  const elem: FontElement = {
    size: baseSize,
    gamma: 1.4,
    renderer: 2,
    source: 2,
    mergeMode: 1,
    lookup: { name: "Source Han Sans SC", familyName: "Source Han Sans SC", stretch: 5, style: 0, weight: 400 },
    transform: { rotation: 0, scaleX: 1.0, scaleY: 1.0, skew: 0 },
    transformationMatrix: [1, 0, 0, 1],
    wrapModifiers: {
      baselineShift: 0,
      verticalOffset: 0,
      horizontalOffset: 0,
      letterSpacing: 0,
      codepoints: JSON.parse(JSON.stringify(CJK_DEFAULT_RANGES)),
    },
    renderSpecific: {
      directwrite: { gridFitMode: 2, measureMode: 2, renderMode: 5 },
      freetype: { forceAutohint: false, noAutohint: false, noBitmap: false, noHinting: false, renderMode: 0 },
      empty: { ascent: Math.round(baseSize * 0.85), lineHeight: Math.round(baseSize * 1.3) },
    },
  };
  ensureElementDefaults(elem, baseSize);
  return elem;
}

// 新元素插在最后一个选中项之后（与原生 FontChanger 的 Clone / Paste 位置一致）。
function insertFontElements(elements: FontElement[]): void {
  const face = getCurrentFace();
  if (!face || elements.length === 0) return;
  const indices = getSelectedElementIndices();
  const insertAt = indices.length > 0 ? indices[indices.length - 1] + 1 : face.elements.length;
  face.elements.splice(insertAt, 0, ...elements);
  setElementSelection(
    elements.map((_, i) => insertAt + i),
    insertAt,
  );
  refreshAfterElementChange();
}

function addEmptyFontElement(openEditor: boolean): void {
  const face = getCurrentFace();
  if (!face) return;
  insertFontElements([makeDefaultFontElement(defaultElementSize(face))]);
  if (openEditor) openElementEditorModal();
}

async function addFontElementFromSystemFont(): Promise<void> {
  const face = getCurrentFace();
  if (!face) return;
  await ensureSystemFonts();
  if (systemFontsList.length === 0) {
    log("[提示] 未获取到系统字体列表，请稍后重试", "warn");
    return;
  }
  const family = await promptModalSelect(
    "从字体添加元素",
    `为 ${face.name} 添加系统字体图层：`,
    systemFontsList,
    el.propFontFamily.value || systemFontsList[0],
  );
  if (!family) return;
  const elem = makeDefaultFontElement(defaultElementSize(face));
  if (elem.lookup) {
    elem.lookup.name = family;
    elem.lookup.familyName = family;
  }
  insertFontElements([elem]);
  openElementEditorModal();
}

function cloneSelectedFontElements(): void {
  const face = getCurrentFace();
  if (!face) return;
  const indices = getSelectedElementIndices();
  if (indices.length === 0) return;
  const clones = indices.map((i) => JSON.parse(JSON.stringify(face.elements[i])) as FontElement);
  insertFontElements(clones);
}

function deleteSelectedFontElements(): void {
  const face = getCurrentFace();
  if (!face) return;
  const indices = getSelectedElementIndices();
  if (indices.length === 0) return;
  if (face.elements.length - indices.length < 1) {
    log("[提示] 每个字体至少保留一个图层", "warn");
    return;
  }
  for (const index of [...indices].sort((a, b) => b - a)) {
    face.elements.splice(index, 1);
  }
  setElementSelection([Math.min(indices[0], face.elements.length - 1)]);
  refreshAfterElementChange();
}

function copySelectedFontElements(): boolean {
  const face = getCurrentFace();
  if (!face) return false;
  const indices = getSelectedElementIndices();
  if (indices.length === 0) return false;
  copiedFontElements = indices.map((i) => JSON.parse(JSON.stringify(face.elements[i])) as FontElement);
  // 内存剪贴板为主；系统剪贴板只是尽量同步一份，被拒绝也不影响复制。
  if (navigator.clipboard) {
    void navigator.clipboard.writeText(JSON.stringify(copiedFontElements)).catch(() => undefined);
  }
  return true;
}

function cutSelectedFontElements(): void {
  if (copySelectedFontElements()) deleteSelectedFontElements();
}

async function pasteFontElements(): Promise<void> {
  const face = getCurrentFace();
  if (!face) return;
  if (copiedFontElements.length === 0 && navigator.clipboard) {
    try {
      const text = await navigator.clipboard.readText();
      const parsed: unknown = JSON.parse(text);
      if (Array.isArray(parsed) && parsed.length > 0 && typeof parsed[0] === "object") {
        copiedFontElements = parsed as FontElement[];
      }
    } catch {
      // 剪贴板里不是字体元素 JSON，按"没有可粘贴内容"处理。
    }
  }
  if (copiedFontElements.length === 0) {
    return;
  }
  const clones = copiedFontElements.map((e) => JSON.parse(JSON.stringify(e)) as FontElement);
  insertFontElements(clones);
}

function moveSelectedFontElements(direction: -1 | 1): void {
  const face = getCurrentFace();
  if (!face) return;
  const indices = getSelectedElementIndices();
  if (indices.length === 0) return;
  const order = direction > 0 ? [...indices].reverse() : [...indices];
  let moved = false;
  for (const index of order) {
    const target = index + direction;
    if (target < 0 || target >= face.elements.length) continue;
    const swap = face.elements[index];
    face.elements[index] = face.elements[target];
    face.elements[target] = swap;
    moved = true;
  }
  if (!moved) return;
  setElementSelection(indices.map((i) => i + direction).filter((i) => i >= 0 && i < face.elements.length));
  refreshAfterElementChange();
}

function selectAllFontElements(): void {
  const face = getCurrentFace();
  if (!face) return;
  setElementSelection(face.elements.map((_, i) => i));
  renderElementList();
}

// scaleElementCopy 按字号比例复制一个图层（原版 Structs::FontElement::Scale 的做法）：
// 字号与排版偏移一起缩放，其余字段原样保留。
function scaleElementCopy(elem: FontElement, ratio: number): FontElement {
  const cloned = JSON.parse(JSON.stringify(elem)) as FontElement;
  if (typeof cloned.size === "number") cloned.size = Number((cloned.size * ratio).toFixed(2));
  const wm = cloned.wrapModifiers;
  if (wm) {
    for (const key of ["baselineShift", "verticalOffset", "horizontalOffset", "letterSpacing"] as const) {
      const value = wm[key];
      if (typeof value === "number" && value) wm[key] = Number((value * ratio).toFixed(2));
    }
  }
  return cloned;
}

// 同系列（同家族、同字体集）的其他字体面。
function sameFamilyFaces(faceName: string): FontFace[] {
  if (!currentMultiFont) return [];
  const family = getFaceFamilyPrefix(faceName);
  const set = fontSetForFace(faceName);
  return getFacesList(currentMultiFont).filter(
    (f) => f.name !== faceName && getFaceFamilyPrefix(f.name) === family && fontSetForFace(f.name) === set,
  );
}

// autoFitFace 调组件的 AutoFit 拟合一个字号；indices 为空表示拟合该字号全部图层。
// 拟合结果由组件返回（元素可能因脚本拆分而增减），前端整组替换，不在这里做任何近似计算。
async function autoFitFace(faceName: string, indices: number[] | null): Promise<boolean> {
  if (!currentMultiFont) return false;
  const face = getFacesList(currentMultiFont).find((f) => f.name === faceName);
  if (!face) return false;
  try {
    const json = await call("AutoFitFontPreset", JSON.stringify(currentMultiFont), faceName, indices ?? []);
    const elements = JSON.parse(json) as FontElement[];
    if (!Array.isArray(elements) || elements.length === 0) throw new Error("组件没有返回拟合结果");
    face.elements = elements;
    return true;
  } catch (err) {
    log(`[字体] ${faceName} 自动适配失败: ${describeError(err)}`, "warn");
    return false;
  }
}

// autoFit 复刻原版 Menu_Edit_AutoFit：useSelection 为真时只拟合选中的图层，否则整面；
// allSizes 为真时再把结果按字号比例铺到同系列其它字号，逐个重新拟合。
async function autoFit(allSizes: boolean, useSelection: boolean): Promise<void> {
  const face = getCurrentFace();
  if (!face || !currentMultiFont) return;
  const selection = useSelection ? getSelectedElementIndices() : [];
  const ok = await autoFitFace(face.name, selection.length > 0 ? selection : null);
  if (!ok) return;

  if (allSizes) {
    const srcSize = parseFaceNominalSize(face.name);
    for (const sibling of sameFamilyFaces(face.name)) {
      const ratio = srcSize > 0 ? parseFaceNominalSize(sibling.name) / srcSize : 1;
      sibling.elements = face.elements.map((elem) => scaleElementCopy(elem, ratio));
      sibling.verticalAlignment = face.verticalAlignment;
      if (!(await autoFitFace(sibling.name, null))) return;
    }
  }
  refreshAfterElementChange();
}

// addSystemFontToFamily 复刻原版「从字体添加…(F)」：
// 选一个系统字体，把它作为新图层加到该系列每一个字号上，并按各自的游戏字号自动适配。
async function addSystemFontToFamily(): Promise<void> {
  const srcFace = getCurrentFace();
  if (!srcFace || !currentMultiFont) return;
  await ensureSystemFonts();
  if (systemFontsList.length === 0) {
    log("[提示] 未获取到系统字体列表，请稍后重试", "warn");
    return;
  }
  const family = await promptModalSelect(
    "从字体添加",
    `选择要添加到「${getFaceFamilyPrefix(srcFace.name)}」系列所有字号的系统字体：`,
    systemFontsList,
    el.propFontFamily.value || systemFontsList[0],
  );
  if (!family) return;

  const targets = [srcFace, ...sameFamilyFaces(srcFace.name)];
  for (const face of targets) {
    const elem = makeDefaultFontElement(parseFaceNominalSize(face.name));
    if (elem.lookup) {
      elem.lookup.name = family;
      elem.lookup.familyName = family;
    }
    face.elements.push(elem);
  }
  for (const face of targets) await autoFitFace(face.name, null);
  refreshAfterElementChange();
}

// ---------- 字体面导航与预览开关 ----------

let fontGroupGameCollapsed = false;
let fontGroupLobbyCollapsed = false;

function expandFaceGroup(faceName: string): void {
  if (!faceName) return;
  if (/_lobby$/i.test(faceName)) {
    fontGroupLobbyCollapsed = false;
  } else {
    fontGroupGameCollapsed = false;
  }
}

function selectFace(faceName: string): void {
  if (!faceName) return;
  activeFaceName = faceName;
  expandFaceGroup(faceName);
  activeElementIndex = 0;
  syncActiveFaceToUI();
  renderFaceTabs();
  renderElementList();
  renderPropsPanel();
  syncOpenSubEditorToSelection();
  schedulePreviewRender(true);
}

function navigateFace(direction: -1 | 1): void {
  const faces = getFacesList(currentMultiFont);
  if (faces.length === 0) return;
  const index = faces.findIndex((f) => f.name === activeFaceName);
  selectFace(faces[(Math.max(index, 0) + direction + faces.length) % faces.length].name);
}


function togglePreviewOption(): void {
  previewWrapEnabled = !previewWrapEnabled;
  syncFontMenuChecks();
  schedulePreviewRender();
}

function setPreviewZoomLevel(percent: number): void {
  currentZoom = percent / 100;
  syncFontMenuChecks();
  updatePreviewTransform();
}

function clampPreviewPan(): void {
  if (!el.previewCanvasBox || !el.previewImage) return;
  const viewportW = el.previewCanvasBox.clientWidth;
  const viewportH = el.previewCanvasBox.clientHeight;
  const naturalW = el.previewImage.naturalWidth || el.previewImage.width || 0;
  const naturalH = el.previewImage.naturalHeight || el.previewImage.height || 0;
  if (!viewportW || !viewportH || !naturalW || !naturalH) {
    previewPanX = Math.min(0, previewPanX);
    previewPanY = Math.min(0, previewPanY);
    return;
  }

  const scaledW = naturalW * currentZoom;
  const scaledH = naturalH * currentZoom;

  if (scaledW > viewportW) {
    previewPanX = Math.min(0, Math.max(viewportW - scaledW, previewPanX));
  } else {
    previewPanX = 0;
  }

  if (scaledH > viewportH) {
    previewPanY = Math.min(0, Math.max(viewportH - scaledH, previewPanY));
  } else {
    previewPanY = 0;
  }
}

// 预览图由字体编译组件按游戏内 1:1 像素渲染；缩放/平移只是对这张图做变换，不改变排版。
function updatePreviewTransform(): void {
  clampPreviewPan();
  el.previewImage.style.transform = `translate(${previewPanX}px, ${previewPanY}px) scale(${currentZoom})`;
}

// ---------- 文件 / 导出命令 ----------

function cleanPresetDisplayName(name: string): string {
  return name.replace(/^(?:国际服|国服|韩服|日服|台服)\s*/i, "");
}

function updateCurrentPresetBadge(customLabel?: string): void {
  if (!el.fcPresetSelect) return;
  el.fcPresetSelect.replaceChildren();

  for (const p of availableFontPresets) {
    const opt = document.createElement("option");
    opt.value = p.id;
    const clean = cleanPresetDisplayName(p.name);
    opt.textContent = `${clean}${p.builtin ? "" : " (自定义)"}`;
    if (p.id === activePresetId && !customLabel) {
      opt.selected = true;
    }
    el.fcPresetSelect.append(opt);
  }

  if (customLabel || !activePresetId) {
    const opt = document.createElement("option");
    opt.value = "";
    opt.textContent = customLabel || "未保存方案";
    opt.selected = true;
    el.fcPresetSelect.prepend(opt);
  }
}

async function openFontJsonFile(): Promise<void> {
  const path = await call("SelectFontJsonFile");
  if (!path) return;
  const content = await call("ReadTextFile", path);
  let parsed: MultiFontSet;
  try {
    parsed = JSON.parse(content) as MultiFontSet;
  } catch {
    throw new Error("无法解析所选文件：不是有效的 JSON 格式");
  }
  const faces = getFacesList(parsed);
  if (faces.length === 0) throw new Error("该 JSON 文件中未包含可用的字体定义");
  currentMultiFont = parsed;
  // 打开的 JSON 还没有对应的方案，Ctrl+S 会走"另存为方案"。
  activePresetId = "";
  const fileName = path.split(/[\\/]/).pop() ?? "";
  updateCurrentPresetBadge(`外部 JSON: ${fileName} (未保存)`);
  if (!faces.some((f) => f.name === activeFaceName)) {
    activeFaceName = pickDefaultFaceName(faces);
  }
  expandFaceGroup(activeFaceName);
  activeElementIndex = 0;
  setElementSelection([0]);
  syncActiveFaceToUI();
  renderFaceTabs();
  renderElementList();
  renderPropsPanel();
  schedulePreviewRender(true);
  syncFontMenuChecks();
}

async function saveFontPresetAsNew(): Promise<void> {
  if (!currentMultiFont) return;
  const preset = availableFontPresets.find((p) => p.id === activePresetId);
  const defaultName = preset ? `${preset.name}_副本` : `字体方案_${new Date().toISOString().slice(0, 10)}`;
  const name = await promptModalInput("保存为方案", "方案名称：", defaultName);
  if (!name?.trim()) return;
  const saved = await call(
    "SaveFontPreset",
    getCurrentFontRegion(),
    name.trim(),
    JSON.stringify(currentMultiFont, null, "\t"),
  );
  activePresetId = saved.id;
  markPresetSelected(saved.id);
  await loadFontPresetsForCurrentRegion();
  updateCurrentPresetBadge();
}

async function saveFontPresetCurrent(): Promise<void> {
  if (!currentMultiFont) return;
  const preset = availableFontPresets.find((p) => p.id === activePresetId);
  if (!preset || preset.builtin) {
    await saveFontPresetAsNew();
    return;
  }
  const saved = await call("SaveFontPreset", getCurrentFontRegion(), preset.name, JSON.stringify(currentMultiFont, null, "\t"));
  activePresetId = saved.id;
  markPresetSelected(saved.id);
  await loadFontPresetsForCurrentRegion();
  updateCurrentPresetBadge();
}

// 后端 SaveFontPreset 会写 config，但前端这份 config 也要同步，否则随后的
// loadFontPresetsForCurrentRegion 会按旧值把方案切回去。
function markPresetSelected(presetId: string): void {
  if (!config.fontPresets) config.fontPresets = {};
  config.fontPresets[getCurrentFontRegion()] = presetId;
}

async function exportFontJsonFile(): Promise<void> {
  if (!currentMultiFont) return;
  const path = await call("SelectFontJsonSavePath", `ffxiv_font_${getCurrentFontRegion().toLowerCase()}.json`);
  if (!path) return;
  await call("WriteTextFile", path, JSON.stringify(currentMultiFont, null, "\t"));
}

function buildEmptyAxisMultiFont(source: MultiFontSet): MultiFontSet {
  const clone: MultiFontSet = JSON.parse(JSON.stringify(source));
  const sets = clone.fontSets ?? (clone.fonts ? [clone] : []);
  for (const set of sets) {
    const faces = set.faces ?? set.fonts ?? [];
    for (const face of faces) {
      if (!face.elements) face.elements = [];
      const gameElements = face.elements.filter((e) => e.renderer === 1);
      if (gameElements.length > 0) {
        face.elements = gameElements;
      } else if (face.elements.length > 0) {
        face.elements = [face.elements[0]];
      }
    }
  }
  return clone;
}

async function createNewEmptyAxisPreset(): Promise<void> {
  const reg = getCurrentFontRegion();
  const name = await promptModalInput("新建空 AXIS 方案", "方案名称：", "空 AXIS 方案");
  if (!name || !name.trim()) return;
  const safeName = name.trim();
  if (availableFontPresets.some((p) => !p.builtin && p.name === safeName)) {
    const overwrite = await showConfirm("覆盖方案", `已存在名为「${safeName}」的自定义方案，覆盖它吗？`, "覆盖");
    if (!overwrite) return;
  }

  let baseTemplate: MultiFontSet | null = null;
  try {
    const builtinJson = await call("GetFontPreset", "chnaxis_system");
    baseTemplate = JSON.parse(builtinJson) as MultiFontSet;
  } catch {
    baseTemplate = currentMultiFont;
  }

  if (!baseTemplate) {
    log("[错误] 未能获取 AXIS 方案基础模板", "error");
    return;
  }

  const emptyFont = buildEmptyAxisMultiFont(baseTemplate);
  try {
    const saved = await call("SaveFontPreset", reg, safeName, JSON.stringify(emptyFont, null, "\t"));
    activePresetId = saved.id;
    markPresetSelected(saved.id);
    await loadFontPresetsForCurrentRegion();
    await applyFontPreset(saved.id);
    closePresetManager();
  } catch (err) {
    log(`[错误] 创建空 AXIS 方案失败: ${describeError(err)}`, "error");
    await showAlert("创建失败", describeError(err));
  }
}

async function duplicateCurrentPreset(): Promise<void> {
  const current = availableFontPresets.find((p) => p.id === activePresetId);
  if (current) {
    await duplicatePreset(current);
  } else {
    await saveFontPresetAsNew();
  }
}

async function resetCurrentFontPreset(): Promise<void> {
  if (!activePresetId) {
    log("[提示] 当前打开的是外部 JSON 文件，没有可重置的方案", "warn");
    return;
  }
  const preset = availableFontPresets.find((p) => p.id === activePresetId);
  const ok = await showConfirm(
    "放弃未保存修改",
    `要把「${preset?.name ?? activePresetId}」还原到上次保存时的状态吗？未保存的调整将会丢失。`,
    "放弃修改",
  );
  if (ok) await loadFontPreset(activePresetId);
}

// ---------- 字体方案管理（应用 / 复制 / 重命名 / 删除 / 导入 / 导出） ----------

// applyFontPreset 切换该区服当前使用的方案：写配置、重新载入内容、同步下拉与摘要。
async function applyFontPreset(presetId: string): Promise<void> {
  invalidatePreviewCache();
  const reg = getCurrentFontRegion();
  await call("SelectFontPreset", reg, presetId);
  markPresetSelected(presetId);
  await loadFontPreset(presetId);
  updateCurrentPresetBadge();
  const current = availableFontPresets.find((p) => p.id === presetId);
  setSummaryText(el.fontPresetSummary, current ? `${current.name}${current.builtin ? "" : " (自定义)"}` : "默认方案");
  void refreshFontBuild();
}

function formatPresetMeta(preset: PresetSummary): string {
  if (preset.builtin) return "";
  const parts: string[] = [];
  if (preset.size) {
    parts.push(
      preset.size >= 1024 * 1024
        ? `${(preset.size / (1024 * 1024)).toFixed(1)} MB`
        : `${Math.max(1, Math.round(preset.size / 1024))} KB`,
    );
  }
  if (preset.updatedAt) {
    const when = new Date(preset.updatedAt);
    if (!Number.isNaN(when.getTime())) {
      const pad = (n: number) => String(n).padStart(2, "0");
      parts.push(
        `${when.getFullYear()}-${pad(when.getMonth() + 1)}-${pad(when.getDate())} ${pad(when.getHours())}:${pad(when.getMinutes())}`,
      );
    }
  }
  return parts.join(" · ");
}

function openPresetManager(): void {
  el.presetModal.classList.remove("hidden");
  renderPresetManager();
}

function closePresetManager(): void {
  el.presetModal.classList.add("hidden");
}

function presetActionButton(label: string, onClick: () => void, danger = false): HTMLButtonElement {
  const btn = document.createElement("button");
  btn.type = "button";
  btn.className = `btn btn-xs${danger ? " btn-danger" : ""}`;
  btn.textContent = label;
  btn.addEventListener("click", (ev) => {
    ev.stopPropagation();
    onClick();
  });
  return btn;
}

function renderPresetManager(): void {
  el.presetModalRegion.textContent = "";
  el.presetManagerList.replaceChildren();

  if (availableFontPresets.length === 0) {
    const empty = document.createElement("div");
    empty.className = "preset-empty";
    empty.textContent = "当前区服还没有可用的字体方案。";
    el.presetManagerList.append(empty);
    return;
  }

  for (const preset of availableFontPresets) {
    const isCurrent = preset.id === activePresetId;
    const row = document.createElement("div");
    row.className = `preset-row${isCurrent ? " current" : ""}`;
    row.dataset.presetId = preset.id;
    if (isCurrent) row.setAttribute("aria-current", "true");

    const main = document.createElement("div");
    main.className = "preset-row-main";
    const title = document.createElement("div");
    title.className = "preset-row-title";
    const nameEl = document.createElement("span");
    nameEl.className = "preset-row-name";
    nameEl.textContent = preset.name;
    title.append(nameEl);
    if (isCurrent) {
      const currentBadge = document.createElement("span");
      currentBadge.className = "preset-badge-current";
      currentBadge.textContent = "使用中";
      title.append(currentBadge);
    }
    if (preset.builtin) {
      const kind = document.createElement("span");
      kind.className = "preset-type-note";
      kind.textContent = "内置";
      title.append(kind);
    }
    const desc = document.createElement("div");
    desc.className = "preset-row-desc";
    desc.textContent = preset.description ?? "";
    main.append(title, desc);

    const metaStr = formatPresetMeta(preset);
    if (metaStr) {
      const meta = document.createElement("div");
      meta.className = "preset-row-meta";
      meta.textContent = metaStr;
      main.append(meta);
    }

    const actions = document.createElement("div");
    actions.className = "preset-row-actions";
    if (!isCurrent) {
      const applyBtn = presetActionButton("应用", () => void applyPresetFromManager(preset));
      applyBtn.classList.add("btn-apply");
      actions.append(applyBtn);
    }
    actions.append(presetActionButton("复制", () => void duplicatePreset(preset)));
    if (!preset.builtin) {
      actions.append(
        presetActionButton("重命名", () => void renamePreset(preset)),
        presetActionButton("删除", () => void deletePreset(preset), true),
      );
    }

    row.append(main, actions);
    el.presetManagerList.append(row);
  }
}

async function applyPresetFromManager(preset: PresetSummary): Promise<void> {
  try {
    await applyFontPreset(preset.id);
    closePresetManager();
  } catch (err) {
    log(`[错误] 切换字体方案失败: ${describeError(err)}`, "error");
  }
}

async function duplicatePreset(preset: PresetSummary): Promise<void> {
  const reg = getCurrentFontRegion();
  const base = preset.name.replace(/(（副本\d*）|_副本\d*)$/, "");
  const name = await promptModalInput("复制字体方案", "新方案名称：", `${base}_副本`);
  if (!name || !name.trim()) return;
  const safeName = name.trim();
  if (availableFontPresets.some((p) => !p.builtin && p.name === safeName)) {
    const overwrite = await showConfirm("覆盖方案", `已存在名为「${safeName}」的自定义方案，覆盖它吗？`, "覆盖");
    if (!overwrite) return;
  }
  try {
    const content = await call("GetFontPreset", preset.id);
    const saved = await call("SaveFontPreset", reg, safeName, content);
    // 后端 SaveFontPreset 会顺手选中新方案，前端配置同步，别让后续加载退回旧方案。
    activePresetId = saved.id;
    markPresetSelected(saved.id);
    await loadFontPresetsForCurrentRegion();
    await applyFontPreset(saved.id);
    closePresetManager();
  } catch (err) {
    log(`[错误] 复制字体方案失败: ${describeError(err)}`, "error");
    await showAlert("复制失败", describeError(err));
  }
}

async function renamePreset(preset: PresetSummary): Promise<void> {
  const reg = getCurrentFontRegion();
  const name = await promptModalInput("重命名字体方案", "新的方案名称：", preset.name);
  if (!name || !name.trim()) return;
  const wasActive = activePresetId === preset.id;
  try {
    const renamed = await call("RenameFontPreset", reg, preset.id, name.trim());
    // 后端改的是配置文件，前端这份 config 不跟着改的话，重新加载会退回默认方案。
    if (wasActive) markPresetSelected(renamed.id);
    await loadFontPresetsForCurrentRegion();
  } catch (err) {
    log(`[错误] 重命名字体方案失败: ${describeError(err)}`, "error");
    await showAlert("重命名失败", describeError(err));
  }
}

async function deletePreset(preset: PresetSummary): Promise<void> {
  const reg = getCurrentFontRegion();
  const ok = await showConfirm(
    "删除字体方案",
    `确定要永久删除自定义方案「${preset.name}」吗？此操作无法撤销。`,
    "删除方案",
  );
  if (!ok) return;
  try {
    await call("DeleteFontPreset", reg, preset.id);
    if (activePresetId === preset.id) activePresetId = "";
    await loadFontPresetsForCurrentRegion();
    // 删的是当前方案时后端已回退到默认方案，这里让前端配置跟上同一结果。
    if (activePresetId) markPresetSelected(activePresetId);
  } catch (err) {
    log(`[错误] 删除字体方案失败: ${describeError(err)}`, "error");
    await showAlert("删除失败", describeError(err));
  }
}

async function scaleCurrentFace(): Promise<void> {
  const face = getCurrentFace();
  if (!face || face.elements.length === 0) return;
  const curNominal = parseFaceNominalSize(face.name);
  const inputStr = await promptModalInput(
    `缩放字体字号 (${face.name})`,
    `缩放到多少 px？（当前 ${curNominal}px，字号、行高与偏移将按同比例缩放）`,
    String(curNominal),
  );
  if (!inputStr) return;
  const targetSize = parseFloat(inputStr);
  if (isNaN(targetSize) || targetSize <= 0) return;
  const ratio = targetSize / curNominal;
  for (const elem of face.elements) {
    if (elem.size) elem.size = Number((elem.size * ratio).toFixed(2));
    if (elem.wrapModifiers) {
      if (elem.wrapModifiers.baselineShift) elem.wrapModifiers.baselineShift = Math.round(elem.wrapModifiers.baselineShift * ratio);
      if (elem.wrapModifiers.verticalOffset) elem.wrapModifiers.verticalOffset = Math.round(elem.wrapModifiers.verticalOffset * ratio);
      if (elem.wrapModifiers.horizontalOffset) elem.wrapModifiers.horizontalOffset = Math.round(elem.wrapModifiers.horizontalOffset * ratio);
      if (elem.wrapModifiers.letterSpacing) elem.wrapModifiers.letterSpacing = Math.round(elem.wrapModifiers.letterSpacing * ratio);
    }
    if (elem.renderSpecific?.empty) {
      if (elem.renderSpecific.empty.ascent) elem.renderSpecific.empty.ascent = Math.round(elem.renderSpecific.empty.ascent * ratio);
      if (elem.renderSpecific.empty.lineHeight) elem.renderSpecific.empty.lineHeight = Math.round(elem.renderSpecific.empty.lineHeight * ratio);
    }
  }
  renderElementList();
  renderPropsPanel();
  schedulePreviewRender();
}

// 复刻原版「将 X 的其他尺寸替换为 Y 的缩放副本」：确认后清空目标字号，
// 用当前字号的全部图层按比例重建，并沿用当前字号的垂直对齐。
async function propagateCurrentFamily(): Promise<void> {
  const srcFace = getCurrentFace();
  if (!srcFace || !currentMultiFont) return;
  const family = getFaceFamilyPrefix(srcFace.name);
  const siblings = sameFamilyFaces(srcFace.name);
  if (siblings.length === 0) {
    return;
  }
  const srcSize = parseFaceNominalSize(srcFace.name);
  const listing = siblings.map((f) => `${f.name}（${parseFaceNominalSize(f.name)}px）`).join("、");
  const ok = await showConfirm(
    `同步「${srcFace.name}」的图层到 ${family} 其他字号？`,
    `目标字号：${listing}\n这些字号原有的图层配置会被覆盖。`,
    "确认同步",
  );
  if (!ok) return;

  for (const target of siblings) {
    const targetSize = parseFaceNominalSize(target.name);
    const ratio = srcSize > 0 ? targetSize / srcSize : 1;
    target.elements = srcFace.elements.map((elem) => scaleElementCopy(elem, ratio));
    target.verticalAlignment = srcFace.verticalAlignment;
  }
  refreshAfterElementChange();
}

async function exportFontTtmp(): Promise<void> {
  if (!currentMultiFont) return;
  try {
    const savePath = await call("SelectTTMPSavePath");
    if (!savePath) return;
    const configJson = JSON.stringify(currentMultiFont);
    await call("ExportFontTTMP", configJson, savePath);
    await showAlert("导出成功", `已导出到：\n${savePath}`);
  } catch (err) {
    log(`[错误] 导出 TTMP 失败: ${describeError(err)}`, "error");
  }
}

// 菜单命令总入口：菜单项与右键菜单都走这里，保证两条路径行为一致。
async function runFontCommand(command: string): Promise<void> {
  try {
    switch (command) {
      // 文件
      case "file-new-empty-axis":
        await createNewEmptyAxisPreset();
        return;
      case "file-duplicate-current":
        await duplicateCurrentPreset();
        return;
      case "file-import-json":
        el.fontPresetImportPicker.value = "";
        el.fontPresetImportPicker.click();
        return;
      case "file-open":
        await openFontJsonFile();
        return;
      case "file-save":
        await saveFontPresetCurrent();
        return;
      case "file-save-as-preset":
        await saveFontPresetAsNew();
        return;
      case "file-export-json":
      case "file-save-as":
        await exportFontJsonFile();
        return;
      case "file-manage-presets":
        openPresetManager();
        return;
      case "file-reset":
        await resetCurrentFontPreset();
        return;
      case "file-back":
        el.btnBackWorkbenchFont.click();
        return;

      // 编辑
      case "edit-add":
        addEmptyFontElement(true);
        return;
      case "edit-add-from-system":
        await addFontElementFromSystemFont();
        return;
      case "edit-propagate-all-sizes":
        await addSystemFontToFamily();
        return;
      case "edit-autofit":
        await autoFit(false, true);
        return;
      case "edit-autofit-all":
        await autoFit(true, true);
        return;
      case "edit-scale-size":
        await scaleCurrentFace();
        return;
      case "edit-cut":
        cutSelectedFontElements();
        return;
      case "edit-copy":
        copySelectedFontElements();
        return;
      case "edit-paste":
        await pasteFontElements();
        return;
      case "edit-delete":
        deleteSelectedFontElements();
        return;
      case "edit-select-all":
        selectAllFontElements();
        return;
      case "edit-details":
        openElementEditorModal();
        return;
      case "edit-move-up":
        moveSelectedFontElements(-1);
        return;
      case "edit-move-down":
        moveSelectedFontElements(1);
        return;
      case "edit-toggle-merge":
        applyQuickStep("merge-cycle");
        return;
      case "edit-vo-up":
        applyQuickStep("vo-up");
        return;
      case "edit-vo-down":
        applyQuickStep("vo-down");
        return;
      case "edit-ho-left":
        applyQuickStep("ho-left");
        return;
      case "edit-ho-right":
        applyQuickStep("ho-right");
        return;
      case "edit-ls-dec":
        applyQuickStep("ls-dec");
        return;
      case "edit-ls-inc":
        applyQuickStep("ls-inc");
        return;
      case "edit-sz-dec-1":
        applyQuickStep("sz-dec");
        return;
      case "edit-sz-inc-1":
        applyQuickStep("sz-inc");
        return;

      // 查看
      case "view-prev-font":
        navigateFace(-1);
        return;
      case "view-next-font":
        navigateFace(1);
        return;
      case "view-toggle-wrap":
        togglePreviewOption();
        return;
      case "view-zoom-100":
        setPreviewZoomLevel(100);
        return;
      case "view-zoom-200":
        setPreviewZoomLevel(200);
        return;
      case "view-zoom-300":
        setPreviewZoomLevel(300);
        return;
      case "view-zoom-400":
        setPreviewZoomLevel(400);
        return;
      case "view-zoom-500":
        setPreviewZoomLevel(500);
        return;

      // 导出
      case "export-ttmp":
        await exportFontTtmp();
        return;
      case "export-raw": {
        if (!currentMultiFont) return;
        try {
          const outDir = await call("SelectRawExportDir");
          if (!outDir) return;
          const configJson = JSON.stringify(currentMultiFont);
          await call("ExportFontRaw", configJson, outDir);
          await showAlert("导出成功", `已导出到：\n${outDir}`);
        } catch (err) {
          log(`[字体] 导出原始文件失败: ${describeError(err)}`, "error");
        }
        return;
      }
      case "export-toggle-16bit": {
        fontTexture16BitEnabled = !fontTexture16BitEnabled;
        await call("SetFontTexture16Bit", fontTexture16BitEnabled).catch(() => {});
        syncFontMenuChecks();
        void refreshFontBuild();
        return;
      }
      default:
        return;
    }
  } catch (err) {
    log(`[错误] 执行菜单命令失败: ${describeError(err)}`, "error");
  }
}

// 拖动分割条改相邻面板的尺寸；预览区尺寸变化后重画一次，避免画布被拉伸变形。
function wireSplitterDrag(
  splitter: HTMLElement,
  axis: "x" | "y",
  target: HTMLElement,
  prop: "width" | "height",
  min: number,
  max: () => number,
): void {
  splitter.addEventListener("mousedown", (ev) => {
    if (ev.button !== 0) return;
    ev.preventDefault();
    const startPos = axis === "x" ? ev.clientX : ev.clientY;
    const rect = target.getBoundingClientRect();
    const startSize = axis === "x" ? rect.width : rect.height;
    const bodyClass = axis === "x" ? "col-resizing" : "row-resizing";
    document.body.classList.add(bodyClass);
    const onMove = (moveEv: MouseEvent) => {
      const delta = (axis === "x" ? moveEv.clientX : moveEv.clientY) - startPos;
      const next = Math.round(Math.min(max(), Math.max(min, startSize + delta)));
      target.style[prop] = `${next}px`;
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
      document.body.classList.remove("col-resizing", "row-resizing");
      if (axis === "x" && previewWrapEnabled) {
        schedulePreviewRender(true);
      }
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  });
}

function wireFontMenuInteractions(): void {
  // 三段式布局的分割条：左栏宽度、元素列表高度、预览文本框高度。
  wireSplitterDrag(el.fcSplitterV, "x", el.fcFontListContainer, "width", 140, () => 480);
  wireSplitterDrag(el.fcSplitter1, "y", el.fcElementsSection, "height", 60, () => Math.round(window.innerHeight * 0.7));
  wireSplitterDrag(el.fcSplitter2, "y", el.fcPreviewEditWrap, "height", 40, () => Math.round(window.innerHeight * 0.7));

  window.addEventListener("resize", () => {
    if (!el.viewFontEditor.classList.contains("hidden") && previewWrapEnabled) {
      schedulePreviewRender();
    }
  });

  el.viewFontEditor.querySelectorAll<HTMLElement>(".fc-menu-item").forEach((item) => {
    const btn = item.querySelector<HTMLButtonElement>(":scope > .fc-menu-btn");
    if (!btn) return;
    btn.addEventListener("click", (ev) => {
      ev.stopPropagation();
      toggleFontMenu(item);
    });
    // 已有菜单展开时，鼠标移到别的菜单上直接切过去（Win32 菜单栏的行为）。
    btn.addEventListener("mouseenter", () => {
      if (openFontMenuItemId && openFontMenuItemId !== item.id) toggleFontMenu(item);
    });
  });

  el.viewFontEditor.querySelectorAll<HTMLButtonElement>("[data-menu]").forEach((entry) => {
    entry.addEventListener("click", () => {
      const command = entry.dataset.menu ?? "";
      closeFontMenus();
      void runFontCommand(command);
    });
  });

  const contextCommands: Array<[string, () => void | Promise<void>]> = [
    ["fc-ctx-autofit", () => void autoFit(false, true)],
    ["fc-ctx-autofit-all", () => void autoFit(true, true)],
    ["fc-ctx-autofit-family", () => void autoFit(true, false)],
    ["fc-ctx-open", () => openElementEditorModal()],
    ["fc-ctx-cut", () => cutSelectedFontElements()],
    ["fc-ctx-copy", () => copySelectedFontElements()],
    ["fc-ctx-clone", () => cloneSelectedFontElements()],
    ["fc-ctx-delete", () => deleteSelectedFontElements()],
    ["fc-ctx-add", () => addEmptyFontElement(true)],
    ["fc-ctx-add-system", () => addFontElementFromSystemFont()],
    ["fc-ctx-paste", () => pasteFontElements()],
    ["fc-ctx-scale-size", () => scaleCurrentFace()],
    ["fc-ctx-replace-family", () => propagateCurrentFamily()],
  ];
  for (const [id, action] of contextCommands) {
    document.getElementById(id)?.addEventListener("click", () => {
      hideAllContextMenus();
      void action();
    });
  }

  // 点击菜单之外的区域关闭菜单；菜单与右键菜单内部的点击由各自的处理器负责。
  document.addEventListener("pointerdown", (ev) => {
    if (el.viewFontEditor.classList.contains("hidden")) return;
    const target = ev.target as HTMLElement | null;
    if (target?.closest(".fc-menubar, .fc-context-menu")) return;
    hideAllContextMenus();
  });

  // 全局接管右键上下文菜单：非文本输入框禁止浏览器默认右键，并关闭已打开菜单。
  // 列表项自身会 stopPropagation 并弹出对应的上下文菜单。
  window.addEventListener("contextmenu", (ev) => {
    const target = ev.target as HTMLElement | null;
    if (target?.closest("input, textarea")) {
      hideAllContextMenus();
      return;
    }
    ev.preventDefault();
    hideAllContextMenus();
  });

  // 快捷键：只在字体编辑器前台、没有打开模态框、且不在输入框里时生效。
  document.addEventListener("keydown", (ev) => {
    if (el.viewFontEditor.classList.contains("hidden")) return;
    if (ev.defaultPrevented) return;
    if (!el.fontElementModal.classList.contains("hidden")) return;
    const target = ev.target as HTMLElement | null;
    if (target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT" || target.isContentEditable)) {
      return;
    }

    const run = (command: string) => {
      ev.preventDefault();
      void runFontCommand(command);
    };
    const key = ev.key.toLowerCase();

    if (ev.ctrlKey || ev.metaKey) {
      if (ev.shiftKey) {
        if (key === "s") return run("file-save-as");
        if (key === "tab") return run("view-prev-font");
        return;
      }
      if (key === "s") return run("file-save");
      if (key === "o") return run("file-open");
      if (key === "q") return run("file-back");
      if (key === "c") return run("edit-copy");
      if (key === "x") return run("edit-cut");
      if (key === "v") return run("edit-paste");
      if (key === "a") return run("edit-select-all");
      if (key === "tab") return run("view-next-font");
      return;
    }
    if (ev.altKey) return;

    switch (ev.key) {
      case "Insert":
        return run(ev.shiftKey ? "edit-add-from-system" : "edit-add");
      case "Delete":
        return run("edit-delete");
      case "Enter":
        return run("edit-details");
      case ",":
        return run("edit-move-up");
      case ".":
        return run("edit-move-down");
      case "[":
        return run("edit-sz-dec-1");
      case "]":
        return run("edit-sz-inc-1");
      case "1":
        return run("view-zoom-100");
      case "2":
        return run("view-zoom-200");
      case "3":
        return run("view-zoom-300");
      case "4":
        return run("view-zoom-400");
      case "5":
        return run("view-zoom-500");
      default:
        break;
    }

    if (key === "f") return run(ev.shiftKey ? "edit-autofit-all" : "edit-autofit");
    if (key === "p") return run("view-toggle-wrap");
    if (key === "w") return run("edit-vo-up");
    if (key === "s") return run("edit-vo-down");
    if (key === "a") return run("edit-ho-left");
    if (key === "d") return run("edit-ho-right");
    if (key === "q") return run("edit-ls-dec");
    if (key === "e") return run("edit-ls-inc");
    if (key === "o") return run("edit-toggle-merge");
  });
}


const CJK_DEFAULT_RANGES: number[][] = [
  [13312, 19903],
  [19968, 40959],
  [63744, 64255],
  [131072, 173791],
  [173824, 191471],
  [194560, 195103],
  [196608, 201551],
];

const CODEPOINT_PRESET_GROUPS: Record<string, number[][]> = {
  all: [[0, 1114111]],
  cjk: CJK_DEFAULT_RANGES,
  noncjk: [
    [32, 591],
    [688, 879],
    [7680, 7935],
    [8192, 8303],
    [8352, 8447],
    [8592, 9215],
  ],
  kana: [
    [12352, 12543],
    [12784, 12799],
    [65381, 65439],
  ],
  hangul: [
    [4352, 4607],
    [12592, 12687],
    [44032, 55203],
  ],
  ascii: [[32, 126]],
  cjksym: [
    [12288, 12351],
    [65072, 65103],
    [65281, 65376],
    [65504, 65519],
  ],
  pua: [[57344, 63743]],
};

function normalizeIntervals(ranges: number[][]): number[][] {
  const valid: [number, number][] = [];
  for (const r of ranges) {
    if (!Array.isArray(r) || r.length === 0) continue;
    const a = Math.max(0, Math.min(1114111, Math.floor(Number(r[0]) || 0)));
    const b = Math.max(0, Math.min(1114111, Math.floor(Number(r[1] ?? r[0]) || 0)));
    valid.push([Math.min(a, b), Math.max(a, b)]);
  }
  if (valid.length === 0) return [];
  valid.sort((x, y) => x[0] - y[0] || x[1] - y[1]);
  const merged: number[][] = [valid[0]];
  for (let i = 1; i < valid.length; i++) {
    const last = merged[merged.length - 1];
    const cur = valid[i];
    if (cur[0] <= last[1] + 1) {
      if (cur[1] > last[1]) last[1] = cur[1];
    } else {
      merged.push([cur[0], cur[1]]);
    }
  }
  return merged;
}

function unionIntervals(base: number[][], add: number[][]): number[][] {
  return normalizeIntervals([...base, ...add]);
}

function subtractIntervals(base: number[][], sub: number[][]): number[][] {
  let result = normalizeIntervals(base);
  const toRemove = normalizeIntervals(sub);
  for (const rem of toRemove) {
    const next: number[][] = [];
    for (const cur of result) {
      if (rem[1] < cur[0] || rem[0] > cur[1]) {
        next.push(cur);
      } else {
        if (rem[0] > cur[0]) {
          next.push([cur[0], rem[0] - 1]);
        }
        if (rem[1] < cur[1]) {
          next.push([rem[1] + 1, cur[1]]);
        }
      }
    }
    result = next;
  }
  return result;
}

function parseCustomCodepointInput(input: string): number[][] {
  const trimmed = input.trim();
  if (!trimmed) return [];
  const ranges: number[][] = [];
  const hexOrDecPattern = /^(?:U\+|0x)?([0-9A-Fa-f]+)\s*(?:[-~]\s*(?:U\+|0x)?([0-9A-Fa-f]+))?$/i;
  const parts = trimmed.split(/[,;]+/).map((s) => s.trim()).filter(Boolean);
  let allMatchedAsRange = parts.length > 0;
  for (const part of parts) {
    const m = part.match(hexOrDecPattern);
    if (!m) {
      allMatchedAsRange = false;
      break;
    }
    const isHex = /^U\+|0x|[A-Fa-f]/i.test(part);
    const radix = isHex ? 16 : 10;
    const s = parseInt(m[1], radix);
    const e = m[2] ? parseInt(m[2], radix) : s;
    if (!isNaN(s) && !isNaN(e)) {
      ranges.push([s, e]);
    }
  }
  if (allMatchedAsRange && ranges.length > 0) {
    return normalizeIntervals(ranges);
  }
  const charRanges: number[][] = [];
  for (const ch of Array.from(trimmed)) {
    const cp = ch.codePointAt(0);
    if (typeof cp === "number" && cp >= 32) {
      charRanges.push([cp, cp]);
    }
  }
  return normalizeIntervals(charRanges);
}

function getFacesList(preset: MultiFontSet | null): FontFace[] {
  if (!preset) return [];
  if (Array.isArray(preset.faces)) return preset.faces;
  if (Array.isArray(preset.fonts)) return preset.fonts;
  if (Array.isArray(preset.fontSets) && preset.fontSets.length > 0) {
    const all: FontFace[] = [];
    for (const fs of preset.fontSets) {
      if (Array.isArray(fs.faces)) all.push(...fs.faces);
      else if (Array.isArray(fs.fonts)) all.push(...fs.fonts);
    }
    return all;
  }
  return [];
}

function getElementFamily(elem: FontElement): string {
  return elem.lookup?.familyName || elem.lookup?.name || "";
}

function setElementFamily(elem: FontElement, family: string): void {
  if (!elem.lookup) {
    elem.lookup = { name: family, familyName: family, stretch: 5, style: 0, weight: 400 };
  } else {
    elem.lookup.name = family;
    elem.lookup.familyName = family;
  }
}

function getElementRenderer(elem: FontElement): number {
  if (typeof elem.source === "number") return elem.source;
  if (typeof elem.renderer === "number") return elem.renderer;
  return 2;
}

function setElementRenderer(elem: FontElement, val: number): void {
  elem.renderer = val;
  elem.source = val;
}

function getElementScaleX(elem: FontElement): number {
  if (typeof elem.transform?.scaleX === "number") return elem.transform.scaleX;
  if (Array.isArray(elem.transformationMatrix) && typeof elem.transformationMatrix[0] === "number") {
    return elem.transformationMatrix[0];
  }
  return 1.0;
}

function getElementScaleY(elem: FontElement): number {
  if (typeof elem.transform?.scaleY === "number") return elem.transform.scaleY;
  if (Array.isArray(elem.transformationMatrix) && typeof elem.transformationMatrix[3] === "number") {
    return elem.transformationMatrix[3];
  }
  return 1.0;
}

function syncTransformationMatrix(elem: FontElement): void {
  if (!elem.transform) {
    elem.transform = { rotation: 0, scaleX: 1.0, scaleY: 1.0, skew: 0 };
  }
  const sx = elem.transform.scaleX ?? 1.0;
  const sy = elem.transform.scaleY ?? 1.0;
  const rot = ((elem.transform.rotation ?? 0) * Math.PI) / 180;
  const skew = ((elem.transform.skew ?? 0) * Math.PI) / 180;
  const cosR = Math.cos(rot);
  const sinR = Math.sin(rot);
  const tanS = Math.tan(skew);
  elem.transformationMatrix = [
    Number((sx * cosR).toFixed(4)),
    Number((sx * tanS * cosR - sy * sinR).toFixed(4)),
    Number((sx * sinR).toFixed(4)),
    Number((sx * tanS * sinR + sy * cosR).toFixed(4)),
  ];
}

function setElementScaleX(elem: FontElement, scaleX: number): void {
  if (!elem.transform) {
    elem.transform = { rotation: 0, scaleX: 1.0, scaleY: 1.0, skew: 0 };
  }
  elem.transform.scaleX = scaleX;
  syncTransformationMatrix(elem);
}

function setElementScaleY(elem: FontElement, scaleY: number): void {
  if (!elem.transform) {
    elem.transform = { rotation: 0, scaleX: 1.0, scaleY: 1.0, skew: 0 };
  }
  elem.transform.scaleY = scaleY;
  syncTransformationMatrix(elem);
}

function getElementBaselineShift(elem: FontElement): number {
  if (typeof elem.wrapModifiers?.verticalOffset === "number") return elem.wrapModifiers.verticalOffset;
  if (typeof elem.wrapModifiers?.baselineShift === "number") return elem.wrapModifiers.baselineShift;
  return 0;
}

function setElementBaselineShift(elem: FontElement, val: number): void {
  if (!elem.wrapModifiers) {
    elem.wrapModifiers = { baselineShift: val, verticalOffset: val, horizontalOffset: 0, letterSpacing: 0, codepoints: [] };
  } else {
    elem.wrapModifiers.baselineShift = val;
    elem.wrapModifiers.verticalOffset = val;
  }
}

function ensureElementDefaults(elem: FontElement, defaultSize = 14.0): void {
  if (typeof elem.size !== "number" || elem.size <= 0) {
    elem.size = defaultSize;
  }
  if (typeof elem.gamma !== "number" || elem.gamma <= 0) {
    elem.gamma = 1.0;
  }
  if (typeof elem.renderer !== "number" && typeof elem.source !== "number") {
    elem.renderer = 2;
    elem.source = 2;
  }
  if (typeof elem.mergeMode !== "number") {
    elem.mergeMode = 1;
  }
  if (!elem.lookup) {
    elem.lookup = { name: "Source Han Sans SC", familyName: "Source Han Sans SC", stretch: 5, style: 0, weight: 400 };
  }
  if (!elem.transform) {
    elem.transform = {
      rotation: 0,
      scaleX: getElementScaleX(elem),
      scaleY: getElementScaleY(elem),
      skew: 0,
    };
  }
  if (!elem.wrapModifiers) {
    elem.wrapModifiers = {
      baselineShift: 0,
      verticalOffset: 0,
      horizontalOffset: 0,
      letterSpacing: 0,
      codepoints: [[0, 1114111]],
    };
  }
  if (!elem.renderSpecific) {
    elem.renderSpecific = {};
  }
  if (!elem.renderSpecific.directwrite) {
    elem.renderSpecific.directwrite = { gridFitMode: 2, measureMode: 2, renderMode: 5 };
  }
  if (!elem.renderSpecific.freetype) {
    elem.renderSpecific.freetype = { forceAutohint: false, noAutohint: false, noBitmap: false, noHinting: false, renderMode: 0 };
  }
  if (!elem.renderSpecific.empty) {
    elem.renderSpecific.empty = { ascent: Math.round(elem.size * 0.8), lineHeight: Math.round(elem.size * 1.25) };
  }
}

async function ensureSystemFonts(): Promise<void> {
  if (systemFontsList.length > 0) return;
  try {
    systemFontsList = await call("GetSystemFonts");
  } catch (err) {
    console.error("获取系统字体列表失败", err);
  }
}

let fontPresetGeneration = 0;

async function loadFontPresetsForCurrentRegion(): Promise<void> {
  const gen = ++fontPresetGeneration;
  const reg = getCurrentFontRegion();
  el.fontRegionBadge.textContent = getRegionShortName(reg);
  el.fontRegionWarning.classList.toggle("hidden", reg === "GLOBAL");
  let presets: PresetSummary[];
  try {
    presets = await call("GetFontPresets", reg);
  } catch (err) {
    if (gen !== fontPresetGeneration || getCurrentFontRegion() !== reg) {
      return;
    }
    log(`[错误] 加载字体方案列表失败: ${describeError(err)}`, "error");
    return;
  }

  if (gen !== fontPresetGeneration || getCurrentFontRegion() !== reg) {
    return;
  }
  availableFontPresets = presets;

  const chnAxisPreset = availableFontPresets.find((p) => p.id === PRESET_ID_CHNAXIS);
  const savedId = config.fontPresets?.[reg] || config.fontPresets?.[reg.toLowerCase()];
  let targetId = "";
  if (chnAxisPreset && isSystemFallbackGlobalPreset(savedId)) {
    targetId = chnAxisPreset.id;
    if (!config.fontPresets) config.fontPresets = {};
    config.fontPresets[reg] = targetId;
  } else {
    const matched = availableFontPresets.find((p) => p.id === savedId);
    targetId = matched ? matched.id : availableFontPresets[0]?.id || "";
  }

  activePresetId = targetId;
  updateCurrentPresetBadge();
  const currentPreset = availableFontPresets.find((p) => p.id === targetId);
  const cleanSummary = currentPreset ? cleanPresetDisplayName(currentPreset.name) : "默认方案";
  setSummaryText(
    el.fontPresetSummary,
    currentPreset ? `${cleanSummary}${currentPreset.builtin ? "" : " (自定义)"}` : "默认方案",
  );

  if (targetId) {
    await loadFontPreset(targetId);
  }
  // 方案管理弹窗开着时，列表要跟着刷新（保存/复制/重命名/删除都会走到这里）。
  if (!el.presetModal.classList.contains("hidden")) {
    renderPresetManager();
  }
  // 方案换了或改过，编译状态跟着失效，界面要立刻反映出来。
  void refreshFontBuild();
}

function pickDefaultFaceName(faces: FontFace[]): string {
  if (faces.length === 0) return "";
  const preferred = ["AXIS_18", "ChnAXIS_180", "KrnAXIS_180", "tcaxis_180", "AXIS_14", "ChnAXIS_140"];
  for (const p of preferred) {
    if (faces.some((f) => f.name === p)) return p;
  }
  return faces[0].name;
}

function getDefaultPreviewTextForFace(face: FontFace): string {
  if (face.previewText && face.previewText.trim()) {
    return face.previewText
      .replace(/^!\s*!\s*/, "")
      .replace(/（魔导神门）\s*（魔导神门）/g, "（魔导神门）")
      .replace(/\(魔导神门\)\s*\(魔导神门\)/g, "（魔导神门）");
  }
  const upper = face.name.toUpperCase();
  if (upper === "JUPITER_45" || upper === "JUPITER_90") {
    return "123,456,789.000!!!";
  }
  if (upper.startsWith("MEIDINGER") || upper.startsWith("MIEDINGER")) {
    return "0123456789?!%+-./ EORZEA LIMSA LOMINSA";
  }
  if (upper.startsWith("TRUMPGOTHIC")) {
    return "DUTY COMMENCED 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  }
  return (
    "【最终幻想XIV】光之战士，欢迎来到艾欧泽亚！\n" +
    "The quick brown fox jumps over the lazy dog. 0123456789\n" +
    "紅蓮のリベレーター 漆黒のヴィランズ 暁月のフィナーレ 黄金のレガシー"
  );
}

function adjustElementsSectionHeight(): void {
  const face = getCurrentFace();
  const rowCount = face ? face.elements.length : 1;
  const headerHeight = 28;
  const rowHeight = 28;
  const scrollbarHeight = 16;
  // 预留至少 3 行空间避免横向滚动条遮挡，最多自适应到 5 行
  const rows = Math.min(5, Math.max(3, rowCount));
  const targetHeight = Math.max(140, headerHeight + rows * rowHeight + scrollbarHeight + 6);
  el.fcElementsSection.style.height = `${targetHeight}px`;
}

function adjustPreviewTextHeight(): void {
  // 精确匹配 8 个完整整数行（单行 20px，8 行 160px，加 padding/border 26px = 186px），绝不显示半截文字
  el.fcPreviewEditWrap.style.height = "186px";
}

function syncActiveFaceToUI(): void {
  const face = getCurrentFace();
  if (!face) return;
  setElementSelection([activeElementIndex]);
  el.previewCustomText.value = getDefaultPreviewTextForFace(face);
  adjustPreviewTextHeight();
}

function fontSetForFace(faceName: string): FontSet | undefined {
  const sets = currentMultiFont?.fontSets ?? [];
  return sets.find((set) => {
    const faces = set.faces ?? set.fonts ?? [];
    return faces.some((face) => face.name === faceName);
  });
}

let fontPresetDetailGeneration = 0;

async function loadFontPreset(presetId: string): Promise<void> {
  const gen = ++fontPresetDetailGeneration;
  try {
    activePresetId = presetId;
    const rawJson = await call("GetFontPreset", presetId);
    if (gen !== fontPresetDetailGeneration) return;
    if (!rawJson) return;
    currentMultiFont = JSON.parse(rawJson) as MultiFontSet;

    const faces = getFacesList(currentMultiFont);
    if (!faces.some((f) => f.name === activeFaceName)) {
      activeFaceName = pickDefaultFaceName(faces);
    }
    expandFaceGroup(activeFaceName);

    activeElementIndex = 0;

    syncActiveFaceToUI();
    renderFaceTabs();
    renderElementList();
    renderPropsPanel();
    syncFontMenuChecks();
    if (!el.viewFontEditor.classList.contains("hidden")) {
      syncOpenSubEditorToSelection();
      schedulePreviewRender();
    }
  } catch (err) {
    log(`[错误] 加载方案内容失败: ${describeError(err)}`, "error");
  }
}

function parseFaceNominalSize(faceName: string): number {
  // 权威来源：上游 game_fonts.json 的字号列（AXIS_96=9.6、TrumpGothic_184=18.4……）。
  const known = GAME_FONT_FACES[faceName]?.size;
  if (typeof known === "number" && known > 0) return known;
  // 自定义/未知字体面才退回按名字后缀解析。
  const m = faceName.match(/_(\d+)$/);
  if (!m) return 14;
  const raw = parseInt(m[1], 10);
  if (raw >= 96 && (faceName.toUpperCase().startsWith("CHNAXIS") || faceName.toUpperCase().startsWith("KRNAXIS") || faceName.toUpperCase().startsWith("TCAXIS"))) {
    return raw / 10;
  }
  if (raw === 96) return 9.6;
  return raw;
}

function getFaceFamilyPrefix(faceName: string): string {
  // 统一字族映射（JupiterN 与 Jupiter 合并为规范的 Jupiter 系列）
  if (/^Jupiter/i.test(faceName)) return "Jupiter";
  const known = GAME_FONT_FACES[faceName]?.family;
  if (known) return known === "JupiterN" ? "Jupiter" : known;
  return faceName.replace(/_lobby$/i, "").replace(/_(\d+)$/, "");
}

function renderFaceItemLabel(faceName: string): HTMLElement {
  const container = document.createElement("span");
  container.className = "fc-font-item-name";
  container.textContent = faceName;
  return container;
}

function renderFaceTabs(): void {
  el.fontFaceTabs.replaceChildren();
  const faces = getFacesList(currentMultiFont);
  if (faces.length === 0) return;

  const gameFaces = faces.filter((f) => !/_lobby$/i.test(f.name));
  const lobbyFaces = faces.filter((f) => /_lobby$/i.test(f.name));

  const createGroup = (
    title: string,
    groupFaces: FontFace[],
    isCollapsed: boolean,
    onToggle: () => void,
  ) => {
    if (groupFaces.length === 0) return;

    const groupEl = document.createElement("div");
    groupEl.className = `fc-font-group ${isCollapsed ? "is-collapsed" : ""}`;

    const header = document.createElement("div");
    header.className = "fc-font-group-header";
    header.setAttribute("role", "button");
    header.setAttribute("aria-expanded", String(!isCollapsed));

    const titleWrap = document.createElement("div");
    titleWrap.className = "fc-group-title";

    const arrow = document.createElement("span");
    arrow.className = "fc-group-arrow";
    arrow.textContent = "▶";

    const label = document.createElement("span");
    label.className = "fc-group-label";
    label.textContent = title;

    titleWrap.append(arrow, label);

    const count = document.createElement("span");
    count.className = "fc-group-count";
    count.textContent = String(groupFaces.length);

    header.append(titleWrap, count);

    header.addEventListener("click", () => {
      onToggle();
      renderFaceTabs();
    });

    const itemsContainer = document.createElement("div");
    itemsContainer.className = "fc-font-group-items";

    for (const face of groupFaces) {
      const faceName = face.name;
      const div = document.createElement("div");
      div.className = `fc-font-item ${faceName === activeFaceName ? "active" : ""}`;

      const nameEl = renderFaceItemLabel(faceName);

      const sizeSpan = document.createElement("span");
      sizeSpan.className = "fc-font-item-size";
      sizeSpan.textContent = `${parseFaceNominalSize(faceName)}px`;

      div.append(nameEl, sizeSpan);

      div.addEventListener("click", () => {
        if (activeFaceName === faceName) return;
        selectFace(faceName);
      });

      div.addEventListener("contextmenu", (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        if (activeFaceName !== faceName) {
          selectFace(faceName);
        }
        showFontListContextMenu(ev.clientX, ev.clientY);
      });

      itemsContainer.append(div);
    }

    groupEl.append(header, itemsContainer);
    el.fontFaceTabs.append(groupEl);
  };

  createGroup("游戏内字体", gameFaces, fontGroupGameCollapsed, () => {
    fontGroupGameCollapsed = !fontGroupGameCollapsed;
  });

  createGroup("大厅字体", lobbyFaces, fontGroupLobbyCollapsed, () => {
    fontGroupLobbyCollapsed = !fontGroupLobbyCollapsed;
  });
}

function getCurrentFace(): FontFace | null {
  const faces = getFacesList(currentMultiFont);
  if (faces.length === 0) return null;
  return faces.find((f) => f.name === activeFaceName) || faces[0] || null;
}

function formatMergeModeLabel(mergeVal: number): string {
  switch (mergeVal) {
    case 1:
      return "添加全部字符";
    case 2:
      return "替换已有字符";
    case 0:
    default:
      return "仅添加新字符";
  }
}

function formatSubfamilyLabel(elem: FontElement): string {
  const w = elem.lookup?.weight ?? 400;
  const st = elem.lookup?.style ?? 0;
  const weightNames: Record<number, string> = {
    100: "Thin",
    200: "ExtraLight",
    300: "Light",
    350: "SemiLight",
    400: "Regular",
    500: "Medium",
    600: "SemiBold",
    700: "Bold",
    800: "ExtraBold",
    900: "Black",
  };
  const wLabel = weightNames[w] || `W${w}`;
  if (st === 2) return `${wLabel} Italic`;
  if (st === 1) return `${wLabel} Oblique`;
  return wLabel;
}

function formatCodepointsSummary(ranges: number[][] | undefined): { label: string; count: number } {
  if (!ranges || ranges.length === 0) {
    return { label: "U+0000 ~ U+10FFFF (全部)", count: 1114112 };
  }
  let total = 0;
  for (const r of ranges) {
    const s = r[0] ?? 0;
    const e = r[1] ?? s;
    total += Math.max(0, e - s + 1);
  }
  if (ranges.length === 1 && ranges[0][0] <= 32 && ranges[0][1] >= 65535) {
    return { label: `U+${ranges[0][0].toString(16).toUpperCase().padStart(4, "0")}~${ranges[0][1].toString(16).toUpperCase()}`, count: total };
  }
  if (isCjkCodepoints(ranges)) {
    return { label: `CJK 汉字 (${ranges.length} 段区间)`, count: total };
  }
  const first = ranges[0];
  const hex0 = `U+${(first[0] ?? 0).toString(16).toUpperCase().padStart(4, "0")}`;
  const hex1 = `U+${(first[1] ?? first[0] ?? 0).toString(16).toUpperCase().padStart(4, "0")}`;
  if (ranges.length === 1) {
    return { label: first[0] === first[1] ? hex0 : `${hex0}~${hex1}`, count: total };
  }
  return { label: `${hex0}~${hex1} 等 ${ranges.length} 段`, count: total };
}

function formatSourceLabel(elem: FontElement): { text: string; title: string } {
  const r = getElementRenderer(elem);
  switch (r) {
    case 0:
      return { text: "空白字体", title: "空白字体 (Empty)" };
    case 1:
      return { text: "游戏字库", title: "游戏内置预渲染字库 (Prerendered)" };
    case 3: {
      const ft = elem.renderSpecific?.freetype;
      const rMode = ft?.renderMode === 1 ? "轻量" : ft?.renderMode === 2 ? "单色" : "标准";
      return {
        text: `FreeType · ${rMode}`,
        title: `FreeType (${rMode}抗锯齿)`,
      };
    }
    case 4:
      return { text: "外部图像", title: "SVG / PNG 外部图像字形" };
    case 2:
    default: {
      const dw = elem.renderSpecific?.directwrite;
      const rMode = dw?.renderMode === 5 ? "自然对称" : dw?.renderMode === 4 ? "自然" : "默认";
      const mMode = dw?.measureMode === 2 ? "GDI 自然" : dw?.measureMode === 1 ? "GDI 传统" : "自然度量";
      const gMode = dw?.gridFitMode === 2 ? "启用" : dw?.gridFitMode === 1 ? "停用" : "默认";
      return {
        text: `DirectWrite · ${rMode}`,
        title: `DirectWrite (${rMode} / ${mMode} / 像素对齐：${gMode})`,
      };
    }
  }
}

function formatLookupLabel(elem: FontElement): { text: string; title: string } {
  const r = getElementRenderer(elem);
  if (r === 0 || r === 1 || r === 4) return { text: "-", title: "不使用系统字体匹配" };
  const fam = getElementFamily(elem) || "Segoe UI";
  const w = elem.lookup?.weight ?? 400;
  const wName = w >= 700 ? "粗体" : w >= 500 ? "中黑" : "常规";
  const st = elem.lookup?.style === 2 ? "斜体" : elem.lookup?.style === 1 ? "倾斜" : "常规";
  return {
    text: `${wName} · ${st}`,
    title: `${fam} (${w} / ${wName} / ${st})`,
  };
}

function renderElementList(): void {
  if (IS_ELEMENT_EDITOR_SUBWINDOW) return;
  el.fontElementList.replaceChildren();
  const face = getCurrentFace();
  if (!face || !face.elements || face.elements.length === 0) {
    const tr = document.createElement("tr");
    const td = document.createElement("td");
    td.colSpan = 13;
    td.className = "preview-empty-tip";
    td.textContent = "当前字体还没有图层，可右键或从顶部「编辑」菜单添加图层";
    tr.append(td);
    el.fontElementList.append(tr);
    return;
  }

  const faceName = face.name;
  const gameMetric = GAME_FONT_METRICS[faceName];

  face.elements.forEach((elem, index) => {
    const tr = document.createElement("tr");
    tr.className = `fc-elem-row ${index === activeElementIndex ? "active" : ""} ${
      selectedElementIndices.has(index) ? "selected" : ""
    }`
      .replace(/\s+/g, " ")
      .trim();

    const rendererVal = getElementRenderer(elem);
    let familyName = "";
    let subfamily = "";
    let sizeStr = "";
    let lineHeightStr = "";
    let ascentStr = "";
    let glyphCount = 0;
    let glyphCountApprox = false;

    if (rendererVal === 1) {
      const faceInfo = faceName ? GAME_FONT_FACES[faceName] : undefined;
      familyName = getElementFamily(elem) || faceInfo?.family || "AXIS";
      const sz = Number((elem.size ?? faceInfo?.size ?? 14).toFixed(1));
      const elemGameMetric =
        Object.values(GAME_FONT_METRICS).find(
          (m) => m.family === familyName && Math.abs(m.size - sz) < 0.05
        ) || gameMetric;
      subfamily = elemGameMetric?.subfamily || faceInfo?.subfamily || "Regular";
      sizeStr = `${sz}px`;
      const lh = elemGameMetric ? `~${elemGameMetric.lineHeight}` : `~${Math.round(sz * 1.33)}`;
      lineHeightStr = `${lh}px`;
      const ascValue = elemGameMetric ? `~${elemGameMetric.ascent}` : `~${Math.round(sz * 1.05)}`;
      const vOff = getElementBaselineShift(elem);
      ascentStr = vOff !== 0 ? `${ascValue}px(${vOff >= 0 ? "+" : ""}${vOff}px)` : `${ascValue}px`;
      glyphCount = elemGameMetric ? elemGameMetric.glyphs : 7593;
      glyphCountApprox = true;
    } else if (rendererVal === 0) {
      familyName = "空白";
      subfamily = "-";
      sizeStr = "-";
      const lh = elem.renderSpecific?.empty?.lineHeight ?? 0;
      lineHeightStr = `${lh}px`;
      const asc = elem.renderSpecific?.empty?.ascent ?? 0;
      ascentStr = `${asc}px`;
      glyphCount = 0;
    } else {
      familyName = getElementFamily(elem) || "Source Han Sans SC";
      subfamily = formatSubfamilyLabel(elem);
      const sz = Number((elem.size ?? 14).toFixed(1));
      sizeStr = `${sz}px`;
      const lh = gameMetric ? `~${gameMetric.lineHeight}` : `~${Math.round(sz * 1.33)}`;
      lineHeightStr = `${lh}px`;
      const ascValue = gameMetric ? `~${gameMetric.ascent}` : `~${Math.round(sz * 1.05)}`;
      const vOff = getElementBaselineShift(elem);
      ascentStr = vOff !== 0 ? `${ascValue}px(${vOff >= 0 ? "+" : ""}${vOff}px)` : `${ascValue}px`;
      const cpSumm = formatCodepointsSummary(elem.wrapModifiers?.codepoints);
      glyphCount = cpSumm.count;
    }

    const hOff = elem.wrapModifiers?.horizontalOffset ?? 0;
    const lSpace = elem.wrapModifiers?.letterSpacing ?? 0;
    const gammaStr = rendererVal === 0 ? "-" : Number((elem.gamma ?? 1.0).toFixed(2)).toString();
    const cpSummary = formatCodepointsSummary(elem.wrapModifiers?.codepoints);
    const mergeLabel = formatMergeModeLabel(elem.mergeMode ?? 0);
    const sourceInfo = formatSourceLabel(elem);
    const lookupInfo = formatLookupLabel(elem);

    const cells: { text: string; title?: string; isNum?: boolean }[] = [
      { text: familyName },
      { text: subfamily },
      { text: sizeStr, isNum: true },
      { text: lineHeightStr, isNum: true },
      { text: ascentStr, isNum: true },
      { text: `${hOff}px`, isNum: true },
      { text: `${lSpace}px`, isNum: true },
      { text: gammaStr, isNum: true },
      { text: cpSummary.label },
      { text: `${glyphCountApprox ? "~" : ""}${glyphCount.toLocaleString("en-US")}`, isNum: true },
      { text: mergeLabel },
      { text: sourceInfo.text, title: sourceInfo.title },
      { text: lookupInfo.text, title: lookupInfo.title },
    ];

    for (const c of cells) {
      const td = document.createElement("td");
      if (c.isNum) td.className = "is-num";
      td.textContent = c.text;
      if (c.title) setTip(td, c.title);
      tr.append(td);
    }

    tr.addEventListener("click", (ev) => {
      // Ctrl/Cmd/Shift 点击切换多选；已选中的行再次普通点击会收窄为单选。
      if (ev.ctrlKey || ev.metaKey || ev.shiftKey) {
        if (selectedElementIndices.has(index) && selectedElementIndices.size > 1) {
          selectedElementIndices.delete(index);
        } else {
          selectedElementIndices.add(index);
        }
      } else {
        selectedElementIndices.clear();
        selectedElementIndices.add(index);
      }
      activeElementIndex = index;
      renderElementList();
      renderPropsPanel();
      syncOpenSubEditorToSelection();
    });

    tr.addEventListener("dblclick", () => {
      activeElementIndex = index;
      selectedElementIndices.clear();
      selectedElementIndices.add(index);
      renderElementList();
      openElementEditorModal();
    });

    tr.addEventListener("contextmenu", (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      if (!selectedElementIndices.has(index)) {
        selectedElementIndices.clear();
        selectedElementIndices.add(index);
      }
      activeElementIndex = index;
      renderElementList();
      showElementContextMenu(ev.clientX, ev.clientY);
    });

    el.fontElementList.append(tr);
  });
  adjustElementsSectionHeight();
}

interface SubEditorStatePayload {
  sessionId: number;
  faceName: string;
  elementIndex: number;
  element: FontElement;
  systemFonts: string[];
}

let subEditorOpen = false;
let activeSubEditorSessionId = 0;
let subEditorFaceName = "";
let subEditorElementIndex = -1;

let isBypassActive = false;
let bypassSavedEnabled = true;
let bypassTarget: { faceName: string; index: number } | null = null;
let isCompareActive = false;
let compareEditingBackup: string | null = null;
let compareTarget: { faceName: string; index: number } | null = null;

function findFaceElement(faceName?: string, elementIndex?: number): { face: FontFace; index: number; elem: FontElement } | null {
  const faces = getFacesList(currentMultiFont);
  const face = (faceName ? faces.find((f) => f.name === faceName) : undefined) || getCurrentFace();
  if (!face) return null;
  const idx = elementIndex !== undefined && elementIndex >= 0 ? elementIndex : activeElementIndex;
  const elem = face.elements[idx];
  if (!elem) return null;
  return { face, index: idx, elem };
}

function buildSubEditorPayload(
  sessionId: number,
  faceName: string,
  elementIndex: number,
  elem: FontElement,
): SubEditorStatePayload {
  return {
    sessionId,
    faceName,
    elementIndex,
    element: JSON.parse(JSON.stringify(elem)) as FontElement,
    systemFonts: systemFontsList,
  };
}

function switchElementModalTab(targetTab = "font"): void {
  document.querySelectorAll<HTMLButtonElement>(".elem-modal-window .elem-tab-btn").forEach((b) => {
    const tab = b.dataset.elemTab || "font";
    b.classList.toggle("active", tab === targetTab);
  });

  el.elemTabFont.classList.toggle("hidden", targetTab !== "font");
  el.elemTabChars.classList.toggle("hidden", targetTab !== "chars");
  el.elemTabTypo.classList.toggle("hidden", targetTab !== "typo");
  el.elemTabMerge.classList.toggle("hidden", targetTab !== "merge");

  const bodyEl = document.querySelector<HTMLElement>(".elem-modal-body");
  if (bodyEl) bodyEl.scrollTop = 0;
}

function activateBypass(faceName?: string, elementIndex?: number): void {
  if (isBypassActive) return;
  const hit = findFaceElement(faceName, elementIndex);
  if (!hit) return;
  isBypassActive = true;
  bypassTarget = { faceName: hit.face.name, index: hit.index };
  el.btnLeverBypass?.classList.add("active");
  bypassSavedEnabled = hit.elem.enabled !== false;
  hit.elem.enabled = false;
  renderElementList();
  schedulePreviewRender(true);
}

function deactivateBypass(): void {
  if (!isBypassActive) return;
  const target = bypassTarget;
  bypassTarget = null;
  isBypassActive = false;
  el.btnLeverBypass?.classList.remove("active");
  const hit = findFaceElement(target?.faceName, target?.index);
  if (hit) {
    hit.elem.enabled = bypassSavedEnabled;
    renderElementList();
    schedulePreviewRender(true);
  }
}

function activateCompare(faceName?: string, elementIndex?: number): void {
  if (isCompareActive || !elementModalSnapshot) return;
  const hit = findFaceElement(faceName, elementIndex);
  if (!hit) return;
  isCompareActive = true;
  compareTarget = { faceName: hit.face.name, index: hit.index };
  el.btnLeverCompare?.classList.add("active");
  compareEditingBackup = JSON.stringify(hit.elem);
  hit.face.elements[hit.index] = JSON.parse(elementModalSnapshot) as FontElement;
  renderElementList();
  schedulePreviewRender(true);
}

function deactivateCompare(): void {
  if (!isCompareActive || !compareEditingBackup) return;
  const target = compareTarget;
  compareTarget = null;
  const backup = compareEditingBackup;
  compareEditingBackup = null;
  isCompareActive = false;
  el.btnLeverCompare?.classList.remove("active");
  const hit = findFaceElement(target?.faceName, target?.index);
  if (hit) {
    hit.face.elements[hit.index] = JSON.parse(backup) as FontElement;
    renderElementList();
    schedulePreviewRender(true);
  }
}

function emitSubEditorUpdate(): void {
  if (!IS_ELEMENT_EDITOR_SUBWINDOW || activeSubEditorSessionId <= 0) return;
  const elem = getCurrentFace()?.elements[activeElementIndex];
  if (!elem) return;
  void Events.Emit("elem-editor:update", {
    sessionId: activeSubEditorSessionId,
    faceName: subEditorFaceName,
    elementIndex: subEditorElementIndex,
    element: elem,
  }).catch(() => {});
}

function releaseSubEditorLevers(emitEvent = true): void {
  if (!IS_ELEMENT_EDITOR_SUBWINDOW) return;
  if (isBypassActive) {
    isBypassActive = false;
    el.btnLeverBypass?.classList.remove("active");
    if (emitEvent && activeSubEditorSessionId > 0) {
      void Events.Emit("elem-editor:bypass", { sessionId: activeSubEditorSessionId, active: false }).catch(() => {});
    }
  }
  if (isCompareActive) {
    isCompareActive = false;
    el.btnLeverCompare?.classList.remove("active");
    if (emitEvent && activeSubEditorSessionId > 0) {
      void Events.Emit("elem-editor:compare", { sessionId: activeSubEditorSessionId, active: false }).catch(() => {});
    }
  }
}

function syncOpenSubEditorToSelection(): void {
  if (IS_ELEMENT_EDITOR_SUBWINDOW || !subEditorOpen) return;
  const face = getCurrentFace();
  const elem = face?.elements[activeElementIndex];
  if (!face || !elem) {
    closeElementEditorModal(true);
    return;
  }

  const targetChanged = face.name !== subEditorFaceName || activeElementIndex !== subEditorElementIndex;
  if (targetChanged) {
    deactivateBypass();
    deactivateCompare();
    activeSubEditorSessionId++;
    subEditorFaceName = face.name;
    subEditorElementIndex = activeElementIndex;
    elementModalSnapshot = JSON.stringify(elem);
  }

  const title = `字体图层属性 - ${face.name} · 图层 #${activeElementIndex + 1}`;
  const payloadJson = JSON.stringify(
    buildSubEditorPayload(activeSubEditorSessionId, face.name, activeElementIndex, elem),
  );
  void call("SyncElementEditorWindow", title, payloadJson).catch(() => {});
}

function openElementEditorModal(): void {
  const face = getCurrentFace();
  const elem = face?.elements[activeElementIndex];
  if (!face || !elem) return;

  deactivateBypass();
  deactivateCompare();

  const sessionId = ++activeSubEditorSessionId;
  subEditorOpen = true;
  subEditorFaceName = face.name;
  subEditorElementIndex = activeElementIndex;
  elementModalSnapshot = JSON.stringify(elem);

  const title = `字体图层属性 - ${face.name} · 图层 #${activeElementIndex + 1}`;
  const payloadJson = JSON.stringify(buildSubEditorPayload(sessionId, face.name, activeElementIndex, elem));
  void call("OpenElementEditorWindow", title, payloadJson).catch((err) => {
    subEditorOpen = false;
    subEditorFaceName = "";
    subEditorElementIndex = -1;
    log(`[错误] 打开图层属性窗口失败: ${describeError(err)}`, "error");
  });
}

function closeElementEditorModal(commit: boolean, hideWindow = true): void {
  deactivateBypass();
  deactivateCompare();
  const hit = findFaceElement(subEditorFaceName, subEditorElementIndex);
  if (!commit && elementModalSnapshot && hit) {
    hit.face.elements[hit.index] = JSON.parse(elementModalSnapshot) as FontElement;
  }
  elementModalSnapshot = null;
  subEditorOpen = false;
  subEditorFaceName = "";
  subEditorElementIndex = -1;

  el.fontElementModal.classList.add("hidden");
  if (hideWindow && !IS_ELEMENT_EDITOR_SUBWINDOW) {
    void call("HideElementEditorWindow").catch(() => {});
  }
  renderElementList();
  renderPropsPanel();
  schedulePreviewRender(true);
}

function isCjkCodepoints(ranges: number[][]): boolean {
  if (ranges.length === 0) return false;
  return ranges.some((r) => r[0] === 19968 && r[1] === 40959);
}

function renderCodepointIntervalsList(elem: FontElement): void {
  el.cpIntervalsBox.replaceChildren();
  const ranges = elem.wrapModifiers?.codepoints || [];
  const summary = formatCodepointsSummary(ranges);
  el.cpTotalSummary.textContent = `${ranges.length} 段 · 共 ${summary.count.toLocaleString("zh-CN")} 字符`;
  el.propCodepointsText.value = ranges.map((r) => `${r[0]}-${r[1] ?? r[0]}`).join(", ");

  if (ranges.length === 0) {
    const empty = document.createElement("div");
    empty.style.padding = "8px";
    empty.style.color = "var(--text-3)";
    empty.textContent = "不限区间（默认全部字符 U+0000 ~ U+10FFFF）";
    el.cpIntervalsBox.append(empty);
    return;
  }

  ranges.forEach((r, idx) => {
    const s = r[0] ?? 0;
    const e = r[1] ?? s;
    const row = document.createElement("div");
    row.className = "cp-interval-row";

    const left = document.createElement("div");
    const hexS = `U+${s.toString(16).toUpperCase().padStart(4, "0")}`;
    const hexE = `U+${e.toString(16).toUpperCase().padStart(4, "0")}`;
    left.textContent = s === e ? `${hexS} (1)` : `${hexS} ~ ${hexE} (${e - s + 1})`;

    if (s >= 32 && s <= 55295) {
      const sample = document.createElement("span");
      sample.className = "cp-interval-sample";
      const chars: string[] = [];
      for (let cp = s; cp <= Math.min(e, s + 3); cp++) {
        try {
          chars.push(String.fromCodePoint(cp));
        } catch {
          // 忽略非法码点
        }
      }
      sample.textContent = `「${chars.join("")}${e > s + 3 ? "…" : ""}」`;
      left.append(sample);
    }

    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "btn btn-quiet btn-xs";
    delBtn.textContent = "删除";
    delBtn.addEventListener("click", () => {
      elem.wrapModifiers!.codepoints!.splice(idx, 1);
      renderCodepointIntervalsList(elem);
      renderElementList();
      schedulePreviewRender();
    });

    row.append(left, delBtn);
    el.cpIntervalsBox.append(row);
  });
}

function renderReplacementsVisualList(elem: FontElement): void {
  if (!el.mergeReplacementsList) return;
  el.mergeReplacementsList.replaceChildren();
  const rep = elem.wrapModifiers?.codepointReplacements || {};
  const entries = Object.entries(rep);

  if (entries.length === 0) {
    const empty = document.createElement("div");
    empty.style.padding = "14px";
    empty.style.textAlign = "center";
    empty.style.color = "var(--text-3)";
    empty.style.fontSize = "var(--fs-label)";
    empty.textContent = "暂无字符替换规则，可点击下方「+ 添加映射规则」或从上方填入预设。";
    el.mergeReplacementsList.append(empty);
    return;
  }

  entries.forEach(([origKey, origVal]) => {
    const row = document.createElement("div");
    row.className = "rep-row-item";

    const srcBox = document.createElement("div");
    srcBox.className = "rep-src-box";

    const previewChar = document.createElement("span");
    previewChar.className = "rep-char-preview";
    previewChar.textContent = origKey || " ";

    const srcInput = document.createElement("input");
    srcInput.className = "input input-mono flex-1";
    srcInput.value = formatReplacementKeyHex(origKey);
    srcInput.placeholder = "源字符或 U+XXXX";

    srcBox.append(previewChar, srcInput);

    const arrow = document.createElement("span");
    arrow.className = "rep-arrow-icon";
    arrow.textContent = "→";

    const dstBox = document.createElement("div");
    dstBox.className = "rep-dst-box";
    const dstInput = document.createElement("input");
    dstInput.className = "input flex-1";
    dstInput.value = origVal;
    dstInput.placeholder = "目标替换文本";
    dstBox.append(dstInput);

    const delBtn = document.createElement("button");
    delBtn.type = "button";
    delBtn.className = "btn btn-quiet btn-xs";
    delBtn.textContent = "删除";

    let curKey = origKey;
    const syncRow = (normalizeSrcInput: boolean) => {
      const res = applyReplacementRowEdit(
        elem.wrapModifiers?.codepointReplacements,
        curKey,
        srcInput.value,
        dstInput.value,
      );
      curKey = res.nextKey;
      previewChar.textContent = curKey || " ";
      if (normalizeSrcInput && curKey) {
        srcInput.value = formatReplacementKeyHex(curKey);
      }

      if (res.dict) {
        elem.wrapModifiers!.codepointReplacements = res.dict;
      } else {
        delete elem.wrapModifiers!.codepointReplacements;
      }
      el.propReplacementsJson.value = formatReplacementsToText(res.dict);
      if (res.needsRerender) {
        renderReplacementsVisualList(elem);
      }
      schedulePreviewRender();
    };

    srcInput.addEventListener("input", () => {
      previewChar.textContent = parseReplacementKey(srcInput.value) || " ";
    });
    srcInput.addEventListener("keydown", (ev) => {
      if (ev.key === "Enter") {
        ev.preventDefault();
        srcInput.blur();
      }
    });
    srcInput.addEventListener("change", () => syncRow(true));
    dstInput.addEventListener("input", () => syncRow(false));

    delBtn.addEventListener("click", () => {
      const dict = elem.wrapModifiers?.codepointReplacements || {};
      delete dict[curKey];
      if (Object.keys(dict).length === 0) {
        delete elem.wrapModifiers!.codepointReplacements;
      } else {
        elem.wrapModifiers!.codepointReplacements = dict;
      }
      el.propReplacementsJson.value = formatReplacementsToText(elem.wrapModifiers?.codepointReplacements);
      renderReplacementsVisualList(elem);
      schedulePreviewRender();
    });

    row.append(srcBox, arrow, dstBox, delBtn);
    el.mergeReplacementsList.append(row);
  });
}

function renderPropsPanel(): void {
  const face = getCurrentFace();
  const elem = face?.elements[activeElementIndex];
  if (!elem) return;

  ensureElementDefaults(elem);

  const curFamily = getElementFamily(elem);
  el.propFontFamily.replaceChildren();
  const optDefault = document.createElement("option");
  optDefault.value = "";
  optDefault.textContent = "-- 选择系统已安装字体，或在右侧输入名称/路径 --";
  el.propFontFamily.append(optDefault);

  const seenFonts = new Set<string>();
  const addFontOption = (name: string) => {
    if (!name || seenFonts.has(name)) return;
    seenFonts.add(name);
    const opt = document.createElement("option");
    opt.value = name;
    opt.textContent = name;
    el.propFontFamily.append(opt);
  };

  if (curFamily) {
    addFontOption(curFamily);
  }
  for (const fontName of systemFontsList) {
    addFontOption(fontName);
  }
  el.propFontFamily.value = curFamily;
  el.propFontFile.value = elem.lookup?.filename || curFamily;

  const rendererVal = getElementRenderer(elem);
  el.propRenderer.value = String(rendererVal);
  el.propMergeMode.value = String(elem.mergeMode ?? 0);

  el.propSize.value = String(Number((elem.size ?? 14).toFixed(2)));
  el.propWeight.value = String(elem.lookup?.weight ?? 400);
  el.propStyle.value = String(elem.lookup?.style ?? 0);
  el.propStretch.value = String(elem.lookup?.stretch ?? 5);

  el.propGamma.value = String(Number((elem.gamma ?? 1.0).toFixed(2)));
  el.propBaselineShift.value = String(getElementBaselineShift(elem));
  el.propHorizontalOffset.value = String(elem.wrapModifiers?.horizontalOffset ?? 0);
  el.propLetterSpacing.value = String(elem.wrapModifiers?.letterSpacing ?? 0);
  el.propScaleX.value = String(Number(getElementScaleX(elem).toFixed(2)));
  el.propScaleY.value = String(Number(getElementScaleY(elem).toFixed(2)));
  el.propSkew.value = String(elem.transform?.skew ?? 0);
  el.propRotation.value = String(elem.transform?.rotation ?? 0);

  el.propsDirectwriteGroup.classList.toggle("hidden", rendererVal !== 2);
  el.propsFreetypeGroup.classList.toggle("hidden", rendererVal !== 3);
  el.propsEmptyGroup.classList.toggle("hidden", rendererVal !== 0);
  el.propsGameFontNotice.classList.toggle("hidden", rendererVal !== 1);
  el.propsEmptyFontNotice.classList.toggle("hidden", rendererVal !== 0);
  el.propsExternalFontWrap.classList.toggle("hidden", rendererVal === 1 || rendererVal === 0);

  const dw = elem.renderSpecific?.directwrite;
  el.propDwGridfit.value = String(dw?.gridFitMode ?? 2);
  el.propDwMeasure.value = String(dw?.measureMode ?? 2);
  el.propDwRender.value = String(dw?.renderMode ?? 5);

  const ft = elem.renderSpecific?.freetype;
  el.propFtRender.value = String(ft?.renderMode ?? 0);
  el.propFtNohinting.checked = Boolean(ft?.noHinting);
  el.propFtAutohint.checked = Boolean(ft?.forceAutohint);
  el.propFtNobitmap.checked = Boolean(ft?.noBitmap);

  const emp = elem.renderSpecific?.empty;
  el.propEmptyAscent.value = String(emp?.ascent ?? Math.round((elem.size ?? 14) * 0.8));
  el.propEmptyLineheight.value = String(emp?.lineHeight ?? Math.round((elem.size ?? 14) * 1.25));

  renderCodepointIntervalsList(elem);

  const features = elem.lookup?.features || [];
  el.otFeaturesGrid.querySelectorAll<HTMLInputElement>("input[data-ot-tag]").forEach((chk) => {
    const tag = chk.dataset.otTag || "";
    chk.checked = features.includes(tag);
  });
  el.propOtCustom.value = features.join(", ");

  el.propReplacementsJson.value = formatReplacementsToText(elem.wrapModifiers?.codepointReplacements);
  renderReplacementsVisualList(elem);
}

const previewCache = new Map<string, string>();

function invalidatePreviewCache(): void {
  previewCache.clear();
}

function schedulePreviewRender(immediate = false): void {
  if (IS_ELEMENT_EDITOR_SUBWINDOW) {
    emitSubEditorUpdate();
    return;
  }
  if (previewDebounceTimer) {
    window.clearTimeout(previewDebounceTimer);
    previewDebounceTimer = null;
  }
  if (immediate) {
    void doPreviewRender();
  } else {
    previewDebounceTimer = window.setTimeout(() => {
      void doPreviewRender();
    }, 200);
  }
}

// 预览交给字体编译组件渲染：同一套栅格化与排版代码，出来的就是写进客户端的字形。
// 组件不可用时只报错，不退回浏览器近似渲染——近似出来的东西和游戏不一致，没有参考价值。
async function doPreviewRender(): Promise<void> {
  if (!currentMultiFont) return;
  const face = getCurrentFace();
  if (!face) return;

  const text = el.previewCustomText.value || getDefaultPreviewTextForFace(face);
  const maxWidth = previewWrapEnabled ? Math.max(64, Math.round(el.previewStage.clientWidth || 800)) : 0;

  const cacheKey = `${face.name}::${maxWidth}::${text}::${JSON.stringify(face.elements)}`;
  const cached = previewCache.get(cacheKey);
  if (cached) {
    el.previewImage.src = cached;
    el.previewImage.classList.remove("hidden");
    el.previewHint.classList.add("hidden");
    el.previewStage.classList.remove("rendering");
    el.previewLoadingOverlay.classList.add("hidden");
    updatePreviewTransform();
    return;
  }

  const token = ++previewRenderToken;
  el.previewStage.classList.add("rendering");
  el.previewLoadingOverlay.classList.remove("hidden");

  try {
    const dataUrl = await call("RenderFontPreview", JSON.stringify(currentMultiFont), face.name, text, maxWidth);
    if (token !== previewRenderToken) return;
    if (!dataUrl) throw new Error("字体编译组件没有返回预览图");

    if (previewCache.size > 50) {
      const oldestKey = previewCache.keys().next().value;
      if (oldestKey) previewCache.delete(oldestKey);
    }
    previewCache.set(cacheKey, dataUrl);

    el.previewImage.src = dataUrl;
    el.previewImage.classList.remove("hidden");
    el.previewHint.classList.add("hidden");
    updatePreviewTransform();
  } catch (err) {
    if (token !== previewRenderToken) return;
    el.previewHint.textContent = `预览不可用：${describeError(err)}`;
    el.previewHint.classList.remove("hidden");
    el.previewImage.classList.add("hidden");
    log(`[字体] 预览渲染失败: ${describeError(err)}`, "warn");
  } finally {
    if (token === previewRenderToken) {
      el.previewStage.classList.remove("rendering");
      el.previewLoadingOverlay.classList.add("hidden");
    }
  }
}

function createOpenBackupButton(): HTMLButtonElement {
  return createDiagActionButton(
    CSV_ICON_FOLDER,
    "打开目录",
    () => void openBackupDir(),
    "在文件管理器中打开原版备份目录",
  );
}

async function openBackupDir(): Promise<void> {
  try {
    await call("OpenBackupDirectory", config.language);
  } catch (err) {
    log(`[错误] 打开备份目录失败: ${describeError(err)}`, "error");
  }
}

function renderGameInfo(): void {
  updateRestoreButtonState();
  updateFontUiAssetState();
  updateWorkbenchState();
  el.gameInfo.replaceChildren();
  if (!currentGameDir()) {
    el.gameInfo.append(createDiagItem("状态", "未选择游戏目录", "warn"));
    return;
  }
  if (!gameInfo) {
    el.gameInfo.append(createDiagItem("状态", "正在读取…"));
    return;
  }

  const isGlobal = isGlobalLanguage(config.language);
  const versionMismatch = Boolean(
    gameInfo.version &&
    gameInfo.backupVersion &&
    gameInfo.version !== gameInfo.backupVersion
  );

  const backupReady = gameInfo.backupExists && (isGlobal || !versionMismatch);
  let backupText = "未备份";
  let backupCls = "";
  if (gameInfo.backupExists) {
    backupText = backupReady ? "已就绪" : "已失效";
    backupCls = backupReady ? "ok" : "warn";
  }

  const backupItem = createDiagItem("本地备份", backupText, backupCls, backupReady ? createOpenBackupButton() : undefined);

  const items = [
    createDiagItem("客户端版本", gameInfo.version || "未知", ""),
    createClientStatusItem(),
    backupItem,
  ];

  el.gameInfo.append(...items);
}

let gameInfoGeneration = 0;

async function refreshGameInfo(): Promise<void> {
  const gen = ++gameInfoGeneration;
  const currentDir = currentGameDir();
  const currentLang = config.language;
  if (!currentDir) {
    gameInfo = null;
    renderGameInfo();
    return;
  }
  try {
    const info = await call("GetGameInfo", currentDir, currentLang);
    if (gen !== gameInfoGeneration || config.language !== currentLang || currentGameDir() !== currentDir) {
      return;
    }
    gameInfo = info;
  } catch (error) {
    if (gen !== gameInfoGeneration || config.language !== currentLang || currentGameDir() !== currentDir) {
      return;
    }
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
function setSummaryText(node: HTMLElement, text: string): void {
  node.textContent = text;
}

function renderScopeSummary(): void {
  const label =
    config.preset === "all"
      ? "全量汉化（全部数据表）"
      : config.preset === "custom"
        ? `自定义（已选 ${selected.size} 张）`
        : "ACT兼容版（不汉化敌人名称）";
  setSummaryText(el.scopeSummary, label);
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
      '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="9 18 15 12 9 6"/></svg>';

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

  if (fragment.childNodes.length === 0) {
    const trEmpty = document.createElement("tr");
    const tdEmpty = document.createElement("td");
    tdEmpty.colSpan = 4;
    tdEmpty.className = "table-empty-cell";
    const rawTerm = el.search.value.trim();
    if (tableList().length === 0) {
      tdEmpty.textContent = "表清单尚未加载，请先同步资源或检查网络连接";
    } else if (rawTerm && filterOnlyUnselected) {
      tdEmpty.textContent = `未找到同时匹配「${rawTerm}」且未选中的数据表`;
    } else if (rawTerm) {
      tdEmpty.textContent = `未找到匹配「${rawTerm}」的数据表`;
    } else if (filterOnlyUnselected) {
      tdEmpty.textContent = "所有数据表均已选中，暂无未选项";
    } else {
      tdEmpty.textContent = "暂无可显示的数据表";
    }
    trEmpty.append(tdEmpty);
    fragment.append(trEmpty);
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

  el.gamePath.addEventListener("input", () => {
    updateWorkbenchState();
  });

  el.gamePath.addEventListener("change", async () => {
    const val = el.gamePath.value.trim();
    if (val !== currentGameDir()) {
      setGameDir(val);
      await refreshGameInfo();
      if (val) {
        void checkClientEnvironment();
      }
    } else {
      updateWorkbenchState();
    }
  });

  el.btnDetect.addEventListener("click", () => {
    if (isDetecting) {
      detectCanceled = true;
      void call("CancelDetect").catch(() => {
        // 忽略停止失败：后端会在扫描结束后自然返回
      });
      return;
    }
    if (busy) {
      return;
    }
    void detectGameDir(false);
  });

  let clientSwitchGeneration = 0;

  el.language.addEventListener("change", async () => {
    if (busy) {
      return;
    }
    const oldLang = config.language;
    const newLang = el.language.value;
    if (oldLang === newLang) {
      return;
    }

    const switchGen = ++clientSwitchGeneration;
    config.language = newLang;
    const rememberedDir = config.gameDirs?.[newLang] ?? "";
    el.gamePath.value = rememberedDir;
    gameInfo = null;
    clientStatus = null;
    checkingClient = false;
    renderGameInfo();

    // 切换区服后刷新汉化语言多选框（国际服恢复已保存勾选或默认日英，单语言区服锁定唯一语言）
    targetLangs = [];
    renderTargetLangs();

    if (!rememberedDir) {
      try {
        const detected = await call("DetectGamePaths", newLang);
        if (switchGen !== clientSwitchGeneration || config.language !== newLang) {
          return;
        }
        if (detected.length > 0 && detected[0] && !currentGameDir()) {
          config.gameDirs[newLang] = detected[0];
          el.gamePath.value = detected[0];
          log(`${DIAG_PREFIX}切区探测到目录：${detected[0]}`, "diag");
          renderGameInfo();
        }
      } catch (err) {
        if (switchGen !== clientSwitchGeneration || config.language !== newLang) {
          return;
        }
        log(`[警告] 自动探测游戏目录失败: ${describeError(err)}`, "warn");
      }
    }

    if (switchGen !== clientSwitchGeneration || config.language !== newLang) {
      return;
    }

    await persistConfig();
    if (switchGen !== clientSwitchGeneration || config.language !== newLang) {
      return;
    }

    // 词条目录按区服保存，切换区服后同步该区服的目录列表与表清单
    await reloadCSVContext();
    if (switchGen !== clientSwitchGeneration || config.language !== newLang) {
      return;
    }

    await refreshGameInfo();
    if (switchGen !== clientSwitchGeneration || config.language !== newLang) {
      return;
    }

    void loadFontPresetsForCurrentRegion();
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
    void refreshFontBuild();
    void persistConfig();
  });

  el.fontSource.addEventListener("change", () => {
    config.fontSource = el.fontSource.value === "compile" ? "compile" : "package";
    updateFontUiAssetState();
    void refreshFontBuild();
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

  el.rulesTrigger.addEventListener("click", (e) => {
    e.stopPropagation();
    if (busy) {
      return;
    }
    toggleRulesMenu();
  });

  el.rulesMenu.addEventListener("click", (e) => {
    e.stopPropagation();
  });

  document.addEventListener("click", () => {
    toggleTargetLangMenu(false);
    toggleCSVMenu(false);
    toggleRulesMenu(false);
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
    lastClientCheckLog = "";
  });

  el.btnCopyLog.addEventListener("click", (e) => {
    // 折叠时日志头本身是「展开」的点击区，别让复制顺带把面板拉起来。
    e.stopPropagation();
    void copyDiagnostics();
  });

  el.toastClose.addEventListener("click", () => hideToast());
  el.toastAction.addEventListener("click", () => toastActionHandler?.());
  el.toast.addEventListener("pointerenter", () => {
    toastHovered = true;
    window.clearTimeout(toastTimer);
  });
  el.toast.addEventListener("pointerleave", () => {
    toastHovered = false;
    if (!el.toast.classList.contains("hidden")) {
      scheduleToastHide(toastDuration);
    }
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
      log("[错误] 区服不匹配，无法还原。", "error");
      return;
    }
    const isGlobal = isGlobalLanguage(config.language);
    const versionMismatch = Boolean(
      gameInfo?.version &&
      gameInfo?.backupVersion &&
      gameInfo.version !== gameInfo.backupVersion
    );
    if (!isGlobal && !gameInfo?.backupExists) {
      log("[警告] 本地没有备份，只能先用官方启动器修复客户端", "warn");
      return;
    }
    if (!isGlobal && versionMismatch) {
      log(`[警告] 游戏版本 v${gameInfo?.version} 与备份 v${gameInfo?.backupVersion} 不一致，请先用官方启动器修复`, "warn");
      return;
    }
    if (!currentGameDir()) {
      log("[错误] 请先选择游戏目录", "error");
      return;
    }
    const confirmMsg = isGlobal && !gameInfo?.backupExists
      ? "本地没有原版备份，将从云端下载官方原版文件并覆盖当前已修改的文件。"
      : "将用原版备份覆盖当前已汉化的文件，恢复为官方未修改状态。";
    const result = await openModal("还原官方原版", confirmMsg, "开始还原", "", false, true);
    if (result !== "ok") {
      return;
    }
    const restoreText = el.restoreTextCheck.checked;
    const restoreFont = el.restoreFontCheck.checked;
    if (!restoreText && !restoreFont) {
      log("[提示] 未勾选要还原的内容，已取消", "warn");
      return;
    }
    try {
      toggleConsole(true);
      setBusy(true, restoreText && restoreFont ? "正在还原官方原版文件…" : restoreText ? "正在还原文本…" : "正在还原字体…");
      const resp = await call("RestoreOriginal", currentGameDir(), config.language, restoreText, restoreFont);
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
      const msg = "当前选择的目录与所选区服不匹配，请切换区服或重新选择游戏目录";
      log(`[错误] ${msg}`, "error");
      showToast({ kind: "error", title: "无法开始汉化", description: msg });
      return;
    }
    if (!currentGameDir()) {
      const msg = "请先选择或自动探测有效的游戏安装目录";
      log(`[错误] ${msg}`, "error");
      showToast({ kind: "error", title: "无法开始汉化", description: msg });
      return;
    }
    if (!config.replaceText && !config.replaceFont) {
      const msg = "请至少开启「文本数据」或「游戏字体」其中一项";
      log(`[错误] ${msg}`, "error");
      showToast({ kind: "error", title: "无法开始汉化", description: msg });
      return;
    }
    if (config.replaceText && !csvSelectionValid()) {
      const msg = (bootstrap?.csv.tableList.length ?? 0) === 0
        ? "缺少数据表清单，请检查网络或代理后重试"
        : "当前选中的词条目录已失效，请切换到内置词条或其他有效目录";
      log(`[错误] ${msg}`, "error");
      showToast({ kind: "error", title: "无法开始汉化", description: msg });
      return;
    }
    if (config.replaceText && selected.size === 0) {
      const msg = "尚未选择任何要汉化的数据表，请在「修改范围」中至少勾选一项";
      log(`[错误] ${msg}`, "error");
      showToast({ kind: "error", title: "无法开始汉化", description: msg });
      return;
    }
    if (config.replaceText && regionLanguageOptions().length > 1 && targetLangs.length === 0) {
      const msg = "请至少勾选一种要替换的目标语言";
      log(`[错误] ${msg}`, "error");
      showToast({ kind: "error", title: "无法开始汉化", description: msg });
      return;
    }

    isPatching = true;
    isStopping = false;
    el.btnPatch.classList.add("is-patching");
    if (el.btnPatchText) el.btnPatchText.textContent = "停止汉化";
    el.btnPatch.disabled = false;

    try {
      toggleConsole(true);
      setBusy(true, "正在汉化…");
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
      await reloadRules();
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

  // ---------- 字体定制工作台事件 (复刻 FFXIV-FontChanger 交互) ----------

  wireFontMenuInteractions();

  el.btnCompileFont.addEventListener("click", async () => {
    if (fontCompiling || el.btnCompileFont.disabled) {
      return;
    }
    fontCompiling = true;
    el.fontBuildBar.style.width = "0%";
    el.btnPatch.disabled = true;
    updateFontUiAssetState();
    try {
      fontBuild = await call("CompileFontPreset");
      if (!fontBuild.ready) {
        log(`[字体] 编译未通过：${fontBuild.reason}`, "warn");
      }
    } catch (err) {
      fontBuild = null;
      log(`[字体] 编译失败：${describeError(err)}`, "error");
    } finally {
      fontCompiling = false;
      updateFontUiAssetState();
    }
  });

  el.btnOpenFontEditor.addEventListener("click", async () => {
    if (busy || el.btnOpenFontEditor.disabled || openingFontEditor) return;
    const gameDir = currentGameDir();
    if (!gameDir) {
      log("[字体] 请先选择游戏目录", "warn");
      return;
    }
    const originalText = "编辑方案";
    openingFontEditor = true;
    el.btnOpenFontEditor.disabled = true;
    try {
      if (isGlobalLanguage(config.language)) {
        const status = await call("CheckClientStatus", gameDir, config.language);
        clientStatus = status;
        if (status.font?.status === "modified") {
          log("[字体] 字体文件已被修改，请先「还原原版」再编辑方案", "error");
          return;
        }
        if (status.font?.status === "unknown") {
          log("[字体] 无法核对字体是否为原版，提取到的字形可能有误", "warn");
        }
      } else {
        log("[字体] 此区服无法检查原版状态，若已打补丁提取字形可能有误", "warn");
      }

      if (!fontCompilerReady) {
        el.btnOpenFontEditor.classList.add("is-syncing");
        el.fontCompilerBar.classList.add("is-pending");
        el.fontCompilerBar.style.width = "100%";
        el.btnOpenFontEditorText.textContent = "准备组件中…";
      }
      await call("EnsureFontCompiler");
      fontCompilerReady = true;
      switchView("font-editor");
      await ensureSystemFonts();
      fontTexture16BitEnabled = await call("GetFontTexture16Bit").catch(() => true);
      syncFontMenuChecks();
      await loadFontPresetsForCurrentRegion();
      schedulePreviewRender();
    } catch (err) {
      log(`[字体] 无法打开字体定制: ${describeError(err)}`, "error");
    } finally {
      openingFontEditor = false;
      resetFontCompilerButtonState(originalText);
    }
  });

  el.btnBackWorkbenchFont.addEventListener("click", () => {
    if (subEditorOpen || !el.fontElementModal.classList.contains("hidden")) {
      closeElementEditorModal(true);
    }
    switchView("workbench");
    void refreshFontBuild();
    const currentPreset = availableFontPresets.find((p) => p.id === activePresetId);
    const cleanSummary = currentPreset ? cleanPresetDisplayName(currentPreset.name) : "默认方案";
    setSummaryText(
      el.fontPresetSummary,
      currentPreset ? `${cleanSummary}${currentPreset.builtin ? "" : " (自定义)"}` : "默认方案",
    );
  });

  // 方案下拉选择器切换
  el.fcPresetSelect.addEventListener("change", async () => {
    const nextId = el.fcPresetSelect.value;
    if (!nextId || nextId === activePresetId) return;
    await loadFontPreset(nextId);
    const reg = getCurrentFontRegion();
    if (!config.fontPresets) config.fontPresets = {};
    config.fontPresets[reg] = nextId;
    void persistConfig();
    updateCurrentPresetBadge();
  });
  el.btnPresetClose.addEventListener("click", () => closePresetManager());
  el.presetModal.addEventListener("mousedown", (ev) => {
    if (ev.target === el.presetModal) closePresetManager();
  });
  el.btnPresetImport.addEventListener("click", () => {
    el.fontPresetImportPicker.value = "";
    el.fontPresetImportPicker.click();
  });
  el.btnPresetNewEmpty.addEventListener("click", () => void createNewEmptyAxisPreset());
  el.btnPresetCloseFoot.addEventListener("click", () => closePresetManager());
  document.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape") {
      if (!el.rulesMenu.classList.contains("hidden")) {
        ev.preventDefault();
        toggleRulesMenu(false);
      } else if (subEditorOpen || !el.fontElementModal.classList.contains("hidden")) {
        ev.preventDefault();
        closeElementEditorModal(false);
      } else if (!el.presetModal.classList.contains("hidden")) {
        ev.preventDefault();
        closePresetManager();
      }
    }
  });

  el.fontPresetImportPicker.addEventListener("change", async () => {
    const file = el.fontPresetImportPicker.files?.[0];
    if (!file) return;
    try {
      const content = await file.text();
      const parsed = JSON.parse(content) as MultiFontSet;
      const curReg = getCurrentFontRegion();
      const defaultName = file.name.replace(/\.[^/.]+$/, "");
      const name = await promptModalInput("导入字体方案", "请为导入的字体方案命名：", defaultName);
      if (!name || !name.trim()) return;
      const saved = await call("SaveFontPreset", curReg, name.trim(), JSON.stringify(parsed, null, "\t"));
      if (!config.fontPresets) config.fontPresets = {};
      config.fontPresets[curReg] = saved.id;
      log(`[方案] 已导入并选用方案：${saved.name}`, "ok");
      await loadFontPresetsForCurrentRegion();
      await loadFontPreset(saved.id);
    } catch (err) {
      const msg = describeError(err);
      log(`[错误] 导入方案失败: ${msg}`, "error");
      await showAlert("导入方案失败", msg);
    }
  });

  el.elementsTableViewport.addEventListener("keydown", (ev) => {
    const face = getCurrentFace();
    if (!face || face.elements.length === 0) return;
    if (ev.key === "ArrowUp") {
      ev.preventDefault();
      if (activeElementIndex > 0) {
        setElementSelection([activeElementIndex - 1]);
        renderElementList();
        renderPropsPanel();
        syncOpenSubEditorToSelection();
      }
      return;
    }
    if (ev.key === "ArrowDown") {
      ev.preventDefault();
      if (activeElementIndex < face.elements.length - 1) {
        setElementSelection([activeElementIndex + 1]);
        renderElementList();
        renderPropsPanel();
        syncOpenSubEditorToSelection();
      }
    }
  });

  wireElementEditorEvents();

  // 预览控制
  el.previewCustomText.addEventListener("input", () => {
    const face = getCurrentFace();
    if (face) {
      face.previewText = el.previewCustomText.value;
    }
    adjustPreviewTextHeight();
    schedulePreviewRender();
  });

  // 预览视口交互（复刻原生 FontChanger）：左键拖动平移、滚轮上下滚动、Ctrl+滚轮缩放、双击复位。
  let panState: { startX: number; startY: number; originX: number; originY: number } | null = null;
  el.previewCanvasBox.addEventListener("mousedown", (ev) => {
    if (ev.button !== 0) return;
    ev.preventDefault();
    panState = { startX: ev.clientX, startY: ev.clientY, originX: previewPanX, originY: previewPanY };
  });
  window.addEventListener("mousemove", (ev) => {
    if (!panState) return;
    previewPanX = panState.originX + (ev.clientX - panState.startX);
    previewPanY = panState.originY + (ev.clientY - panState.startY);
    updatePreviewTransform();
    panState.originX = previewPanX;
    panState.startX = ev.clientX;
    panState.originY = previewPanY;
    panState.startY = ev.clientY;
  });
  window.addEventListener("mouseup", () => {
    panState = null;
  });
  el.previewCanvasBox.addEventListener(
    "wheel",
    (ev) => {
      ev.preventDefault();
      if (ev.ctrlKey || ev.metaKey) {
        const steps = [100, 200, 300, 400, 500];
        const current = Math.round(currentZoom * 100);
        const index = Math.max(0, steps.indexOf(current));
        const next = Math.min(steps.length - 1, Math.max(0, index + (ev.deltaY < 0 ? 1 : -1)));
        setPreviewZoomLevel(steps[next]);
        return;
      }
      previewPanY -= ev.deltaY;
      updatePreviewTransform();
    },
    { passive: false },
  );
  el.previewCanvasBox.addEventListener("dblclick", () => {
    previewPanX = 0;
    previewPanY = 0;
    setPreviewZoomLevel(100);
  });
  el.previewImage.addEventListener("load", () => updatePreviewTransform());
}

function wireElementEditorEvents(): void {
  // 模态框四页签切换与底部控制
  document.querySelectorAll<HTMLButtonElement>(".elem-modal-window .elem-tab-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      const targetTab = btn.dataset.elemTab || "font";
      switchElementModalTab(targetTab);
    });
  });

  const commitSubEditor = async () => {
    releaseSubEditorLevers(false);
    await Events.Emit("elem-editor:commit", {
      sessionId: activeSubEditorSessionId,
      faceName: subEditorFaceName,
      elementIndex: subEditorElementIndex,
      element: getCurrentFace()?.elements[0],
    }).catch(() => {});
    void call("HideElementEditorWindow").catch(() => {});
  };

  const cancelSubEditor = async () => {
    releaseSubEditorLevers(false);
    await Events.Emit("elem-editor:cancel", { sessionId: activeSubEditorSessionId }).catch(() => {});
    void call("HideElementEditorWindow").catch(() => {});
  };

  el.btnElemModalOk.addEventListener("click", () => {
    const active = document.activeElement;
    if (active instanceof HTMLInputElement || active instanceof HTMLTextAreaElement) {
      active.blur();
    }
    if (IS_ELEMENT_EDITOR_SUBWINDOW) {
      void commitSubEditor();
    } else {
      closeElementEditorModal(true);
    }
  });

  el.btnElemModalCancel.addEventListener("click", () => {
    if (IS_ELEMENT_EDITOR_SUBWINDOW) {
      void cancelSubEditor();
    } else {
      closeElementEditorModal(false);
    }
  });

  // 底部对比与旁通推杆
  const releaseAllLevers = () => {
    if (IS_ELEMENT_EDITOR_SUBWINDOW) {
      releaseSubEditorLevers(true);
      return;
    }
    if (isBypassActive) deactivateBypass();
    if (isCompareActive) deactivateCompare();
  };

  el.btnLeverBypass?.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    try {
      el.btnLeverBypass.setPointerCapture(ev.pointerId);
    } catch {
      // 忽略
    }
    if (IS_ELEMENT_EDITOR_SUBWINDOW) {
      if (isBypassActive || activeSubEditorSessionId <= 0) return;
      isBypassActive = true;
      el.btnLeverBypass.classList.add("active");
      void Events.Emit("elem-editor:bypass", { sessionId: activeSubEditorSessionId, active: true }).catch(() => {});
    } else {
      activateBypass();
    }
  });
  el.btnLeverBypass?.addEventListener("pointerup", releaseAllLevers);
  el.btnLeverBypass?.addEventListener("pointercancel", releaseAllLevers);

  el.btnLeverCompare?.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    try {
      el.btnLeverCompare.setPointerCapture(ev.pointerId);
    } catch {
      // 忽略
    }
    if (IS_ELEMENT_EDITOR_SUBWINDOW) {
      if (isCompareActive || activeSubEditorSessionId <= 0) return;
      isCompareActive = true;
      el.btnLeverCompare.classList.add("active");
      void Events.Emit("elem-editor:compare", { sessionId: activeSubEditorSessionId, active: true }).catch(() => {});
    } else {
      activateCompare();
    }
  });
  el.btnLeverCompare?.addEventListener("pointerup", releaseAllLevers);
  el.btnLeverCompare?.addEventListener("pointercancel", releaseAllLevers);

  window.addEventListener("pointerup", releaseAllLevers);
  window.addEventListener("pointercancel", releaseAllLevers);
  window.addEventListener("blur", releaseAllLevers);
  window.addEventListener("pagehide", releaseAllLevers);
  document.addEventListener("visibilitychange", () => {
    if (document.hidden) releaseAllLevers();
  });

  if (IS_ELEMENT_EDITOR_SUBWINDOW) {
    document.addEventListener("keydown", (ev) => {
      if (ev.key === "Escape") {
        ev.preventDefault();
        void cancelSubEditor();
      }
    });
  }

  // Tab 1: 字体属性面板交互
  el.propFontFamily.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = el.propFontFamily.value;
    if (val) {
      setElementFamily(elem, val);
      if (elem.lookup) delete elem.lookup.filename;
      el.propFontFile.value = val;
    }
    renderElementList();
    schedulePreviewRender();
  });

  el.btnBrowseFontFile.addEventListener("click", async () => {
    try {
      const filePath = await call("SelectFontFile");
      if (!filePath) return;
      const elem = getCurrentFace()?.elements[activeElementIndex];
      if (!elem) return;
      const fileName = filePath.replace(/\\/g, "/").split("/").pop() || filePath;
      const baseFamily = fileName.replace(/\.[^/.]+$/, "");
      setElementFamily(elem, baseFamily);
      if (elem.lookup) {
        elem.lookup.filename = filePath;
      }
      el.propFontFile.value = filePath;
      renderElementList();
      renderPropsPanel();
      schedulePreviewRender();
    } catch (err) {
      if (!IS_ELEMENT_EDITOR_SUBWINDOW) {
        log(`[错误] 选择字体文件失败: ${describeError(err)}`, "error");
      }
    }
  });

  el.propFontFile.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = el.propFontFile.value.trim();
    if (val) {
      setElementFamily(elem, val);
    }
    renderElementList();
    schedulePreviewRender();
  });

  el.propRenderer.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const rendererVal = Number(el.propRenderer.value);
    const actual = isNaN(rendererVal) ? 2 : rendererVal;
    setElementRenderer(elem, actual);
    el.propsDirectwriteGroup.classList.toggle("hidden", actual !== 2);
    el.propsFreetypeGroup.classList.toggle("hidden", actual !== 3);
    el.propsEmptyGroup.classList.toggle("hidden", actual !== 0);
    el.propsGameFontNotice.classList.toggle("hidden", actual !== 1);
    el.propsEmptyFontNotice.classList.toggle("hidden", actual !== 0);
    el.propsExternalFontWrap.classList.toggle("hidden", actual === 1 || actual === 0);
    renderElementList();
    schedulePreviewRender();
  });

  el.propWeight.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.lookup!.weight = Number(el.propWeight.value) || 400;
    renderElementList();
    schedulePreviewRender();
  });

  el.propStyle.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.lookup!.style = Number(el.propStyle.value) || 0;
    renderElementList();
    schedulePreviewRender();
  });

  el.propStretch.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.lookup!.stretch = Number(el.propStretch.value) || 5;
    renderElementList();
    schedulePreviewRender();
  });

  el.propMergeMode.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const modeVal = Number(el.propMergeMode.value);
    elem.mergeMode = isNaN(modeVal) ? 0 : modeVal;
    renderElementList();
    schedulePreviewRender();
  });

  el.propSize.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = parseFloat(el.propSize.value);
    if (!isNaN(val) && val > 0) {
      elem.size = val;
      renderElementList();
      schedulePreviewRender();
    }
  });

  el.propGamma.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = parseFloat(el.propGamma.value);
    if (!isNaN(val) && val > 0) {
      elem.gamma = val;
      renderElementList();
      schedulePreviewRender();
    }
  });

  el.propBaselineShift.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = parseFloat(el.propBaselineShift.value);
    setElementBaselineShift(elem, isNaN(val) ? 0 : val);
    renderElementList();
    schedulePreviewRender();
  });

  el.propHorizontalOffset.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    const val = parseFloat(el.propHorizontalOffset.value);
    elem.wrapModifiers!.horizontalOffset = isNaN(val) ? 0 : val;
    renderElementList();
    schedulePreviewRender();
  });

  el.propLetterSpacing.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    const val = parseFloat(el.propLetterSpacing.value);
    elem.wrapModifiers!.letterSpacing = isNaN(val) ? 0 : val;
    renderElementList();
    schedulePreviewRender();
  });

  el.propScaleX.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = parseFloat(el.propScaleX.value);
    setElementScaleX(elem, !isNaN(val) && val > 0 ? val : 1.0);
    schedulePreviewRender();
  });

  el.propScaleY.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    const val = parseFloat(el.propScaleY.value);
    setElementScaleY(elem, !isNaN(val) && val > 0 ? val : 1.0);
    schedulePreviewRender();
  });

  el.propSkew.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.transform!.skew = parseFloat(el.propSkew.value) || 0;
    syncTransformationMatrix(elem);
    schedulePreviewRender();
  });

  el.propRotation.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.transform!.rotation = parseFloat(el.propRotation.value) || 0;
    syncTransformationMatrix(elem);
    schedulePreviewRender();
  });

  const bindDwSelect = (select: HTMLSelectElement, field: keyof DirectWriteSpecific) => {
    select.addEventListener("change", () => {
      const elem = getCurrentFace()?.elements[activeElementIndex];
      if (!elem) return;
      ensureElementDefaults(elem);
      elem.renderSpecific!.directwrite![field] = Number(select.value) || 0;
      schedulePreviewRender();
    });
  };

  bindDwSelect(el.propDwGridfit, "gridFitMode");
  bindDwSelect(el.propDwMeasure, "measureMode");
  bindDwSelect(el.propDwRender, "renderMode");

  el.propFtRender.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.renderSpecific!.freetype!.renderMode = Number(el.propFtRender.value) || 0;
    schedulePreviewRender();
  });

  el.propFtNohinting.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.renderSpecific!.freetype!.noHinting = el.propFtNohinting.checked;
    schedulePreviewRender();
  });

  el.propFtAutohint.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.renderSpecific!.freetype!.forceAutohint = el.propFtAutohint.checked;
    schedulePreviewRender();
  });

  el.propFtNobitmap.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.renderSpecific!.freetype!.noBitmap = el.propFtNobitmap.checked;
    schedulePreviewRender();
  });

  el.propEmptyAscent.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.renderSpecific!.empty!.ascent = parseInt(el.propEmptyAscent.value, 10) || 0;
    renderElementList();
    schedulePreviewRender();
  });

  el.propEmptyLineheight.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.renderSpecific!.empty!.lineHeight = parseInt(el.propEmptyLineheight.value, 10) || 0;
    renderElementList();
    schedulePreviewRender();
  });

  // Tab 2: 字符码点范围区间运算 (并集/差集/替换)
  el.btnCpClear.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.wrapModifiers!.codepoints = [];
    renderCodepointIntervalsList(elem);
    renderElementList();
    schedulePreviewRender();
  });

  document.querySelectorAll<HTMLButtonElement>("button[data-cp-op]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const elem = getCurrentFace()?.elements[activeElementIndex];
      if (!elem) return;
      ensureElementDefaults(elem);
      const op = btn.dataset.cpOp;
      const grp = btn.dataset.cpGroup || "";
      const targetRanges = CODEPOINT_PRESET_GROUPS[grp];
      if (!targetRanges) return;
      const cur = elem.wrapModifiers!.codepoints || [];
      if (op === "set") {
        elem.wrapModifiers!.codepoints = normalizeIntervals(targetRanges);
      } else if (op === "add") {
        elem.wrapModifiers!.codepoints = unionIntervals(cur, targetRanges);
      } else if (op === "sub") {
        const base = cur.length === 0 ? [[0, 1114111]] : cur;
        elem.wrapModifiers!.codepoints = subtractIntervals(base, targetRanges);
      }
      renderCodepointIntervalsList(elem);
      renderElementList();
      schedulePreviewRender();
    });
  });

  el.btnCpCustomAdd.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    const parsed = parseCustomCodepointInput(el.cpCustomInput.value);
    if (parsed.length === 0) return;
    elem.wrapModifiers!.codepoints = unionIntervals(elem.wrapModifiers!.codepoints || [], parsed);
    renderCodepointIntervalsList(elem);
    renderElementList();
    schedulePreviewRender();
  });

  el.btnCpCustomSub.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    const parsed = parseCustomCodepointInput(el.cpCustomInput.value);
    if (parsed.length === 0) return;
    const cur = elem.wrapModifiers!.codepoints || [];
    const base = cur.length === 0 ? [[0, 1114111]] : cur;
    elem.wrapModifiers!.codepoints = subtractIntervals(base, parsed);
    renderCodepointIntervalsList(elem);
    renderElementList();
    schedulePreviewRender();
  });

  el.propCodepointsText.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    const text = el.propCodepointsText.value;
    const ranges: number[][] = [];
    for (const part of text.split(",")) {
      const [startStr, endStr] = part.trim().split("-");
      const start = parseInt(startStr, 10);
      const end = endStr ? parseInt(endStr, 10) : start;
      if (!isNaN(start) && !isNaN(end)) {
        ranges.push([start, end]);
      }
    }
    elem.wrapModifiers!.codepoints = normalizeIntervals(ranges);
    renderCodepointIntervalsList(elem);
    renderElementList();
    schedulePreviewRender();
  });

  // Tab 3: OpenType 字体特性
  el.otFeaturesGrid.querySelectorAll<HTMLInputElement>("input[data-ot-tag]").forEach((chk) => {
    chk.addEventListener("change", () => {
      const elem = getCurrentFace()?.elements[activeElementIndex];
      if (!elem) return;
      ensureElementDefaults(elem);
      const selectedTags: string[] = [];
      el.otFeaturesGrid.querySelectorAll<HTMLInputElement>("input[data-ot-tag]:checked").forEach((c) => {
        if (c.dataset.otTag) selectedTags.push(c.dataset.otTag);
      });
      elem.lookup!.features = selectedTags;
      el.propOtCustom.value = selectedTags.join(", ");
      schedulePreviewRender();
    });
  });

  el.propOtCustom.addEventListener("change", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    const tags = el.propOtCustom.value
      .split(",")
      .map((s) => s.trim())
      .filter((s) => s.length === 4);
    elem.lookup!.features = tags;
    el.otFeaturesGrid.querySelectorAll<HTMLInputElement>("input[data-ot-tag]").forEach((chk) => {
      chk.checked = tags.includes(chk.dataset.otTag || "");
    });
    schedulePreviewRender();
  });

  // Tab 4: 字形合并与特殊符号替换
  el.btnMergePresetChsTime.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.wrapModifiers!.codepointReplacements = {
      ...(elem.wrapModifiers!.codepointReplacements || {}),
      "\uE0B0": "艾",
      "\uE0B1": "本",
      "\uE0B2": "服",
    };
    el.propReplacementsJson.value = formatReplacementsToText(elem.wrapModifiers!.codepointReplacements);
    renderReplacementsVisualList(elem);
    schedulePreviewRender();
  });

  el.btnMergePresetEnTime.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.wrapModifiers!.codepointReplacements = {
      ...(elem.wrapModifiers!.codepointReplacements || {}),
      "\uE0B0": "ET",
      "\uE0B1": "LT",
      "\uE0B2": "ST",
    };
    el.propReplacementsJson.value = formatReplacementsToText(elem.wrapModifiers!.codepointReplacements);
    renderReplacementsVisualList(elem);
    schedulePreviewRender();
  });

  el.btnMergeClear.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    delete elem.wrapModifiers!.codepointReplacements;
    el.propReplacementsJson.value = "";
    renderReplacementsVisualList(elem);
    schedulePreviewRender();
  });

  el.propReplacementsJson.addEventListener("input", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    elem.wrapModifiers!.codepointReplacements = parseTextToReplacements(el.propReplacementsJson.value);
    renderReplacementsVisualList(elem);
    schedulePreviewRender();
  });

  el.btnMergeToggleRaw.addEventListener("click", () => {
    const isHidden = el.mergeRawContainer.classList.toggle("hidden");
    el.btnMergeToggleRaw.textContent = isHidden ? "展开批量文本编辑" : "收起批量文本编辑";
  });

  el.btnMergeAddRule.addEventListener("click", () => {
    const elem = getCurrentFace()?.elements[activeElementIndex];
    if (!elem) return;
    ensureElementDefaults(elem);
    if (!elem.wrapModifiers!.codepointReplacements) {
      elem.wrapModifiers!.codepointReplacements = {};
    }
    const defaultKey = nextReplacementKey(elem.wrapModifiers!.codepointReplacements);
    elem.wrapModifiers!.codepointReplacements[defaultKey] = "";
    el.propReplacementsJson.value = formatReplacementsToText(elem.wrapModifiers!.codepointReplacements);
    renderReplacementsVisualList(elem);
    const inputs = el.mergeReplacementsList.querySelectorAll<HTMLInputElement>("input");
    if (inputs.length > 0) {
      inputs[inputs.length - 2]?.focus();
      inputs[inputs.length - 2]?.select();
    }
  });
}

function wireRuntimeEvents(): void {
  // 自更新进度：下载阶段显示百分比，替换与重启阶段只更新提示文案。
  Events.On("app-update-progress", (event) => {
    const ev = asProgress(event.data);
    if (!busy) return;
    if (ev.total > 0) {
      const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
      el.busyIndicator.textContent = `${ev.detail}… ${pct.toFixed(0)}%`;
      el.progressBar.style.width = `${pct.toFixed(1)}%`;
      el.actionProgressPercent.textContent = `${pct.toFixed(0)}%`;
      return;
    }
    el.busyIndicator.textContent = ev.detail ? `${ev.detail}…` : "正在更新…";
    el.actionProgressPercent.textContent = "";
  });

  // 制作副本要逐个创建 7114 个 CSV（约 2.7 秒），进度同样压在选择器上。
  Events.On("csv-copy-progress", (event) => {
    const ev = asProgress(event.data);
    if (!csvBusyText || ev.total <= 0) return;
    const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
    el.csvSummary.textContent = `${ev.detail || "制作副本"} ${pct.toFixed(0)}%`;
    el.csvSyncBar.classList.remove("is-pending");
    el.csvSyncBar.style.width = `${pct.toFixed(1)}%`;
  });

  Events.On("font-compiler-progress", (event) => {
    const ev = asProgress(event.data);
    if (ev.stage === "done" || ev.stage === "failed") {
      if (ev.stage === "done" && ev.detail === "ready") {
        fontCompilerReady = true;
      }
      if (!openingFontEditor) {
        resetFontCompilerButtonState();
      }
      return;
    }
    if (!openingFontEditor) return;
    el.btnOpenFontEditor.classList.add("is-syncing");
    if (ev.total > 0) {
      const pct = Math.min(100, Math.max(0, (ev.done / ev.total) * 100));
      el.btnOpenFontEditorText.textContent = `${ev.detail || "准备组件中"} ${pct.toFixed(0)}%`;
      el.fontCompilerBar.classList.remove("is-pending");
      el.fontCompilerBar.style.width = `${pct.toFixed(1)}%`;
    } else {
      el.btnOpenFontEditorText.textContent = `${ev.detail || "准备组件中"}…`;
      el.fontCompilerBar.classList.add("is-pending");
    }
  });

  // 资源同步的进度直接压在内置词条选择器上；汉化期间的进度同时走顶部进度条。
  let pendingResourceProgress: PatchProgressEvent | null = null;
  let resourceRafId = 0;
  Events.On("resource-progress", (event) => {
    const ev = asProgress(event.data);
    pendingResourceProgress = ev;
    if (!resourceRafId) {
      resourceRafId = requestAnimationFrame(() => {
        resourceRafId = 0;
        const current = pendingResourceProgress;
        if (!current) return;
        if (!isPatching && !syncingResource) return;
        // 内置词条的同步进度直接压在选择器文字上，文案用后端给的当前阶段。
        if (!bootstrap?.csv.selected && current.stage !== "font") {
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
          el.progressBar.style.width = "0%";
          el.busyIndicator.textContent = `同步资源：${current.detail || "请稍候…"}`;
          el.actionProgressPercent.textContent = "";
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

  Events.On("patch-progress", (event) => {
    const ev = asProgress(event.data);
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

  // 字体编译是独立动作，进度只压在这一行下方的小进度条上。
  Events.On("font-build-progress", (event) => {
    if (!fontCompiling) return;
    const ev = asProgress(event.data);
    const pct = ev.total > 0 ? Math.min(100, Math.max(0, (ev.done / ev.total) * 100)) : 0;
    el.fontBuildBar.style.width = `${pct.toFixed(1)}%`;
  });

  Events.On("patch-log", (event) => {
    if (typeof event.data === "string") {
      log(event.data);
    }
  });

  Events.On("unknown-confirm", async (event) => {
    const text =
      typeof event.data === "string"
        ? event.data
        : "云端暂未收录当前游戏版本，无法核对是否为原版，确定继续？";
    const ok = await showConfirm("未知版本", text, "继续汉化");
    try {
      await call("AnswerUnknownConfirm", ok);
    } catch (error) {
      log(`确认反馈失败: ${describeError(error)}`, "error");
    }
  });

  Events.On("launcher-confirm", async (event) => {
    const text =
      typeof event.data === "string"
        ? event.data
        : "游戏启动器正在运行，是否强制关闭并继续？";
    const ok = await showConfirm("启动器正在运行", text, "强制关闭并继续");
    try {
      await call("AnswerLauncherConfirm", ok);
    } catch (error) {
      log(`确认反馈失败: ${describeError(error)}`, "error");
    }
  });

  Events.On("elem-editor:update", (event) => {
    const data = event.data as {
      sessionId?: number;
      faceName?: string;
      elementIndex?: number;
      element?: FontElement;
    } | null;
    if (!subEditorOpen || !data || data.sessionId !== activeSubEditorSessionId || !data.element) return;
    if (isCompareActive || isBypassActive) return;
    const hit = findFaceElement(data.faceName, data.elementIndex);
    if (!hit) return;
    hit.face.elements[hit.index] = data.element;
    renderElementList();
    schedulePreviewRender();
  });

  Events.On("elem-editor:bypass", (event) => {
    const data = event.data as { sessionId?: number; active?: boolean } | null;
    if (!subEditorOpen || !data || data.sessionId !== activeSubEditorSessionId) return;
    if (data.active) {
      activateBypass(subEditorFaceName, subEditorElementIndex);
    } else {
      deactivateBypass();
    }
  });

  Events.On("elem-editor:compare", (event) => {
    const data = event.data as { sessionId?: number; active?: boolean } | null;
    if (!subEditorOpen || !data || data.sessionId !== activeSubEditorSessionId) return;
    if (data.active) {
      activateCompare(subEditorFaceName, subEditorElementIndex);
    } else {
      deactivateCompare();
    }
  });

  Events.On("elem-editor:commit", (event) => {
    const data = event.data as {
      sessionId?: number;
      faceName?: string;
      elementIndex?: number;
      element?: FontElement;
    } | null;
    if (!subEditorOpen || !data || data.sessionId !== activeSubEditorSessionId) return;
    deactivateBypass();
    deactivateCompare();
    if (data.element) {
      const hit = findFaceElement(data.faceName, data.elementIndex);
      if (hit) {
        hit.face.elements[hit.index] = data.element;
      }
    }
    closeElementEditorModal(true, false);
  });

  Events.On("elem-editor:cancel", (event) => {
    const data = event.data as { sessionId?: number } | null;
    if (!subEditorOpen || (data?.sessionId && data.sessionId !== activeSubEditorSessionId)) return;
    closeElementEditorModal(false, false);
  });

  Events.On("elem-editor:native-close", () => {
    if (!subEditorOpen) return;
    closeElementEditorModal(false, false);
  });
}

initTooltips();
void bootstrapApp();
