package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	goruntime "runtime"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/longbridgeapp/opencc"
	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/text"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/updater"
)

// CSVDirLabel 是用户为词条目录自定义的名称与描述，仅用于界面展示。
type CSVDirLabel struct {
	Title string `json:"title"`
	Desc  string `json:"desc"`
}

// CSVConfig 按区服保存自定义词条目录。Selected 为空表示使用资源目录下的内置词条。
type CSVConfig struct {
	Dirs     []string               `json:"dirs"`
	Selected string                 `json:"selected"`
	Labels   map[string]CSVDirLabel `json:"labels,omitempty"`
}

// CSVDirsByRegion 按区服保存词条目录配置，键为区服代码。
type CSVDirsByRegion map[string]CSVConfig

// CSVDirOption 是前端下拉列表中的一项。
type CSVDirOption struct {
	Path      string `json:"path"`
	Valid     bool   `json:"valid"`
	Title     string `json:"title,omitempty"`
	Desc      string `json:"desc,omitempty"`
	UpdatedAt string `json:"updatedAt,omitempty"`
	// CanDeleteFiles 表示目录由程序生成（custom-csv），移除时可一并删除文件。
	CanDeleteFiles bool `json:"canDeleteFiles"`
}

// CSVContext 是当前区服可用的词条目录与对应的表清单。
type CSVContext struct {
	Dirs              []CSVDirOption `json:"dirs"`
	Selected          string         `json:"selected"`
	Custom            bool           `json:"custom"`
	DefaultDir        string         `json:"defaultDir"`
	TableList         []TableEntry   `json:"tableList"`
	ResourceReady     bool           `json:"resourceReady"`
	ResourceUpdatedAt string         `json:"resourceUpdatedAt,omitempty"`
	// ResourceHash 是内置词条包（rawexd.zip）的 SHA256，用于标识副本基于哪份快照。
	ResourceHash string `json:"resourceHash,omitempty"`
}

// errCSVDirInvalid 表示目录里没有与内置 tables.json 对得上的词条 CSV。
var errCSVDirInvalid = errors.New("所选目录中没有可识别的词条 CSV（文件名需与内置词条一致）")

// normalizeCSVConfig 去空白、去重（Windows 下大小写不敏感），并保证 Selected 属于 Dirs。
func normalizeCSVConfig(conf CSVConfig) CSVConfig {
	out := CSVConfig{Dirs: make([]string, 0, len(conf.Dirs))}
	seen := make(map[string]bool, len(conf.Dirs))
	for _, dir := range conf.Dirs {
		trimmed := strings.TrimSpace(dir)
		if trimmed == "" {
			continue
		}
		cleaned := filepath.Clean(trimmed)
		key := strings.ToLower(cleaned)
		if seen[key] {
			continue
		}
		seen[key] = true
		out.Dirs = append(out.Dirs, cleaned)
	}
	if selected := strings.TrimSpace(conf.Selected); selected != "" {
		key := strings.ToLower(filepath.Clean(selected))
		for _, dir := range out.Dirs {
			if strings.ToLower(dir) == key {
				out.Selected = dir
				break
			}
		}
	}
	if len(conf.Labels) > 0 {
		byKey := make(map[string]string, len(out.Dirs))
		for _, dir := range out.Dirs {
			byKey[strings.ToLower(dir)] = dir
		}
		for path, label := range conf.Labels {
			trimmed := strings.TrimSpace(path)
			if trimmed == "" {
				continue
			}
			actual, ok := byKey[strings.ToLower(filepath.Clean(trimmed))]
			if !ok {
				continue
			}
			title := strings.TrimSpace(label.Title)
			desc := strings.TrimSpace(label.Desc)
			if title == "" && desc == "" {
				continue
			}
			if out.Labels == nil {
				out.Labels = make(map[string]CSVDirLabel, len(conf.Labels))
			}
			out.Labels[actual] = CSVDirLabel{Title: title, Desc: desc}
		}
	}
	return out
}

// knownTableKeys 返回 tables.json 收录的全部表键。
func knownTableKeys(doc TablesDoc) map[string]bool {
	known := make(map[string]bool, len(doc.Files)+len(doc.Tables))
	for _, key := range doc.Files {
		known[key] = true
	}
	for key := range doc.Tables {
		known[key] = true
	}
	return known
}

