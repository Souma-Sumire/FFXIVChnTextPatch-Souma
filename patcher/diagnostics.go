package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	goruntime "runtime"
	"sort"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/fontengine"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/netproxy"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/updater"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

const diagPrefix = verifier.DiagPrefix

var sessionStartedAt = time.Now()

// 调试信息会贴到公开 issue。路径里的用户名先按已知的用户目录精确替换，
// 再用正则兜底其他写法（配置 JSON 里的转义路径、其他盘符下的用户目录等）。
var (
	userPathReplacer = newUserPathReplacer()
	windowsUserPath  = regexp.MustCompile(`(?i)[a-z]:[\\/]+(?:users|documents and settings)[\\/]+[^\\/\s"',|]+`)
	unixUserPath     = regexp.MustCompile(`(?i)/(?:home|users)/[^/\s"',|]+`)
)

func newUserPathReplacer() *strings.Replacer {
	type entry struct{ path, placeholder string }
	var entries []entry
	add := func(path, placeholder string) {
		path = strings.TrimRight(strings.TrimSpace(path), `\/`)
		if len(path) < 4 {
			return
		}
		entries = append(entries, entry{path, placeholder})
	}
	home, _ := os.UserHomeDir()
	add(home, "%USERPROFILE%")
	add(os.Getenv("USERPROFILE"), "%USERPROFILE%")
	add(os.Getenv("LOCALAPPDATA"), "%LOCALAPPDATA%")
	add(os.Getenv("APPDATA"), "%APPDATA%")
	add(os.Getenv("TEMP"), "%TEMP%")
	add(os.Getenv("TMP"), "%TEMP%")
	if drive, tail := os.Getenv("HOMEDRIVE"), os.Getenv("HOMEPATH"); drive != "" && tail != "" {
		add(drive+tail, "%USERPROFILE%")
	}
	// 长路径先替换，否则 USERPROFILE 会把 LOCALAPPDATA 的开头吃掉。
	sort.SliceStable(entries, func(i, j int) bool { return len(entries[i].path) > len(entries[j].path) })
	pairs := make([]string, 0, len(entries)*2)
	seen := make(map[string]bool, len(entries))
	for _, e := range entries {
		if seen[e.path] {
			continue
		}
		seen[e.path] = true
		pairs = append(pairs, e.path, e.placeholder)
	}
	return strings.NewReplacer(pairs...)
}

func redactPaths(text string) string {
	if text == "" {
		return text
	}
	text = userPathReplacer.Replace(text)
	text = windowsUserPath.ReplaceAllString(text, "%USERPROFILE%")
	return unixUserPath.ReplaceAllString(text, "~")
}

func diagLine(format string, args ...any) string {
	return diagPrefix + fmt.Sprintf(format, args...)
}

func (a *App) emitDiag(format string, args ...any) {
	a.emitPatchLog(diagLine(format, args...))
}

// GetDiagnostics 返回一次会话的完整本地诊断信息，供前端写入日志。
// 路径中的用户名在这里统一隐去，不必等用户手动删。
func (a *App) GetDiagnostics() []string {
	lines := a.diagnosticsLines()
	for i, line := range lines {
		lines[i] = redactPaths(line)
	}
	return lines
}

