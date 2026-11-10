package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	goruntime "runtime"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/wailsapp/wails/v3/pkg/application"
	"github.com/wailsapp/wails/v3/pkg/events"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/fontengine"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/gamecfg"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/gameproc"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/gamescan"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/replace"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/updater"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

var appVersion = "dev"

// appUpdateRepo 是主程序发布所在的仓库，资源版本核对与自更新共用。
const appUpdateRepo = "Souma-Sumire/FFXIVChnTextPatch-Souma"

const tcBackupURL = "https://github.com/NaofumiRaphtalia/FFXIVChnTextPatch-Souma/releases"

// resourceRefreshInterval 是开始汉化前重新检查内置词条更新的间隔。
// 只在启动时同步的话，长期不关程序的用户会一直用旧词条。
const resourceRefreshInterval = 30 * time.Minute

// App 是 Wails 绑定到前端的应用对象。
type App struct {
	ctx context.Context

	mu   sync.Mutex
	busy bool

	confirmMu sync.Mutex
	confirmCh chan bool

	launcherConfirmMu sync.Mutex
	launcherConfirmCh chan bool

	updater *updater.Updater

	fontEngine *fontengine.Engine

	// resourceLock 互斥“写资源目录”（同步）与“读资源目录”（汉化）。
	// 解压是逐文件覆盖，若与汉化并发，CSV 可能被读到一半，产出部分表未汉化的补丁。
	resourceLock sync.Mutex

	// fontCompilerMu 序列化字体编译组件的检查、迁移与下载，避免并发覆盖 .part 临时文件。
	fontCompilerMu sync.Mutex

	// builtinSynced 记录本次运行内置资源是否已同步通过。
	// 未通过时内置词条不允许汉化，避免读到未校验或残缺的 CSV。
	builtinSynced atomic.Bool

	// resourceSyncedAt 是上次内置资源检查完成的时间（UnixNano）。
	// 程序可能长期不退出，开始汉化前据此判断要不要重新确认云端词条版本。
	resourceSyncedAt atomic.Int64

	progressMu   sync.Mutex
	lastProgress time.Time
	lastDetail   string
	lastStage    string

	// cfgMu 序列化配置写盘：前端每个开关都异步触发 SaveConfig，
	// 无锁并发写同一个 .tmp 再互相 rename 会丢更新甚至写出混合内容。
	cfgMu sync.Mutex

	updateEmitMu sync.Mutex
	updateEmitAt time.Time

	resourceDirOverride string
	cloudOverride       verifier.Cloud
	skipProcessCheck    bool

	// cloud 复用同一个 GitHub 客户端：镜像节点的连通性记忆（是否可直连、哪个节点最快）
	// 需要在更新检查、汉化与还原之间共享，否则每条链路都要重新探测一遍。
	cloudOnce sync.Once
	cloud     *verifier.GitHubClient

	scanMu      sync.Mutex
	scanDirs    []string
	scanCached  bool
	scanRunning chan struct{}

	detectMu     sync.Mutex
	detectCancel context.CancelFunc

	cancelPatchMu sync.Mutex
	cancelPatch   context.CancelFunc

	wailsApp *application.App
	mainWin  *application.WebviewWindow

	elemWinMu   sync.Mutex
	elemWin     *application.WebviewWindow
	elemState   string
	elemStateMu sync.RWMutex
	quitting    atomic.Bool
}

func (a *App) getCloud() verifier.Cloud {
	if a != nil && a.cloudOverride != nil {
		return a.cloudOverride
	}
	if a == nil {
		return verifier.NewGitHubClient()
	}
	a.cloudOnce.Do(func() { a.cloud = verifier.NewGitHubClient() })
	return a.cloud
}

func (a *App) resourceDir() string {
	if a != nil && a.resourceDirOverride != "" {
		return a.resourceDirOverride
	}
	return resolveResourceDir()
}

// NewApp 创建应用实例。
func NewApp() *App {
	u := updater.New()
	u.LocalOnly = localResourceOnly
	return &App{
		updater:    u,
		fontEngine: fontengine.NewEngine(""),
	}
}

// SetRuntime 注入 Wails v3 应用与主窗口实例。
func (a *App) SetRuntime(wailsApp *application.App, mainWin *application.WebviewWindow) {
	a.wailsApp = wailsApp
	a.mainWin = mainWin
}

// ServiceStartup 保存 Wails 服务上下文，并清理上一次自更新留下的旧程序文件。
func (a *App) ServiceStartup(ctx context.Context, _ application.ServiceOptions) error {
	a.ctx = ctx
	if exe, err := os.Executable(); err == nil {
		updater.CleanupStaleAppFiles(exe)
	}
	return nil
}

// ServiceShutdown 在服务退出前释放资源。
func (a *App) ServiceShutdown() error {
	return nil
}

// BeginQuit 在主窗口关闭时联动销毁单例编辑子窗口并退出应用。
func (a *App) BeginQuit() {
	if a == nil || a.quitting.Swap(true) {
		return
	}
	a.elemWinMu.Lock()
	ew := a.elemWin
	a.elemWin = nil
	a.elemWinMu.Unlock()
	if ew != nil {
		ew.Close()
	}
	if a.wailsApp != nil {
		a.wailsApp.Quit()
	}
}

func (a *App) hasUI() bool {
	return a != nil && a.wailsApp != nil
}

func (a *App) emitEvent(name string, data ...any) {
	if !a.hasUI() {
		return
	}
	a.wailsApp.Event.Emit(name, data...)
}

func (a *App) activeDialogWindow() application.Window {
	if a == nil {
		return nil
	}
	a.elemWinMu.Lock()
	ew := a.elemWin
	a.elemWinMu.Unlock()
	if ew != nil && ew.IsVisible() && ew.IsFocused() {
		return ew
	}
	if a.mainWin != nil {
		return a.mainWin
	}
	return nil
}

func normalizeDialogSelection(path string, err error) (string, error) {
	if err != nil {
		if strings.Contains(strings.ToLower(err.Error()), "cancel") {
			return "", nil
		}
		return "", err
	}
	return path, nil
}

// LanguageOption 是前端可选的语言。
type LanguageOption struct {
	Code      string `json:"code"`
	Name      string `json:"name"`
	Region    string `json:"region"`
	BackupURL string `json:"backupUrl"`
}

// RegionLanguage 是区服内可勾选参与汉化的语言选项。
type RegionLanguage struct {
	Code string `json:"code"`
	Name string `json:"name"`
}

// globalTargetLanguages 是国际服允许勾选的全部客户端语言（固定顺序）。
var globalTargetLanguages = []RegionLanguage{
	{Code: string(region.JA), Name: region.LanguageName(region.JA)},
	{Code: string(region.EN), Name: region.LanguageName(region.EN)},
	{Code: string(region.DE), Name: region.LanguageName(region.DE)},
	{Code: string(region.FR), Name: region.LanguageName(region.FR)},
}

// defaultGlobalTargetLanguages 是国际服未保存过勾选时的默认汉化语言。
var defaultGlobalTargetLanguages = []string{"JA", "EN"}

// sanitizeTargetLanguages 校验并规范化目标语言列表。
// 仅国际服支持多语言选择；非国际服固定为该区服唯一语言，仍返回单值以便配置可读。
// 返回值按 globalTargetLanguages 的固定顺序排列。
func sanitizeTargetLanguages(lang region.Language, langs []string) []string {
	if region.Of(lang) != region.Global {
		return region.TargetLanguagesFor(lang)
	}
	seen := map[string]bool{}
	for _, l := range langs {
		u := strings.ToUpper(strings.TrimSpace(l))
		for _, opt := range globalTargetLanguages {
			if u == opt.Code {
				seen[u] = true
			}
		}
	}
	out := make([]string, 0, len(seen))
	for _, opt := range globalTargetLanguages {
		if seen[opt.Code] {
			out = append(out, opt.Code)
		}
	}
	return out
}

// TableMeta 是 tables.json 中的表信息。
// TablesDoc 对应 resource/tables.json。
type TablesDoc struct {
	Folders map[string]string `json:"folders"`
	Tables  map[string]string `json:"tables"`
	Files   []string          `json:"files"`
}

// recommendedExcludedTables 是推荐预设下默认排除的数据表列表（仅默认排除影响战斗解析的 BNpcName）。
var recommendedExcludedTables = []string{"BNpcName"}

// compatibilityWarnings 是表级插件兼容提示，运行时由后端统一提供。
var compatibilityWarnings = map[string]string{
	"BNpcName":   "FFLOGS、Cactbot 等插件依赖敌人名称，汉化后可能无法正常工作。",
	"LogMessage": "Cactbot Radar 等插件依赖系统日志，汉化后可能无法正确识别副本区切换。",
	"Lobby":      "Lifestream 等插件依赖登录大厅文本，汉化后自动登录等功能可能失效。",
}

// TableEntry 是前端表格选择列表中的一项。
type TableEntry struct {
	Key     string `json:"key"`
	Name    string `json:"name"`
	Folder  string `json:"folder"`
	Warning string `json:"warning"`
}

// LanguagesByRegion 按区服保存汉化语言勾选，键为区服代码。
// 旧版把它存成数组，反序列化时迁移到空键，由 loadConfig 归入当前区服。
type LanguagesByRegion map[string][]string

// UnmarshalJSON 兼容旧版的数组形式，避免整份配置因字段类型变化而丢弃。
func (m *LanguagesByRegion) UnmarshalJSON(data []byte) error {
	if len(data) == 0 || string(data) == "null" {
		return nil
	}
	if data[0] == '[' {
		var legacy []string
		if err := json.Unmarshal(data, &legacy); err != nil {
			return err
		}
		*m = LanguagesByRegion{"": legacy}
		return nil
	}
	var byRegion map[string][]string
	if err := json.Unmarshal(data, &byRegion); err != nil {
		return err
	}
	*m = byRegion
	return nil
}

// AppConfig 是持久化配置。
// 客户端目录与汉化语言都按区服保存，当前值由 Language 对应的条目决定。
type AppConfig struct {
	GameDirs        map[string]string `json:"gameDirs"`
	Language        string            `json:"language"`
	TargetLanguages LanguagesByRegion `json:"targetLanguages"`
	ReplaceText     bool              `json:"replaceText"`
	ReplaceFont     bool              `json:"replaceFont"`
	Rules           map[string]bool   `json:"rules"`
	Preset          string            `json:"preset"`
	CSVDirs         CSVDirsByRegion   `json:"csvDirs"`
	FontPresets     map[string]string `json:"fontPresets,omitempty"`
	// FontSource 决定换字体时用内置字体包还是现场编译自定义方案：package | compile。
	FontSource string `json:"fontSource,omitempty"`
	// FontTexture16Bit 是否开启 16 位贴图压缩（默认开启，减半贴图体积与显存）。
	FontTexture16Bit *bool `json:"fontTexture16Bit,omitempty"`
}

// BootstrapInfo 是前端启动所需的全部静态信息。
type BootstrapInfo struct {
	Version         string                      `json:"version"`
	ResourceDir     string                      `json:"resourceDir"`
	ResourceReady   bool                        `json:"resourceReady"`
	DevMode         bool                        `json:"devMode"`
	Rules           []pipeline.RuleMeta         `json:"rules"`
	Languages       []LanguageOption            `json:"languages"`
	RegionLanguages map[string][]RegionLanguage `json:"regionLanguages"`
	Config          AppConfig                   `json:"config"`
	Folders         map[string]string           `json:"folders"`
	CSV             CSVContext                  `json:"csv"`
	Excluded        []string                    `json:"excluded"`
	ResourceTime    string                      `json:"resourceTime,omitempty"`
	ResourceHash    string                      `json:"resourceHash,omitempty"`
	FontCompilerReady bool                      `json:"fontCompilerReady"`
}

// FileStatus 描述单个客户端文件状态。
type FileStatus struct {
	Name   string `json:"name"`
	Exists bool   `json:"exists"`
	Size   int64  `json:"size"`
}

// GameInfo 描述游戏目录与版本状态。
type GameInfo struct {
	GameDir        string       `json:"gameDir"`
	Version        string       `json:"version"`
	Valid          bool         `json:"valid"`
	Files          []FileStatus `json:"files"`
	BackupVersion  string       `json:"backupVersion"`
	BackupExists   bool         `json:"backupExists"`
	Region         string       `json:"region"`
	DetectedRegion string       `json:"detectedRegion,omitempty"`
	RegionMismatch bool         `json:"regionMismatch"`
	ResourceTime   string       `json:"resourceTime,omitempty"`
	ResourceHash   string       `json:"resourceHash,omitempty"`
	UiAssetType    int          `json:"uiAssetType"`
}

// PatchRequest 是汉化请求。
type PatchRequest struct {
	GameDir         string          `json:"gameDir"`
	Language        string          `json:"language"`
	TargetLanguages []string        `json:"targetLanguages"`
	ReplaceText     bool            `json:"replaceText"`
	ReplaceFont     bool            `json:"replaceFont"`
	Rules           map[string]bool `json:"rules"`
	SelectedTables  []string        `json:"selectedTables"`
	Preset          string          `json:"preset"`
}

// ProgressEvent 是进度事件。
type ProgressEvent struct {
	Stage  string `json:"stage"`
	Detail string `json:"detail"`
	Done   int64  `json:"done"`
	Total  int64  `json:"total"`
}

// PatchResponse 是汉化结果。
type PatchResponse struct {
	Plan       verifier.Plan `json:"plan"`
	Text       replace.Stats `json:"text"`
	Font       replace.Stats `json:"font"`
	Cancelled  bool          `json:"cancelled"`
	Downloaded []string      `json:"downloaded"`
	Warnings   []string      `json:"warnings"`
	Notes      []string      `json:"notes"`
}

// RestoreResponse 是还原结果。
type RestoreResponse struct {
	Restored  []string `json:"restored"`
	Cancelled bool     `json:"cancelled"`
}

// SyncSummary 是云端同步结果。
type SyncSummary struct {
	UpToDate bool `json:"upToDate"`
	Changed  int  `json:"changed"`
	Full     bool `json:"full"`
}

func executableDir() string {
	exe, err := os.Executable()
	if err != nil {
		return "."
	}
	return filepath.Dir(exe)
}

// inAppBundle 判断程序是否从 macOS 的 .app 包内启动。
func inAppBundle(dir string) bool {
	return goruntime.GOOS == "darwin" && isAppBundlePath(dir)
}

// isAppBundlePath 单独抽出路径判断，便于在没有 Mac 的环境验证。
func isAppBundlePath(dir string) bool {
	parts := strings.Split(filepath.ToSlash(filepath.Clean(dir)), "/")
	for i := 0; i+2 < len(parts); i++ {
		if strings.HasSuffix(parts[i], ".app") && parts[i+1] == "Contents" && parts[i+2] == "MacOS" {
			return true
		}
	}
	return false
}

// userDataResourceDir 返回 macOS 发布包专用的数据目录。
// 从 Finder 启动时工作目录是 /，往 .app 包里写资源会破坏签名，
// 覆盖式升级还会把配置、自定义词条和还原基线一起删掉。
func userDataResourceDir() string {
	base, err := os.UserConfigDir()
	if err != nil || base == "" {
		return ""
	}
	dir := filepath.Join(base, "FFXIVChnTextPatch", "resource")
	if os.MkdirAll(dir, 0o755) == nil && isWritableDir(dir) {
		return dir
	}
	return ""
}

var (
	resourceDirOnce sync.Once
	resourceDirPath string
)

type writableDirEntry struct {
	ok        bool
	checkedAt time.Time
}