// scanCSVKeys 收集目录下全部 CSV 的相对路径键，与 tables.json 的 files 键同构。
func scanCSVKeys(dir string) ([]string, error) {
	keys := make([]string, 0, 256)
	err := filepath.WalkDir(dir, func(path string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".csv") {
			return nil
		}
		rel, relErr := filepath.Rel(dir, path)
		if relErr != nil {
			return nil
		}
		slashed := filepath.ToSlash(rel)
		keys = append(keys, slashed[:len(slashed)-len(filepath.Ext(slashed))])
		return nil
	})
	if err != nil {
		return nil, err
	}
	sort.Strings(keys)
	return keys, nil
}

// validCSVKeys 只保留 tables.json 收录的表键。
func validCSVKeys(dir string, known map[string]bool) ([]string, error) {
	keys, err := scanCSVKeys(dir)
	if err != nil {
		return nil, err
	}
	out := keys[:0]
	for _, key := range keys {
		if known[key] {
			out = append(out, key)
		}
	}
	return out, nil
}

func csvDirHasTables(dir string, known map[string]bool) bool {
	keys, err := validCSVKeys(dir, known)
	return err == nil && len(keys) > 0
}

// csvDirUpdatedAt 返回目录内最新的文件修改时间，展示在词条列表的名字后面。
func csvDirUpdatedAt(dir string) string {
	var latest time.Time
	_ = filepath.WalkDir(dir, func(_ string, entry os.DirEntry, err error) error {
		if err != nil || entry.IsDir() {
			return nil
		}
		info, infoErr := entry.Info()
		if infoErr != nil {
			return nil
		}
		if mt := info.ModTime(); mt.After(latest) {
			latest = mt
		}
		return nil
	})
	return formatUpdatedAt(latest)
}

// copyCSVTree 并发复制词条目录。7114 个小文件的逐个创建是主要开销（读取几乎都能命中
// 系统缓存），实测整文件读写 + 8 并发约 2.7 秒，流式 io.CopyBuffer 约 3.7 秒；
// 从 rawexd.zip 解压同样要创建 7114 个文件，没有更快，还多一次解压与时间戳修复。
// transform 非空时在落盘前改写文件内容（简繁转换副本用）。
func copyCSVTree(src, dst string, transform func([]byte) ([]byte, error), onProgress func(done, total int)) error {
	type entry struct {
		rel string
		dir bool
	}
	entries := make([]entry, 0, 8192)
	err := filepath.WalkDir(src, func(path string, d os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		rel, relErr := filepath.Rel(src, path)
		if relErr != nil {
			return relErr
		}
		if rel == "." {
			return nil
		}
		entries = append(entries, entry{rel: rel, dir: d.IsDir()})
		return nil
	})
	if err != nil {
		return err
	}
	for _, it := range entries {
		if it.dir {
			if err := os.MkdirAll(filepath.Join(dst, it.rel), 0o755); err != nil {
				return err
			}
		}
	}

	files := make([]string, 0, len(entries))
	for _, it := range entries {
		if !it.dir {
			files = append(files, it.rel)
		}
	}
	total := len(files)
	if total == 0 {
		return nil
	}

	const workers = 8
	jobs := make(chan int, total)
	for i := range files {
		jobs <- i
	}
	close(jobs)

	var (
		wg       sync.WaitGroup
		mu       sync.Mutex
		firstErr error
		done     atomic.Int64
	)
	for i := 0; i < workers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for idx := range jobs {
				mu.Lock()
				failed := firstErr != nil
				mu.Unlock()
				if failed {
					return
				}
				if err := copyCSVFile(filepath.Join(src, files[idx]), filepath.Join(dst, files[idx]), transform); err != nil {
					mu.Lock()
					if firstErr == nil {
						firstErr = err
					}
					mu.Unlock()
					return
				}
				if onProgress != nil {
					onProgress(int(done.Add(1)), total)
				}
			}
		}()
	}
	wg.Wait()
	return firstErr
}

// copyCSVFile 一次读完再落盘：CSV 平均只有 11KB，比读写循环少两次系统调用。
func copyCSVFile(src, dst string, transform func([]byte) ([]byte, error)) error {
	data, err := os.ReadFile(src)
	if err != nil {
		return err
	}
	if transform != nil {
		data, err = transform(data)
		if err != nil {
			return err
		}
	}
	return os.WriteFile(dst, data, 0o644)
}

// s2twConverter 复用同一份 OpenCC 台湾正体字典。Convert 只读字典，可并发调用。
var (
	s2twOnce sync.Once
	s2twConv *opencc.OpenCC
	s2twErr  error
)