// diagnosticsLines 不改变业务状态；同类信息合并成一行，避免刷屏。
// 副作用只有 isWritableDir 的写探测，它命中启动时的缓存，不会重复落盘。
func (a *App) diagnosticsLines() []string {
	var lines []string
	add := func(format string, args ...any) {
		lines = append(lines, diagLine(format, args...))
	}

	resourceDir := a.resourceDir()
	dataRoot := dataRootFor(resourceDir)
	cfgPath := configPathFor(resourceDir)
	backupRoot := backupRootFor(resourceDir)

	buildMode := "release"
	if localResourceOnly {
		buildMode = "development"
	}
	add("=== 会话快照 %s ===", time.Now().Format("2006-01-02 15:04:05 -07:00"))
	add("环境：%s（%s）｜Go %s｜%s/%s｜%d 核｜权限 %s｜PID %d｜启动 %s",
		updater.DisplayAppVersion(appVersion), buildMode, goruntime.Version(), goruntime.GOOS, goruntime.GOARCH,
		goruntime.NumCPU(), processPrivilege(), os.Getpid(), sessionStartedAt.Format("2006-01-02 15:04:05"))

	exe, exeErr := os.Executable()
	cwd, cwdErr := os.Getwd()
	add("路径：exe=%s｜cwd=%s｜resource=%s（%s，可写 %t）｜config=%s（%s）｜backup=%s（%s）｜data=%s",
		pathOrError(exe, exeErr), pathOrError(cwd, cwdErr), resourceDir, briefPath(resourceDir), isWritableDir(resourceDir),
		cfgPath, briefPath(cfgPath), backupRoot, briefPath(backupRoot), dataRoot)

	fileParts := make([]string, 0, 6)
	for _, p := range []string{
		filepath.Join(resourceDir, "manifest.json"),
		filepath.Join(resourceDir, "tables.json"),
		filepath.Join(resourceDir, "rules.json"),
		filepath.Join(resourceDir, "rawexd.zip"),
		filepath.Join(resourceDir, ".verify.json"),
		fontBuildMetaPath(resourceDir),
	} {
		fileParts = append(fileParts, fmt.Sprintf("%s=%s", filepath.Base(p), briefPath(p)))
	}
	add("资源文件：%s", strings.Join(fileParts, "｜"))

	cfg := a.loadConfig(resourceDir)
	if data, err := json.Marshal(cfg); err == nil {
		add("配置：%s", data)
	} else {
		add("配置：序列化失败 %v", err)
	}

	// 首次启动时这些文件本来就不存在，要报"未生成"而不是"读取失败"，
	// 否则用户和排查者会把正常状态当成故障。
	manifest, manifestErr := updater.ReadLocalManifest(resourceDir)
	manifestDesc := "未生成"
	switch {
	case manifestErr == nil && manifest != nil:
		manifestDesc = fmt.Sprintf("%s rootHash=%s rawexd=%s/%dB/%d font=%s/%dB rules=%s/%dB tables=%s/%dB",
			manifest.GeneratedAt, manifest.RootHash, manifest.RawEXD.SHA256, manifest.RawEXD.Size, manifest.RawEXD.Count,
			manifest.Font.SHA256, manifest.Font.Size, manifest.Rules.SHA256, manifest.Rules.Size, manifest.Tables.SHA256, manifest.Tables.Size)
	case manifestErr != nil && !errors.Is(manifestErr, fs.ErrNotExist):
		manifestDesc = "读取失败: " + manifestErr.Error()
	}

	// 实际文件数与清单记录对不上，说明上次同步没写完，这比单独一个数字有用。
	csvDesc := "目录缺失"
	if count, err := countFilesWithExt(filepath.Join(resourceDir, "rawexd"), ".csv"); err == nil {
		switch {
		case manifest == nil || manifest.RawEXD.Count == 0:
			csvDesc = fmt.Sprintf("%d", count)
		case manifest.RawEXD.Count == count:
			csvDesc = fmt.Sprintf("%d（与清单一致）", count)
		default:
			csvDesc = fmt.Sprintf("%d（清单 %d，不一致）", count, manifest.RawEXD.Count)
		}
	} else if !errors.Is(err, fs.ErrNotExist) {
		csvDesc = "统计失败: " + err.Error()
	}

	tablesDesc := "未生成"
	if data, err := os.ReadFile(filepath.Join(resourceDir, "tables.json")); err == nil {
		var doc TablesDoc
		if err := json.Unmarshal(data, &doc); err == nil {
			tablesDesc = fmt.Sprintf("%d 表/%d 目录/%d 文件", len(doc.Tables), len(doc.Folders), len(doc.Files))
		} else {
			tablesDesc = "解析失败: " + err.Error()
		}
	} else if !errors.Is(err, fs.ErrNotExist) {
		tablesDesc = "读取失败: " + err.Error()
	}
	rulesDesc := "未生成"
	if rules, err := loadRules(resourceDir); err != nil {
		rulesDesc = err.Error()
	} else if len(rules) == 0 {
		rulesDesc = "0 项"
	} else {
		parts := make([]string, 0, len(rules))
		for _, rule := range rules {
			meta := rule.Meta()
			parts = append(parts, fmt.Sprintf("%s:%s/默认%t", meta.ID, meta.Status, meta.DefaultOn))
		}
		rulesDesc = fmt.Sprintf("%d 项（%s）", len(rules), strings.Join(parts, "、"))
	}
	add("资源：字体来源=%s｜16 位=%t｜就绪=%t｜CSV=%s｜表清单=%s｜规则=%s｜清单=%s",
		a.fontSourceMode(resourceDir), a.fontTexture16Bit(resourceDir), updater.HasLocalResource(resourceDir),
		csvDesc, tablesDesc, rulesDesc, manifestDesc)

	fontPkg := "未安装"
	if dir, ok := localFontPackage(resourceDir); ok {
		count, side := fontTextureStats(dir)
		fontPkg = fmt.Sprintf("%s（%d 张/%dpx）", dir, count, side)
	}
	buildDesc := "无"
	if count, side := fontTextureStats(fontBuildDir(resourceDir)); count > 0 {
		buildDesc = fmt.Sprintf("%d 张/%dpx", count, side)
	}
	compilerPath := filepath.Join(dataRoot, "fontchanger", fontengine.CLIBinaryName)
	add("字体：包=%s｜编译产物=%s｜编译组件=%t（%s %s）", fontPkg, buildDesc,
		a.HasFontCompiler(), compilerPath, briefPath(compilerPath))

	regionKey := normalizeRegionCode(cfg.Language)
	lang, langErr := region.ParseLanguage(cfg.Language)
	if langErr != nil {
		lang = region.GLOBAL
	}
	targetLangs := cfg.TargetLanguages[regionKey]
	targetDesc := strings.Join(targetLangs, "、")
	if targetDesc == "" {
		targetDesc = "默认"
	}
	csvDirDesc := "未指定"
	if dir := a.csvSelectedDir(resourceDir, regionKey); dir != "" {
		csvDirDesc = dir
	}
	add("区服：%s（语言 %s）｜目标语言=%s｜字体方案=%s｜自定义词条目录=%s",
		region.Of(lang), cfg.Language, targetDesc, a.getActiveFontPreset(regionKey), csvDirDesc)

	gameDir := cfg.GameDirs[regionKey]
	if gameDir == "" {
		add("客户端：游戏目录未设置，文件与备份状态无法检查")
	} else if info, infoErr := a.GetGameInfo(gameDir, cfg.Language); infoErr != nil {
		add("客户端：读取失败 %v", infoErr)
	} else {
		add("客户端：目录=%s｜版本=%s｜有效=%t｜区服=%s｜检测=%s｜不匹配=%t｜备份版本=%s｜备份存在=%t｜高分辨率=%d｜资源=%s/%s",
			info.GameDir, info.Version, info.Valid, info.Region, info.DetectedRegion, info.RegionMismatch,
			info.BackupVersion, info.BackupExists, info.UiAssetType, info.ResourceTime, info.ResourceHash)
		fileParts := make([]string, 0, len(info.Files))
		for _, f := range info.Files {
			if f.Exists {
				fileParts = append(fileParts, fmt.Sprintf("%s=%dB", f.Name, f.Size))
			} else {
				fileParts = append(fileParts, f.Name+"=缺失")
			}
		}
		add("客户端文件：%s", strings.Join(fileParts, "｜"))
		sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
		hashParts := make([]string, 0, 4)
		for _, name := range []string{"0a0000.win32.index", "0a0000.win32.index2", "000000.win32.index", "000000.win32.index2"} {
			hash, hashErr := verifier.HashFile(filepath.Join(sqpackDir, name))
			if hashErr != nil {
				hashParts = append(hashParts, fmt.Sprintf("%s=失败(%v)", name, hashErr))
			} else {
				hashParts = append(hashParts, fmt.Sprintf("%s=%s", name, hash.SHA256))
			}
		}
		add("客户端索引哈希：%s", strings.Join(hashParts, "｜"))
	}

	// 四个区服各一行概览；当前区服额外逐文件列出，方便核对被改过的文件。
	currentReg := region.Of(lang)
	regions := []region.Region{region.Global, region.China, region.Taiwan, region.Korea}
	backupSummary := make([]string, 0, len(regions))
	for _, reg := range regions {
		data, err := os.ReadFile(filepath.Join(backupRoot, string(reg), "meta.json"))
		switch {
		case errors.Is(err, fs.ErrNotExist):
			backupSummary = append(backupSummary, fmt.Sprintf("%s=无", reg))
			continue
		case err != nil:
			backupSummary = append(backupSummary, fmt.Sprintf("%s=读取失败", reg))
			continue
		}
		var meta verifier.BackupMeta
		if err := json.Unmarshal(data, &meta); err != nil {
			backupSummary = append(backupSummary, fmt.Sprintf("%s=解析失败", reg))
			continue
		}
		backupSummary = append(backupSummary, fmt.Sprintf("%s=%s/%d 文件", reg, meta.Version, len(meta.Files)))
		if reg != currentReg {
			continue
		}
		names := make([]string, 0, len(meta.Files))
		for name := range meta.Files {
			names = append(names, name)
		}
		sort.Strings(names)
		parts := make([]string, 0, len(names))
		for _, name := range names {
			h := meta.Files[name]
			parts = append(parts, fmt.Sprintf("%s=%s/%dB", name, h.SHA256, h.Size))
		}
		add("备份 %s：版本=%s｜更新=%s｜%s", reg, meta.Version, meta.UpdatedAt, strings.Join(parts, "｜"))
	}
	add("备份概览：%s", strings.Join(backupSummary, "｜"))

	var envSet []string
	for _, key := range []string{"HTTP_PROXY", "http_proxy", "HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy"} {
		if strings.TrimSpace(os.Getenv(key)) != "" {
			envSet = append(envSet, key)
		}
	}
	envDesc := "未设置"
	if len(envSet) > 0 {
		envDesc = strings.Join(envSet, ",") + " 已设置（值不记录）"
	}
	egress := "直连"
	if req, err := http.NewRequest(http.MethodGet, "https://github.com", nil); err == nil {
		proxyURL, proxyErr := netproxy.Func()(req)
		switch {
		case proxyErr != nil:
			egress = "探测失败: " + proxyErr.Error()
		case proxyURL != nil:
			// 只记协议，代理主机可能是内网地址。
			egress = fmt.Sprintf("代理已启用（%s）", proxyURL.Scheme)
		}
	}
	cloudDesc := "未初始化"
	if cloud, ok := a.getCloud().(*verifier.GitHubClient); ok {
		node, directFast := cloud.NetworkState()
		cloudDesc = fmt.Sprintf("首选镜像=%s/直连足够快=%t", node, directFast)
	}
	add("网络：代理变量=%s｜出口=%s｜%s", envDesc, egress, cloudDesc)

	return lines
}