const writableDirCacheTTL = time.Minute

var writableDirCache sync.Map

// isWritableDir 需要真正落一个探测文件，调用一次就会在目标目录写盘。
// 结果做短时缓存：既避免诊断快照连续调用时重复写盘，也不会因权限或目录变化永久失效。
func isWritableDir(dir string) bool {
	if cached, ok := writableDirCache.Load(dir); ok {
		entry := cached.(writableDirEntry)
		if time.Since(entry.checkedAt) < writableDirCacheTTL {
			if !entry.ok {
				return false
			}
			if st, err := os.Stat(dir); err == nil && st.IsDir() {
				return true
			}
		}
	}
	probe := filepath.Join(dir, ".write_test")
	f, err := os.Create(probe)
	if err == nil {
		_ = f.Close()
		_ = os.Remove(probe)
	}
	writableDirCache.Store(dir, writableDirEntry{ok: err == nil, checkedAt: time.Now()})
	return err == nil
}

func resolveResourceDir() string {
	resourceDirOnce.Do(func() {
		if inAppBundle(executableDir()) {
			if dir := userDataResourceDir(); dir != "" {
				resourceDirPath = dir
				return
			}
		}
		existing := []string{
			"resource",
			filepath.Join("..", "resource"),
			filepath.Join(executableDir(), "resource"),
		}
		for _, dir := range existing {
			if st, err := os.Stat(filepath.Join(dir, "rawexd")); err == nil && st.IsDir() && isWritableDir(dir) {
				resourceDirPath = dir
				return
			}
		}
		create := []string{filepath.Join(executableDir(), "resource")}
		if cacheDir, err := os.UserCacheDir(); err == nil && cacheDir != "" {
			create = append(create, filepath.Join(cacheDir, "FFXIVChnTextPatch", "resource"))
		}
		if local := os.Getenv("LOCALAPPDATA"); local != "" {
			create = append(create, filepath.Join(local, "FFXIVChnTextPatch", "resource"))
		}
		for _, dir := range create {
			if err := os.MkdirAll(dir, 0o755); err == nil && isWritableDir(dir) {
				resourceDirPath = dir
				return
			}
		}
		resourceDirPath = filepath.Join(executableDir(), "resource")
	})
	return resourceDirPath
}

// dataRootFor 返回持久化数据目录。macOS 下若资源位于 Caches，则备份与配置改放到
// Application Support，避免被系统清理。
func dataRootFor(resourceDir string) string {
	parent := filepath.Dir(resourceDir)
	if goruntime.GOOS == "darwin" {
		if cache, err := os.UserCacheDir(); err == nil && withinDir(parent, cache) {
			if configDir, err := os.UserConfigDir(); err == nil {
				return filepath.Join(configDir, "FFXIVChnTextPatch")
			}
		}
	}
	return parent
}

// withinDir 按目录边界判断 child 是否位于 parent 之内。
func withinDir(child, parent string) bool {
	return child == parent || strings.HasPrefix(child, parent+string(os.PathSeparator))
}

func backupRootFor(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "backup")
}

func (a *App) backupDirFor(reg region.Region) string {
	return filepath.Join(backupRootFor(a.resourceDir()), string(reg))
}

func configPathFor(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "config.json")
}

func presetsDirFor(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "presets")
}

func normalizeRegionCode(lang string) string {
	parsed, err := region.ParseLanguage(lang)
	if err != nil {
		return string(region.GLOBAL)
	}
	switch region.Of(parsed) {
	case region.China:
		return string(region.CHS)
	case region.Taiwan:
		return string(region.TC)
	case region.Korea:
		return string(region.KO)
	default:
		return string(region.GLOBAL)
	}
}

func defaultConfig() AppConfig {
	default16Bit := true
	return AppConfig{
		GameDirs:        map[string]string{},
		Language:        string(region.GLOBAL),
		TargetLanguages: LanguagesByRegion{},
		ReplaceText:     true,
		ReplaceFont:     true,
		Rules:           pipeline.DefaultRuleStates(),
		Preset:          "recommended",
		CSVDirs:         CSVDirsByRegion{},
		FontPresets: map[string]string{
			"GLOBAL": fontengine.BuiltinChnAXISSystemPresetID,
			"CHS":    fontengine.BuiltinChnPresetID,
			"KO":     fontengine.BuiltinKrnPresetID,
			"TC":     fontengine.BuiltinTcPresetID,
		},
		FontTexture16Bit: &default16Bit,
	}
}

// loadRules 读取 resource/rules.json 构造规则集合。
// 规则数据只随资源下发，缺失或损坏时返回不含数据的规则集，
// 对应规则在 Meta 中标注为不可用，由前端禁用开关，不再回退到编译期数据。
func loadRules(resourceDir string) ([]pipeline.Rule, error) {
	rules, err := pipeline.LoadRulesFromFile(filepath.Join(resourceDir, "rules.json"))
	if err == nil {
		return rules, nil
	}
	if rules != nil {
		return rules, fmt.Errorf("规则数据 rules.json 无效（%v）", err)
	}
	if errors.Is(err, os.ErrNotExist) {
		// 资源尚未同步时由首次汉化或手动同步处理，此处不重复提示；
		// 已存在词条目录却缺规则文件，说明资源同步不完整。
		if _, statErr := os.Stat(filepath.Join(resourceDir, "rawexd")); statErr == nil {
			return pipeline.DataLessRules(), errors.New("规则数据 rules.json 缺失，相关选项不可用")
		}
		return pipeline.DataLessRules(), nil
	}
	return pipeline.DataLessRules(), fmt.Errorf("规则数据 rules.json 无法读取（%v）", err)
}

func (a *App) loadConfig(resourceDir string) AppConfig {
	a.cfgMu.Lock()
	defer a.cfgMu.Unlock()
	return a.loadConfigLocked(resourceDir)
}

func (a *App) loadConfigLocked(resourceDir string) AppConfig {
	cfg := defaultConfig()
	cfgPath := configPathFor(resourceDir)
	data, err := os.ReadFile(cfgPath)
	if err != nil {
		if !errors.Is(err, os.ErrNotExist) {
			a.emitPatchLog(fmt.Sprintf("[警告] 读取配置文件 %s 失败: %v，已使用默认配置", cfgPath, err))
		}
		return cfg
	}
	var saved AppConfig
	if err := json.Unmarshal(data, &saved); err != nil {
		a.emitPatchLog(fmt.Sprintf("[警告] 解析配置文件 %s 失败: %v，已使用默认配置", cfgPath, err))
		return cfg
	}
	if saved.Language != "" {
		parsed, err := region.ParseLanguage(saved.Language)
		if err == nil && region.IsGlobal(parsed) {
			cfg.Language = string(region.GLOBAL)
		} else if err == nil {
			cfg.Language = string(parsed)
		}
	}
	if saved.GameDirs != nil {
		cfg.GameDirs = saved.GameDirs
	}
	// 旧配置把客户端目录写在顶层 gameDir，仅在完全没有分区记录时迁移一次。
	if len(cfg.GameDirs) == 0 {
		var legacy struct {
			GameDir string `json:"gameDir"`
		}
		if json.Unmarshal(data, &legacy) == nil && legacy.GameDir != "" {
			cfg.GameDirs[normalizeRegionCode(cfg.Language)] = legacy.GameDir
		}
	}

	// 汉化语言按区服分别保存；旧版的单个数组只可能来自国际服，固定迁入 GLOBAL。
	if legacy, ok := saved.TargetLanguages[""]; ok {
		globalKey := string(region.GLOBAL)
		if len(saved.TargetLanguages[globalKey]) == 0 {
			saved.TargetLanguages[globalKey] = legacy
		}
		delete(saved.TargetLanguages, "")
	}
	for code, langs := range saved.TargetLanguages {
		parsed, err := region.ParseLanguage(normalizeRegionCode(code))
		if err != nil {
			continue
		}
		if cleaned := sanitizeTargetLanguages(parsed, langs); len(cleaned) > 0 {
			cfg.TargetLanguages[normalizeRegionCode(code)] = cleaned
		}
	}

	// 布尔开关仅在配置里确实写了该字段时才覆盖默认值：
	// 旧版配置缺字段时零值会把默认勾选的「替换文本/字体」悄悄变成不勾选。
	var boolFields struct {
		ReplaceText      *bool `json:"replaceText"`
		ReplaceFont      *bool `json:"replaceFont"`
		FontTexture16Bit *bool `json:"fontTexture16Bit"`
	}
	if json.Unmarshal(data, &boolFields) == nil {
		if boolFields.ReplaceText != nil {
			cfg.ReplaceText = *boolFields.ReplaceText
		}
		if boolFields.ReplaceFont != nil {
			cfg.ReplaceFont = *boolFields.ReplaceFont
		}
		if boolFields.FontTexture16Bit != nil {
			cfg.FontTexture16Bit = boolFields.FontTexture16Bit
		}
	}
	cfg.Preset = saved.Preset
	if len(saved.CSVDirs) > 0 {
		for code, conf := range saved.CSVDirs {
			normalized := normalizeCSVConfig(conf)
			if len(normalized.Dirs) == 0 {
				continue
			}
			cfg.CSVDirs[normalizeRegionCode(code)] = normalized
		}
	}
	if len(saved.Rules) > 0 {
		for id := range cfg.Rules {
			if v, ok := saved.Rules[id]; ok {
				cfg.Rules[id] = v
			}
		}
	}
	if len(saved.FontPresets) > 0 {
		for code, p := range saved.FontPresets {
			cfg.FontPresets[normalizeRegionCode(code)] = p
		}
	}
	if cfg.FontPresets["GLOBAL"] == fontengine.BuiltinChnAXISSystemPresetID && isGameClientDir(cfg.GameDirs[string(region.CHS)]) {
		cfg.FontPresets["GLOBAL"] = fontengine.BuiltinChnAXISPresetID
	}
	if strings.EqualFold(strings.TrimSpace(saved.FontSource), "compile") {
		cfg.FontSource = "compile"
	}
	return cfg
}

// emitPatchLog 是后端日志的唯一出口。诊断行会随反馈贴到公开 issue，
// 统一在这里隐去路径中的用户名，避免某处漏改就把本机账户带出去。
func (a *App) emitPatchLog(msg string) {
	a.emitEvent("patch-log", redactPaths(msg))
}

func shortHash(h string) string {
	if len(h) > 8 {
		return h[:8]
	}
	return h
}

// formatResourceHashDetail 生成“CSV: xxxxxxxx”描述，跳过缺失的哈希。
// 字体不再随资源包分发，因此这里只剩 CSV。
func formatResourceHashDetail(m *updater.Manifest) string {
	if m == nil {
		return "哈希未知"
	}
	if h := shortHash(m.RawEXD.SHA256); h != "" {
		return "CSV: " + h
	}
	return "哈希未知"
}

func formatResourceTime(isoStr string) string {
	if isoStr == "" {
		return ""
	}
	t, err := time.Parse(time.RFC3339, isoStr)
	if err != nil {
		return isoStr
	}
	return t.Local().Format("2006-01-02 15:04")
}

// lockMutexWithContext 取得互斥锁，等待期间可被 ctx 取消。
func lockMutexWithContext(ctx context.Context, mu *sync.Mutex) (func(), error) {
	if ctx == nil {
		ctx = context.Background()
	}
	if mu.TryLock() {
		return mu.Unlock, nil
	}
	done := make(chan struct{})
	go func() {
		mu.Lock()
		close(done)
	}()
	select {
	case <-done:
		return mu.Unlock, nil
	case <-ctx.Done():
		// 放弃等待，但等锁真正到手后必须立即释放，否则这把锁会永远被占住。
		go func() {
			<-done
			mu.Unlock()
		}()
		return nil, ctx.Err()
	}
}

// lockResource 取得资源目录的独占访问权，等待期间可被 ctx 取消。
func (a *App) lockResource(ctx context.Context) (func(), error) {
	return lockMutexWithContext(ctx, &a.resourceLock)
}

// ensureBuiltinResource 拉取云端清单并校验内置资源，缺失或过期时下载修复。
// 内部自行占用资源锁。
func (a *App) ensureBuiltinResource(ctx context.Context, resourceDir string) error {
	unlock, err := a.lockResource(ctx)
	if err != nil {
		return err
	}
	defer unlock()

	status, err := a.updater.Provision(ctx, resourceDir, a.emitResourceProgress)
	a.emitDiag("资源同步结果：%+v，原始错误=%v", status, err)
	// 失败也算检查过：否则每次开始汉化都要为同一次网络故障等超时。
	a.resourceSyncedAt.Store(time.Now().UnixNano())
	if err != nil {
		if a.updater.LocalOnly {
			return fmt.Errorf("本地内置资源不可用：%w", err)
		}
		return fmt.Errorf("同步内置资源失败：%w", err)
	}
	if status.Updated && status.DownloadedFiles > 0 {
		updates := make([]string, 0, 2)
		if status.CSVUpdated {
			updates = append(updates, "CSV")
		}
		if len(updates) == 0 {
			updates = append(updates, "规则配置")
		}
		a.emitPatchLog(fmt.Sprintf("[资源] 已同步最新资源（%s）", strings.Join(updates, "、")))
	} else if status.Updated {
		a.emitPatchLog("[资源] 已修复内置资源并完成哈希校验")
	} else if a.updater.LocalOnly {
		a.emitPatchLog("[资源] 开发构建：直接使用 resource/ 中的词条，跳过云端同步")
	} else {
		a.emitPatchLog("[资源] 内置资源已是最新，哈希校验通过")
	}
	a.builtinSynced.Store(true)
	return nil
}

// resourceIncomplete 判断本次请求是否缺少必须的内置资源文件。
// 表清单由 ensureTableDoc 单独补齐，不触发整包同步。字体由 fontengine 动态管理。
func resourceIncomplete(resourceDir string, custom bool) bool {
	if custom {
		return false
	}
	_, err := os.Stat(filepath.Join(resourceDir, "rawexd"))
	return err != nil
}

// resourceSyncStale 判断内置词条是否需要在开始汉化前重新确认云端版本。
func (a *App) resourceSyncStale() bool {
	last := a.resourceSyncedAt.Load()
	return last == 0 || time.Since(time.Unix(0, last)) >= resourceRefreshInterval
}

// syncFailureHint 按构建模式给出资源同步失败后的处理建议。
func (a *App) syncFailureHint() string {
	if a.updater.LocalOnly {
		return "（本地开发构建不联网，请从仓库恢复 resource/ 后重试）"
	}
	return "（可联网重试或改用自定义词条目录）"
}

// ensureTableDoc 只补齐 tables.json：表清单体积远小于快照包，
// 首次运行或缺失时不应为了列目录去同步整个内置资源。
func (a *App) ensureTableDoc(ctx context.Context, resourceDir string) error {
	if tablesDocUsable(resourceDir) {
		return nil
	}
	a.emitPatchLog("[资源] 正在同步表清单 tables.json...")
	if _, err := a.syncTableDoc(ctx, resourceDir); err != nil {
		return fmt.Errorf("获取表清单失败：%w", err)
	}
	a.emitPatchLog("[资源] 表清单已更新")
	return nil
}

func (a *App) syncTableDoc(ctx context.Context, resourceDir string) (bool, error) {
	unlock, err := a.lockResource(ctx)
	if err != nil {
		return false, err
	}
	defer unlock()
	return a.updater.SyncTableDoc(ctx, resourceDir)
}