// convertCSVS2TW 按简繁转换规则改写整份 CSV，<hex:...> 占位保持不变。
func convertCSVS2TW(data []byte) ([]byte, error) {
	s2twOnce.Do(func() { s2twConv, s2twErr = opencc.New("s2tw") })
	if s2twErr != nil {
		return nil, fmt.Errorf("简繁转换不可用: %w", s2twErr)
	}
	var convErr error
	out := text.TransformPlain(string(data), func(plain string) string {
		if plain == "" || isASCII(plain) {
			return plain
		}
		converted, err := s2twConv.Convert(plain)
		if err != nil {
			convErr = err
			return plain
		}
		return converted
	})
	if convErr != nil {
		return nil, convErr
	}
	return []byte(out), nil
}

func isASCII(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] >= 0x80 {
			return false
		}
	}
	return true
}

// resourceUpdatedAt 返回内置词条快照的生成时间。
func resourceUpdatedAt(resourceDir string) string {
	mf, err := updater.ReadLocalManifest(resourceDir)
	if err != nil || mf == nil {
		return ""
	}
	t, parseErr := time.Parse(time.RFC3339, mf.GeneratedAt)
	if parseErr != nil {
		return ""
	}
	return formatUpdatedAt(t)
}

// resourceHash 返回内置词条包（rawexd.zip）的 SHA256；清单缺失时退回根哈希。
func resourceHash(resourceDir string) string {
	mf, err := updater.ReadLocalManifest(resourceDir)
	if err != nil || mf == nil {
		return ""
	}
	if mf.RawEXD.SHA256 != "" {
		return mf.RawEXD.SHA256
	}
	return mf.RootHash
}

func formatUpdatedAt(t time.Time) string {
	if t.IsZero() {
		return ""
	}
	return t.Local().Format("2006-01-02 15:04")
}

// csvContext 依据配置生成前端的目录列表与表清单。
func (a *App) csvContext(cfg AppConfig, doc TablesDoc, regionKey string) CSVContext {
	known := knownTableKeys(doc)
	conf := normalizeCSVConfig(cfg.CSVDirs[regionKey])
	resourceDir := a.resourceDir()
	ctx := CSVContext{
		Dirs:          make([]CSVDirOption, 0, len(conf.Dirs)),
		DefaultDir:    filepath.Join(resourceDir, "rawexd"),
		ResourceReady: updater.HasLocalResource(resourceDir),
	}
	if ctx.ResourceReady {
		ctx.ResourceUpdatedAt = resourceUpdatedAt(resourceDir)
		ctx.ResourceHash = resourceHash(resourceDir)
	}
	for _, dir := range conf.Dirs {
		label := conf.Labels[dir]
		ctx.Dirs = append(ctx.Dirs, CSVDirOption{
			Path:           dir,
			Valid:          csvDirHasTables(dir, known),
			Title:          label.Title,
			Desc:           label.Desc,
			UpdatedAt:      csvDirUpdatedAt(dir),
			CanDeleteFiles: sameDirPath(dir, customCSVDir(resourceDir)),
		})
	}
	ctx.Selected = conf.Selected
	if ctx.Selected == "" {
		ctx.TableList = buildTableList(doc)
		return ctx
	}
	ctx.Custom = true
	keys, err := validCSVKeys(ctx.Selected, known)
	if err != nil {
		keys = nil
	}
	ctx.TableList = tableEntries(doc, keys)
	return ctx
}

// csvSelectedDir 返回当前区服选中的自定义目录，空字符串表示内置词条。
func (a *App) csvSelectedDir(resourceDir, regionKey string) string {
	cfg := a.loadConfig(resourceDir)
	return normalizeCSVConfig(cfg.CSVDirs[regionKey]).Selected
}

// resolveCSVDir 返回本次汉化使用的词条目录；custom 为 true 表示用户自选目录。
func (a *App) resolveCSVDir(resourceDir, regionKey string) (string, bool, error) {
	cfg := a.loadConfig(resourceDir)
	conf := normalizeCSVConfig(cfg.CSVDirs[regionKey])
	if conf.Selected == "" {
		return filepath.Join(resourceDir, "rawexd"), false, nil
	}
	doc := a.loadTablesDoc(resourceDir)
	known := knownTableKeys(doc)
	if len(known) == 0 {
		return "", true, errors.New("本地缺少表清单（tables.json），无法识别自定义词条目录；请先联网同步一次资源")
	}
	if !csvDirHasTables(conf.Selected, known) {
		return "", true, fmt.Errorf("自定义词条目录不可用：%s（目录不存在或没有可识别的词条 CSV），请重新选择或删除后重试", conf.Selected)
	}
	return conf.Selected, true, nil
}

