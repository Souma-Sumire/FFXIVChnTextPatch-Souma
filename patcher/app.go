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
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/gamecfg"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/gameproc"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/replace"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/updater"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

var appVersion = "dev"

const tcBackupURL = "https://github.com/NaofumiRaphtalia/FFXIVChnTextPatch-Souma/releases"

// App 是 Wails 绑定到前端的应用对象。
type App struct {
	ctx context.Context

	mu   sync.Mutex
	busy bool

	confirmMu sync.Mutex
	confirmCh chan bool

	launcherConfirmMu sync.Mutex
	launcherConfirmCh chan bool

	updater          *updater.Updater
	resourceSyncOnce sync.Once
	resourceReady    chan struct{}
	resourceErr      error
	resourceMu       sync.RWMutex
	offline          bool

	progressMu   sync.Mutex
	lastProgress time.Time
	lastDetail   string
	lastStage    string

	resourceDirOverride string
	cloudOverride       verifier.Cloud
	skipProcessCheck    bool
}

func (a *App) getCloud() verifier.Cloud {
	if a != nil && a.cloudOverride != nil {
		return a.cloudOverride
	}
	return verifier.NewGitHubClient()
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
		updater:       updater.New(),
		resourceReady: make(chan struct{}),
	}
}

// OnStartup 保存 Wails 上下文。
func (a *App) OnStartup(ctx context.Context) { a.ctx = ctx }

// OnShutdown 在退出前释放资源。
func (a *App) OnShutdown(ctx context.Context) {}