// EnsureTableDoc 供前端启动后补齐表清单；本地清单可用时不发请求。
func (a *App) EnsureTableDoc() (bool, error) {
	resourceDir := a.resourceDir()
	if tablesDocUsable(resourceDir) {
		return false, nil
	}
	ctx := a.ctx
	if ctx == nil {
		ctx = context.Background()
	}
	updated, err := a.syncTableDoc(ctx, resourceDir)
	if err != nil {
		return false, err
	}
	if updated {
		a.emitPatchLog("[资源] 已更新表清单 tables.json")
	}
	return updated, nil
}

// tablesDocUsable 判断本地表清单是否可用：文件存在、能解析、且至少有一张表。
// 只有文件存在但内容为空或损坏时也视为不可用，触发重新下载。
func tablesDocUsable(resourceDir string) bool {
	data, err := os.ReadFile(filepath.Join(resourceDir, "tables.json"))
	if err != nil {
		return false
	}
	var doc TablesDoc
	if err := json.Unmarshal(data, &doc); err != nil {
		return false
	}
	return len(doc.Files) > 0 || len(doc.Tables) > 0
}

// SyncBuiltinResource 由界面显式触发一次内置资源同步，用于首次运行或资源缺失。
func (a *App) SyncBuiltinResource() error {
	if err := a.beginBusy(); err != nil {
		return err
	}
	defer a.endBusy()
	ctx := a.ctx
	if ctx == nil {
		ctx = context.Background()
	}
	if err := a.ensureBuiltinResource(ctx, a.resourceDir()); err != nil {
		a.emitPatchLog(fmt.Sprintf("[错误] %v", err))
		return err
	}
	return nil
}

// GetAppVersion 返回补丁器自身版本号。
func (a *App) GetAppVersion() string {
	return updater.DisplayAppVersion(appVersion)
}

// GetRules 返回当前规则集合的元数据列表（供资源同步后前端刷新规则状态）。
func (a *App) GetRules() []pipeline.RuleMeta {
	return a.getRules(a.resourceDir())
}

func (a *App) getRules(resourceDir string) []pipeline.RuleMeta {
	ruleList, ruleErr := loadRules(resourceDir)
	if ruleErr != nil {
		a.emitPatchLog(fmt.Sprintf("[警告] %v", ruleErr))
	}
	ruleMetas := make([]pipeline.RuleMeta, 0, len(ruleList))
	for _, rule := range ruleList {
		ruleMetas = append(ruleMetas, rule.Meta())
	}
	return ruleMetas
}

// GetBootstrap 返回前端启动数据。只读本地文件，不做任何联网操作。
func (a *App) GetBootstrap() (BootstrapInfo, error) {
	resourceDir := a.resourceDir()

	cfg := a.loadConfig(resourceDir)

	tables := a.loadTablesDoc(resourceDir)
	csvCtx := a.csvContext(cfg, tables, normalizeRegionCode(cfg.Language))

	ruleMetas := a.getRules(resourceDir)

	var resTime, resHash string
	if mf, err := updater.ReadLocalManifest(resourceDir); err == nil && mf != nil {
		resTime = formatResourceTime(mf.GeneratedAt)
		resHash = shortHash(mf.RawEXD.SHA256)
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		a.emitPatchLog(fmt.Sprintf("[警告] 读取本地 manifest.json 失败: %v", err))
	}

	return BootstrapInfo{
		Version:       updater.DisplayAppVersion(appVersion),
		ResourceDir:   resourceDir,
		ResourceReady: updater.HasLocalResource(resourceDir),
		DevMode:       a.updater.LocalOnly,
		Rules:         ruleMetas,
		Languages: []LanguageOption{
			{Code: string(region.GLOBAL), Name: "国际服", Region: string(region.Global)},
			{Code: string(region.CHS), Name: "国服", Region: string(region.China)},
			{Code: string(region.TC), Name: "繁中服", Region: string(region.Taiwan), BackupURL: tcBackupURL},
			{Code: string(region.KO), Name: "韩服", Region: string(region.Korea)},
		},
		RegionLanguages: map[string][]RegionLanguage{
			string(region.Global): globalTargetLanguages,
			string(region.China):  {{Code: string(region.CHS), Name: region.LanguageName(region.CHS)}},
			string(region.Taiwan): {{Code: string(region.TC), Name: region.LanguageName(region.TC)}},
			string(region.Korea):  {{Code: string(region.KO), Name: region.LanguageName(region.KO)}},
		},
		Config:       cfg,
		Folders:      tables.Folders,
		CSV:          csvCtx,
		Excluded:     recommendedExcludedTables,
		ResourceTime:      resTime,
		ResourceHash:      resHash,
		FontCompilerReady: a.HasFontCompiler(),
	}, nil
}