// GetCSVContext 返回指定区服当前的词条目录配置与可选表清单。
func (a *App) GetCSVContext(language string) (CSVContext, error) {
	resourceDir := a.resourceDir()
	cfg := a.loadConfig(resourceDir)
	return a.csvContext(cfg, a.loadTablesDoc(resourceDir), normalizeRegionCode(language)), nil
}

// SetCSVDirectoryMeta 设置词条目录在界面上的名称与描述；两者都为空时恢复默认显示。
func (a *App) SetCSVDirectoryMeta(language, path, title, desc string) (CSVContext, error) {
	resourceDir := a.resourceDir()
	regionKey := normalizeRegionCode(language)
	cfg := a.loadConfig(resourceDir)
	doc := a.loadTablesDoc(resourceDir)
	current := a.csvContext(cfg, doc, regionKey)

	target := strings.TrimSpace(path)
	if target == "" {
		return current, errors.New("未指定词条目录")
	}
	conf := normalizeCSVConfig(cfg.CSVDirs[regionKey])
	matched := ""
	for _, dir := range conf.Dirs {
		if strings.EqualFold(dir, filepath.Clean(target)) {
			matched = dir
			break
		}
	}
	if matched == "" {
		return current, errors.New("该词条目录不在当前区服的列表中")
	}

	title = strings.TrimSpace(title)
	desc = strings.TrimSpace(desc)
	if len([]rune(title)) > 40 {
		title = string([]rune(title)[:40])
	}
	if len([]rune(desc)) > 120 {
		desc = string([]rune(desc)[:120])
	}
	if title == "" && desc == "" {
		delete(conf.Labels, matched)
	} else {
		if conf.Labels == nil {
			conf.Labels = make(map[string]CSVDirLabel, 1)
		}
		conf.Labels[matched] = CSVDirLabel{Title: title, Desc: desc}
	}
	cfg.CSVDirs[regionKey] = normalizeCSVConfig(conf)
	if err := a.SaveConfig(cfg); err != nil {
		return current, err
	}
	return a.csvContext(cfg, doc, regionKey), nil
}

// OpenCSVDirectory 在系统文件管理器中打开词条目录；path 为空时打开内置词条目录。
func (a *App) OpenCSVDirectory(language, path string) error {
	resourceDir := a.resourceDir()
	defaultDir := filepath.Join(resourceDir, "rawexd")
	conf := normalizeCSVConfig(a.loadConfig(resourceDir).CSVDirs[normalizeRegionCode(language)])

	target := ""
	if strings.TrimSpace(path) == "" {
		target = defaultDir
	} else {
		cleaned := filepath.Clean(strings.TrimSpace(path))
		for _, dir := range conf.Dirs {
			if strings.EqualFold(dir, cleaned) {
				target = dir
				break
			}
		}
		if target == "" && strings.EqualFold(defaultDir, cleaned) {
			target = defaultDir
		}
	}
	if target == "" {
		return errors.New("只能打开词条目录列表中的目录")
	}
	if st, err := os.Stat(target); err != nil || !st.IsDir() {
		return fmt.Errorf("目录不存在: %s", target)
	}
	return openInFileManager(target)
}

// openInFileManager 调用系统文件管理器打开目录。
func openInFileManager(dir string) error {
	var cmd *exec.Cmd
	switch goruntime.GOOS {
	case "windows":
		cmd = exec.Command("explorer", dir)
	case "darwin":
		cmd = exec.Command("open", dir)
	default:
		cmd = exec.Command("xdg-open", dir)
	}
	return cmd.Start()
}

// customCSVDirName 是自动生成的自定义词条目录名，位于 resource 目录内。
const customCSVDirName = "custom-csv"

// customCSVDir 返回自动生成的自定义词条目录路径。dataRootFor 只在资源落到系统缓存目录时
// 改写到持久目录，那种情况下用户编辑的 CSV 同样不该留在缓存里。
func customCSVDir(resourceDir string) string {
	if root := dataRootFor(resourceDir); root != filepath.Dir(resourceDir) {
		return filepath.Join(root, customCSVDirName)
	}
	return filepath.Join(resourceDir, customCSVDirName)
}

