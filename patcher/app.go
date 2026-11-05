package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	goruntime "runtime"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

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

	// resourceLock 互斥“写资源目录”（同步）与“读资源目录”（汉化）。
	// 解压是逐文件覆盖，若与汉化并发，CSV 可能被读到一半，产出部分表未汉化的补丁。
	resourceLock sync.Mutex

	// builtinSynced 记录本次运行内置资源是否已同步通过。
	// 未通过时内置词条不允许汉化，避免读到未校验或残缺的 CSV。
	builtinSynced atomic.Bool

	// fontPackSynced 记录本次运行字体包是否已校验通过（自定义词条目录场景单独校验）。
	fontPackSynced atomic.Bool

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
	return &App{
		updater: updater.New(),
	}
}

// OnStartup 保存 Wails 上下文，并清理上一次自更新留下的旧程序文件。
func (a *App) OnStartup(ctx context.Context) {
	a.ctx = ctx
	if exe, err := os.Executable(); err == nil {
		updater.CleanupStaleAppFiles(exe)
	}
}

// OnShutdown 在退出前释放资源。
func (a *App) OnShutdown(ctx context.Context) {}

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
}

// BootstrapInfo 是前端启动所需的全部静态信息。
type BootstrapInfo struct {
	Version         string                      `json:"version"`
	ResourceDir     string                      `json:"resourceDir"`
	ResourceReady   bool                        `json:"resourceReady"`
	Rules           []pipeline.RuleMeta         `json:"rules"`
	Languages       []LanguageOption            `json:"languages"`
	RegionLanguages map[string][]RegionLanguage `json:"regionLanguages"`
	Config          AppConfig                   `json:"config"`
	Folders         map[string]string           `json:"folders"`
	CSV             CSVContext                  `json:"csv"`
	Excluded        []string                    `json:"excluded"`
	ResourceTime    string                      `json:"resourceTime,omitempty"`
	ResourceHash    string                      `json:"resourceHash,omitempty"`
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

var (
	resourceDirOnce sync.Once
	resourceDirPath string
)

func isWritableDir(dir string) bool {
	probe := filepath.Join(dir, ".write_test")
	f, err := os.Create(probe)
	if err != nil {
		return false
	}
	_ = f.Close()
	_ = os.Remove(probe)
	return true
}

func resolveResourceDir() string {
	resourceDirOnce.Do(func() {
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
		if cache, err := os.UserCacheDir(); err == nil && strings.HasPrefix(parent, cache) {
			if configDir, err := os.UserConfigDir(); err == nil {
				return filepath.Join(configDir, "FFXIVChnTextPatch")
			}
		}
	}
	return parent
}

func backupRootFor(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "backup")
}

func configPathFor(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "config.json")
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
	return AppConfig{
		GameDirs:        map[string]string{},
		Language:        string(region.GLOBAL),
		TargetLanguages: LanguagesByRegion{},
		ReplaceText:     true,
		ReplaceFont:     true,
		Rules:           pipeline.DefaultRuleStates(),
		Preset:          "recommended",
		CSVDirs:         CSVDirsByRegion{},
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
		ReplaceText *bool `json:"replaceText"`
		ReplaceFont *bool `json:"replaceFont"`
	}
	if json.Unmarshal(data, &boolFields) == nil {
		if boolFields.ReplaceText != nil {
			cfg.ReplaceText = *boolFields.ReplaceText
		}
		if boolFields.ReplaceFont != nil {
			cfg.ReplaceFont = *boolFields.ReplaceFont
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
	return cfg
}

func (a *App) emitPatchLog(msg string) {
	if a.ctx != nil {
		runtime.EventsEmit(a.ctx, "patch-log", msg)
	}
}

func shortHash(h string) string {
	if len(h) > 8 {
		return h[:8]
	}
	return h
}

// formatResourceHashDetail 生成“CSV: xxxxxxxx、字体: yyyyyyyy”描述，跳过缺失的哈希。
func formatResourceHashDetail(m *updater.Manifest) string {
	if m == nil {
		return "哈希未知"
	}
	parts := make([]string, 0, 2)
	if h := shortHash(m.RawEXD.SHA256); h != "" {
		parts = append(parts, "CSV: "+h)
	}
	if h := shortHash(m.Font.SHA256); h != "" {
		parts = append(parts, "字体: "+h)
	}
	if len(parts) == 0 {
		return "哈希未知"
	}
	return strings.Join(parts, "、")
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

// lockResource 取得资源目录的独占访问权，等待期间可被 ctx 取消。
func (a *App) lockResource(ctx context.Context) (func(), error) {
	if ctx == nil {
		ctx = context.Background()
	}
	if a.resourceLock.TryLock() {
		return a.resourceLock.Unlock, nil
	}
	done := make(chan struct{})
	go func() {
		a.resourceLock.Lock()
		close(done)
	}()
	select {
	case <-done:
		return a.resourceLock.Unlock, nil
	case <-ctx.Done():
		// 放弃等待，但等锁真正到手后必须立即释放，否则这把锁会永远被占住。
		go func() {
			<-done
			a.resourceLock.Unlock()
		}()
		return nil, ctx.Err()
	}
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
	// 失败也算检查过：否则每次开始汉化都要为同一次网络故障等超时。
	a.resourceSyncedAt.Store(time.Now().UnixNano())
	if err != nil {
		return fmt.Errorf("同步内置资源失败：%w", err)
	}
	if status.Updated && status.DownloadedFiles > 0 {
		updates := make([]string, 0, 2)
		if status.CSVUpdated {
			updates = append(updates, "CSV")
		}
		if status.FontUpdated {
			updates = append(updates, "字体")
		}
		if len(updates) == 0 {
			updates = append(updates, "规则配置")
		}
		a.emitPatchLog(fmt.Sprintf("[资源] 已同步最新资源（%s）", strings.Join(updates, "、")))
	} else if status.Updated {
		a.emitPatchLog("[资源] 已修复内置资源并完成哈希校验")
	} else {
		a.emitPatchLog("[资源] 内置资源已是最新，哈希校验通过")
	}
	a.builtinSynced.Store(true)
	return nil
}

// ensureFontPack 校验（必要时下载）字体包。自定义词条目录场景跳过整包同步，
// 但字体仍取自内置资源目录，只凭目录存在就写入等于跳过全部校验。
func (a *App) ensureFontPack(ctx context.Context, resourceDir string) error {
	unlock, err := a.lockResource(ctx)
	if err != nil {
		return err
	}
	defer unlock()
	updated, err := a.updater.EnsureFontPack(ctx, resourceDir, a.emitResourceProgress)
	if err != nil {
		return fmt.Errorf("字体包校验失败：%w", err)
	}
	if updated {
		a.emitPatchLog("[资源] 字体包不完整，已重新下载并校验")
	} else {
		a.emitPatchLog("[资源] 字体包校验通过")
	}
	a.fontPackSynced.Store(true)
	return nil
}

// resourceIncomplete 判断本次请求是否缺少必须的内置资源文件。
// 表清单由 ensureTableDoc 单独补齐，不触发整包同步。
func resourceIncomplete(resourceDir string, custom, replaceFont bool) bool {
	missing := func(name string) bool {
		_, err := os.Stat(filepath.Join(resourceDir, name))
		return err != nil
	}
	if replaceFont && missing("font") {
		return true
	}
	return !custom && missing("rawexd")
}

// resourceSyncStale 判断内置词条是否需要在开始汉化前重新确认云端版本。
func (a *App) resourceSyncStale() bool {
	last := a.resourceSyncedAt.Load()
	return last == 0 || time.Since(time.Unix(0, last)) >= resourceRefreshInterval
}

// ensureTableDoc 只补齐 tables.json：表清单体积远小于快照包，
// 首次运行或缺失时不应为了列目录去同步整个内置资源。
func (a *App) ensureTableDoc(ctx context.Context, resourceDir string) error {
	if _, err := os.Stat(filepath.Join(resourceDir, "tables.json")); err == nil {
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
	return updater.CleanAppVersion(appVersion)
}

// GetBootstrap 返回前端启动数据。只读本地文件，不做任何联网操作。
func (a *App) GetBootstrap() (BootstrapInfo, error) {
	resourceDir := a.resourceDir()

	cfg := a.loadConfig(resourceDir)

	tables := a.loadTablesDoc(resourceDir)
	csvCtx := a.csvContext(cfg, tables, normalizeRegionCode(cfg.Language))

	ruleList, ruleErr := loadRules(resourceDir)
	if ruleErr != nil {
		a.emitPatchLog(fmt.Sprintf("[警告] %v", ruleErr))
	}
	ruleMetas := make([]pipeline.RuleMeta, 0, 4)
	for _, rule := range ruleList {
		ruleMetas = append(ruleMetas, rule.Meta())
	}

	var resTime, resHash string
	if mf, err := updater.ReadLocalManifest(resourceDir); err == nil && mf != nil {
		resTime = formatResourceTime(mf.GeneratedAt)
		resHash = shortHash(mf.RawEXD.SHA256)
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		a.emitPatchLog(fmt.Sprintf("[警告] 读取本地 manifest.json 失败: %v", err))
	}

	return BootstrapInfo{
		Version:       updater.CleanAppVersion(appVersion),
		ResourceDir:   resourceDir,
		ResourceReady: updater.HasLocalResource(resourceDir),
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
		ResourceTime: resTime,
		ResourceHash: resHash,
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

	var suffixes []string
	switch targetReg {
	case region.China:
		suffixes = []string{
			filepath.Join("Program Files (x86)", "盛趣游戏", "最终幻想XIV"),
			filepath.Join("Program Files", "盛趣游戏", "最终幻想XIV"),
			filepath.Join("Program Files (x86)", "盛大游戏", "最终幻想XIV"),
			filepath.Join("WeGameApps", "rail_apps", "最终幻想14(2000340)"),
			filepath.Join("WeGame", "rail_apps", "最终幻想14(2000340)"),
		}
	case region.Global:
		suffixes = []string{
			filepath.Join("Program Files (x86)", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
			filepath.Join("Program Files", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
			filepath.Join("Steam", "steamapps", "common", "FINAL FANTASY XIV Online"),
			filepath.Join("Program Files (x86)", "Steam", "steamapps", "common", "FINAL FANTASY XIV Online"),
		}
	case region.Korea, region.Taiwan:
		suffixes = nil
	default:
		suffixes = []string{
			filepath.Join("Program Files (x86)", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
			filepath.Join("Program Files", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn"),
			filepath.Join("Steam", "steamapps", "common", "FINAL FANTASY XIV Online"),
			filepath.Join("Program Files (x86)", "Steam", "steamapps", "common", "FINAL FANTASY XIV Online"),
			filepath.Join("Program Files (x86)", "盛趣游戏", "最终幻想XIV"),
			filepath.Join("Program Files", "盛趣游戏", "最终幻想XIV"),
			filepath.Join("Program Files (x86)", "盛大游戏", "最终幻想XIV"),
			filepath.Join("WeGameApps", "rail_apps", "最终幻想14(2000340)"),
			filepath.Join("WeGame", "rail_apps", "最终幻想14(2000340)"),
		}
	}

	for _, root := range getScanDriveRoots() {
		for _, suffix := range suffixes {
			add(filepath.Join(root, suffix))
		}
	}

	// 常见位置未命中时，用磁盘 3 层扫描的结果兜底。
	if len(candidates) == 0 && (targetReg == "" || targetReg == region.Global) {
		for _, p := range a.scanGlobalDirs(ctx) {
			add(p)
		}
	}

	if targetReg == "" {
		return candidates
	}

	matched := make([]string, 0, len(candidates))
	for _, p := range candidates {
		if actualReg, err := verifier.DetectClientRegion(p); err == nil {
			if actualReg == targetReg {
				matched = append(matched, p)
			}
		} else {
			matched = append(matched, p)
		}
	}
	return matched
}

// scanGlobalDirs 在各磁盘根目录 3 层以内查找国际服客户端目录。
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
	dirs := gamescan.Find(ctx, roots, gamecfg.SubFolder, gamescan.DefaultMaxDepth, isGlobalClientDir)

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
	cfg := a.loadConfig(a.resourceDir())
	defaultDir := cfg.GameDirs[normalizeRegionCode(language)]
	return runtime.OpenDirectoryDialog(a.ctx, runtime.OpenDialogOptions{
		Title:                "选择 FFXIV 客户端目录（包含 boot 与 game 文件夹）",
		DefaultDirectory:     defaultDir,
		CanCreateDirectories: false,
	})
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

	metaPath := filepath.Join(backupRootFor(a.resourceDir()), string(region.Of(lang)), "meta.json")
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
	if mf, err := updater.ReadLocalManifest(a.resourceDir()); err == nil && mf != nil {
		info.ResourceTime = formatResourceTime(mf.GeneratedAt)
		info.ResourceHash = shortHash(mf.RawEXD.SHA256)
	}
	if cfgPath, err := gamecfg.FindConfigFile(); err == nil {
		if val, exists, err := gamecfg.ReadUiAssetType(cfgPath); err == nil && exists {
			info.UiAssetType = val
		}
	}
	return info, nil
}

// ClientStatusResult 表示客户端环境状态。
type ClientStatusResult struct {
	Status      string `json:"status"` // "original" | "modified" | "unknown" | "mismatch"
	Label       string `json:"label"`  // "官方原版" | "已修改" | "未知版本" | "区服不匹配"
	Description string `json:"description"`
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
			Description: fmt.Sprintf("当前目录检测为【%s】客户端，与所选目标区服【%s】不一致，请切换区服或重新选择目录", region.DisplayName(actualReg), region.DisplayName(reg)),
		}, nil
	}

	versionData, err := os.ReadFile(filepath.Join(gameDir, "game", "ffxivgame.ver"))
	if err != nil {
		return ClientStatusResult{Status: "unknown", Label: "版本未知", Description: fmt.Sprintf("无法读取 ffxivgame.ver：%v", err)}, nil
	}
	version := strings.TrimSpace(string(versionData))

	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	fontIndex := "000000.win32.index"
	textIndex := "0a0000.win32.index"

	fontHash, fontErr := verifier.HashFile(filepath.Join(sqpackDir, fontIndex))
	textHash, textErr := verifier.HashFile(filepath.Join(sqpackDir, textIndex))
	if fontErr != nil && textErr != nil {
		return ClientStatusResult{Status: "unknown", Label: "文件缺失", Description: fmt.Sprintf("无法读取客户端关键索引数据包（文本索引: %v；字体索引: %v）", textErr, fontErr)}, nil
	}

	// 2. 检查本地是否有当前版本的原版备份
	backupDir := filepath.Join(backupRootFor(a.resourceDir()), string(reg))
	metaPath := filepath.Join(backupDir, "meta.json")
	var meta verifier.BackupMeta
	hasBackupMeta := false
	if data, err := os.ReadFile(metaPath); err == nil {
		if json.Unmarshal(data, &meta) == nil && meta.Version == version && len(meta.Files) > 0 {
			hasBackupMeta = true
		}
	}

	if hasBackupMeta {
		matchedBackup := true
		if backupFont, ok := meta.Files[fontIndex]; ok && fontErr == nil {
			if !strings.EqualFold(fontHash.SHA256, backupFont.SHA256) {
				matchedBackup = false
			}
		}
		if backupText, ok := meta.Files[textIndex]; ok && textErr == nil {
			if !strings.EqualFold(textHash.SHA256, backupText.SHA256) {
				matchedBackup = false
			}
		}
		if matchedBackup {
			return ClientStatusResult{
				Status:      "original",
				Label:       "官方原版",
				Description: version,
			}, nil
		}
		return ClientStatusResult{
			Status:      "modified",
			Label:       "已修改",
			Description: "文件与本地原版备份不一致",
		}, nil
	}

	// 3. 本地无当前版本备份时，国际服查询云端 Release 进行比对
	if reg == region.Global {
		cloud := a.getCloud()
		repo := "Souma-Sumire/FFXIVChnTextPatch-Souma"
		parentCtx := a.ctx
		if parentCtx == nil {
			parentCtx = context.Background()
		}
		ctx, cancel := context.WithTimeout(parentCtx, 4*time.Second)
		defer cancel()

		r, err := cloud.ReleaseByTag(ctx, repo, version)
		if err == nil && r != nil {
			matchedCloud := true
			checkedCloudCount := 0
			if wantFont, ok := r.AssetSHA256(fontIndex); ok && fontErr == nil {
				checkedCloudCount++
				if !strings.EqualFold(fontHash.SHA256, wantFont) {
					matchedCloud = false
				}
			}
			if wantText, ok := r.AssetSHA256(textIndex); ok && textErr == nil {
				checkedCloudCount++
				if !strings.EqualFold(textHash.SHA256, wantText) {
					matchedCloud = false
				}
			}
			if checkedCloudCount > 0 {
				if matchedCloud {
					return ClientStatusResult{
						Status:      "original",
						Label:       "官方原版",
						Description: version,
					}, nil
				}
				return ClientStatusResult{
					Status:      "modified",
					Label:       "已修改",
					Description: "文件与云端官方原版不一致",
				}, nil
			}
		}
	}

	// 4. 【云端没更新 + 游戏更新了 = 显示未知】
	return ClientStatusResult{
		Status:      "unknown",
		Label:       "未知版本",
		Description: "云端尚未收录该版本的原版备份",
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

// AnswerFontConfirm 保留作为向前兼容别名。
func (a *App) AnswerFontConfirm(ok bool) {
	a.AnswerUnknownConfirm(ok)
}

func (a *App) waitUnknownConfirm(ctx context.Context, version string) (bool, error) {
	if a == nil || a.ctx == nil {
		return false, nil
	}
	ch := make(chan bool, 1)
	a.confirmMu.Lock()
	a.confirmCh = ch
	a.confirmMu.Unlock()

	runtime.EventsEmit(a.ctx, "unknown-confirm", fmt.Sprintf("当前游戏版本（%s）在云端尚未收录原版备份，无法保证当前客户端文件是官方原版，继续汉化可能导致异常或封号风险。是否仍要继续汉化？", version))
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
	if a == nil || a.ctx == nil {
		return false, nil
	}
	ch := make(chan bool, 1)
	a.launcherConfirmMu.Lock()
	a.launcherConfirmCh = ch
	a.launcherConfirmMu.Unlock()

	namesStr := strings.Join(launcherNames, "、")
	prompt := fmt.Sprintf("检测到游戏启动器正在运行（%s），为防止文件冲突或更新损坏，需要先关闭启动器。\n\n是否强制结束启动器进程并继续？", namesStr)
	runtime.EventsEmit(a.ctx, "launcher-confirm", prompt)

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
	case <-a.ctx.Done():
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

// KillLauncherProcesses 扫描并强制结束当前运行中的冲突启动器进程。
func (a *App) KillLauncherProcesses(gameDir string) ([]string, error) {
	return gameproc.KillLauncherProcesses(gameDir)
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
	if a.ctx == nil || a.shouldThrottleProgress(stage, detail, done, total) {
		return
	}
	runtime.EventsEmit(a.ctx, "resource-progress", ProgressEvent{Stage: stage, Detail: detail, Done: done, Total: total})
}

func (a *App) emitProgress(stage, detail string, done, total int64) {
	if a.ctx == nil || a.shouldThrottleProgress(stage, detail, done, total) {
		return
	}
	runtime.EventsEmit(a.ctx, "patch-progress", ProgressEvent{Stage: stage, Detail: detail, Done: done, Total: total})
}

// patchWroteData 判断本次汉化是否已向 dat 追加过数据块，即客户端文件是否已被改动。
func patchWroteData(resp PatchResponse) bool {
	return resp.Text.AppendedBytes > 0 || resp.Font.AppendedBytes > 0
}

// autoRestore 在汉化中断或失败后把官方原文件写回客户端，并把进度回报到界面。
// autoRestore 在汉化中断或失败后把官方原文件写回客户端，并把进度回报到界面。
func (a *App) autoRestore(ctx context.Context, gameDir, backupRoot string, lang region.Language) ([]string, error) {
	restored, err := verifier.RestoreToGameWithContext(ctx, gameDir, backupRoot, lang, a.emitProgress,
		func(format string, args ...any) {
			a.emitPatchLog(fmt.Sprintf(format, args...))
		})
	if err != nil {
		return restored, err
	}
	a.emitPatchLog("[提示] 已自动恢复至官方原版状态")
	return restored, nil
}

// StartPatch 执行完整的校验与汉化流程。
func (a *App) StartPatch(req PatchRequest) (PatchResponse, error) {
	var resp PatchResponse
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

	if err := a.checkGameNotRunning(patchCtx, req.GameDir); err != nil {
		if errors.Is(err, gameproc.ErrCancelled) {
			a.emitPatchLog("[提示] 汉化已取消：游戏启动器正在运行")
			resp.Cancelled = true
			return resp, nil
		}
		return resp, err
	}

	targetReg := region.Of(lang)
	if actualReg, err := verifier.DetectClientRegion(req.GameDir); err == nil && actualReg != "" && actualReg != targetReg {
		return resp, fmt.Errorf("客户端区服不匹配：当前目录检测为【%s】客户端，与目标区服【%s】不一致；已中止汉化以防止损坏客户端数据，请切换区服或重新选择目录",
			region.DisplayName(actualReg), region.DisplayName(targetReg))
	}

	if req.ReplaceFont {
		cfgRes, cfgErr := gamecfg.EnsureHighResolutionUiAsset("")
		if cfgErr != nil {
			if errors.Is(cfgErr, gamecfg.ErrConfigNotFound) {
				a.emitPatchLog("[提示] 未在“我的文档”中找到游戏配置文件，请在游戏系统设置中确认开启高分辨率界面")
			} else {
				a.emitPatchLog(fmt.Sprintf("[警告] 检查/修改游戏配置文件失败: %v", cfgErr))
			}
		} else if cfgRes.Modified {
			a.emitPatchLog("[配置] 已自动备份游戏配置文件并开启高分辨率模式")
		} else {
			a.emitPatchLog("[配置] 游戏界面已是高分辨率模式")
		}
	}

	resourceDir := a.resourceDir()
	regionKey := normalizeRegionCode(string(lang))

	// 表清单缺失时先补一份；补不到就无法列出可汉化的表，直接中止。
	if err := a.ensureTableDoc(patchCtx, resourceDir); err != nil {
		if errors.Is(err, context.Canceled) {
			a.emitPatchLog("[提示] 汉化已取消：同步表清单时被中止")
			resp.Cancelled = true
			return resp, nil
		}
		return resp, fmt.Errorf("%w；已中止汉化，请联网重试", err)
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
	needSync := resourceIncomplete(resourceDir, customSelected, req.ReplaceFont)
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
			return resp, fmt.Errorf("内置词条未同步通过，已中止汉化（可联网重试或改用自定义词条目录）：%w", err)
		}
	}
	// 自定义词条目录不触发整包同步，但勾选了替换字体时字体包必须经过校验，
	// 否则内置资源目录里残留的旧字体会被直接写入客户端。
	if customSelected && req.ReplaceFont && !a.builtinSynced.Load() && !a.fontPackSynced.Load() {
		if err := a.ensureFontPack(patchCtx, resourceDir); err != nil {
			if errors.Is(err, context.Canceled) {
				a.emitPatchLog("[提示] 汉化已取消：校验字体包时被中止")
				resp.Cancelled = true
				return resp, nil
			}
			return resp, err
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

	manifest, manifestErr := updater.ReadLocalManifest(resourceDir)
	if manifest != nil {
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
			runtime.EventsEmit(a.ctx, "patch-log", fmt.Sprintf(format, args...))
		},
	}
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
		stats, err := replace.ReplaceEXDF(replace.Options{
			Context:         ctx,
			TextIndexPath:   filepath.Join(sqpackDir, "0a0000.win32.index"),
			CSVResourceDir:  csvDir,
			QuietMissingCSV: customCSV,
			SourceLanguage:  string(lang),
			TargetLanguages: targetLangs,
			IncludeFiles:    include,
			Rules:           registry,
			Progress:        formatter,
			Log:             cfg.Log,
		})
		if err == nil {
			a.emitPatchLog(fmt.Sprintf("[文本] 文本汉化完成：修改 %d 个数据表，更新 %d 个文件，替换 %d 条文本", stats.Tables, stats.Files, stats.Entries))
		}
		resp.Text = stats
		return err
	}
	cfg.ApplyFont = func(ctx context.Context) error {
		a.emitPatchLog("[字体] 开始写入高清中文字体...")
		stats, err := replace.ReplaceFontWithContext(
			ctx,
			filepath.Join(sqpackDir, "000000.win32.index"),
			filepath.Join(resourceDir, "font"),
			func(done, total int, file string) {
				a.emitProgress("font", file, int64(done), int64(total))
			},
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
		// 只有真正写入过数据块才需要还原：资源缺失、备份校验失败等早期错误没有改动
		// 客户端文件，此时还原只会白拷数 GB，还会把上一次已成功的汉化回滚成原版。
		if !patchWroteData(resp) {
			if cancelled {
				a.emitPatchLog("[提示] 汉化已取消，客户端保持原样")
				resp.Cancelled = true
				return resp, nil
			}
			a.emitPatchLog(fmt.Sprintf("[错误] 汉化失败：%v（客户端保持原样，无需还原）", err))
			return resp, err
		}
		if cancelled {
			a.emitPatchLog("[提示] 汉化已被用户中止，正在自动恢复官方原版...")
			// 已取消的 ctx 无法用于还原，恢复流程必须完整跑完。
			if _, restoreErr := a.autoRestore(context.Background(), req.GameDir, backupRoot, lang); restoreErr != nil {
				a.emitPatchLog(fmt.Sprintf("[警告] 恢复官方原版失败: %v，请手动点击还原", restoreErr))
			}
			resp.Cancelled = true
			return resp, nil
		}
		a.emitPatchLog(fmt.Sprintf("[错误] 汉化失败：%v，正在自动恢复官方原版以保护游戏文件...", err))
		if _, restoreErr := a.autoRestore(patchCtx, req.GameDir, backupRoot, lang); restoreErr != nil {
			a.emitPatchLog(fmt.Sprintf("[警告] 自动恢复官方原版失败: %v，请手动点击还原", restoreErr))
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
func (a *App) RestoreOriginal(gameDir, language string) (RestoreResponse, error) {
	var resp RestoreResponse
	if gameDir == "" {
		return resp, errors.New("请先选择游戏目录")
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
	if actualReg, err := verifier.DetectClientRegion(gameDir); err == nil && actualReg != "" && actualReg != targetReg {
		return resp, fmt.Errorf("客户端区服不匹配：当前目录检测为【%s】客户端，但尝试以【%s】进行还原；已中止操作以防止覆盖错误的原文件",
			region.DisplayName(actualReg), region.DisplayName(targetReg))
	}

	a.emitPatchLog("[还原] 开始执行官方原版恢复流程...")
	resourceDir := a.resourceDir()
	restored, err := verifier.Restore(patchCtx, verifier.RestoreConfig{
		GameDir:    gameDir,
		BackupRoot: backupRootFor(resourceDir),
		Language:   lang,
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
		runtime.EventsEmit(a.ctx, "sync-progress", ProgressEvent{
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
	runtime.BrowserOpenURL(a.ctx, url)
	return nil
}

// EnsureHighResolutionUiAsset 检查并确保 FFXIV.cfg 开启高分辨率贴图模式 (UiAssetType 1)。
func (a *App) EnsureHighResolutionUiAsset() (gamecfg.ModifyResult, error) {
	return gamecfg.EnsureHighResolutionUiAsset("")
}

// CheckAppUpdate 查询云端获取汉化器主程序的最新版本并比对当前版本。
// 汉化、还原进行中直接拒绝：此时替换程序或拉取元数据都会干扰正在写入客户端的任务。
func (a *App) CheckAppUpdate() (updater.AppUpdateInfo, error) {
	current := updater.CleanAppVersion(appVersion)
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
		if a.ctx != nil {
			runtime.Quit(a.ctx)
			return
		}
		os.Exit(0)
	}()
	return nil
}

// emitAppUpdateProgress 汇报自更新进度。下载回调触发很密，只按时间间隔转发。
func (a *App) emitAppUpdateProgress(stage, detail string, done, total int64) {
	if a.ctx == nil {
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
	runtime.EventsEmit(a.ctx, "app-update-progress", ProgressEvent{Stage: stage, Detail: detail, Done: done, Total: total})
}