// loadTablesDoc 读取资源目录中的 tables.json，缺失或损坏时返回空文档。
func (a *App) loadTablesDoc(resourceDir string) TablesDoc {
	var tables TablesDoc
	tablesPath := filepath.Join(resourceDir, "tables.json")
	if data, err := os.ReadFile(tablesPath); err == nil {
		if err := json.Unmarshal(data, &tables); err != nil {
			a.emitPatchLog(fmt.Sprintf("[警告] 解析本地 tables.json 失败: %v", err))
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		a.emitPatchLog(fmt.Sprintf("[警告] 读取本地 tables.json (%s) 失败: %v", tablesPath, err))
	}
	if tables.Folders == nil {
		tables.Folders = map[string]string{}
	}
	return tables
}

func buildTableList(tables TablesDoc) []TableEntry {
	keys := append([]string(nil), tables.Files...)
	if len(keys) == 0 {
		for key := range tables.Tables {
			keys = append(keys, key)
		}
	}
	return tableEntries(tables, keys)
}

// tableEntries 按给定表键生成表清单，供内置词条与自定义词条目录共用。
func tableEntries(tables TablesDoc, keys []string) []TableEntry {
	sorted := append([]string(nil), keys...)
	sort.Strings(sorted)
	out := make([]TableEntry, 0, len(sorted))
	for _, key := range sorted {
		entry := TableEntry{Key: key, Folder: "root", Name: "-"}
		if name, ok := tables.Tables[key]; ok && name != "" {
			entry.Name = name
		}
		if i := strings.Index(key, "/"); i > 0 {
			folder := key[:i]
			entry.Folder = folder
			folderName := tables.Folders[folder]
			if folderName == "" {
				folderName = folder
			}
			entry.Name = folderName
		} else {
			entry.Warning = compatibilityWarnings[key]
		}
		out = append(out, entry)
	}
	return out
}

// regionLanguageOrder 是配置中目标语言的固定区服顺序。
var regionLanguageOrder = []region.Language{region.GLOBAL, region.CHS, region.TC, region.KO}

// completeTargetLanguages 为每个区服补上目标语言，缺失时取该区服的默认值。
// 配置里始终保留全部区服，便于直接阅读各服设定。
func completeTargetLanguages(saved LanguagesByRegion) LanguagesByRegion {
	out := make(LanguagesByRegion, len(regionLanguageOrder))
	for _, lang := range regionLanguageOrder {
		key := string(lang)
		if list := saved[key]; len(list) > 0 {
			out[key] = list
			continue
		}
		if region.Of(lang) == region.Global {
			out[key] = append([]string{}, defaultGlobalTargetLanguages...)
			continue
		}
		out[key] = sanitizeTargetLanguages(lang, nil)
	}
	return out
}

// SaveConfig 保存前端配置到 config.json。
// 写盘段全程持锁：并发调用会在固定名称的 .tmp 上互相踩踏。
func (a *App) SaveConfig(cfg AppConfig) error {
	a.cfgMu.Lock()
	defer a.cfgMu.Unlock()
	resourceDir := a.resourceDir()
	dirs := make(map[string]string, len(cfg.GameDirs))
	for lang, dir := range cfg.GameDirs {
		if dir == "" {
			continue
		}
		dirs[normalizeRegionCode(lang)] = dir
	}
	cfg.GameDirs = dirs
	langs := make(LanguagesByRegion, len(cfg.TargetLanguages))
	for code, list := range cfg.TargetLanguages {
		parsed, err := region.ParseLanguage(normalizeRegionCode(code))
		if err != nil {
			continue
		}
		if cleaned := sanitizeTargetLanguages(parsed, list); len(cleaned) > 0 {
			langs[normalizeRegionCode(code)] = cleaned
		}
	}
	cfg.TargetLanguages = completeTargetLanguages(langs)
	csvDirs := make(CSVDirsByRegion, len(cfg.CSVDirs))
	for code, conf := range cfg.CSVDirs {
		normalized := normalizeCSVConfig(conf)
		if len(normalized.Dirs) == 0 {
			continue
		}
		csvDirs[normalizeRegionCode(code)] = normalized
	}
	cfg.CSVDirs = csvDirs
	fontPresets := defaultConfig().FontPresets
	for code, p := range cfg.FontPresets {
		if p != "" {
			fontPresets[normalizeRegionCode(code)] = p
		}
	}
	cfg.FontPresets = fontPresets
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	cfgPath := configPathFor(resourceDir)
	if err := os.MkdirAll(filepath.Dir(cfgPath), 0o755); err != nil {
		return err
	}
	return writeFileAtomic(cfgPath, append(data, '\n'))
}

// DetectGamePaths 扫描指定区服的常见安装位置，必要时在磁盘 3 层目录内搜索；
// 扫描可被 CancelDetect 中止。
func (a *App) DetectGamePaths(language string) []string {
	ctx, cancel := context.WithCancel(context.Background())
	a.detectMu.Lock()
	a.detectCancel = cancel
	a.detectMu.Unlock()
	defer func() {
		cancel()
		a.detectMu.Lock()
		a.detectCancel = nil
		a.detectMu.Unlock()
	}()

	var targetReg region.Region
	if language != "" {
		if parsed, err := region.ParseLanguage(language); err == nil {
			targetReg = region.Of(parsed)
		}
	}
	return a.detectRegionPaths(ctx, targetReg)
}

func detectLauncherGamePaths() []string {
	appData, err := os.UserConfigDir()
	if err != nil || appData == "" {
		return nil
	}
	var out []string
	for _, dirName := range []string{"XIVLauncher", "XIVLauncherCN"} {
		cfgPath := filepath.Join(appData, dirName, "launcherConfigV3.json")
		raw, err := os.ReadFile(cfgPath)
		if err != nil || len(raw) == 0 {
			continue
		}
		var doc struct {
			GamePath string `json:"GamePath"`
		}
		if json.Unmarshal(raw, &doc) == nil && strings.TrimSpace(doc.GamePath) != "" {
			out = append(out, strings.TrimSpace(doc.GamePath))
		}
	}
	return out
}

func (a *App) detectRegionPaths(ctx context.Context, targetReg region.Region) []string {
	seen := map[string]bool{}
	candidates := make([]string, 0, 8)
	add := func(p string) {
		p = filepath.Clean(p)
		if seen[p] {
			return
		}
		if st, err := os.Stat(filepath.Join(p, "game", "ffxiv_dx11.exe")); err == nil && !st.IsDir() {
			seen[p] = true
			candidates = append(candidates, p)
		}
	}

	for _, p := range detectLauncherGamePaths() {
		add(p)
	}

	if targetReg == "" || targetReg == region.Global {
		if home, err := os.UserHomeDir(); err == nil && home != "" {
			macCandidates := []string{
				filepath.Join(home, "Library", "Application Support", "FINAL FANTASY XIV ONLINE", "Bottles", "published_Final_Fantasy", "drive_c", "Program Files (x86)", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
				filepath.Join(home, "Library", "Application Support", "XIV on Mac", "ffxiv"),
				filepath.Join(home, "Library", "Application Support", "XIV on Mac", "wineprefix", "drive_c", "Program Files (x86)", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
			}
			for _, p := range macCandidates {
				add(p)
			}
		}
	}

	chinaSuffixes := []string{
		filepath.Join("Program Files (x86)", "盛趣游戏", "最终幻想XIV"),
		filepath.Join("Program Files", "盛趣游戏", "最终幻想XIV"),
		filepath.Join("Program Files (x86)", "盛大游戏", "最终幻想XIV"),
		filepath.Join("Program Files (x86)", "上海数龙科技有限公司", "最终幻想XIV"),
		filepath.Join("Program Files (x86)", "SNDA", "FFXIV"),
		filepath.Join("WeGameApps", "rail_apps", "最终幻想14(2000340)"),
		filepath.Join("WeGame", "rail_apps", "最终幻想14(2000340)"),
		filepath.Join("Games", "FFXIV"),
		filepath.Join("Games", "最终幻想XIV"),
		"FFXIV",
		"最终幻想XIV",
	}
	globalSuffixes := []string{
		filepath.Join("Program Files (x86)", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
		filepath.Join("Program Files", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
		filepath.Join("Steam", "steamapps", "common", "FINAL FANTASY XIV Online"),
		filepath.Join("Program Files (x86)", "Steam", "steamapps", "common", "FINAL FANTASY XIV Online"),
	}

	var suffixes []string
	switch targetReg {
	case region.China:
		suffixes = chinaSuffixes
	case region.Global:
		suffixes = globalSuffixes
	case region.Korea, region.Taiwan:
		suffixes = nil
	default:
		suffixes = append(append([]string{}, globalSuffixes...), chinaSuffixes...)
	}

	for _, root := range getScanDriveRoots() {
		for _, suffix := range suffixes {
			add(filepath.Join(root, suffix))
		}
	}

	filterByRegion := func(list []string) []string {
		if targetReg == "" {
			return list
		}
		out := make([]string, 0, len(list))
		for _, p := range list {
			if actualReg, err := verifier.DetectClientRegion(p); err == nil {
				if actualReg == targetReg {
					out = append(out, p)
				}
			} else {
				out = append(out, p)
			}
		}
		return out
	}

	matched := filterByRegion(candidates)
	if len(matched) == 0 {
		for _, p := range a.scanGlobalDirs(ctx) {
			add(p)
		}
		matched = filterByRegion(candidates)
	}
	return matched
}

// scanGlobalDirs 在各磁盘根目录 4 层以内查找客户端目录（通过 boot 与 game/ffxiv_dx11.exe 特征判定，不限文件夹名）。
// 命中结果在进程内复用；已有扫描在进行时复用同一轮结果，ctx 取消则立即返回 nil。
func (a *App) scanGlobalDirs(ctx context.Context) []string {
	a.scanMu.Lock()
	if a.scanCached {
		dirs := a.scanDirs
		a.scanMu.Unlock()
		return dirs
	}
	if running := a.scanRunning; running != nil {
		a.scanMu.Unlock()
		select {
		case <-running:
		case <-ctx.Done():
			return nil
		}
		a.scanMu.Lock()
		dirs := a.scanDirs
		a.scanMu.Unlock()
		return dirs
	}
	done := make(chan struct{})
	a.scanRunning = done
	a.scanMu.Unlock()

	roots := getScanDriveRoots()
	dirs := gamescan.FindClients(ctx, roots, gamescan.DefaultMaxDepth, isGlobalClientDir)

	a.scanMu.Lock()
	// 只有完整跑完的扫描才缓存，避免把被中止的部分结果当成最终结论。
	if len(dirs) > 0 && ctx.Err() == nil {
		a.scanDirs = dirs
		a.scanCached = true
	}
	a.scanRunning = nil
	a.scanMu.Unlock()
	close(done)
	return dirs
}

// CancelDetect 中止正在进行的目录探测。
func (a *App) CancelDetect() {
	a.detectMu.Lock()
	cancel := a.detectCancel
	a.detectMu.Unlock()
	if cancel != nil {
		cancel()
	}
}

// isGlobalClientDir 校验目录是完整客户端：同时存在 boot、game 目录与 game/ffxiv_dx11.exe。
func isGlobalClientDir(dir string) bool {
	for _, sub := range []string{"boot", "game"} {
		if st, err := os.Stat(filepath.Join(dir, sub)); err != nil || !st.IsDir() {
			return false
		}
	}
	st, err := os.Stat(filepath.Join(dir, "game", "ffxiv_dx11.exe"))
	return err == nil && !st.IsDir()
}

// SelectGameDirectory 打开目录选择对话框。
func (a *App) SelectGameDirectory(language string) (string, error) {
	if !a.hasUI() {
		return "", errors.New("桌面运行时未就绪")
	}
	cfg := a.loadConfig(a.resourceDir())
	defaultDir := cfg.GameDirs[normalizeRegionCode(language)]
	dlg := a.wailsApp.Dialog.OpenFile().
		SetTitle("选择 FFXIV 客户端目录（包含 boot 与 game 文件夹）").
		CanChooseDirectories(true).
		CanChooseFiles(false).
		CanCreateDirectories(false)
	if defaultDir != "" {
		dlg.SetDirectory(defaultDir)
	}
	if win := a.activeDialogWindow(); win != nil {
		dlg.AttachToWindow(win)
	}
	return normalizeDialogSelection(dlg.PromptForSingleSelection())
}

// GetGameInfo 返回游戏目录、版本与文件状态。
func (a *App) GetGameInfo(gameDir, language string) (GameInfo, error) {
	info := GameInfo{GameDir: gameDir}
	if gameDir == "" {
		return info, nil
	}
	lang, err := region.ParseLanguage(language)
	if err != nil {
		lang = region.JA
	}
	info.Region = string(region.Of(lang))

	if actualReg, err := verifier.DetectClientRegion(gameDir); err == nil && actualReg != "" {
		info.DetectedRegion = string(actualReg)
		if actualReg != region.Of(lang) {
			info.RegionMismatch = true
		}
	}

	versionData, err := os.ReadFile(filepath.Join(gameDir, "game", "ffxivgame.ver"))
	if err == nil {
		info.Version = strings.TrimSpace(string(versionData))
	}
	if st, err := os.Stat(filepath.Join(gameDir, "game", "ffxiv_dx11.exe")); err == nil && !st.IsDir() {
		info.Valid = true
	}
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	for _, name := range verifier.AllFiles {
		status := FileStatus{Name: name}
		if st, err := os.Stat(filepath.Join(sqpackDir, name)); err == nil {
			status.Exists = true
			status.Size = st.Size()
		}
		info.Files = append(info.Files, status)
	}

	metaPath := filepath.Join(a.backupDirFor(region.Of(lang)), "meta.json")
	var meta struct {
		Version string `json:"version"`
		Files   map[string]struct {
			SHA256 string `json:"sha256"`
			Size   int64  `json:"size"`
		} `json:"files"`
	}
	if data, err := os.ReadFile(metaPath); err == nil {
		if json.Unmarshal(data, &meta) == nil {
			info.BackupVersion = meta.Version
			info.BackupExists = len(meta.Files) > 0
		}
	}
	info.UiAssetType = -1
	if !a.updater.LocalOnly {
		if mf, err := updater.ReadLocalManifest(a.resourceDir()); err == nil && mf != nil {
			info.ResourceTime = formatResourceTime(mf.GeneratedAt)
			info.ResourceHash = shortHash(mf.RawEXD.SHA256)
		}
	}
	if cfgPath, err := gamecfg.FindConfigFile(); err == nil {
		if val, exists, err := gamecfg.ReadUiAssetType(cfgPath); err == nil && exists {
			info.UiAssetType = val
		}
	}
	return info, nil
}

// OpenBackupDirectory 在系统文件管理器中打开当前区服的原版备份目录。
func (a *App) OpenBackupDirectory(language string) error {
	lang, err := region.ParseLanguage(language)
	if err != nil {
		lang = region.JA
	}
	target := a.backupDirFor(region.Of(lang))
	if st, err := os.Stat(target); err != nil || !st.IsDir() {
		return fmt.Errorf("目录不存在: %s", target)
	}
	return openInFileManager(target)
}

// ClientStatusResult 表示客户端环境状态。
// ClientPartStatus 描述文本 / 字体其中一组文件的状态。
type ClientPartStatus struct {
	Status      string `json:"status"`
	Label       string `json:"label"`
	Description string `json:"description,omitempty"`
}

type ClientStatusResult struct {
	Status      string `json:"status"` // "original" | "modified" | "unknown" | "mismatch"
	Label       string `json:"label"`  // "官方原版" | "已修改" | "未知版本" | "区服不匹配"
	Description string `json:"description"`
	// Text / Font 是分开的两组状态：文本（0a0000）与字体（000000）。
	Text ClientPartStatus `json:"text"`
	Font ClientPartStatus `json:"font"`
}

// writeFileAtomic 先写临时文件再改名，避免进程中断留下截断的清单文件。
func writeFileAtomic(path string, data []byte) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

// CheckClientStatus 手动计算并比对客户端文件哈希，返回当前客户端状态。
func (a *App) CheckClientStatus(gameDir, language string) (ClientStatusResult, error) {
	if gameDir == "" {
		return ClientStatusResult{Status: "unknown", Label: "未选择目录", Description: "请先指定游戏安装目录"}, nil
	}
	lang, err := region.ParseLanguage(language)
	if err != nil {
		lang = region.JA
	}
	reg := region.Of(lang)

	// 1. 检查客户端真实区服与当前选择区服是否匹配
	if actualReg, err := verifier.DetectClientRegion(gameDir); err == nil && actualReg != "" && actualReg != reg {
		return ClientStatusResult{
			Status:      "mismatch",
			Label:       "区服不匹配",
			Description: fmt.Sprintf("目录为【%s】客户端，与所选区服【%s】不匹配，请切换区服或重新选择目录", region.DisplayName(actualReg), region.DisplayName(reg)),
		}, nil
	}

	versionData, err := os.ReadFile(filepath.Join(gameDir, "game", "ffxivgame.ver"))
	version := ""
	versionErr := err
	if err == nil {
		version = strings.TrimSpace(string(versionData))
	}

	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	fontIndex := "000000.win32.index"
	textIndex := "0a0000.win32.index"

	fontHash, fontErr := verifier.HashFile(filepath.Join(sqpackDir, fontIndex))
	textHash, textErr := verifier.HashFile(filepath.Join(sqpackDir, textIndex))
	if fontErr != nil && textErr != nil {
		missing := ClientPartStatus{Status: "missing", Label: "文件缺失", Description: fmt.Sprintf("文本索引: %v；字体索引: %v", textErr, fontErr)}
		return ClientStatusResult{Status: "unknown", Label: "文件缺失", Description: missing.Description, Text: missing, Font: missing}, nil
	}

	// 本地原版备份（同版本才有效）
	var meta verifier.BackupMeta
	hasBackupMeta := false
	backupDir := a.backupDirFor(reg)
	if data, err := os.ReadFile(filepath.Join(backupDir, "meta.json")); err == nil {
		if json.Unmarshal(data, &meta) == nil && versionErr == nil && meta.Version == version && len(meta.Files) > 0 {
			hasBackupMeta = true
		}
	}

	// 国际服且本地没有备份时，用云端 Release 作为比对基准
	var cloudAsset func(string) (string, bool)
	cloudDesc := "本地尚无该版本的原版备份"
	if reg == region.Global {
		cloudDesc = "云端尚未收录该版本的官方原版备份，继续汉化可能出错"
	}
	if reg == region.Global && !hasBackupMeta && versionErr == nil {
		cloud := a.getCloud()
		parentCtx := a.ctx
		if parentCtx == nil {
			parentCtx = context.Background()
		}
		ctx, cancel := context.WithTimeout(parentCtx, 8*time.Second)
		r, relErr := cloud.ReleaseByTag(ctx, "Souma-Sumire/FFXIVChnTextPatch-Souma", version)
		cancel()
		if relErr == nil && r != nil {
			cloudAsset = func(name string) (string, bool) { return r.AssetSHA256(name) }
		} else if relErr != nil && !errors.Is(relErr, verifier.ErrNotFound) {
			a.emitDiag("云端原版信息查询原始错误：%v", relErr)
			cloudDesc = fmt.Sprintf("获取云端原版信息失败（%s），无法验证原版完整性，继续汉化可能出错", verifier.CleanNetError(relErr))
		}
	}

	// 文本 / 字体分别判定，互不影响
	partStatus := func(index string, hash verifier.FileHash, hashErr error) ClientPartStatus {
		if hashErr != nil {
			return ClientPartStatus{Status: "missing", Label: "文件缺失", Description: hashErr.Error()}
		}
		if hasBackupMeta {
			if want, ok := meta.Files[index]; ok {
				if strings.EqualFold(hash.SHA256, want.SHA256) {
					return ClientPartStatus{Status: "original", Label: "原版", Description: version}
				}
				return ClientPartStatus{Status: "modified", Label: "已修改", Description: "与原版备份不一致"}
			}
		}
		if cloudAsset != nil {
			if want, ok := cloudAsset(index); ok {
				if strings.EqualFold(hash.SHA256, want) {
					return ClientPartStatus{Status: "original", Label: "原版", Description: version}
				}
				return ClientPartStatus{Status: "modified", Label: "已修改", Description: "与云端原版不一致"}
			}
		}
		return ClientPartStatus{Status: "unknown", Label: "未知版本", Description: cloudDesc}
	}

	textPart := partStatus(textIndex, textHash, textErr)
	fontPart := partStatus(fontIndex, fontHash, fontErr)

	summary := ClientPartStatus{Status: "unknown", Label: "未知版本", Description: cloudDesc}
	switch {
	case textPart.Status == "original" && fontPart.Status == "original":
		summary = ClientPartStatus{Status: "original", Label: "原版", Description: version}
	case textPart.Status == "modified" || fontPart.Status == "modified":
		summary = ClientPartStatus{Status: "modified", Label: "已修改", Description: "部分文件与原版不一致"}
	case textPart.Status == "missing" || fontPart.Status == "missing":
		summary = ClientPartStatus{Status: "unknown", Label: "文件缺失", Description: "客户端索引文件缺失"}
	}

	return ClientStatusResult{
		Status:      summary.Status,
		Label:       summary.Label,
		Description: summary.Description,
		Text:        textPart,
		Font:        fontPart,
	}, nil
}

// AnswerUnknownConfirm 接收前端对“未知版本是否继续汉化”的确认反馈。
func (a *App) AnswerUnknownConfirm(ok bool) {
	a.confirmMu.Lock()
	ch := a.confirmCh
	a.confirmCh = nil
	a.confirmMu.Unlock()
	if ch != nil {
		select {
		case ch <- ok:
		default:
		}
	}
}

func (a *App) waitUnknownConfirm(ctx context.Context, version string) (bool, error) {
	if !a.hasUI() {
		return false, nil
	}
	ch := make(chan bool, 1)
	a.confirmMu.Lock()
	a.confirmCh = ch
	a.confirmMu.Unlock()

	a.emitEvent("unknown-confirm", fmt.Sprintf("无法确认游戏版本 %s 是否为官方原版（云端信息未获取或尚未收录）。\n\n继续汉化可能出错；程序将把当前客户端文件备份下来，供以后还原使用。若文件已被其他汉化修改，还原只能回到当前状态。\n\n如需还原到官方原版，请先运行官方启动器修复客户端。\n\n是否继续？", version))
	select {
	case ok := <-ch:
		return ok, nil
	case <-time.After(10 * time.Minute):
		return false, errors.New("等待确认超时")
	case <-ctx.Done():
		return false, ctx.Err()
	}
}

// AnswerLauncherConfirm 接收前端对“是否强制结束启动器”的确认。
func (a *App) AnswerLauncherConfirm(ok bool) {
	a.launcherConfirmMu.Lock()
	ch := a.launcherConfirmCh
	a.launcherConfirmCh = nil
	a.launcherConfirmMu.Unlock()
	if ch != nil {
		select {
		case ch <- ok:
		default:
		}
	}
}

func (a *App) waitLauncherConfirm(ctx context.Context, launcherNames []string) (bool, error) {
	if !a.hasUI() {
		return false, nil
	}
	ch := make(chan bool, 1)
	a.launcherConfirmMu.Lock()
	a.launcherConfirmCh = ch
	a.launcherConfirmMu.Unlock()

	namesStr := strings.Join(launcherNames, "、")
	prompt := fmt.Sprintf("检测到游戏启动器正在运行（%s），为防止文件冲突或更新损坏，需要先关闭启动器。\n\n是否强制结束启动器进程并继续？", namesStr)
	a.emitEvent("launcher-confirm", prompt)

	var appDone <-chan struct{}
	if a.ctx != nil {
		appDone = a.ctx.Done()
	}

	select {
	case ok := <-ch:
		return ok, nil
	case <-time.After(5 * time.Minute):
		return false, errors.New("等待启动器确认超时")
	case <-ctx.Done():
		// 用户点了取消（或任务被中止）：立即退出等待并清掉挂着的应答通道，
		// 不再干等到 5 分钟超时。
		a.launcherConfirmMu.Lock()
		if a.launcherConfirmCh == ch {
			a.launcherConfirmCh = nil
		}
		a.launcherConfirmMu.Unlock()
		return false, ctx.Err()
	case <-appDone:
		return false, a.ctx.Err()
	}
}

func (a *App) checkGameNotRunning(ctx context.Context, gameDir string) error {
	if a != nil && a.skipProcessCheck {
		return nil
	}
	return gameproc.CheckGameNotRunningWithOpts(gameproc.CheckOptions{
		GameDir: gameDir,
		OnLauncherConfirm: func(launcherNames []string) (bool, error) {
			return a.waitLauncherConfirm(ctx, launcherNames)
		},
		OnLaunchersKilled: func(killed []string) {
			a.emitPatchLog(fmt.Sprintf("[提示] 已强制结束游戏启动器进程（%s）", strings.Join(killed, "、")))
		},
	})
}

// CancelPatch 中止当前正在执行的汉化任务。
func (a *App) CancelPatch() {
	a.cancelPatchMu.Lock()
	cancel := a.cancelPatch
	a.cancelPatchMu.Unlock()
	if cancel != nil {
		cancel()
	}
}

func (a *App) beginBusy() error {
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.busy {
		return errors.New("已有任务正在进行，请稍候")
	}
	a.busy = true
	return nil
}

func (a *App) endBusy() {
	a.mu.Lock()
	a.busy = false
	a.mu.Unlock()
}

// isBusy 判断当前是否有汉化、还原等占用客户端的任务在跑。
func (a *App) isBusy() bool {
	if a == nil {
		return false
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.busy
}

func (a *App) shouldThrottleProgress(stage, detail string, done, total int64) bool {
	a.progressMu.Lock()
	defer a.progressMu.Unlock()
	now := time.Now()
	isFinish := total > 0 && done >= total
	isStageOrDetailChange := a.lastStage != stage || (a.lastDetail != detail && (total == 0 || done == 0))
	if isFinish || isStageOrDetailChange || now.Sub(a.lastProgress) >= 500*time.Millisecond {
		a.lastProgress = now
		a.lastStage = stage
		a.lastDetail = detail
		return false
	}
	return true
}

func (a *App) emitResourceProgress(stage, detail string, done, total int64) {
	if !a.hasUI() || a.shouldThrottleProgress(stage, detail, done, total) {
		return
	}
	a.emitEvent("resource-progress", ProgressEvent{Stage: stage, Detail: detail, Done: done, Total: total})
}

func (a *App) emitProgress(stage, detail string, done, total int64) {
	if !a.hasUI() || a.shouldThrottleProgress(stage, detail, done, total) {
		return
	}
	a.emitEvent("patch-progress", ProgressEvent{Stage: stage, Detail: detail, Done: done, Total: total})
}

// revertWrites 逆序撤回本次已开始写入的文件组，返回真正被改回的文件组数。
// 每个文件组各自独立，某组失败不影响继续撤回其他组。
func revertWrites(undos []*replace.Undo) (int, error) {
	changed := 0
	var errs []error
	for i := len(undos) - 1; i >= 0; i-- {
		reverted, err := undos[i].Revert()
		if reverted {
			changed++
		}
		if err != nil {
			errs = append(errs, err)
		}
	}
	return changed, errors.Join(errs...)
}

func fontCompilerPaths(resourceDir string) (dir, path, legacyPath string) {
	dir = filepath.Join(dataRootFor(resourceDir), "fontchanger")
	path = filepath.Join(dir, fontengine.CLIBinaryName)
	if abs, err := filepath.Abs(path); err == nil {
		path = abs
		dir = filepath.Dir(path)
	}
	legacyPath = filepath.Join(filepath.Dir(dir), fontengine.CLIBinaryName)
	return dir, path, legacyPath
}

func (a *App) emitFontCompilerProgress(stage, detail string, done, total int64) {
	a.emitEvent("font-compiler-progress", map[string]any{
		"stage":  stage,
		"detail": detail,
		"done":   done,
		"total":  total,
	})
}

// ensureFontCompiler 确保字体编译组件就绪：已存在就直接用，否则按需下载。
// 组件单独发布在 fontchanger-cli 分支，不随主程序打包。
func (a *App) ensureFontCompiler(ctx context.Context, resourceDir string) error {
	if goruntime.GOOS != "windows" {
		return errors.New("字体编译组件仅支持 Windows，当前系统无法编译和预览")
	}
	unlock, err := lockMutexWithContext(ctx, &a.fontCompilerMu)
	if err != nil {
		return err
	}
	defer unlock()

	// 组件和它自己读写的 config.json 必须留在独立目录：CLI 会写同目录的 config.json，
	// 与主程序配置同目录时会互相覆盖。因此优先使用隔离目录，并把旧的同目录组件迁进来；
	// 程序目录/PATH 里的旧组件不再直接复用。
	dir, path, legacyPath := fontCompilerPaths(resourceDir)

	if verifyExecutable(path) == nil {
		a.fontEngine.SetBinPath(path)
		return nil
	}
	if verifyExecutable(legacyPath) == nil {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return fmt.Errorf("创建字体组件目录失败: %w", err)
		}
		if err := os.Rename(legacyPath, path); err != nil {
			data, readErr := os.ReadFile(legacyPath)
			if readErr != nil {
				return fmt.Errorf("迁移字体编译组件失败: %w", readErr)
			}
			if writeErr := os.WriteFile(path, data, 0o755); writeErr != nil {
				return fmt.Errorf("迁移字体编译组件失败: %w", writeErr)
			}
			_ = os.Remove(legacyPath)
		}
		a.fontEngine.SetBinPath(path)
		return nil
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return fmt.Errorf("创建字体组件目录失败: %w", err)
	}

	ready := false
	a.emitFontCompilerProgress("downloading", "准备组件中", 0, 0)
	defer func() {
		if ready {
			a.emitFontCompilerProgress("done", "ready", 0, 0)
		} else {
			a.emitFontCompilerProgress("failed", "", 0, 0)
		}
	}()

	a.emitPatchLog("[字体] 正在下载字体编译组件...")
	var lastErr error
	for _, url := range updater.FontCompilerURLs() {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		tmp := path + ".part"
		_ = os.Remove(tmp)
		progressCb := func(done, total int64) {
			a.emitFontCompilerProgress("downloading", "准备组件中", done, total)
		}
		if err := a.updater.DownloadFile(ctx, url, tmp, progressCb); err != nil {
			lastErr = err
			_ = os.Remove(tmp)
			continue
		}
		if err := verifyExecutable(tmp); err != nil {
			lastErr = err
			_ = os.Remove(tmp)
			continue
		}
		if err := os.Rename(tmp, path); err != nil {
			lastErr = err
			_ = os.Remove(tmp)
			continue
		}
		ready = true
		a.fontEngine.SetBinPath(path)
		a.emitPatchLog("[字体] 字体编译组件已就绪")
		return nil
	}
	if lastErr == nil {
		lastErr = errors.New("没有可用的下载节点")
	}
	return fmt.Errorf("下载字体编译组件失败: %w", lastErr)
}

// verifyExecutable 粗略确认下载到的是 Windows 可执行文件（MZ 头 + 合理体积）。
func verifyExecutable(path string) error {
	st, err := os.Stat(path)
	if err != nil {
		return err
	}
	if st.IsDir() {
		return errors.New("目标路径是目录")
	}
	if st.Size() < 1<<20 {
		return fmt.Errorf("下载内容过小（%d 字节）", st.Size())
	}
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	var head [2]byte
	if _, err := io.ReadFull(f, head[:]); err != nil {
		return err
	}
	if head[0] != 'M' || head[1] != 'Z' {
		return errors.New("下载内容不是有效的 Windows 可执行文件")
	}
	return nil
}

func (a *App) syncFontEngineDirs(_ context.Context, resourceDir, primaryGameDir string) {
	cfg := a.loadConfig(resourceDir)
	dirs := make(map[string]string, len(cfg.GameDirs))
	for k, v := range cfg.GameDirs {
		if clean := strings.TrimSpace(v); clean != "" && isGameClientDir(clean) {
			dirs[normalizeRegionCode(k)] = clean
		}
	}
	a.fontEngine.SetGameDirs(primaryGameDir, dirs)
}

// checkFontSource 在写入任何客户端文件之前确认本次字体来源可用。
// 选了「自定义方案」就必须能拿到编译组件：拿不到直接报错中止，
// 不再等文本汉化跑完再悄悄换成内置字体包。
func (a *App) checkFontSource(ctx context.Context, resourceDir, gameDir string) error {
	if a.fontSourceMode(resourceDir) != "compile" {
		return nil
	}
	if goruntime.GOOS != "windows" {
		return errors.New("自定义方案需在 Windows 上编译字体，当前系统不支持，请改用「内置字体包」")
	}
	a.syncFontEngineDirs(ctx, resourceDir, gameDir)
	if err := a.ensureFontCompiler(ctx, resourceDir); err != nil {
		return fmt.Errorf("字体来源为「自定义方案」，但编译组件不可用：%w。请改用「内置字体包」", err)
	}
	// 「编译」时已产出并校验贴图，这里再次确认，避免方案变更后直接开始汉化。
	if _, err := a.requireFontBuild(resourceDir); err != nil {
		return err
	}
	return nil
}

// fontSourceMode 返回界面选择的字体来源：compile（自定义方案）或 package（内置字体包）。
func (a *App) fontSourceMode(resourceDir string) string {
	if strings.EqualFold(strings.TrimSpace(a.loadConfig(resourceDir).FontSource), "compile") {
		return "compile"
	}
	return "package"
}

// hasFontTexture 判断目录里是否已有字体贴图（.tex）。
// 字体包与现场编译的产物都以 .tex 作为「确实生成了字体」的判据。
func hasFontTexture(dir string) bool {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return false
	}
	for _, entry := range entries {
		if !entry.IsDir() && strings.HasSuffix(strings.ToLower(entry.Name()), ".tex") {
			return true
		}
	}
	return false
}

// localFontPackage 判断本地是否已有可用的预编译字体包（resource/font 下有 .tex）。
// 有就直接用，不再走云端校验，避免把离线生成的字体覆盖掉。
func localFontPackage(resourceDir string) (string, bool) {
	dir := filepath.Join(resourceDir, "font")
	if !hasFontTexture(dir) {
		return "", false
	}
	return dir, true
}

// maxFontTextureSize 读字体目录里每个 .tex 的宽高，返回最大边长。
// 1024 与原版字体一致，不需要游戏开启高分辨率贴图；更大的才需要。
func maxFontTextureSize(dir string) int {
	_, side := fontTextureStats(dir)
	return side
}

// constrainPresetForCompile 只固定用户方案的贴图边长。
//
// 内置方案的边长是设计好的：主集 4096、大厅 2048。大厅用 2048 是上游 FFXIV-FontChanger
// 验证过的规格，跟着改会凭空多出一组没验证过的配置；用户自带方案则来源不可控，
// 1024/2048 装不下中文字形，只会在替换阶段失败。
func constrainPresetForCompile(presetID, configJson string) (string, int, error) {
	if _, builtin := fontengine.BuiltinPresetMap[presetID]; builtin {
		return configJson, 0, nil
	}
	return fontengine.ConstrainSideLength(configJson)
}

// StartPatch 执行完整的校验与汉化流程。
func (a *App) StartPatch(req PatchRequest) (resp PatchResponse, err error) {
	started := time.Now()
	// 资源、备份、配置的位置在会话快照里已经有了，这里只记与本次请求相关的。
	sqpackDesc := "未选择游戏目录"
	if req.GameDir != "" {
		sqpackDesc = filepath.Join(req.GameDir, "game", "sqpack", "ffxiv")
	}
	a.emitDiag("=== 开始汉化 %s === 请求：%s｜sqpack=%s",
		started.Format("2006-01-02 15:04:05"), patchRequestSummary(req), sqpackDesc)
	defer func() {
		state := "成功"
		if err != nil {
			state = "失败"
		} else if resp.Cancelled {
			state = "已取消"
		}
		var extra strings.Builder
		if err != nil {
			fmt.Fprintf(&extra, "｜原始错误=%v", err)
		}
		if len(resp.Warnings) > 0 {
			fmt.Fprintf(&extra, "｜警告=%s", strings.Join(resp.Warnings, "、"))
		}
		if len(resp.Notes) > 0 {
			fmt.Fprintf(&extra, "｜备注=%s", strings.Join(resp.Notes, "、"))
		}
		if len(resp.Downloaded) > 0 {
			fmt.Fprintf(&extra, "｜本次下载=%s", strings.Join(resp.Downloaded, "、"))
		}
		// 提前失败时 Plan 是零值，打出来只是噪音。
		planDesc := "未执行"
		if resp.Plan.GameVersion != "" {
			planDesc = fmt.Sprintf("%+v", resp.Plan)
		}
		a.emitDiag("汉化结束：%s｜耗时 %s｜计划=%s｜文本=%+v｜字体=%+v%s",
			state, time.Since(started).Round(time.Millisecond), planDesc, resp.Text, resp.Font, extra.String())
	}()
	if req.GameDir == "" {
		return resp, errors.New("请先选择游戏目录")
	}
	lang, err := region.ParseLanguage(req.Language)
	if err != nil {
		return resp, err
	}
	if !req.ReplaceText && !req.ReplaceFont {
		return resp, errors.New("请至少勾选“替换文本”或“替换字体”")
	}
	if err := a.beginBusy(); err != nil {
		return resp, err
	}
	defer a.endBusy()

	parentCtx := a.ctx
	if parentCtx == nil {
		parentCtx = context.Background()
	}
	patchCtx, cancel := context.WithCancel(parentCtx)
	a.cancelPatchMu.Lock()
	a.cancelPatch = cancel
	a.cancelPatchMu.Unlock()
	defer func() {
		a.cancelPatchMu.Lock()
		a.cancelPatch = nil
		a.cancelPatchMu.Unlock()
		cancel()
	}()

	a.emitPatchLog(fmt.Sprintf("[程序] 汉化器版本：%s", updater.DisplayAppVersion(appVersion)))

	if err := a.checkGameNotRunning(patchCtx, req.GameDir); err != nil {
		if errors.Is(err, gameproc.ErrCancelled) {
			a.emitPatchLog("[提示] 汉化已取消：游戏启动器正在运行")
			resp.Cancelled = true
			return resp, nil
		}
		return resp, err
	}

	targetReg := region.Of(lang)
	actualReg, detectErr := verifier.DetectClientRegion(req.GameDir)
	a.emitDiag("客户端区服探测：结果=%s，错误=%v", actualReg, detectErr)
	if detectErr == nil && actualReg != "" && actualReg != targetReg {
		return resp, fmt.Errorf("区服不匹配：目录为【%s】客户端，目标区服为【%s】，已停止汉化。请切换区服或重新选择目录",
			region.DisplayName(actualReg), region.DisplayName(targetReg))
	}

	resourceDir := a.resourceDir()
	regionKey := normalizeRegionCode(string(lang))

	// 字体来源先定下来：选了自定义方案却拿不到编译组件时，在写入任何文件之前就报错，
	// 而不是等文本汉化跑完再回退成内置字体包。
	if req.ReplaceFont {
		if err := a.checkFontSource(patchCtx, resourceDir, req.GameDir); err != nil {
			if errors.Is(err, context.Canceled) {
				a.emitPatchLog("[提示] 汉化已取消：下载字体编译组件时被中止")
				resp.Cancelled = true
				return resp, nil
			}
			return resp, err
		}
	}

	// 表清单缺失时先补一份；补不到就无法列出可汉化的表，直接中止。
	if err := a.ensureTableDoc(patchCtx, resourceDir); err != nil {
		if errors.Is(err, context.Canceled) {
			a.emitPatchLog("[提示] 汉化已取消：同步表清单时被中止")
			resp.Cancelled = true
			return resp, nil
		}
		return resp, fmt.Errorf("%w；已中止汉化%s", err, a.syncFailureHint())
	}
	customSelected := a.csvSelectedDir(resourceDir, regionKey) != ""
	// 程序可能长期不退出，只在启动时同步会让词条一直停在旧版本：
	// 距上次检查满 30 分钟就先确认一次云端更新。自定义目录不联网。
	if !customSelected && a.builtinSynced.Load() && a.resourceSyncStale() {
		a.emitPatchLog("[资源] 距上次检查词条更新已超过 30 分钟，正在确认云端版本…")
		if err := a.ensureBuiltinResource(patchCtx, resourceDir); err != nil {
			if errors.Is(err, context.Canceled) {
				a.emitPatchLog("[提示] 汉化已取消：检查词条更新时被中止")
				resp.Cancelled = true
				return resp, nil
			}
			// 刷新失败不阻断：本次运行启动时已校验通过，按本地已有资源继续。
			a.emitPatchLog(fmt.Sprintf("[警告] 检查词条更新失败，继续使用本地资源：%v", err))
		}
	}
	// 内置词条只有同步跑通后才允许汉化；启动时同步失败过就在开始前再试一次。
	needSync := resourceIncomplete(resourceDir, customSelected)
	if !customSelected && !a.builtinSynced.Load() {
		needSync = true
		a.emitPatchLog("[资源] 内置词条尚未同步通过，开始前先重试一次…")
	}
	if needSync {
		if err := a.ensureBuiltinResource(patchCtx, resourceDir); err != nil {
			if errors.Is(err, context.Canceled) {
				a.emitPatchLog("[提示] 汉化已取消：同步资源时被中止")
				resp.Cancelled = true
				return resp, nil
			}
			return resp, fmt.Errorf("内置词条未同步通过，已中止汉化%s：%w", a.syncFailureHint(), err)
		}
	}

	csvDir, customCSV, err := a.resolveCSVDir(resourceDir, regionKey)
	if err != nil {
		return resp, err
	}
	if !customCSV {
		if st, statErr := os.Stat(csvDir); statErr != nil || !st.IsDir() {
			return resp, fmt.Errorf("内置词条不可用：本地缺少 rawexd 目录，请联网同步一次资源后再试")
		}
	}

	if a.updater.LocalOnly {
		a.emitPatchLog("[资源] 本地开发构建：使用工作区 resource/ 中的内置词条")
	} else if manifest, manifestErr := updater.ReadLocalManifest(resourceDir); manifest != nil {
		resTime := formatResourceTime(manifest.GeneratedAt)
		resDetail := formatResourceHashDetail(manifest)
		if resTime != "" {
			a.emitPatchLog(fmt.Sprintf("[资源] 内置资源版本：构建于 %s（%s）", resTime, resDetail))
		} else {
			a.emitPatchLog(fmt.Sprintf("[资源] 内置资源版本（%s）", resDetail))
		}
	} else if manifestErr != nil && !errors.Is(manifestErr, os.ErrNotExist) {
		a.emitPatchLog(fmt.Sprintf("[警告] 读取本地 manifest.json 失败: %v", manifestErr))
	}
	if customCSV {
		a.emitPatchLog(fmt.Sprintf("[词条] 本次使用自定义词条目录：%s（扩展功能已停用）", csvDir))
	}

	// 与资源同步互斥，避免读到正在被解压覆盖的 CSV。
	unlockResource, lockErr := a.lockResource(patchCtx)
	if lockErr != nil {
		if errors.Is(lockErr, context.Canceled) {
			a.emitPatchLog("[提示] 汉化已取消：等待资源更新完成时被中止")
			resp.Cancelled = true
			return resp, nil
		}
		return resp, lockErr
	}
	defer unlockResource()
	backupRoot := backupRootFor(resourceDir)
	sqpackDir := filepath.Join(req.GameDir, "game", "sqpack", "ffxiv")

	ruleList, ruleErr := loadRules(resourceDir)
	if ruleErr != nil {
		a.emitPatchLog(fmt.Sprintf("[警告] %v", ruleErr))
	}
	registry := pipeline.NewRegistry(ruleList)
	if customCSV {
		// 自定义词条目录无法保证与内置规则数据配套，全部规则停用。
		for _, meta := range registry.List() {
			registry.SetEnabled(meta.ID, false)
		}
	} else {
		for id, on := range req.Rules {
			registry.SetEnabled(id, on)
		}
	}
	ruleStates := make([]string, 0, len(registry.List()))
	for _, meta := range registry.List() {
		ruleStates = append(ruleStates, fmt.Sprintf("%s=%t", meta.ID, registry.Enabled(meta.ID)))
	}

	// 目标语言：非国际服固定为该区服唯一语言；国际服使用用户勾选列表，
	// 未指定时默认处理日语与英语。
	targetLangs := region.TargetLanguagesFor(lang)
	if region.Of(lang) == region.Global {
		if filtered := sanitizeTargetLanguages(lang, req.TargetLanguages); len(filtered) > 0 {
			targetLangs = filtered
		} else {
			targetLangs = []string{"JA", "EN"}
		}
	}
	labels := make([]string, 0, len(targetLangs))
	for _, l := range targetLangs {
		code := strings.ToUpper(l)
		name := region.LanguageName(region.Language(code))
		if name == code {
			labels = append(labels, code)
			continue
		}
		labels = append(labels, fmt.Sprintf("%s（%s）", name, code))
	}
	a.emitPatchLog(fmt.Sprintf("[范围] 本次汉化目标语言：%s", strings.Join(labels, "、")))
	a.emitDiag("生效配置：词条目录=%s（自定义=%t）｜sqpack=%s｜目标语言=%v｜词表=%d/%s｜规则=%s｜规则数据加载错误=%v",
		csvDir, customCSV, sqpackDir, targetLangs, len(req.SelectedTables),
		stringListFingerprint(req.SelectedTables), strings.Join(ruleStates, "、"), ruleErr)

	if req.ReplaceText && len(req.SelectedTables) == 0 {
		return resp, errors.New("未选择任何需要汉化的文本数据表")
	}

	include := map[string]bool{}
	if len(req.SelectedTables) > 0 {
		for _, key := range req.SelectedTables {
			include["exd/"+strings.ToLower(key)] = true
		}
	}

	cfg := verifier.Config{
		GameDir:        req.GameDir,
		BackupRoot:     backupRoot,
		Language:       lang,
		ReplaceText:    req.ReplaceText,
		ReplaceFont:    req.ReplaceFont,
		Cloud:          a.getCloud(),
		ConfirmUnknown: a.waitUnknownConfirm,
		OnEvent: func(ev verifier.Event) {
			a.emitProgress(ev.Stage, ev.Detail, ev.Done, ev.Total)
		},
		Log: func(format string, args ...any) {
			a.emitPatchLog(fmt.Sprintf(format, args...))
		},
	}
	// undos 记录本次已开始写入的文件组，失败或中止时按相反顺序撤回。
	// 它只撤销「本次写入」，不依赖原版备份，所以未知版本也能回滚。
	var undos []*replace.Undo

	cfg.ApplyText = func(ctx context.Context) error {
		a.emitPatchLog("[文本] 开始汉化游戏文本数据...")
		var lastLoggedPct int = -1
		formatter := func(done, total int, file string) {
			a.emitProgress("text", file, int64(done), int64(total))
			if total > 0 {
				pct := (done * 100) / total
				if (pct == 25 || pct == 50 || pct == 75) && pct != lastLoggedPct {
					lastLoggedPct = pct
					a.emitPatchLog(fmt.Sprintf("[文本] 汉化进度：%d%% (%d/%d)", pct, done, total))
				}
			}
		}
		textIndexPath := filepath.Join(sqpackDir, "0a0000.win32.index")
		a.emitDiag("文本写入：索引=%s｜包含表=%d", textIndexPath, len(include))
		undo, undoErr := replace.NewUndo(textIndexPath)
		if undoErr != nil {
			return undoErr
		}
		undos = append(undos, undo)
		stats, err := replace.ReplaceEXDF(replace.Options{
			Context:         ctx,
			TextIndexPath:   textIndexPath,
			CSVResourceDir:  csvDir,
			QuietMissingCSV: customCSV,
			SourceLanguage:  string(lang),
			TargetLanguages: targetLangs,
			IncludeFiles:    include,
			Rules:           registry,
			Progress:        formatter,
			Log:             cfg.Log,
			Undo:            undo,
		})
		if err == nil {
			a.emitPatchLog(fmt.Sprintf("[文本] 文本汉化完成：修改 %d 个数据表，更新 %d 个文件，替换 %d 条文本", stats.Tables, stats.Files, stats.Entries))
		}
		resp.Text = stats
		return err
	}
	cfg.ApplyFont = func(ctx context.Context) error {
		a.emitPatchLog("[字体] 开始处理字体...")
		presetID, configJson, _, err := a.resolveFontPresetContent(resourceDir, regionKey, region.Of(lang))
		if err != nil {
			return err
		}

		a.syncFontEngineDirs(ctx, resourceDir, req.GameDir)
		fontDir := ""
		// 字体来源由界面选择，两条路互斥：选哪条就用哪条，任何一步失败都直接中止，
		// 不会自动换成另一种来源，否则用户无法判断最终生效的到底是哪一套字体。
		if a.fontSourceMode(resourceDir) == "compile" {
			if goruntime.GOOS != "windows" {
				return errors.New("自定义方案需在 Windows 上编译字体，当前系统不支持，请改用「内置字体包」")
			}
			// 贴图在「编译」时已产出并校验，这里只确认它与当前方案一致。
			// 不一致时停止，让用户重新编译；不在汉化流程中临时补编，避免中途才发现问题。
			info := a.fontBuildInfoFor(resourceDir, presetID, configJson, a.fontTexture16Bit(resourceDir))
			if !info.Ready {
				return fmt.Errorf("字体方案尚未编译完成：%s。请回到工作台，在字体来源处执行「编译」，通过后再开始汉化", info.Reason)
			}
			a.emitPatchLog(fmt.Sprintf("[字体] 使用已编译的方案（%d 张贴图，边长 %dpx）", info.TexCount, info.SideLength))
			fontDir = fontBuildDir(resourceDir)
		} else {
			// 内置字体包：本地已有 resource/font 就直接用，不联网、不覆盖。
			if dir, ok := localFontPackage(resourceDir); ok {
				a.emitPatchLog("[字体] 使用内置字体包")
				fontDir = dir
			} else {
				a.emitPatchLog("[字体] 本地没有字体包，正在下载（首次使用）...")
				if err := a.updater.EnsureFontPack(ctx, resourceDir, a.emitResourceProgress); err != nil {
					return fmt.Errorf("获取字体包失败：%w", err)
				}
				fontDir = filepath.Join(resourceDir, "font")
			}
		}

		fontCount, fontSide := fontTextureStats(fontDir)
		cfgFileDesc, cfgFileErr := gamecfg.FindConfigFile()
		if cfgFileErr != nil {
			cfgFileDesc = fmt.Sprintf("未找到（%v）", cfgFileErr)
		}
		a.emitDiag("字体：方案=%s（sha256 %s）｜来源=%s｜16 位=%t｜目录=%s｜%d 张/%dpx｜游戏配置=%s",
			presetID, hashText(configJson), a.fontSourceMode(resourceDir), a.fontTexture16Bit(resourceDir),
			fontDir, fontCount, fontSide, cfgFileDesc)

		// 4096 的字体贴图需要游戏开启「高分辨率 UI 贴图」（FFXIV-FontChanger 的
		// Wiki 也是这么要求的），否则大厅/界面会闪退。1024 的原版规格不需要。
		if side := maxFontTextureSize(fontDir); side > 1024 {
			cfgRes, cfgErr := gamecfg.EnsureHighResolutionUiAsset("")
			if cfgErr != nil {
				if errors.Is(cfgErr, gamecfg.ErrConfigNotFound) {
					a.emitPatchLog(fmt.Sprintf("[提示] 字体贴图为 %dpx，请在游戏设置中开启高分辨率界面贴图，否则可能闪退", side))
				} else {
					a.emitPatchLog(fmt.Sprintf("[警告] 检查/修改游戏配置文件失败: %v", cfgErr))
				}
			} else if cfgRes.Modified {
				a.emitPatchLog(fmt.Sprintf("[配置] 字体贴图为 %dpx，已备份配置并开启高分辨率贴图", side))
			} else {
				a.emitPatchLog(fmt.Sprintf("[配置] 字体贴图为 %dpx，游戏已是高分辨率贴图", side))
			}
		}

		fontIndexPath := filepath.Join(sqpackDir, "000000.win32.index")
		a.emitPatchLog("[字体] 正在校验贴图数量...")
		if err := replace.CheckFontFiles(fontIndexPath, fontDir); err != nil {
			return err
		}

		a.emitPatchLog("[字体] 正在写入客户端数据包...")
		undo, undoErr := replace.NewUndo(fontIndexPath)
		if undoErr != nil {
			return undoErr
		}
		undos = append(undos, undo)
		stats, err := replace.ReplaceFontWithUndo(
			ctx,
			fontIndexPath,
			fontDir,
			func(done, total int, file string) {
				a.emitProgress("font", file, int64(done), int64(total))
			},
			undo,
		)
		if err == nil {
			a.emitPatchLog(fmt.Sprintf("[字体] 字体汉化完成：替换 %d 个字体文件", stats.Files))
		}
		resp.Font = stats
		return err
	}

	result, err := verifier.Run(patchCtx, cfg)
	if err != nil {
		cancelled := errors.Is(err, context.Canceled)
		restoredOriginals := result != nil && (result.RestoredText || result.RestoredFont)

		if len(undos) > 0 {
			// 本次已经动过客户端文件：只撤回本次写入，回到「汉化之前」的样子，
			// 不再用原版备份把 6 个文件整体覆盖——那会连带回滚本次没碰的文件组，
			// 而且未知版本根本没有可用的原版备份。
			if cancelled {
				a.emitPatchLog("[提示] 汉化已被用户中止，正在撤回本次写入，恢复为写入前的文件...")
			} else {
				a.emitPatchLog(fmt.Sprintf("[错误] 汉化失败：%v，正在撤回本次写入，恢复为写入前的文件...", err))
			}
			changed, rbErr := revertWrites(undos)
			switch {
			case rbErr != nil:
				a.emitPatchLog(fmt.Sprintf("[警告] 撤回本次写入失败: %v，请点击「还原」手动恢复", rbErr))
			case changed > 0:
				a.emitPatchLog("[回滚] 已撤回本次写入，客户端文件回到写入前的状态")
			case restoredOriginals:
				a.emitPatchLog("[提示] 本次尚未写入数据；客户端文件已按官方原版还原")
			default:
				a.emitPatchLog("[提示] 本次尚未写入数据，客户端文件未被修改")
			}
			resp.Cancelled = cancelled
			if cancelled {
				return resp, nil
			}
			return resp, err
		}

		// 写入前就失败了：不做任何还原，只按实际发生的事写日志。
		switch {
		case cancelled && restoredOriginals:
			a.emitPatchLog("[提示] 汉化已取消：本次尚未写入数据，客户端文件已按官方原版还原")
		case cancelled:
			a.emitPatchLog("[提示] 汉化已取消，客户端文件未被修改")
		case restoredOriginals:
			a.emitPatchLog(fmt.Sprintf("[错误] 汉化失败：%v（本次尚未写入数据；客户端文件已按官方原版还原）", err))
		default:
			a.emitPatchLog(fmt.Sprintf("[错误] 汉化失败：%v（本次未修改客户端文件）", err))
		}
		resp.Cancelled = cancelled
		if cancelled {
			return resp, nil
		}
		return resp, err
	}
	if !result.Cancelled {
		a.emitPatchLog("[完成] 汉化补丁全部应用成功，可以启动游戏！")
	}
	resp.Plan = result.Plan
	resp.Cancelled = result.Cancelled
	resp.Downloaded = result.Downloaded
	resp.Warnings = result.Warnings
	resp.Notes = result.Plan.Notes

	// 保存游戏目录与语言，便于下次启动。
	cfgSaved := a.loadConfig(resourceDir)
	if cfgSaved.GameDirs == nil {
		cfgSaved.GameDirs = make(map[string]string)
	}
	normLang := normalizeRegionCode(string(lang))
	cfgSaved.GameDirs[normLang] = req.GameDir
	cfgSaved.Language = string(lang)
	if cfgSaved.TargetLanguages == nil {
		cfgSaved.TargetLanguages = LanguagesByRegion{}
	}
	cfgSaved.TargetLanguages[normLang] = targetLangs
	cfgSaved.ReplaceText = req.ReplaceText
	cfgSaved.ReplaceFont = req.ReplaceFont
	cfgSaved.Preset = req.Preset
	if cfgSaved.Rules == nil {
		cfgSaved.Rules = map[string]bool{}
	}
	for id, on := range req.Rules {
		cfgSaved.Rules[id] = on
	}
	if err := a.SaveConfig(cfgSaved); err != nil {
		a.emitPatchLog(fmt.Sprintf("[警告] 保存本地配置失败: %v", err))
	}

	return resp, nil
}

// RestoreOriginal 从本地备份或云端备份还原文本与字体原文件。
func (a *App) RestoreOriginal(gameDir, language string, restoreText, restoreFont bool) (resp RestoreResponse, err error) {
	started := time.Now()
	a.emitDiag("=== 开始还原 %s === 目录=%s｜区服=%s｜文本=%t｜字体=%t｜云端仓库=%s",
		started.Format("2006-01-02 15:04:05"), gameDir, language, restoreText, restoreFont, appUpdateRepo)
	defer func() {
		state := "成功"
		if err != nil {
			state = "失败"
		} else if resp.Cancelled {
			state = "已取消"
		}
		extra := ""
		if err != nil {
			extra = fmt.Sprintf("｜原始错误=%v", err)
		}
		a.emitDiag("还原结束：%s｜耗时 %s｜已恢复 %d 个文件（%v）%s",
			state, time.Since(started).Round(time.Millisecond), len(resp.Restored), resp.Restored, extra)
	}()
	if gameDir == "" {
		return resp, errors.New("请先选择游戏目录")
	}
	if !restoreText && !restoreFont {
		return resp, errors.New("请至少勾选文本或字体")
	}
	lang, err := region.ParseLanguage(language)
	if err != nil {
		return resp, err
	}
	if err := a.beginBusy(); err != nil {
		return resp, err
	}
	defer a.endBusy()

	// 还原同样接入 CancelPatch：用户可以在还原中途取消，而不是只能等它跑完。
	parentCtx := a.ctx
	if parentCtx == nil {
		parentCtx = context.Background()
	}
	patchCtx, cancel := context.WithCancel(parentCtx)
	a.cancelPatchMu.Lock()
	a.cancelPatch = cancel
	a.cancelPatchMu.Unlock()
	defer func() {
		a.cancelPatchMu.Lock()
		a.cancelPatch = nil
		a.cancelPatchMu.Unlock()
		cancel()
	}()

	if err := a.checkGameNotRunning(patchCtx, gameDir); err != nil {
		if errors.Is(err, gameproc.ErrCancelled) {
			a.emitPatchLog("[提示] 还原已取消：游戏启动器正在运行")
			resp.Cancelled = true
			return resp, nil
		}
		return resp, err
	}

	targetReg := region.Of(lang)
	actualReg, detectErr := verifier.DetectClientRegion(gameDir)
	a.emitDiag("客户端区服探测：结果=%s，错误=%v", actualReg, detectErr)
	if detectErr == nil && actualReg != "" && actualReg != targetReg {
		return resp, fmt.Errorf("区服不匹配：目录为【%s】客户端，目标区服为【%s】，已停止还原",
			region.DisplayName(actualReg), region.DisplayName(targetReg))
	}

	scope := "文本 + 字体"
	if restoreText != restoreFont {
		if restoreText {
			scope = "仅文本"
		} else {
			scope = "仅字体"
		}
	}
	a.emitPatchLog(fmt.Sprintf("[程序] 汉化器版本：%s", updater.DisplayAppVersion(appVersion)))
	a.emitPatchLog(fmt.Sprintf("[还原] 开始执行官方原版恢复流程（%s）...", scope))
	resourceDir := a.resourceDir()
	restored, err := verifier.Restore(patchCtx, verifier.RestoreConfig{
		GameDir:    gameDir,
		BackupRoot: backupRootFor(resourceDir),
		Language:   lang,
		Text:       restoreText,
		Font:       restoreFont,
		Repo:       "Souma-Sumire/FFXIVChnTextPatch-Souma",
		Cloud:      a.getCloud(),
		OnProgress: func(stage, detail string, done, total int64) {
			a.emitProgress(stage, detail, done, total)
		},
		Log: func(format string, args ...any) {
			a.emitPatchLog(fmt.Sprintf(format, args...))
		},
	})
	if err != nil {
		if errors.Is(err, context.Canceled) {
			a.emitPatchLog("[提示] 还原已取消")
			resp.Cancelled = true
			return resp, nil
		}
		a.emitPatchLog(fmt.Sprintf("[错误] 还原失败：%v", err))
		return resp, err
	}
	resp.Restored = restored
	if len(restored) == 0 {
		a.emitPatchLog("[完成] 客户端已是官方原版，无需还原")
	} else {
		a.emitPatchLog("[完成] 官方原版文件已全部恢复完成")
	}
	return resp, nil
}

// SyncResources 从云端同步 CSV 汉化数据。
func (a *App) SyncResources() (SyncSummary, error) {
	var summary SyncSummary
	if a.updater.LocalOnly {
		return summary, errors.New("本地开发构建不访问云端资源，请直接修改工作区 resource/")
	}
	if err := a.beginBusy(); err != nil {
		return summary, err
	}
	defer a.endBusy()

	resourceDir := a.resourceDir()
	unlock, lockErr := a.lockResource(a.ctx)
	if lockErr != nil {
		return summary, lockErr
	}
	defer unlock()
	// 复用共享的 updater 实例：节点连通性记忆（直连是否够快、哪个镜像最快）
	// 在手动同步与汉化前检查之间共享，避免重新探测一遍。
	u := a.updater
	status, err := u.Provision(a.ctx, resourceDir, func(stage, detail string, done, total int64) {
		a.emitEvent("sync-progress", ProgressEvent{
			Stage: stage, Detail: detail, Done: done, Total: total,
		})
	})
	if err != nil {
		return summary, err
	}
	// 手动同步成功同样标记「已同步」，否则下次汉化前还会再联网确认一次。
	a.resourceSyncedAt.Store(time.Now().UnixNano())
	a.builtinSynced.Store(true)
	summary.UpToDate = !status.Updated
	summary.Changed = status.DownloadedFiles
	summary.Full = status.Updated
	return summary, nil
}

// OpenLink 使用系统浏览器打开链接。
func (a *App) OpenLink(url string) error {
	if !strings.HasPrefix(url, "https://") {
		return errors.New("仅允许打开 HTTPS 链接")
	}
	if !a.hasUI() {
		return errors.New("桌面运行时未就绪")
	}
	return a.wailsApp.Browser.OpenURL(url)
}

// EnsureHighResolutionUiAsset 检查并确保 FFXIV.cfg 开启高分辨率贴图模式 (UiAssetType 1)。
func (a *App) EnsureHighResolutionUiAsset() (gamecfg.ModifyResult, error) {
	return gamecfg.EnsureHighResolutionUiAsset("")
}

// CheckAppUpdate 查询云端获取汉化器主程序的最新版本并比对当前版本。
// 汉化、还原进行中直接拒绝：此时替换程序或拉取元数据都会干扰正在写入客户端的任务。
func (a *App) CheckAppUpdate() (updater.AppUpdateInfo, error) {
	current := updater.DisplayAppVersion(appVersion)
	if a.updater.LocalOnly {
		return updater.AppUpdateInfo{CurrentVersion: current, LatestVersion: current}, nil
	}
	if a.isBusy() {
		return updater.AppUpdateInfo{CurrentVersion: current, LatestVersion: current}, errors.New("汉化进行中，已跳过检查更新")
	}

	// API 阶段与网页端回退各有一次超时，整体预算需要覆盖「直连 API 不通 → 镜像 API → 网页端」的完整链路。
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	info, err := updater.CheckAppUpdate(ctx, a.getCloud(), appUpdateRepo, appVersion)
	if err != nil {
		return updater.AppUpdateInfo{CurrentVersion: current, LatestVersion: current}, err
	}
	return *info, nil
}

// InstallAppUpdate 下载最新版本、替换当前程序并重启。
// 成功后当前进程退出，由新版本接管；下载或替换失败时保持原程序可用并返回错误。
func (a *App) InstallAppUpdate() error {
	if a.updater.LocalOnly {
		return errors.New("本地开发构建不支持自动更新，请重新构建")
	}
	if !updater.SelfUpdateSupported() {
		return updater.ErrSelfUpdateUnsupported
	}
	// 借用忙碌互斥：更新期间不允许开始汉化，汉化期间也不允许进入更新。
	if err := a.beginBusy(); err != nil {
		return err
	}
	defer a.endBusy()

	exe, err := os.Executable()
	if err != nil {
		return fmt.Errorf("无法定位当前程序: %w", err)
	}

	checkCtx, cancelCheck := context.WithTimeout(context.Background(), 15*time.Second)
	info, err := updater.CheckAppUpdate(checkCtx, a.getCloud(), appUpdateRepo, appVersion)
	cancelCheck()
	if err != nil {
		return err
	}
	if info == nil || !info.HasUpdate {
		return errors.New("当前已是最新版本")
	}

	a.emitAppUpdateProgress("download", fmt.Sprintf("正在下载 v%s", info.LatestVersion), 0, 0)

	downloadCtx, cancelDownload := context.WithTimeout(context.Background(), 30*time.Minute)
	defer cancelDownload()
	// 正常网络下首个线路即可完成下载，线路名对用户没有意义。
	// 只有首条线路失败、真正发生切换时才记录，便于排查网络问题。
	node := ""
	detail := fmt.Sprintf("正在下载 v%s", info.LatestVersion)
	tmpPath, err := updater.PrepareAppUpdate(downloadCtx, a.getCloud(), info, exe, func(done, total int64) {
		a.emitAppUpdateProgress("download", detail, done, total)
	}, func(label string) {
		if node == "" {
			node = label
			return
		}
		a.emitPatchLog(fmt.Sprintf("[更新] 下载线路 %s 不可用，已切换", node))
		node = label
	})
	if err != nil {
		return fmt.Errorf("下载新版本失败: %w", err)
	}

	a.emitAppUpdateProgress("install", "正在安装更新", 0, 0)
	if _, err := updater.SwapExecutable(tmpPath, exe); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}

	a.emitPatchLog(fmt.Sprintf("[更新] 新版本 v%s 已安装", info.LatestVersion))
	a.emitAppUpdateProgress("done", "更新已安装，重启后生效", 0, 0)
	return nil
}

// RestartApp 启动已安装的新版本并退出当前进程。
// 更新完成后由用户确认再调用，避免程序在用户没准备好的时候直接关闭。
func (a *App) RestartApp() error {
	// macOS 发布包是 .app，直接拉起包内二进制会绕过 LaunchServices，
	// 自更新本来就走手动下载，这里挡住误调用。
	if !updater.SelfUpdateSupported() {
		return updater.ErrSelfUpdateUnsupported
	}
	if a.isBusy() {
		return errors.New("有任务正在进行，无法重启")
	}
	exe, err := os.Executable()
	if err != nil {
		return fmt.Errorf("无法定位当前程序: %w", err)
	}
	if err := updater.LaunchExecutable(exe); err != nil {
		// 新版本起不来时把旧程序放回原位，避免下次启动直接失败。
		if rbErr := updater.RollbackExecutable(exe, exe+".old"); rbErr == nil {
			return fmt.Errorf("启动新版本失败: %v，已恢复原程序", err)
		}
		return fmt.Errorf("启动新版本失败: %v", err)
	}

	go func() {
		time.Sleep(700 * time.Millisecond)
		if a.hasUI() {
			a.BeginQuit()
			return
		}
		os.Exit(0)
	}()
	return nil
}

// emitAppUpdateProgress 汇报自更新进度。下载回调触发很密，只按时间间隔转发。
func (a *App) emitAppUpdateProgress(stage, detail string, done, total int64) {
	if !a.hasUI() {
		return
	}
	if total > 0 && done < total {
		a.updateEmitMu.Lock()
		if time.Since(a.updateEmitAt) < 200*time.Millisecond {
			a.updateEmitMu.Unlock()
			return
		}
		a.updateEmitAt = time.Now()
		a.updateEmitMu.Unlock()
	}
	a.emitEvent("app-update-progress", ProgressEvent{Stage: stage, Detail: detail, Done: done, Total: total})
}

func isGameClientDir(dir string) bool {
	if dir == "" {
		return false
	}
	st, err := os.Stat(filepath.Join(dir, "game", "ffxiv_dx11.exe"))
	return err == nil && !st.IsDir()
}

func defaultPresetForRegionWithDirs(targetReg region.Region, gameDirs map[string]string) string {
	if targetReg == region.Global {
		chnDir := ""
		if gameDirs != nil {
			chnDir = gameDirs[string(region.CHS)]
		}
		if chnDir != "" && isGameClientDir(chnDir) {
			return fontengine.BuiltinChnAXISPresetID
		}
		return fontengine.BuiltinChnAXISSystemPresetID
	}
	return fontengine.DefaultPresetForRegion(targetReg)
}

func defaultPresetForRegionWithConfig(targetReg region.Region, cfg AppConfig) string {
	return defaultPresetForRegionWithDirs(targetReg, cfg.GameDirs)
}

func availablePresetsForRegionWithDirs(targetReg region.Region, customPresetsDir string, gameDirs map[string]string) []fontengine.PresetSummary {
	all := fontengine.GetPresetsForRegion(targetReg, customPresetsDir)
	if targetReg != region.Global {
		return all
	}
	chnDir := ""
	if gameDirs != nil {
		chnDir = gameDirs[string(region.CHS)]
	}
	if isGameClientDir(chnDir) {
		return all
	}
	filtered := make([]fontengine.PresetSummary, 0, len(all))
	for _, p := range all {
		if p.ID == fontengine.BuiltinChnAXISPresetID {
			continue
		}
		filtered = append(filtered, p)
	}
	return filtered
}

// getActiveFontPreset 返回该区服当前选中的方案 id。
// 配置里记的方案可能已被删除、改名或换过版本，这里只返回确实还能用的 id，
// 否则就退回该区服的默认方案（方案文件本身缺失才会走到更外层的报错）。
func (a *App) getActiveFontPreset(regionKey string) string {
	a.cfgMu.Lock()
	defer a.cfgMu.Unlock()
	normKey := normalizeRegionCode(regionKey)
	resourceDir := a.resourceDir()
	cfg := a.loadConfigLocked(resourceDir)

	parsedLang, _ := region.ParseLanguage(normKey)
	targetReg := region.Of(parsedLang)
	fallback := defaultPresetForRegionWithConfig(targetReg, cfg)

	stored := ""
	if cfg.FontPresets != nil {
		stored = cfg.FontPresets[normKey]
	}
	if stored == "" || stored == fallback {
		return fallback
	}
	for _, preset := range availablePresetsForRegionWithDirs(targetReg, presetsDirFor(resourceDir), cfg.GameDirs) {
		if preset.ID == stored {
			return stored
		}
	}
	return fallback
}

func (a *App) resolveLanguageAndGameDir(language string) (region.Language, string) {
	a.cfgMu.Lock()
	cfg := a.loadConfigLocked(a.resourceDir())
	a.cfgMu.Unlock()

	langStr := strings.TrimSpace(language)
	if langStr == "" {
		langStr = cfg.Language
	}
	parsedLang, err := region.ParseLanguage(langStr)
	if err != nil {
		parsedLang = region.GLOBAL
	}
	normKey := normalizeRegionCode(string(parsedLang))
	gameDir := ""
	if cfg.GameDirs != nil {
		gameDir = cfg.GameDirs[normKey]
	}
	return parsedLang, gameDir
}

func (a *App) GetFontPresets(language string) ([]fontengine.PresetSummary, error) {
	parsedLang, _ := a.resolveLanguageAndGameDir(language)
	targetReg := region.Of(parsedLang)
	resourceDir := a.resourceDir()
	a.cfgMu.Lock()
	cfg := a.loadConfigLocked(resourceDir)
	a.cfgMu.Unlock()
	return availablePresetsForRegionWithDirs(targetReg, presetsDirFor(resourceDir), cfg.GameDirs), nil
}

func (a *App) GetFontPreset(id string) (string, error) {
	return fontengine.GetPresetContent(id, presetsDirFor(a.resourceDir()))
}

func (a *App) SaveFontPreset(language string, name string, configJson string) (fontengine.PresetSummary, error) {
	parsedLang, _ := a.resolveLanguageAndGameDir(language)
	targetReg := region.Of(parsedLang)
	resourceDir := a.resourceDir()
	summary, err := fontengine.SaveCustomPreset(name, configJson, presetsDirFor(resourceDir), targetReg)
	if err != nil {
		return fontengine.PresetSummary{}, err
	}
	_ = a.SelectFontPreset(string(parsedLang), summary.ID)
	return summary, nil
}

// DeleteFontPreset 删除自定义字体方案；内置方案不允许删除。
// 删掉的正好是当前选中方案时，配置切回该区服的默认方案。
func (a *App) DeleteFontPreset(language string, presetId string) error {
	parsedLang, _ := a.resolveLanguageAndGameDir(language)
	resourceDir := a.resourceDir()
	if err := fontengine.DeleteCustomPreset(presetId, presetsDirFor(resourceDir)); err != nil {
		return err
	}
	targetReg := region.Of(parsedLang)
	regionKey := normalizeRegionCode(string(parsedLang))

	a.cfgMu.Lock()
	defer a.cfgMu.Unlock()
	cfg := a.loadConfigLocked(resourceDir)
	if cfg.FontPresets == nil || cfg.FontPresets[regionKey] != presetId {
		return nil
	}
	cfg.FontPresets[regionKey] = defaultPresetForRegionWithConfig(targetReg, cfg)
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(configPathFor(resourceDir), append(data, '\n'))
}

// RenameFontPreset 重命名字体方案（仅自定义方案）；配置里指向旧 id 时同步改成新 id。
func (a *App) RenameFontPreset(language string, presetId string, newName string) (fontengine.PresetSummary, error) {
	parsedLang, _ := a.resolveLanguageAndGameDir(language)
	resourceDir := a.resourceDir()
	summary, err := fontengine.RenameCustomPreset(presetId, newName, presetsDirFor(resourceDir))
	if err != nil {
		return fontengine.PresetSummary{}, err
	}
	regionKey := normalizeRegionCode(string(parsedLang))

	a.cfgMu.Lock()
	defer a.cfgMu.Unlock()
	cfg := a.loadConfigLocked(resourceDir)
	if cfg.FontPresets == nil || cfg.FontPresets[regionKey] != presetId {
		return summary, nil
	}
	cfg.FontPresets[regionKey] = summary.ID
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return summary, err
	}
	if err := writeFileAtomic(configPathFor(resourceDir), append(data, '\n')); err != nil {
		return summary, err
	}
	return summary, nil
}

// SelectFontPreset 记录该区服选中的方案。只接受当前区服可用的方案，
// 避免配置中留下会因区服不匹配而编译失败的 id。
func (a *App) SelectFontPreset(language string, presetId string) error {
	resourceDir := a.resourceDir()
	a.cfgMu.Lock()
	defer a.cfgMu.Unlock()

	cfg := a.loadConfigLocked(resourceDir)
	langStr := strings.TrimSpace(language)
	if langStr == "" {
		langStr = cfg.Language
	}
	parsedLang, err := region.ParseLanguage(langStr)
	if err != nil {
		return err
	}
	targetReg := region.Of(parsedLang)
	available := availablePresetsForRegionWithDirs(targetReg, presetsDirFor(resourceDir), cfg.GameDirs)
	usable := false
	for _, preset := range available {
		if preset.ID == presetId {
			usable = true
			break
		}
	}
	if !usable {
		return fmt.Errorf("字体方案不可用于当前区服：%s", presetId)
	}

	regionKey := normalizeRegionCode(langStr)
	if cfg.FontPresets == nil {
		cfg.FontPresets = map[string]string{}
	}
	cfg.FontPresets[regionKey] = presetId

	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(configPathFor(resourceDir), append(data, '\n'))
}

func (a *App) GetSystemFonts() ([]string, error) {
	return fontengine.ListSystemFonts()
}

// AutoFitFontPreset 用字体编译组件拟合该字号里的系统字体图层，
// 返回拟合后的元素列表 JSON；组件不可用时直接报错，不在前端用近似算法顶替。
func (a *App) AutoFitFontPreset(configJson string, fontName string, elementIndices []int) (string, error) {
	ctx := context.Background()
	if a.ctx != nil {
		ctx = a.ctx
	}
	_, gameDir := a.resolveLanguageAndGameDir("")
	a.syncFontEngineDirs(ctx, a.resourceDir(), gameDir)
	if err := a.ensureFontCompiler(ctx, a.resourceDir()); err != nil {
		return "", fmt.Errorf("字体拟合需要字体编译组件：%w", err)
	}
	return a.fontEngine.AutoFitPreset(ctx, configJson, fontName, elementIndices)
}

// RenderFontPreview 用字体编译组件渲染预览：与写进客户端的字形走同一套栅格化，
// 所以预览看到的就是游戏里的效果。组件不可用时直接报错，不用浏览器近似渲染顶替。
func (a *App) RenderFontPreview(configJson string, fontName string, text string, maxWidth int) (string, error) {
	ctx := context.Background()
	if a.ctx != nil {
		ctx = a.ctx
	}
	_, gameDir := a.resolveLanguageAndGameDir("")
	a.syncFontEngineDirs(ctx, a.resourceDir(), gameDir)
	if err := a.ensureFontCompiler(ctx, a.resourceDir()); err != nil {
		return "", fmt.Errorf("预览需要字体编译组件：%w", err)
	}
	pngBytes, err := a.fontEngine.RenderPreview(ctx, configJson, fontName, text, maxWidth)
	if err != nil {
		return "", err
	}
	b64 := base64.StdEncoding.EncodeToString(pngBytes)
	return "data:image/png;base64," + b64, nil
}

// HasFontCompiler 检查本地字体编译组件是否已就绪。
func (a *App) HasFontCompiler() bool {
	_, path, legacyPath := fontCompilerPaths(a.resourceDir())
	return verifyExecutable(path) == nil || verifyExecutable(legacyPath) == nil
}

// EnsureFontCompiler 供前端进入字体定制视图或执行字体操作前显式预检/下载字体编译组件。
func (a *App) EnsureFontCompiler() error {
	ctx := context.Background()
	if a.ctx != nil {
		ctx = a.ctx
	}
	return a.ensureFontCompiler(ctx, a.resourceDir())
}

func (a *App) ExportFontTTMP(configJson string, savePath string) error {
	ctx := context.Background()
	if a.ctx != nil {
		ctx = a.ctx
	}
	_, gameDir := a.resolveLanguageAndGameDir("")
	a.syncFontEngineDirs(ctx, a.resourceDir(), gameDir)
	if err := a.ensureFontCompiler(ctx, a.resourceDir()); err != nil {
		return fmt.Errorf("导出 TTMP 需要字体编译组件：%w", err)
	}
	constrained, _, err := fontengine.ConstrainSideLength(configJson)
	if err != nil {
		return err
	}
	return a.fontEngine.CompileTTMP(ctx, constrained, savePath, nil)
}

func (a *App) fontTexture16Bit(resourceDir string) bool {
	cfg := a.loadConfig(resourceDir)
	if cfg.FontTexture16Bit == nil {
		return true
	}
	return *cfg.FontTexture16Bit
}

// GetFontTexture16Bit 获取当前是否开启 16 位贴图压缩
func (a *App) GetFontTexture16Bit() bool {
	return a.fontTexture16Bit(a.resourceDir())
}

// SetFontTexture16Bit 设置是否开启 16 位贴图压缩并持久化
func (a *App) SetFontTexture16Bit(enable bool) error {
	cfg := a.loadConfig(a.resourceDir())
	cfg.FontTexture16Bit = &enable
	return a.SaveConfig(cfg)
}

// ExportFontRaw 编译并导出原始字体文件 (.fdt / .tex)
func (a *App) ExportFontRaw(configJson string, outDir string) error {
	ctx := context.Background()
	if a.ctx != nil {
		ctx = a.ctx
	}
	_, gameDir := a.resolveLanguageAndGameDir("")
	a.syncFontEngineDirs(ctx, a.resourceDir(), gameDir)
	if err := a.ensureFontCompiler(ctx, a.resourceDir()); err != nil {
		return fmt.Errorf("导出原始文件需要字体编译组件：%w", err)
	}
	use16Bit := a.fontTexture16Bit(a.resourceDir())
	constrained, _, err := fontengine.ConstrainSideLength(configJson)
	if err != nil {
		return err
	}
	return a.fontEngine.CompileRaw(ctx, constrained, outDir, use16Bit, nil)
}

func (a *App) openDirectoryDialog(title string, defaultDir string) (string, error) {
	if !a.hasUI() {
		return "", errors.New("桌面运行时未就绪")
	}
	dlg := a.wailsApp.Dialog.OpenFile().
		SetTitle(title).
		CanChooseDirectories(true).
		CanChooseFiles(false)
	if defaultDir != "" {
		dlg.SetDirectory(defaultDir)
	}
	if win := a.activeDialogWindow(); win != nil {
		dlg.AttachToWindow(win)
	}
	return normalizeDialogSelection(dlg.PromptForSingleSelection())
}

func (a *App) openFileDialog(title string, filters []application.FileFilter) (string, error) {
	if !a.hasUI() {
		return "", errors.New("桌面运行时未就绪")
	}
	dlg := a.wailsApp.Dialog.OpenFile().
		SetTitle(title).
		CanChooseFiles(true).
		CanChooseDirectories(false)
	for _, f := range filters {
		dlg.AddFilter(f.DisplayName, f.Pattern)
	}
	if win := a.activeDialogWindow(); win != nil {
		dlg.AttachToWindow(win)
	}
	return normalizeDialogSelection(dlg.PromptForSingleSelection())
}

func (a *App) saveFileDialog(title string, defaultName string, filters []application.FileFilter) (string, error) {
	if !a.hasUI() {
		return "", errors.New("桌面运行时未就绪")
	}
	dlg := a.wailsApp.Dialog.SaveFile()
	dlg.SetOptions(&application.SaveFileDialogOptions{
		Title:                title,
		Filename:             defaultName,
		Filters:              filters,
		Window:               a.activeDialogWindow(),
		CanCreateDirectories: true,
	})
	return normalizeDialogSelection(dlg.PromptForSingleSelection())
}

// SelectRawExportDir 弹出原生目录选择框供用户指定原始文件导出目录
func (a *App) SelectRawExportDir() (string, error) {
	return a.openDirectoryDialog("选择原始字体文件 (fdt/tex) 导出目录", "")
}

// SelectFontFile 弹出原生文件选择框供用户选取本地 TTF/OTF 字体文件
func (a *App) SelectFontFile() (string, error) {
	return a.openFileDialog("选择字体文件", []application.FileFilter{
		{DisplayName: "字体文件 (*.ttf;*.otf)", Pattern: "*.ttf;*.otf"},
		{DisplayName: "所有文件 (*.*)", Pattern: "*.*"},
	})
}

// SelectFontJsonFile 弹出原生文件选择框供用户选取字体方案 JSON 文件
func (a *App) SelectFontJsonFile() (string, error) {
	return a.openFileDialog("导入字体方案文件", []application.FileFilter{
		{DisplayName: "字体方案 (*.json)", Pattern: "*.json"},
		{DisplayName: "所有文件 (*.*)", Pattern: "*.*"},
	})
}

// SelectFontJsonSavePath 弹出原生文件保存框供用户导出 JSON 方案
func (a *App) SelectFontJsonSavePath(defaultName string) (string, error) {
	if defaultName == "" {
		defaultName = "FFXIV_Font_Preset.json"
	}
	return a.saveFileDialog("导出字体方案 JSON 文件", defaultName, []application.FileFilter{
		{DisplayName: "字体方案 (*.json)", Pattern: "*.json"},
	})
}

// SelectTTMPSavePath 弹出原生文件保存框供用户指定 TTMP2 输出路径
func (a *App) SelectTTMPSavePath() (string, error) {
	return a.saveFileDialog("导出 Penumbra TTMP2 模组包", "FFXIV_Custom_Font.ttmp2", []application.FileFilter{
		{DisplayName: "Penumbra TTMP2 模组包 (*.ttmp2)", Pattern: "*.ttmp2"},
	})
}

// OpenElementEditorWindow 打开或复用单例「字体图层属性」原生子窗口。
func (a *App) OpenElementEditorWindow(title string, stateJson string) error {
	if !a.hasUI() {
		return errors.New("桌面运行时未就绪")
	}
	if strings.TrimSpace(title) == "" {
		title = "字体图层属性"
	}

	a.elemStateMu.Lock()
	a.elemState = stateJson
	a.elemStateMu.Unlock()

	a.elemWinMu.Lock()
	win := a.elemWin
	if win == nil {
		win = a.wailsApp.Window.NewWithOptions(application.WebviewWindowOptions{
			Name:             "element-editor",
			Title:            title,
			Width:            860,
			Height:           700,
			MinWidth:         680,
			MinHeight:        520,
			InitialPosition:  application.WindowCentered,
			URL:              "/?window=element-editor",
			BackgroundColour: application.NewRGB(22, 23, 26),
			Windows: application.WindowsWindow{
				Theme:       application.Dark,
				DisableMenu: true,
			},
		})
		win.RegisterHook(events.Common.WindowClosing, func(e *application.WindowEvent) {
			if a.quitting.Load() {
				return
			}
			e.Cancel()
			win.Hide()
			a.emitEvent("elem-editor:native-close")
		})
		a.elemWin = win
		a.elemWinMu.Unlock()
		return nil
	}
	a.elemWinMu.Unlock()

	win.SetTitle(title)
	win.Show()
	win.Focus()
	a.emitEvent("elem-editor:open", stateJson)
	return nil
}

// SyncElementEditorWindow 若图层属性子窗口当前可见，则刷新其标题与图层内容。
func (a *App) SyncElementEditorWindow(title string, stateJson string) error {
	if !a.hasUI() {
		return nil
	}
	a.elemStateMu.Lock()
	a.elemState = stateJson
	a.elemStateMu.Unlock()

	a.elemWinMu.Lock()
	win := a.elemWin
	a.elemWinMu.Unlock()
	if win == nil || !win.IsVisible() {
		return nil
	}
	if strings.TrimSpace(title) != "" {
		win.SetTitle(title)
	}
	a.emitEvent("elem-editor:sync", stateJson)
	return nil
}

// HideElementEditorWindow 隐藏图层属性子窗口并切回主窗口焦点。
func (a *App) HideElementEditorWindow() {
	if !a.hasUI() {
		return
	}
	a.elemWinMu.Lock()
	win := a.elemWin
	a.elemWinMu.Unlock()
	if win != nil && win.IsVisible() {
		win.Hide()
	}
	if a.mainWin != nil {
		a.mainWin.Focus()
	}
}

// GetElementEditorState 返回最近一次打开或同步的图层属性状态快照 JSON。
func (a *App) GetElementEditorState() string {
	a.elemStateMu.RLock()
	defer a.elemStateMu.RUnlock()
	return a.elemState
}

// ReadTextFile 读取指定文本文件内容
func (a *App) ReadTextFile(filePath string) (string, error) {
	bytes, err := os.ReadFile(filePath)
	if err != nil {
		return "", err
	}
	return string(bytes), nil
}

// WriteTextFile 将文本内容原子写入指定文件
func (a *App) WriteTextFile(filePath string, content string) error {
	if strings.TrimSpace(filePath) == "" {
		return errors.New("文件路径不能为空")
	}
	return writeFileAtomic(filepath.Clean(filePath), []byte(content))
}