// CopyBuiltinCSVDirectory 自动生成自定义词条目录（内置词条的一份副本）并切换过去，
// 无需用户选择位置。目录已存在时直接复用，不覆盖用户已修改的内容；
// convert 为 true 时先按内置的简繁转换（OpenCC 台湾正体）改写副本内容。
func (a *App) CopyBuiltinCSVDirectory(language string, convert bool) (CSVContext, error) {
	resourceDir := a.resourceDir()
	regionKey := normalizeRegionCode(language)
	cfg := a.loadConfig(resourceDir)
	doc := a.loadTablesDoc(resourceDir)
	current := a.csvContext(cfg, doc, regionKey)

	srcDir := filepath.Join(resourceDir, "rawexd")
	if st, err := os.Stat(srcDir); err != nil || !st.IsDir() {
		return current, errors.New("内置词条尚未同步，没有可复制的 CSV；请先联网同步一次资源")
	}
	dstDir := customCSVDir(resourceDir)
	if pathWithin(dstDir, srcDir) || pathWithin(srcDir, dstDir) {
		return current, errors.New("自定义词条目录不能与内置词条目录重叠")
	}

	entries, err := os.ReadDir(dstDir)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return current, err
	}
	created := len(entries) == 0
	if !created {
		if convert {
			return current, errors.New("已存在内置词条副本，如需繁體副本请先删除现有副本目录")
		}
		a.emitPatchLog("[词条] 已存在内置词条副本，直接使用其中的 CSV（不会覆盖你的修改）")
	} else {
		transform := func(data []byte) ([]byte, error) { return data, nil }
		detail := "制作副本"
		if convert {
			// 提前初始化字典：失败时不必先复制一遍再回滚。
			if _, err := convertCSVS2TW(nil); err != nil {
				return current, err
			}
			transform = convertCSVS2TW
			detail = "简繁转换"
		}
		if err := os.MkdirAll(dstDir, 0o755); err != nil {
			return current, fmt.Errorf("创建自定义词条目录失败: %w", err)
		}
		var lastEmit time.Time
		copyErr := copyCSVTree(srcDir, dstDir, transform, func(done, total int) {
			if a.ctx == nil || (done != total && time.Since(lastEmit) < 100*time.Millisecond) {
				return
			}
			lastEmit = time.Now()
			runtime.EventsEmit(a.ctx, "csv-copy-progress", ProgressEvent{
				Stage:  "copy",
				Detail: detail,
				Done:   int64(done),
				Total:  int64(total),
			})
		})
		if copyErr != nil {
			// 复制中断时清掉半成品，避免下次误把它当成可用副本。
			_ = os.RemoveAll(dstDir)
			return current, fmt.Errorf("复制内置词条失败: %w", copyErr)
		}
		if convert {
			a.emitPatchLog("[词条] 已生成内置词条副本（已简繁转换），可直接编辑其中的 CSV")
		} else {
			a.emitPatchLog("[词条] 已生成内置词条副本，可直接编辑其中的 CSV")
		}
	}

	conf := normalizeCSVConfig(cfg.CSVDirs[regionKey])
	conf.Dirs = append(conf.Dirs, dstDir)
	if created {
		if conf.Labels == nil {
			conf.Labels = make(map[string]CSVDirLabel, 1)
		}
		conf.Labels[dstDir] = CSVDirLabel{Desc: copyDescription(resourceDir, convert)}
	}
	conf = normalizeCSVConfig(conf)
	conf.Selected = dstDir
	cfg.CSVDirs[regionKey] = conf
	if err := a.SaveConfig(cfg); err != nil {
		return current, err
	}
	return a.csvContext(cfg, doc, regionKey), nil
}

// copyDescription 记录副本基于哪份内置词条快照，简繁转换副本额外注明。
func copyDescription(resourceDir string, convert bool) string {
	hash := resourceHash(resourceDir)
	desc := "基于内置词条制作"
	if hash != "" {
		short := hash
		if len(short) > 8 {
			short = short[:8]
		}
		desc = "基于内置词条 " + short + " 制作"
	}
	if convert {
		desc += "（已简繁转换）"
	}
	return desc
}

// pathWithin 判断 path 是否位于 base 之内（含相等）。
func pathWithin(path, base string) bool {
	rel, err := filepath.Rel(base, path)
	if err != nil {
		return false
	}
	return rel == "." || (rel != ".." && !strings.HasPrefix(rel, ".."+string(os.PathSeparator)))
}