// LanguageOption 是前端可选的语言。
type LanguageOption struct {
	Code      string `json:"code"`
	Name      string `json:"name"`
	Region    string `json:"region"`
	BackupURL string `json:"backupUrl"`
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

// AppConfig 是持久化配置。
type AppConfig struct {
	GameDir     string            `json:"gameDir"`
	GameDirs    map[string]string `json:"gameDirs,omitempty"`
	Language    string            `json:"language"`
	ReplaceText bool              `json:"replaceText"`
	ReplaceFont bool              `json:"replaceFont"`
	Rules       map[string]bool   `json:"rules"`
	Preset      string            `json:"preset"`
}

// BootstrapInfo 是前端启动所需的全部静态信息。
type BootstrapInfo struct {
	Version     string              `json:"version"`
	ResourceDir string              `json:"resourceDir"`
	Offline     bool                `json:"offline"`
	Rules       []pipeline.RuleMeta `json:"rules"`
	Languages   []LanguageOption    `json:"languages"`
	Config      AppConfig           `json:"config"`
	Folders      map[string]string   `json:"folders"`
	TableList    []TableEntry        `json:"tableList"`
	Excluded     []string            `json:"excluded"`
	ResourceTime string              `json:"resourceTime,omitempty"`
	ResourceHash string              `json:"resourceHash,omitempty"`
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
	GameDir        string          `json:"gameDir"`
	Language       string          `json:"language"`
	ReplaceText    bool            `json:"replaceText"`
	ReplaceFont    bool            `json:"replaceFont"`
	Rules          map[string]bool `json:"rules"`
	SelectedTables []string        `json:"selectedTables"`
	Preset         string          `json:"preset"`
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
	Offline  bool `json:"offline"`
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
	rules := map[string]bool{}
	for _, rule := range pipeline.BuiltinRules() {
		rules[rule.ID()] = rule.Meta().DefaultOn
	}
	return AppConfig{
		GameDirs:    map[string]string{},
		Language:    string(region.GLOBAL),
		ReplaceText: true,
		ReplaceFont: true,
		Rules:       rules,
		Preset:      "recommended",
	}
}

func loadRules(resourceDir string) []pipeline.Rule {
	rules, err := pipeline.LoadRulesFromFile(filepath.Join(resourceDir, "rules.json"))
	if err != nil || len(rules) == 0 {
		return pipeline.BuiltinRules()
	}
	return rules
}

func (a *App) loadConfig(resourceDir string) AppConfig {
	cfg := defaultConfig()
	data, err := os.ReadFile(configPathFor(resourceDir))
	if err != nil {
		return cfg
	}
	var saved AppConfig
	if err := json.Unmarshal(data, &saved); err != nil {
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
	} else {
		cfg.GameDirs = make(map[string]string)
	}
	if saved.GameDir != "" {
		oldKey := normalizeRegionCode(saved.Language)
		if cfg.GameDirs[oldKey] == "" {
			cfg.GameDirs[oldKey] = saved.GameDir
		}
	}
	curKey := normalizeRegionCode(cfg.Language)
	cfg.GameDir = cfg.GameDirs[curKey]

	cfg.ReplaceText = saved.ReplaceText
	cfg.ReplaceFont = saved.ReplaceFont
	cfg.Preset = saved.Preset
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

func (a *App) syncLatestResourcesInBackground(resourceDir string) {
	defer func() {
		select {
		case <-a.resourceReady:
		default:
			close(a.resourceReady)
		}
	}()

	status, err := a.updater.Provision(context.Background(), resourceDir, nil)
	if err != nil {
		a.resourceMu.Lock()
		a.resourceErr = err
		a.resourceMu.Unlock()
		a.emitPatchLog(fmt.Sprintf("[提示] 检查云端资源更新失败: %v，将继续使用本地已有资源。", err))
		return
	}

	a.resourceMu.Lock()
	a.offline = status.Offline
	a.resourceMu.Unlock()

	if status.Updated && status.DownloadedFiles > 0 {
		var updates []string
		if status.CSVUpdated {
			updates = append(updates, fmt.Sprintf("CSV: %s", shortHash(status.CSVHash)))
		}
		if status.FontUpdated {
			updates = append(updates, fmt.Sprintf("字体: %s", shortHash(status.FontHash)))
		}
		if len(updates) == 0 {
			updates = append(updates, "规则配置")
		}
		a.emitPatchLog(fmt.Sprintf("[更新] 汉化资源已更新（%s），已同步 %d 个文件（%.2f MB）。",
			strings.Join(updates, ", "), status.DownloadedFiles, float64(status.DownloadedBytes)/1024/1024))
	} else if !status.Offline && status.CSVHash != "" {
		resTime := formatResourceTime(status.GeneratedAt)
		if resTime != "" {
			a.emitPatchLog(fmt.Sprintf("[资源] 汉化资源已是最新（构建时间：%s，CSV: %s, 字体: %s）。",
				resTime, shortHash(status.CSVHash), shortHash(status.FontHash)))
		} else {
			a.emitPatchLog(fmt.Sprintf("[资源] 汉化资源已是最新（CSV: %s, 字体: %s）。",
				shortHash(status.CSVHash), shortHash(status.FontHash)))
		}
	}
}

func (a *App) waitResourceReady(ctx context.Context) error {
	a.resourceMu.RLock()
	readyCh := a.resourceReady
	a.resourceMu.RUnlock()

	if readyCh == nil {
		return nil
	}

	handleResourceErr := func(err error) error {
		if err == nil {
			return nil
		}
		if updater.HasLocalResource(a.resourceDir()) {
			a.emitPatchLog(fmt.Sprintf("[提示] 检查云端最新资源失败 (%v)，已使用本地已有完整资源继续汉化。", err))
			return nil
		}
		return err
	}

	select {
	case <-readyCh:
		a.resourceMu.RLock()
		err := a.resourceErr
		a.resourceMu.RUnlock()
		return handleResourceErr(err)
	default:
		a.emitProgress("sync", "正在确认汉化资源为最新版本...", 0, 0)
		a.emitPatchLog("[资源] 正在确认汉化资源为云端最新版本，请稍候...")
		select {
		case <-readyCh:
			a.resourceMu.RLock()
			err := a.resourceErr
			a.resourceMu.RUnlock()
			return handleResourceErr(err)
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(30 * time.Second):
			if updater.HasLocalResource(a.resourceDir()) {
				a.emitPatchLog("[提示] 等待最新资源检查超时，已回退使用本地已有资源继续汉化。")
				return nil
			}
			return errors.New("等待最新资源超时，请检查网络连接")
		}
	}
}

// GetAppVersion 返回补丁器自身版本号。
func (a *App) GetAppVersion() string {
	return appVersion
}

// GetBootstrap 返回前端启动数据。
func (a *App) GetBootstrap() (BootstrapInfo, error) {
	resourceDir := a.resourceDir()
	var isOffline bool

	if updater.HasLocalResource(resourceDir) {
		// 本地已有完整基础资源：立即秒开进入主工作台，后台并发比对与同步最新资源
		a.resourceSyncOnce.Do(func() {
			go a.syncLatestResourcesInBackground(resourceDir)
		})
	} else {
		// 本地无基础资源（首次运行或文件缺失）：同步执行准备并展示全屏进度条
		status, err := a.updater.Provision(a.ctx, resourceDir, a.emitResourceProgress)
		if err != nil {
			a.resourceMu.Lock()
			a.resourceErr = err
			a.resourceMu.Unlock()
			close(a.resourceReady)
			isOffline = true
			a.emitPatchLog(fmt.Sprintf("[提示] 获取云端资源失败: %v，已进入离线模式。", err))
		} else {
			a.resourceMu.Lock()
			a.offline = status.Offline
			a.resourceMu.Unlock()
			close(a.resourceReady)
			isOffline = status.Offline
		}
	}

	cfg := a.loadConfig(resourceDir)

	var tables TablesDoc
	if data, err := os.ReadFile(filepath.Join(resourceDir, "tables.json")); err == nil {
		if err := json.Unmarshal(data, &tables); err != nil {
			a.emitPatchLog(fmt.Sprintf("[警告] 解析本地 tables.json 失败: %v", err))
		}
	}
	if tables.Folders == nil {
		tables.Folders = map[string]string{}
	}

	tableList := buildTableList(tables)
	ruleMetas := make([]pipeline.RuleMeta, 0, 3)
	for _, rule := range loadRules(resourceDir) {
		ruleMetas = append(ruleMetas, rule.Meta())
	}

	a.resourceMu.RLock()
	if !isOffline {
		isOffline = a.offline
	}
	a.resourceMu.RUnlock()

	var resTime, resHash string
	if mf, err := updater.ReadLocalManifest(resourceDir); err == nil && mf != nil {
		resTime = formatResourceTime(mf.GeneratedAt)
		resHash = shortHash(mf.RawEXD.SHA256)
	}

	return BootstrapInfo{
		Version:     appVersion,
		ResourceDir: resourceDir,
		Offline:     isOffline,
		Rules:       ruleMetas,
		Languages: []LanguageOption{
			{Code: string(region.GLOBAL), Name: "国际服", Region: string(region.Global)},
			{Code: string(region.CHS), Name: "国服", Region: string(region.China)},
			{Code: string(region.TC), Name: "繁中服", Region: string(region.Taiwan), BackupURL: tcBackupURL},
			{Code: string(region.KO), Name: "韩服", Region: string(region.Korea)},
		},
		Config:       cfg,
		Folders:      tables.Folders,
		TableList:    tableList,
		Excluded:     recommendedExcludedTables,
		ResourceTime: resTime,
		ResourceHash: resHash,
	}, nil
}

func buildTableList(tables TablesDoc) []TableEntry {
	keys := append([]string(nil), tables.Files...)
	if len(keys) == 0 {
		for key := range tables.Tables {
			keys = append(keys, key)
		}
	}
	sort.Strings(keys)
	out := make([]TableEntry, 0, len(keys))
	for _, key := range keys {
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

// SaveConfig 保存前端配置到 config.json。
func (a *App) SaveConfig(cfg AppConfig) error {
	resourceDir := a.resourceDir()
	if cfg.GameDirs == nil {
		cfg.GameDirs = make(map[string]string)
	}
	norm := normalizeRegionCode(cfg.Language)
	if cfg.GameDir != "" {
		cfg.GameDirs[norm] = cfg.GameDir
	}
	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(configPathFor(resourceDir), append(data, '\n'), 0o644)
}

// DetectGamePaths 扫描指定区服的常见安装位置。
func (a *App) DetectGamePaths(language string) []string {
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

	for _, drive := range []string{"C:", "D:", "E:", "F:", "G:"} {
		for _, suffix := range suffixes {
			add(drive + string(os.PathSeparator) + suffix)
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

// SelectGameDirectory 打开目录选择对话框。
func (a *App) SelectGameDirectory(language string) (string, error) {
	cfg := a.loadConfig(a.resourceDir())
	defaultDir := cfg.GameDir
	if language != "" {
		norm := normalizeRegionCode(language)
		if d, ok := cfg.GameDirs[norm]; ok && d != "" {
			defaultDir = d
		}
	}
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
	if mf, err := updater.ReadLocalManifest(a.resourceDir()); err == nil && mf != nil {
		info.ResourceTime = formatResourceTime(mf.GeneratedAt)
		info.ResourceHash = shortHash(mf.RawEXD.SHA256)
	}
	info.UiAssetType = -1
	if cfgPath, err := gamecfg.FindConfigFile(); err == nil {
		if val, exists, err := gamecfg.ReadUiAssetType(cfgPath); err == nil && exists {
			info.UiAssetType = val
		}
	}
	return info, nil
}

// ClientStatusResult 表示客户端哈希校验结果。
type ClientStatusResult struct {
	Status      string `json:"status"`      // "original" | "patched" | "modified" | "unknown"
	Label       string `json:"label"`       // "官方原版" | "已汉化" | "非原版" | "未收录版本"
	Description string `json:"description"`
}

// PatchedRecord 记录本工具上次成功汉化后的版本与文件哈希。
type PatchedRecord struct {
	GameDir     string                       `json:"gameDir"`
	Version     string                       `json:"version"`
	Language    string                       `json:"language"`
	PatchedAt   time.Time                    `json:"patchedAt"`
	ReplaceText bool                         `json:"replaceText"`
	ReplaceFont bool                         `json:"replaceFont"`
	Files       map[string]verifier.FileHash `json:"files"`
}

func patchedRecordPath(resourceDir string) string {
	return filepath.Join(resourceDir, "patched_records.json")
}

func (a *App) savePatchedRecord(rec PatchedRecord) error {
	p := patchedRecordPath(a.resourceDir())
	records := a.loadAllPatchedRecords()
	cleanDir := filepath.Clean(rec.GameDir)
	records[cleanDir] = rec
	data, err := json.MarshalIndent(records, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(p, data, 0o644)
}

func (a *App) loadAllPatchedRecords() map[string]PatchedRecord {
	res := make(map[string]PatchedRecord)
	p := patchedRecordPath(a.resourceDir())
	data, err := os.ReadFile(p)
	if err == nil {
		if err := json.Unmarshal(data, &res); err == nil {
			return res
		}
	}
	// 兼容迁移旧单记录格式并自动清理历史遗留文件
	oldPath := filepath.Join(a.resourceDir(), "patched_record.json")
	if oldData, err := os.ReadFile(oldPath); err == nil {
		var single PatchedRecord
		if err := json.Unmarshal(oldData, &single); err == nil && single.GameDir != "" {
			res[filepath.Clean(single.GameDir)] = single
		}
		_ = os.Remove(oldPath)
	}
	return res
}

func (a *App) loadPatchedRecord(gameDir string) (PatchedRecord, bool) {
	records := a.loadAllPatchedRecords()
	cleanDir := filepath.Clean(gameDir)
	for k, v := range records {
		if strings.EqualFold(k, cleanDir) {
			return v, true
		}
	}
	return PatchedRecord{}, false
}

func (a *App) clearPatchedRecord(gameDir string) {
	records := a.loadAllPatchedRecords()
	cleanDir := filepath.Clean(gameDir)
	deleteKey := ""
	for k := range records {
		if strings.EqualFold(k, cleanDir) {
			deleteKey = k
			break
		}
	}
	if deleteKey != "" {
		delete(records, deleteKey)
		data, err := json.MarshalIndent(records, "", "  ")
		if err == nil {
			_ = os.WriteFile(patchedRecordPath(a.resourceDir()), data, 0o644)
		}
	}
	_ = os.Remove(filepath.Join(a.resourceDir(), "patched_record.json"))
}

func checkGameNotRunning(gameDir string) error {
	return gameproc.CheckGameNotRunning(gameDir)
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

	// 0. 检查客户端真实区服与当前选择区服是否匹配
	if actualReg, err := verifier.DetectClientRegion(gameDir); err == nil && actualReg != "" && actualReg != reg {
		return ClientStatusResult{
			Status:      "mismatch",
			Label:       "区服不匹配",
			Description: fmt.Sprintf("当前目录检测为【%s】客户端，与所选目标区服【%s】不一致，请切换区服或重新选择目录", region.DisplayName(actualReg), region.DisplayName(reg)),
		}, nil
	}

	// 1. 优先检查本汉化器的已汉化记录（全区服通用，核对游戏目录、版本号及文件哈希）
	if rec, ok := a.loadPatchedRecord(gameDir); ok {
		cleanGameDir := filepath.Clean(gameDir)
		cleanRecDir := filepath.Clean(rec.GameDir)
		recLang, _ := region.ParseLanguage(rec.Language)
		if strings.EqualFold(cleanGameDir, cleanRecDir) && region.Of(recLang) == reg && len(rec.Files) > 0 {
			sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
			allMatch := true
			for fName, wantHash := range rec.Files {
				curHash, err := verifier.HashFile(filepath.Join(sqpackDir, fName))
				if err != nil || !strings.EqualFold(curHash.SHA256, wantHash.SHA256) {
					allMatch = false
					break
				}
			}
			if allMatch {
				return ClientStatusResult{
					Status:      "patched",
					Label:       "已汉化",
					Description: fmt.Sprintf("已成功应用本汉化器补丁 (%s)", rec.Version),
				}, nil
			}
		}
	}

	// 2. 检查本地官方原版备份（权威基准，支持所有已建立本地备份的区服）
	metaPath := filepath.Join(backupRootFor(a.resourceDir()), string(reg), "meta.json")
	var meta verifier.BackupMeta
	hasBackupMeta := false
	if data, err := os.ReadFile(metaPath); err == nil {
		if json.Unmarshal(data, &meta) == nil && meta.Files != nil && len(meta.Files) > 0 {
			hasBackupMeta = true
		}
	}

	// 3. 非国际服无本地有效备份时，直接返回不支持原版检测
	if reg != region.Global && !hasBackupMeta {
		return ClientStatusResult{
			Status:      "unsupported",
			Label:       "不支持检测",
			Description: "非国际服未收录官方原版哈希，仅国际服支持原版哈希检测",
		}, nil
	}

	versionData, err := os.ReadFile(filepath.Join(gameDir, "game", "ffxivgame.ver"))
	if err != nil {
		return ClientStatusResult{Status: "unknown", Label: "版本未知", Description: "无法读取 ffxivgame.ver"}, nil
	}
	version := strings.TrimSpace(string(versionData))

	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	fontIndex := "000000.win32.index"
	textIndex := "0a0000.win32.index"

	fontHash, fontErr := verifier.HashFile(filepath.Join(sqpackDir, fontIndex))
	textHash, textErr := verifier.HashFile(filepath.Join(sqpackDir, textIndex))
	if fontErr != nil && textErr != nil {
		return ClientStatusResult{Status: "unknown", Label: "文件缺失", Description: "无法读取客户端关键索引数据包"}, nil
	}

	// 如果有本地备份，核对本地备份
	if hasBackupMeta && meta.Version == version {
		matchedBackup := true
		checkedCount := 0
		if backupFont, ok := meta.Files[fontIndex]; ok && fontErr == nil {
			checkedCount++
			if !strings.EqualFold(fontHash.SHA256, backupFont.SHA256) {
				matchedBackup = false
			}
		}
		if backupText, ok := meta.Files[textIndex]; ok && textErr == nil {
			checkedCount++
			if !strings.EqualFold(textHash.SHA256, backupText.SHA256) {
				matchedBackup = false
			}
		}
		if checkedCount > 0 && matchedBackup {
			return ClientStatusResult{
				Status:      "original",
				Label:       "官方原版",
				Description: fmt.Sprintf("与官方原版备份 (%s) 哈希完全一致", version),
			}, nil
		}
		// 本地有当前版本的备份但哈希不匹配，且非本工具汉化产物 -> 被外部修改
		return ClientStatusResult{
			Status:      "modified",
			Label:       "非原版",
			Description: fmt.Sprintf("与本地原版备份 (%s) 哈希不符，当前文件已被外部修改", version),
		}, nil
	}

	// 非国际服在本地备份版本与当前游戏版本不一致时，也返回不支持检测（无法验证新版本的原版哈希）
	if reg != region.Global {
		return ClientStatusResult{
			Status:      "unsupported",
			Label:       "不支持检测",
			Description: "非国际服未收录官方原版哈希，仅国际服支持原版哈希检测",
		}, nil
	}

	// 4. 校验客户端关键索引数据包结构有效性
	if textErr == nil {
		if _, err := sqpack.OpenIndex(filepath.Join(sqpackDir, textIndex)); err != nil {
			return ClientStatusResult{Status: "unknown", Label: "文件损坏", Description: fmt.Sprintf("文本索引文件解析异常: %v", err)}, nil
		}
	}
	if fontErr == nil {
		if _, err := sqpack.OpenIndex(filepath.Join(sqpackDir, fontIndex)); err != nil {
			return ClientStatusResult{Status: "unknown", Label: "文件损坏", Description: fmt.Sprintf("字体索引文件解析异常: %v", err)}, nil
		}
	}

	// 5. 若云端 Release 存在，进行严格的权威哈希核对（核对失败直接判定为非原版，严禁穿透）
	cloud := a.getCloud()
	repo := "Souma-Sumire/FFXIVChnTextPatch-Souma"
	parentCtx := a.ctx
	if parentCtx == nil {
		parentCtx = context.Background()
	}
	ctx, cancel := context.WithTimeout(parentCtx, 4*time.Second)
	defer cancel()

	if reg == region.Global {
		if r, err := cloud.ReleaseByTag(ctx, repo, version); err == nil && r != nil {
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
						Description: fmt.Sprintf("与云端官方原版 (%s) 哈希完全一致", version),
					}, nil
				}
				// 官方 Release 存在且哈希不匹配 -> 绝对是非原版！绝不能漏判为原版！
				return ClientStatusResult{
					Status:      "modified",
					Label:       "非原版",
					Description: fmt.Sprintf("与云端官方原版 (%s) 哈希不符，当前文件已被外部修改", version),
				}, nil
			}
		}
	}

	// 6. 检查是否检测到第三方修改/汉化痕迹
	if verifier.DetectModifiedSqPack(sqpackDir, reg) {
		return ClientStatusResult{
			Status:      "modified",
			Label:       "非原版",
			Description: "检测到客户端关键数据已被第三方汉化或外部修改",
		}, nil
	}

	// 7. 本地尚无备份且数据包结构完好的纯净客户端（首次使用或刚更新游戏）
	return ClientStatusResult{
		Status:      "original",
		Label:       "官方原版",
		Description: fmt.Sprintf("客户端关键数据包完整，为纯净原版状态 (%s)", version),
	}, nil
}


// AnswerFontConfirm 接收前端对“是否继续”的确认。
func (a *App) AnswerFontConfirm(ok bool) {
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

func (a *App) waitFontConfirm(ctx context.Context) (bool, error) {
	if a == nil || a.ctx == nil {
		return false, nil
	}
	ch := make(chan bool, 1)
	a.confirmMu.Lock()
	a.confirmCh = ch
	a.confirmMu.Unlock()

	runtime.EventsEmit(a.ctx, "font-confirm", "当前为官方原版字体，不替换字体将无法显示中文（已使用第三方插件加载字体的除外），确定继续吗？")
	select {
	case ok := <-ch:
		return ok, nil
	case <-time.After(10 * time.Minute):
		return false, errors.New("等待字体确认超时")
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

func (a *App) waitLauncherConfirm(launcherNames []string) (bool, error) {
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
	case <-a.ctx.Done():
		return false, a.ctx.Err()
	}
}

func (a *App) checkGameNotRunning(gameDir string) error {
	if a != nil && a.skipProcessCheck {
		return nil
	}
	return gameproc.CheckGameNotRunningWithOpts(gameproc.CheckOptions{
		GameDir:           gameDir,
		OnLauncherConfirm: a.waitLauncherConfirm,
		OnLaunchersKilled: func(killed []string) {
			a.emitPatchLog(fmt.Sprintf("[提示] 已强制结束游戏启动器进程（%s）", strings.Join(killed, "、")))
		},
	})
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

	if err := a.checkGameNotRunning(req.GameDir); err != nil {
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

	// 确保执行汉化时后台检查与同步已完成，必定使用最新资源
	if err := a.waitResourceReady(a.ctx); err != nil {
		return resp, fmt.Errorf("确保最新资源失败: %w", err)
	}

	resourceDir := a.resourceDir()
	if manifest, err := updater.ReadLocalManifest(resourceDir); err == nil && manifest != nil {
		resTime := formatResourceTime(manifest.GeneratedAt)
		if resTime != "" {
			a.emitPatchLog(fmt.Sprintf("[补丁] 当前使用资源构建时间：%s（CSV: %s, 字体: %s）",
				resTime, shortHash(manifest.RawEXD.SHA256), shortHash(manifest.Font.SHA256)))
		} else {
			a.emitPatchLog(fmt.Sprintf("[补丁] 当前使用资源版本（CSV: %s, 字体: %s）",
				shortHash(manifest.RawEXD.SHA256), shortHash(manifest.Font.SHA256)))
		}
	}
	backupRoot := backupRootFor(resourceDir)
	sqpackDir := filepath.Join(req.GameDir, "game", "sqpack", "ffxiv")

	registry := pipeline.NewRegistry(loadRules(resourceDir))
	for id, on := range req.Rules {
		registry.SetEnabled(id, on)
	}

	include := map[string]bool{}
	if len(req.SelectedTables) > 0 {
		for _, key := range req.SelectedTables {
			include["exd/"+strings.ToLower(key)] = true
		}
	}

	cfg := verifier.Config{
		GameDir:     req.GameDir,
		BackupRoot:  backupRoot,
		Language:    lang,
		ReplaceText: req.ReplaceText,
		ReplaceFont: req.ReplaceFont,
		Cloud:       a.getCloud(),
		ConfirmFont: a.waitFontConfirm,
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
			TextIndexPath:   filepath.Join(sqpackDir, "0a0000.win32.index"),
			CSVResourceDir:  filepath.Join(resourceDir, "rawexd"),
			SourceLanguage:  string(lang),
			TargetLanguages: region.TargetLanguagesFor(lang),
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
		stats, err := replace.ReplaceFont(
			filepath.Join(sqpackDir, "000000.win32.index"),
			filepath.Join(resourceDir, "font"),
			func(done, total int, file string) {
				a.emitProgress("font", file, int64(done), int64(total))
			},
		)
		if err == nil {
			a.emitPatchLog(fmt.Sprintf("[字体] 字体写入完成：已替换 %d 个字体文件", stats.Files))
		}
		resp.Font = stats
		return err
	}

	result, err := verifier.Run(a.ctx, cfg)
	if err != nil {
		a.emitPatchLog(fmt.Sprintf("[错误] 汉化失败：%v", err))
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

	// 汉化成功且未取消时，记录汉化产物的文件哈希与当前客户端版本号
	if !resp.Cancelled && (req.ReplaceText || req.ReplaceFont) {
		verPath := filepath.Join(req.GameDir, "game", "ffxivgame.ver")
		if vBytes, err := os.ReadFile(verPath); err == nil {
			curVer := strings.TrimSpace(string(vBytes))
			if curVer != "" {
				recordFiles := map[string]verifier.FileHash{}
				if req.ReplaceText {
					if h, err := verifier.HashFile(filepath.Join(sqpackDir, "0a0000.win32.index")); err == nil {
						recordFiles["0a0000.win32.index"] = h
					}
				}
				if req.ReplaceFont {
					if h, err := verifier.HashFile(filepath.Join(sqpackDir, "000000.win32.index")); err == nil {
						recordFiles["000000.win32.index"] = h
					}
				}
				if len(recordFiles) > 0 {
					_ = a.savePatchedRecord(PatchedRecord{
						GameDir:     req.GameDir,
						Version:     curVer,
						Language:    string(lang),
						PatchedAt:   time.Now(),
						ReplaceText: req.ReplaceText,
						ReplaceFont: req.ReplaceFont,
						Files:       recordFiles,
					})
				}
			}
		}
	}

	// 保存游戏目录与语言，便于下次启动。
	cfgSaved := a.loadConfig(resourceDir)
	if cfgSaved.GameDirs == nil {
		cfgSaved.GameDirs = make(map[string]string)
	}
	normLang := normalizeRegionCode(string(lang))
	cfgSaved.GameDir = req.GameDir
	cfgSaved.GameDirs[normLang] = req.GameDir
	cfgSaved.Language = string(lang)
	cfgSaved.ReplaceText = req.ReplaceText
	cfgSaved.ReplaceFont = req.ReplaceFont
	cfgSaved.Preset = req.Preset
	if cfgSaved.Rules == nil {
		cfgSaved.Rules = map[string]bool{}
	}
	for id, on := range req.Rules {
		cfgSaved.Rules[id] = on
	}
	_ = a.SaveConfig(cfgSaved)

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

	if err := a.checkGameNotRunning(gameDir); err != nil {
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
	restored, err := verifier.Restore(a.ctx, verifier.RestoreConfig{
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
		a.emitPatchLog(fmt.Sprintf("[错误] 还原失败：%v", err))
		return resp, err
	}
	a.clearPatchedRecord(gameDir)
	resp.Restored = restored
	if len(restored) == 0 {
		a.emitPatchLog("[完成] 经哈希校验，客户端全部 6 个核心文件均与官方原版一致，无需重复还原")
	} else {
		a.emitPatchLog(fmt.Sprintf("[完成] 官方原版文件已全部恢复（共还原 %d 个被修改的文件，其余 %d 个文件已是原版未变动）", len(restored), len(verifier.AllFiles)-len(restored)))
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
	u := updater.New()
	status, err := u.Provision(a.ctx, resourceDir, func(stage, detail string, done, total int64) {
		runtime.EventsEmit(a.ctx, "sync-progress", ProgressEvent{
			Stage: stage, Detail: detail, Done: done, Total: total,
		})
	})
	if err != nil {
		return summary, err
	}
	summary.Offline = status.Offline
	summary.UpToDate = !status.Updated && !status.Offline
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