func pathOrError(path string, err error) string {
	if err != nil {
		return "读取失败: " + err.Error()
	}
	return path
}

func briefPath(path string) string {
	st, err := os.Stat(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return "缺失"
		}
		return "读取失败"
	}
	if st.IsDir() {
		return "目录"
	}
	return fmt.Sprintf("%dB", st.Size())
}

func countFilesWithExt(dir, ext string) (int, error) {
	count := 0
	err := filepath.WalkDir(dir, func(_ string, entry fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !entry.IsDir() && strings.EqualFold(filepath.Ext(entry.Name()), ext) {
			count++
		}
		return nil
	})
	return count, err
}

// stringListFingerprint 用短指纹标识大列表，避免把 7000 多个表名刷进日志。
func stringListFingerprint(items []string) string {
	h := sha256.New()
	for _, item := range items {
		_, _ = io.WriteString(h, item)
		_, _ = io.WriteString(h, "\x00")
	}
	return hex.EncodeToString(h.Sum(nil))[:16]
}

func firstStrings(items []string, n int) []string {
	if len(items) <= n {
		return items
	}
	return items[:n]
}

// patchRequestSummary 把请求压成一行。表清单可能有上千条，只留数量、指纹与前几项，
// 其余字段原样序列化，排查时能直接看到前端到底传了什么。
func patchRequestSummary(req PatchRequest) string {
	type summaryRequest struct {
		PatchRequest
		SelectedTables []string `json:"selectedTables,omitempty"`
	}
	trimmed := summaryRequest{PatchRequest: req}
	trimmed.PatchRequest.SelectedTables = nil
	raw, err := json.Marshal(trimmed)
	if err != nil {
		return fmt.Sprintf("序列化失败 %v", err)
	}
	desc := fmt.Sprintf("selectedTables=%d", len(req.SelectedTables))
	if len(req.SelectedTables) > 0 {
		desc += fmt.Sprintf("（指纹 %s，示例 %s）",
			stringListFingerprint(req.SelectedTables), strings.Join(firstStrings(req.SelectedTables, 8), "、"))
	}
	return string(raw) + "，" + desc
}