// sameDirPath 判断两个路径是否指向同一目录；Windows 盘符与路径大小写不敏感。
func sameDirPath(a, b string) bool {
	return strings.EqualFold(filepath.Clean(a), filepath.Clean(b))
}

// SetCSVSelection 切换当前区服使用的词条目录，空字符串表示回到内置词条。
func (a *App) SetCSVSelection(language, path string) (CSVContext, error) {
	resourceDir := a.resourceDir()
	regionKey := normalizeRegionCode(language)
	cfg := a.loadConfig(resourceDir)
	doc := a.loadTablesDoc(resourceDir)
	current := a.csvContext(cfg, doc, regionKey)

	conf := normalizeCSVConfig(cfg.CSVDirs[regionKey])
	conf.Selected = ""
	if strings.TrimSpace(path) != "" {
		target := filepath.Clean(strings.TrimSpace(path))
		matched := ""
		for _, dir := range conf.Dirs {
			if strings.EqualFold(dir, target) {
				matched = dir
				break
			}
		}
		if matched == "" {
			return current, errors.New("该词条目录不在当前区服的列表中")
		}
		if !csvDirHasTables(matched, knownTableKeys(doc)) {
			return current, errCSVDirInvalid
		}
		conf.Selected = matched
	}
	cfg.CSVDirs[regionKey] = conf
	if err := a.SaveConfig(cfg); err != nil {
		return current, err
	}
	return a.csvContext(cfg, doc, regionKey), nil
}

// removeCSVDirectoryFiles 删除程序生成的词条副本目录。
// 只接受规范的 custom-csv 路径：用户自选目录、内置词条目录、资源目录本身及其父级一律拒绝，
// 目标为符号链接/联接点时也不删，避免沿着链接删到别处。
func removeCSVDirectoryFiles(resourceDir, target string) error {
	managed := filepath.Clean(customCSVDir(resourceDir))
	if !sameDirPath(managed, target) {
		return errors.New("只能删除程序生成的词条副本目录，该目录的文件不会被删除")
	}
	if filepath.Dir(managed) == managed {
		return errors.New("拒绝删除文件系统根目录")
	}
	if pathWithin(resourceDir, managed) {
		return errors.New("目标目录包含资源目录，已取消删除")
	}
	info, err := os.Lstat(managed)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		return err
	}
	if !info.IsDir() {
		return errors.New("目标不是目录，已取消删除")
	}
	if info.Mode()&os.ModeSymlink != 0 {
		return errors.New("目标目录是符号链接，已取消删除")
	}
	if resolved, resolveErr := filepath.EvalSymlinks(managed); resolveErr == nil && !sameDirPath(resolved, managed) {
		return errors.New("目标目录指向其他位置，已取消删除")
	}
	return os.RemoveAll(managed)
}

// RemoveCSVDirectory 从当前区服的列表中移除目录，被移除项正在使用时回到内置词条。
// deleteFiles 为 true 时一并删除程序生成的目录文件；删除失败则保留列表项，便于重试。
func (a *App) RemoveCSVDirectory(language, path string, deleteFiles bool) (CSVContext, error) {
	resourceDir := a.resourceDir()
	regionKey := normalizeRegionCode(language)
	cfg := a.loadConfig(resourceDir)
	doc := a.loadTablesDoc(resourceDir)
	current := a.csvContext(cfg, doc, regionKey)

	target := filepath.Clean(strings.TrimSpace(path))
	conf := normalizeCSVConfig(cfg.CSVDirs[regionKey])
	matched := ""
	for _, dir := range conf.Dirs {
		if strings.EqualFold(dir, target) {
			matched = dir
			break
		}
	}
	if matched == "" {
		return current, errors.New("该词条目录不在当前区服的列表中")
	}
	if deleteFiles {
		if err := removeCSVDirectoryFiles(resourceDir, matched); err != nil {
			return current, err
		}
	}

	kept := make([]string, 0, len(conf.Dirs))
	for _, dir := range conf.Dirs {
		if dir != matched {
			kept = append(kept, dir)
		}
	}
	conf.Dirs = kept
	if strings.EqualFold(conf.Selected, matched) {
		conf.Selected = ""
	}
	for key := range conf.Labels {
		if strings.EqualFold(key, matched) {
			delete(conf.Labels, key)
		}
	}
	cfg.CSVDirs[regionKey] = normalizeCSVConfig(conf)
	if err := a.SaveConfig(cfg); err != nil {
		return a.csvContext(cfg, doc, regionKey), err
	}
	return a.csvContext(cfg, doc, regionKey), nil
}
