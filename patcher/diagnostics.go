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
	a.emitLog(logLevelDiag, "", format, args...)
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

// diagnosticsLines 不改变业务状态；同类信息按固定顺序精简输出，只保留排障所需字段。
// 副作用只有 isWritableDir 的写探测，它命中启动时的缓存，不会重复落盘。
func (a *App) diagnosticsLines() []string {
	var lines []string
	add := func(format string, args ...any) {
		lines = append(lines, diagLine(format, args...))
	}

	resourceDir := a.resourceDir()
	dataRoot := dataRootFor(resourceDir)
	backupRoot := backupRootFor(resourceDir)

	buildMode := "release"
	if localResourceOnly {
		buildMode = "dev"
	}
	add("--- 会话快照 (%s) ---", time.Now().Format("-07:00"))
	add("环境：%s (%s) | %s/%s | 权限=%s",
		updater.DisplayAppVersion(appVersion), buildMode, goruntime.GOOS, goruntime.GOARCH, processPrivilege())

	exe, exeErr := os.Executable()
	cwd, cwdErr := os.Getwd()
	pathParts := []string{fmt.Sprintf("exe=%s", pathOrError(exe, exeErr))}
	if cwdErr != nil || (exeErr == nil && !strings.EqualFold(cwd, filepath.Dir(exe))) {
		pathParts = append(pathParts, fmt.Sprintf("cwd=%s", pathOrError(cwd, cwdErr)))
	}
	writeState := "可写"
	if !isWritableDir(resourceDir) {
		writeState = "只读"
	}
	pathParts = append(pathParts, fmt.Sprintf("resource=%s(%s)", resourceDir, writeState))
	if dataRoot != "." {
		pathParts = append(pathParts, fmt.Sprintf("data=%s", dataRoot))
	}
	add("路径：%s", strings.Join(pathParts, " | "))

	cfg := a.loadConfig(resourceDir)
	regionKey := normalizeRegionCode(cfg.Language)
	lang, langErr := region.ParseLanguage(cfg.Language)
	if langErr != nil {
		lang = region.GLOBAL
	}
	targetLangs := cfg.TargetLanguages[regionKey]
	targetDesc := strings.Join(targetLangs, ",")
	if targetDesc == "" {
		targetDesc = "默认"
	}
	csvDirDesc := "内置"
	if dir := a.csvSelectedDir(resourceDir, regionKey); dir != "" {
		csvDirDesc = dir
	}
	presetDesc := cfg.Preset
	if presetDesc == "" {
		presetDesc = "recommended"
	}
	fontMode := a.fontSourceMode(resourceDir)
	font16Bit := a.fontTexture16Bit(resourceDir)
	cfgParts := []string{
		fmt.Sprintf("区服=%s", cfg.Language),
		fmt.Sprintf("替换=%s", formatReplaceSwitches(cfg.ReplaceText, cfg.ReplaceFont)),
		fmt.Sprintf("预设=%s", presetDesc),
		fmt.Sprintf("语言=%s", targetDesc),
		fmt.Sprintf("词条=%s", csvDirDesc),
		fmt.Sprintf("字体=%s/%s/16位=%t", fontMode, a.getActiveFontPreset(regionKey), font16Bit),
		fmt.Sprintf("规则=%s", formatRuleSwitches(cfg.Rules)),
	}
	if dirs := configuredGameRegions(cfg.GameDirs); len(dirs) > 0 {
		cfgParts = append(cfgParts, fmt.Sprintf("已设目录=%s", strings.Join(dirs, ",")))
	}
	add("配置：%s", strings.Join(cfgParts, " | "))

	// 首次启动时这些文件本来就不存在，要报"未生成"而不是"读取失败"，
	// 否则用户和排查者会把正常状态当成故障。
	manifest, manifestErr := updater.ReadLocalManifest(resourceDir)
	manifestDesc := "未生成"
	switch {
	case manifestErr == nil && manifest != nil:
		manifestDesc = fmt.Sprintf("%s (root=%s rawexd=%s font=%s rules=%s tables=%s)",
			manifest.GeneratedAt, shortHash(manifest.RootHash), shortHash(manifest.RawEXD.SHA256),
			shortHash(manifest.Font.SHA256), shortHash(manifest.Rules.SHA256), shortHash(manifest.Tables.SHA256))
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
			csvDesc = fmt.Sprintf("%d/%d", count, manifest.RawEXD.Count)
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
			tablesDesc = fmt.Sprintf("%d表/%d文件", len(doc.Tables), len(doc.Files))
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
		rulesDesc = "0项"
	} else {
		var bad []string
		for _, rule := range rules {
			meta := rule.Meta()
			if meta.Status != "ok" {
				bad = append(bad, fmt.Sprintf("%s=%s", meta.ID, meta.Status))
			}
		}
		if len(bad) == 0 {
			rulesDesc = fmt.Sprintf("%d项(ok)", len(rules))
		} else {
			rulesDesc = fmt.Sprintf("%d项(异常: %s)", len(rules), strings.Join(bad, ","))
		}
	}
	add("资源：就绪=%t | CSV=%s | 表清单=%s | 规则=%s",
		updater.HasLocalResource(resourceDir), csvDesc, tablesDesc, rulesDesc)
	add("清单：%s", manifestDesc)

	fontPkg := "未安装"
	if dir, ok := localFontPackage(resourceDir); ok {
		count, side := fontTextureStats(dir)
		fontPkg = fmt.Sprintf("%s(%d张/%dpx)", dir, count, side)
	}
	buildDesc := "无"
	if count, side := fontTextureStats(fontBuildDir(resourceDir)); count > 0 {
		buildDesc = fmt.Sprintf("%d张/%dpx", count, side)
	}
	compilerPath := filepath.Join(dataRoot, "fontchanger", fontengine.CLIBinaryName)
	compilerDesc := "未安装"
	if a.HasFontCompiler() {
		compilerDesc = "就绪"
	} else if fontMode == "compile" {
		compilerDesc = fmt.Sprintf("缺失(%s)", compilerPath)
	}
	add("字体：内置包=%s | 编译产物=%s | 编译组件=%s", fontPkg, buildDesc, compilerDesc)

	gameDir := cfg.GameDirs[regionKey]
	if gameDir == "" {
		add("客户端：未设置游戏目录")
	} else if info, infoErr := a.GetGameInfo(gameDir, cfg.Language); infoErr != nil {
		add("客户端：读取失败 (%v)", infoErr)
	} else {
		clientParts := []string{
			fmt.Sprintf("目录=%s", info.GameDir),
			fmt.Sprintf("版本=%s", info.Version),
		}
		if !info.Valid {
			clientParts = append(clientParts, "有效=false")
		}
		if info.RegionMismatch {
			clientParts = append(clientParts, fmt.Sprintf("区服不匹配(目标=%s,实际=%s)", info.Region, info.DetectedRegion))
		} else {
			clientParts = append(clientParts, fmt.Sprintf("区服=%s", info.DetectedRegion))
		}
		clientParts = append(clientParts, fmt.Sprintf("高分辨率=%d", info.UiAssetType))
		if info.BackupExists {
			clientParts = append(clientParts, fmt.Sprintf("备份=%s", info.BackupVersion))
		} else {
			clientParts = append(clientParts, "备份=无")
		}
		add("客户端：%s", strings.Join(clientParts, " | "))

		sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
		fileMap := make(map[string]FileStatus, len(info.Files))
		for _, f := range info.Files {
			fileMap[f.Name] = f
		}
		formatGroup := func(prefix string) string {
			parts := make([]string, 0, 3)
			for _, suffix := range []string{"dat0", "index", "index2"} {
				name := fmt.Sprintf("%s.win32.%s", prefix, suffix)
				f, ok := fileMap[name]
				if !ok || !f.Exists {
					parts = append(parts, suffix+"=缺失")
					continue
				}
				if suffix == "dat0" {
					parts = append(parts, fmt.Sprintf("%s=%dB", suffix, f.Size))
					continue
				}
				hash, hashErr := verifier.HashFile(filepath.Join(sqpackDir, name))
				if hashErr != nil {
					parts = append(parts, fmt.Sprintf("%s=%dB(哈希失败:%v)", suffix, f.Size, hashErr))
				} else {
					parts = append(parts, fmt.Sprintf("%s=%dB(%s)", suffix, f.Size, diagHash(hash.SHA256)))
				}
			}
			return strings.Join(parts, " | ")
		}
		add("文本文件(0a0000)：%s", formatGroup("0a0000"))
		add("字体文件(000000)：%s", formatGroup("000000"))
	}

	currentReg := region.Of(lang)
	regions := []region.Region{region.Global, region.China, region.Taiwan, region.Korea}
	var backupSummary []string
	var currentBackupDetail string
	for _, reg := range regions {
		data, err := os.ReadFile(filepath.Join(backupRoot, string(reg), "meta.json"))
		switch {
		case errors.Is(err, fs.ErrNotExist):
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
		backupSummary = append(backupSummary, fmt.Sprintf("%s=%s/%d文件", reg, meta.Version, len(meta.Files)))
		if reg == currentReg {
			names := make([]string, 0, len(meta.Files))
			for name := range meta.Files {
				names = append(names, name)
			}
			sort.Strings(names)
			parts := make([]string, 0, len(names))
			for _, name := range names {
				h := meta.Files[name]
				parts = append(parts, fmt.Sprintf("%s=%s/%dB", name, diagHash(h.SHA256), h.Size))
			}
			source := meta.Source
			if source == "" {
				source = "legacy"
			}
			currentBackupDetail = fmt.Sprintf("备份详情(%s)：版本=%s | 来源=%s | 更新=%s | %s",
				reg, meta.Version, source, meta.UpdatedAt, strings.Join(parts, " | "))
		}
	}
	if len(backupSummary) == 0 {
		add("本地备份：无")
	} else {
		add("本地备份：%s", strings.Join(backupSummary, " | "))
		if currentBackupDetail != "" {
			add("%s", currentBackupDetail)
		}
	}

	var envSet []string
	seenEnv := make(map[string]bool)
	for _, key := range []string{"HTTP_PROXY", "http_proxy", "HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy"} {
		if strings.TrimSpace(os.Getenv(key)) != "" {
			upper := strings.ToUpper(key)
			if !seenEnv[upper] {
				seenEnv[upper] = true
				envSet = append(envSet, upper)
			}
		}
	}
	envDesc := "无"
	if len(envSet) > 0 {
		envDesc = strings.Join(envSet, ",")
	}
	egress := "直连"
	if req, err := http.NewRequest(http.MethodGet, "https://github.com", nil); err == nil {
		proxyURL, proxyErr := netproxy.Func()(req)
		switch {
		case proxyErr != nil:
			egress = "探测失败:" + proxyErr.Error()
		case proxyURL != nil:
			// 只记协议，代理主机可能是内网地址。
			egress = fmt.Sprintf("代理(%s)", proxyURL.Scheme)
		}
	}
	cloudDesc := "未初始化"
	if cloud, ok := a.getCloud().(*verifier.GitHubClient); ok {
		node, directFast := cloud.NetworkState()
		switch {
		case node != "":
			cloudDesc = fmt.Sprintf("镜像=%s(直连快=%t)", node, directFast)
		case directFast:
			cloudDesc = "直连快=true"
		default:
			cloudDesc = "镜像=未选"
		}
	}
	add("网络：出口=%s | 环境变量=%s | %s", egress, envDesc, cloudDesc)

	return lines
}

func pathOrError(path string, err error) string {
	if err != nil {
		return "读取失败: " + err.Error()
	}
	return path
}

func diagHash(h string) string {
	if len(h) > 12 {
		return h[:12]
	}
	return h
}

func formatReplaceSwitches(textOn, fontOn bool) string {
	switch {
	case textOn && fontOn:
		return "文本+字体"
	case textOn:
		return "仅文本"
	case fontOn:
		return "仅字体"
	default:
		return "全关"
	}
}

func formatRuleSwitches(rules map[string]bool) string {
	if len(rules) == 0 {
		return "默认"
	}
	keys := make([]string, 0, len(rules))
	for k := range rules {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	var enabled []string
	for _, k := range keys {
		if rules[k] {
			enabled = append(enabled, k)
		}
	}
	if len(enabled) == 0 {
		return "全关"
	}
	return strings.Join(enabled, ",")
}

func configuredGameRegions(dirs map[string]string) []string {
	if len(dirs) == 0 {
		return nil
	}
	keys := make([]string, 0, len(dirs))
	for k, v := range dirs {
		if strings.TrimSpace(v) != "" {
			keys = append(keys, k)
		}
	}
	sort.Strings(keys)
	return keys
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
