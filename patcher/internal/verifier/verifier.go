package verifier

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

// DiagPrefix 标记诊断行，前端按这个前缀把日志降级显示。
// 改这里要同步 frontend/src/main.ts 的 DIAG_PREFIX。
const DiagPrefix = "[诊断] "

var (
	TextFiles = []string{"0a0000.win32.dat0", "0a0000.win32.index", "0a0000.win32.index2"}
	FontFiles = []string{"000000.win32.dat0", "000000.win32.index", "000000.win32.index2"}
	AllFiles  = append(append([]string{}, TextFiles...), FontFiles...)
)

type Plan struct {
	Region      region.Region `json:"region"`
	GameVersion string        `json:"gameVersion"`
	Notes       []string      `json:"notes"`
}

type Result struct {
	Plan        Plan `json:"plan"`
	TextPatched bool `json:"textPatched"`
	FontPatched bool `json:"fontPatched"`
	// RestoredText/RestoredFont 表示对应文件是否真的被覆盖还原过；
	// 客户端本来就是原版时不会写任何文件，两者为 false。
	RestoredText bool     `json:"restoredText"`
	RestoredFont bool     `json:"restoredFont"`
	Cancelled    bool     `json:"cancelled"`
	Downloaded   []string `json:"downloaded"`
	Warnings     []string `json:"warnings"`
}

type Config struct {
	GameDir     string
	BackupRoot  string
	Language    region.Language
	ReplaceText bool
	ReplaceFont bool
	Repo        string
	Cloud       Cloud

	ConfirmUnknown func(ctx context.Context, version string) (bool, error)
	ApplyText      func(ctx context.Context) error
	ApplyFont      func(ctx context.Context) error
	OnEvent        func(Event)
	Log            func(format string, args ...any)
}

type Event struct {
	Stage  string `json:"stage"`
	Detail string `json:"detail"`
	Done   int64  `json:"done"`
	Total  int64  `json:"total"`
}

const (
	BackupSourceCloud = "cloud"
	BackupSourceLocal = "local"
)

type BackupMeta struct {
	Region    string              `json:"region"`
	Version   string              `json:"version"`
	Source    string              `json:"source,omitempty"`
	Files     map[string]FileHash `json:"files"`
	UpdatedAt string              `json:"updatedAt"`
}

func (m *BackupMeta) IsVerifiedOriginal() bool {
	return m != nil && m.Source == BackupSourceCloud
}

func (c *Config) log(format string, args ...any) {
	if c.Log != nil {
		c.Log(format, args...)
	}
}

func (c *Config) event(stage, detail string, done, total int64) {
	if c.OnEvent != nil {
		c.OnEvent(Event{Stage: stage, Detail: detail, Done: done, Total: total})
	}
}

// Run 执行汉化主流程：
// 1. 确保基线一致：本地有可信原版备份时直接还原，否则国际服优先从云端下载可信原版；
// 2. 无法从云端核对（未知版本）时，优先复用既有同版本本地快照还原；首次遇到则警告并经确认后保存操作前本地快照；
// 3. 执行汉化替换。
//
// 第 1 步逐文件比对哈希，只覆盖真正被改动的文件；与基线一致时不会写入任何文件，
// 日志也会如实区分「官方原版」与「本地快照」。
//
// 出错时同样返回已填好 RestoredText/RestoredFont 的 Result，方便调用方如实说明
// 客户端到底被改动了什么（写入阶段的撤回由调用方负责）。
func Run(ctx context.Context, cfg Config) (*Result, error) {
	if cfg.GameDir == "" {
		return nil, fmt.Errorf("未设置游戏目录")
	}
	if cfg.BackupRoot == "" {
		return nil, fmt.Errorf("未设置备份目录")
	}
	if cfg.Repo == "" {
		cfg.Repo = "Souma-Sumire/FFXIVChnTextPatch-Souma"
	}

	reg := region.Of(cfg.Language)
	result := &Result{Plan: Plan{Region: reg}}
	sqpackDir := filepath.Join(cfg.GameDir, "game", "sqpack", "ffxiv")
	backupDir := filepath.Join(cfg.BackupRoot, string(reg))

	version, err := readGameVersion(cfg.GameDir)
	if err != nil {
		return result, err
	}
	result.Plan.GameVersion = version
	cfg.log("[校验] 客户端版本 %s（%s），核对原版备份...", version, region.DisplayName(reg))

	meta, err := readBackupMeta(backupDir)
	if err != nil {
		return result, err
	}

	hasBackup := IsBackupComplete(meta, backupDir, version)
	hasVerifiedBackup := hasBackup && meta.IsVerifiedOriginal()

	if hasVerifiedBackup {
		cfg.log("[备份] 命中本地原版备份 (%s)", version)
		result.RestoredText, result.RestoredFont, err = restoreOriginals(ctx, &cfg, backupDir, sqpackDir, meta)
		if err != nil {
			return result, err
		}
		if !result.RestoredText && !result.RestoredFont && (cfg.ReplaceText || cfg.ReplaceFont) {
			cfg.log("[校验] 客户端当前为官方原版，无需恢复")
		}
	} else {
		// 本地没有当前版本的可信原版备份，国际服先尝试从云端核对/升级。
		var release *Release
		var cloudErr error
		if reg == region.Global && cfg.Cloud != nil {
			rel, relErr := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
			if relErr == nil && rel != nil && hasReleaseFiles(rel, AllFiles) {
				release = rel
			} else if relErr != nil {
				cloudErr = relErr
			}
		}

		if release != nil {
			cfg.log("[备份] 下载 %s 原版文件...", version)
			downloadedMeta, dlErr := downloadAndCreateBackup(ctx, cfg, release, backupDir, version, string(reg), result)
			if dlErr != nil {
				return result, fmt.Errorf("从云端建立原版备份失败: %w", dlErr)
			}
			meta = downloadedMeta

			result.RestoredText, result.RestoredFont, err = restoreOriginals(ctx, &cfg, backupDir, sqpackDir, meta)
			if err != nil {
				return result, err
			}
			if !result.RestoredText && !result.RestoredFont && (cfg.ReplaceText || cfg.ReplaceFont) {
				cfg.log("[校验] 客户端当前为官方原版，无需恢复")
			}
		} else if hasBackup {
			cfg.log("[备份] 命中本地操作前快照 (%s)", version)
			result.RestoredText, result.RestoredFont, err = restoreOriginals(ctx, &cfg, backupDir, sqpackDir, meta)
			if err != nil {
				return result, err
			}
			if !result.RestoredText && !result.RestoredFont && (cfg.ReplaceText || cfg.ReplaceFont) {
				cfg.log("[校验] 客户端与操作前快照一致，无需恢复")
			}
		} else {
			// 云端未收录 或 网络失败 + 游戏已更新 = 未知版本
			var note string
			if cloudErr != nil && !errors.Is(cloudErr, ErrNotFound) {
				note = fmt.Sprintf("获取云端原版信息失败（%s），无法核对当前文件", CleanNetError(cloudErr))
				cfg.log(DiagPrefix+"云端原版查询原始错误：%v", cloudErr)
				cfg.log("[警告] %s", note)
			} else if reg == region.Global {
				note = fmt.Sprintf("云端暂未收录版本 %s", version)
				cfg.log("[警告] %s", note)
			} else {
				note = fmt.Sprintf("尚无版本 %s 的原版记录，无法确认是否被修改", version)
				cfg.log("[警告] %s", note)
			}
			result.Plan.Notes = append(result.Plan.Notes, note)

			if cfg.ConfirmUnknown == nil {
				return result, fmt.Errorf("无法确认版本 %s 是否为原版，缺少确认回调", version)
			}
			ok, confirmErr := cfg.ConfirmUnknown(ctx, version)
			if confirmErr != nil {
				return result, confirmErr
			}
			if !ok {
				result.Cancelled = true
				return result, nil
			}
			cfg.log("[备份] 正在备份当前客户端文件...")
			createdMeta, backupErr := createLocalBackupFromGameDir(ctx, cfg, sqpackDir, backupDir, version, string(reg))
			if backupErr != nil {
				return result, fmt.Errorf("备份当前文件失败: %w", backupErr)
			}
			meta = createdMeta
		}
	}

	// 执行写入
	if cfg.ReplaceText && cfg.ApplyText != nil {
		cfg.event("text", "正在替换文本", 0, 0)
		if err := cfg.ApplyText(ctx); err != nil {
			return result, fmt.Errorf("文本汉化失败: %w", err)
		}
		result.TextPatched = true
	}

	if cfg.ReplaceFont && cfg.ApplyFont != nil {
		cfg.event("font", "正在替换字体", 0, 0)
		if err := cfg.ApplyFont(ctx); err != nil {
			return result, fmt.Errorf("字体汉化失败: %w", err)
		}
		result.FontPatched = true
	}

	return result, nil
}

func createLocalBackupFromGameDir(ctx context.Context, cfg Config, sqpackDir, backupDir, version, regionName string) (*BackupMeta, error) {
	if err := os.MkdirAll(backupDir, 0o755); err != nil {
		return nil, fmt.Errorf("创建备份目录失败: %w", err)
	}
	meta := &BackupMeta{
		Region:  regionName,
		Version: version,
		Source:  BackupSourceLocal,
		Files:   make(map[string]FileHash),
	}
	for i, name := range AllFiles {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		src := filepath.Join(sqpackDir, name)
		dst := filepath.Join(backupDir, name)
		if _, err := os.Stat(src); err != nil {
			return nil, fmt.Errorf("客户端缺失文件 %s: %w", name, err)
		}
		cfg.event("backup", fmt.Sprintf("正在备份原版文件 (%d/%d): %s", i+1, len(AllFiles), name), 0, 0)
		cfg.log(DiagPrefix+"备份文件 %s", name)
		got, err := copyFile(ctx, src, dst, "")
		if err != nil {
			return nil, fmt.Errorf("提取 %s 到备份目录失败: %w", name, err)
		}
		meta.Files[name] = got
	}
	if err := writeBackupMeta(backupDir, meta); err != nil {
		return nil, fmt.Errorf("保存备份清单失败: %w", err)
	}
	cfg.log("[备份] 已备份当前文件 (%s)", version)
	return meta, nil
}

// restoreOriginals 在汉化前把本次要处理的文件组还原为官方原版。
// 返回值表示该组是否真的覆盖过客户端文件——本来就是原版时一个字节都不会写。
func restoreOriginals(ctx context.Context, cfg *Config, backupDir, sqpackDir string, meta *BackupMeta) (textRestored, fontRestored bool, err error) {
	if cfg.ReplaceText {
		textRestored, err = restoreGroup(ctx, cfg, backupDir, sqpackDir, TextFiles, meta, "文本")
		if err != nil {
			return textRestored, fontRestored, restoreFailure("文本", textRestored, err)
		}
	}
	if cfg.ReplaceFont {
		fontRestored, err = restoreGroup(ctx, cfg, backupDir, sqpackDir, FontFiles, meta, "字体")
		if err != nil {
			return textRestored, fontRestored, restoreFailure("字体", fontRestored, err)
		}
	}
	return textRestored, fontRestored, nil
}

// restoreFailure 如实描述还原中断时客户端的状态：已经覆盖过的文件不会因为报错而消失。
func restoreFailure(label string, restored bool, err error) error {
	state := "客户端文件未被修改"
	if restored {
		state = "部分文件已还原，客户端可能处于原版与汉化混合状态"
	}
	return fmt.Errorf("从本地备份还原%s原文件失败（%s）: %w", label, state, err)
}

// restoreGroup 还原一组文件，并且只说实际发生的事：
// 客户端已是原版时提示无需还原，真的覆盖了文件才逐条提示正在还原。
func restoreGroup(ctx context.Context, cfg *Config, backupDir, sqpackDir string, files []string, meta *BackupMeta, label string) (bool, error) {
	restored, err := RestoreFiles(ctx, backupDir, sqpackDir, files, meta, func(name string) {
		cfg.event("restore", "正在还原 "+name, 0, 0)
		cfg.log(DiagPrefix+"[%s] 还原 %s", label, name)
	})
	if err != nil {
		// 已经覆盖过的文件不能对外声称「未修改」：调用方据此判断客户端是否处于半还原状态。
		return len(restored) > 0, err
	}
	if len(restored) == 0 {
		if meta.IsVerifiedOriginal() {
			cfg.log(DiagPrefix+"[%s] 客户端%s已是原版，无需还原", label, label)
		} else {
			cfg.log(DiagPrefix+"[%s] 客户端%s与操作前快照一致，无需还原", label, label)
		}
		return false, nil
	}
	if meta.IsVerifiedOriginal() {
		cfg.log("[%s] 已从备份还原 %d 个原版文件", label, len(restored))
	} else {
		cfg.log("[%s] 已从备份还原 %d 个文件", label, len(restored))
	}
	return true, nil
}

func readGameVersion(gameDir string) (string, error) {
	path := filepath.Join(gameDir, "game", "ffxivgame.ver")
	data, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("读取 ffxivgame.ver 失败: %w", err)
	}
	v := strings.TrimSpace(string(data))
	if v == "" {
		return "", fmt.Errorf("ffxivgame.ver 内容为空")
	}
	return v, nil
}

func DetectClientRegion(gameDir string) (region.Region, error) {
	if gameDir == "" {
		return "", errors.New("游戏目录为空")
	}

	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	idxPath := filepath.Join(sqpackDir, "0a0000.win32.index")
	idx, err := sqpack.OpenIndex(idxPath)
	if err != nil {
		return "", fmt.Errorf("读取索引文件失败: %w", err)
	}

	if idx.Lookup("exd", "addon_0_chs.exd") != nil {
		return region.China, nil
	}
	if idx.Lookup("exd", "addon_0_ko.exd") != nil {
		return region.Korea, nil
	}
	if idx.Lookup("exd", "addon_0_tc.exd") != nil {
		return region.Taiwan, nil
	}
	if idx.Lookup("exd", "addon_0_ja.exd") != nil || idx.Lookup("exd", "addon_0_en.exd") != nil ||
		idx.Lookup("exd", "addon_0_de.exd") != nil || idx.Lookup("exd", "addon_0_fr.exd") != nil ||
		idx.Lookup("exd", "addon_0_cht.exd") != nil {
		return region.Global, nil
	}

	return "", errors.New("无法确定客户端所属区服")
}

func IsBackupComplete(meta *BackupMeta, backupDir string, version string) bool {
	if meta == nil || meta.Version != version || len(meta.Files) == 0 {
		return false
	}
	for _, name := range AllFiles {
		want, ok := meta.Files[name]
		if !ok || want.SHA256 == "" {
			return false
		}
		p := filepath.Join(backupDir, name)
		fi, err := os.Stat(p)
		if err != nil || fi.Size() != want.Size {
			return false
		}
		got, err := HashFile(p)
		if err != nil || !strings.EqualFold(got.SHA256, want.SHA256) {
			return false
		}
	}
	return true
}

func downloadAndCreateBackup(ctx context.Context, cfg Config, release *Release, backupDir, version, regionName string, result *Result) (*BackupMeta, error) {
	meta := &BackupMeta{
		Region:  regionName,
		Version: version,
		Source:  BackupSourceCloud,
		Files:   make(map[string]FileHash),
	}

	for _, name := range AllFiles {
		cloudSHA, hasSHA := release.AssetSHA256(name)
		asset, hasAsset := release.Assets[name]
		if !hasSHA || !hasAsset || asset.DownloadURL == "" {
			return nil, fmt.Errorf("云端缺少文件 %s", name)
		}
		backupPath := filepath.Join(backupDir, name)
		cfg.event("download", name, 0, 0)
		cfg.log(DiagPrefix+"下载原版 %s", name)

		err := cfg.Cloud.Download(ctx, asset.DownloadURL, backupPath, func(done, total int64) {
			cfg.event("download", name, done, total)
		})
		if err != nil {
			return nil, fmt.Errorf("下载原版 %s 失败: %w", name, err)
		}

		cfg.event("verify", "正在校验 "+name, 0, 0)
		got, err := HashFile(backupPath)
		if err != nil {
			_ = os.Remove(backupPath)
			return nil, fmt.Errorf("校验 %s 哈希失败: %w", name, err)
		}
		if !strings.EqualFold(got.SHA256, cloudSHA) {
			_ = os.Remove(backupPath)
			return nil, fmt.Errorf("下载原版 %s 后 SHA256 校验失败（期望 %s，实际 %s）", name, shortHash(cloudSHA), shortHash(got.SHA256))
		}
		meta.Files[name] = got
		if result != nil {
			result.Downloaded = append(result.Downloaded, name)
		}
	}

	if err := writeBackupMeta(backupDir, meta); err != nil {
		return nil, fmt.Errorf("保存备份清单失败: %w", err)
	}
	return meta, nil
}

func restoreSequence(files []string) []string {
	ordered := make([]string, 0, len(files))
	for _, name := range files {
		if !strings.HasSuffix(name, ".dat0") {
			ordered = append(ordered, name)
		}
	}
	for _, name := range files {
		if strings.HasSuffix(name, ".dat0") {
			ordered = append(ordered, name)
		}
	}
	return ordered
}

// RestoreFiles 把 files 中与备份哈希不一致的文件从备份覆盖回客户端，返回实际写入的文件名。
// 已经和备份一致的文件直接跳过、不产生写入；onRestore 在每次真正开始覆盖前回调（可为 nil）。
func RestoreFiles(ctx context.Context, backupDir, sqpackDir string, files []string, meta *BackupMeta, onRestore func(name string)) ([]string, error) {
	seq := restoreSequence(files)
	restored := make([]string, 0, len(seq))
	for _, name := range seq {
		if err := ctx.Err(); err != nil {
			return restored, err
		}
		src := filepath.Join(backupDir, name)
		dst := filepath.Join(sqpackDir, name)
		if _, err := os.Stat(src); err != nil {
			return restored, fmt.Errorf("备份文件缺失 (%s): %w", name, err)
		}
		want := ""
		if meta != nil {
			if h, ok := meta.Files[name]; ok {
				want = h.SHA256
			}
		}
		if want == "" {
			return restored, fmt.Errorf("备份清单中缺少 %s 的哈希记录", name)
		}
		if dstHash, err := HashFile(dst); err == nil && strings.EqualFold(want, dstHash.SHA256) {
			continue
		}
		if onRestore != nil {
			onRestore(name)
		}
		if _, err := copyFile(ctx, src, dst, want); err != nil {
			return restored, fmt.Errorf("还原 %s 失败: %w", name, err)
		}
		restored = append(restored, name)
	}
	return restored, nil
}

func copyFile(ctx context.Context, src, dst, wantSHA string) (FileHash, error) {
	if err := ctx.Err(); err != nil {
		return FileHash{}, err
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return FileHash{}, err
	}
	in, err := os.Open(src)
	if err != nil {
		return FileHash{}, err
	}
	defer in.Close()

	tmp := dst + ".tmp"
	out, err := os.Create(tmp)
	if err != nil {
		return FileHash{}, err
	}
	h := sha256.New()
	buf := make([]byte, 1024*1024)
	var size int64
	for {
		if err := ctx.Err(); err != nil {
			_ = out.Close()
			_ = os.Remove(tmp)
			return FileHash{}, err
		}
		nr, er := in.Read(buf)
		if nr > 0 {
			nw, ew := out.Write(buf[0:nr])
			if nw < 0 || nr < nw {
				nw = 0
				if ew == nil {
					ew = errors.New("写入异常")
				}
			}
			h.Write(buf[0:nw])
			size += int64(nw)
			if ew != nil {
				_ = out.Close()
				_ = os.Remove(tmp)
				return FileHash{}, ew
			}
			if nr != nw {
				_ = out.Close()
				_ = os.Remove(tmp)
				return FileHash{}, io.ErrShortWrite
			}
		}
		if er != nil {
			if er != io.EOF {
				_ = out.Close()
				_ = os.Remove(tmp)
				return FileHash{}, er
			}
			break
		}
	}
	if err := out.Sync(); err != nil {
		_ = out.Close()
		_ = os.Remove(tmp)
		return FileHash{}, err
	}
	if err := out.Close(); err != nil {
		_ = os.Remove(tmp)
		return FileHash{}, err
	}

	got := FileHash{SHA256: hex.EncodeToString(h.Sum(nil)), Size: size}
	if wantSHA != "" && !strings.EqualFold(got.SHA256, wantSHA) {
		_ = os.Remove(tmp)
		return FileHash{}, fmt.Errorf("复制 %s 后哈希校验失败（期望 %s，实际 %s）", filepath.Base(src), shortHash(wantSHA), shortHash(got.SHA256))
	}
	if err := os.Rename(tmp, dst); err != nil {
		_ = os.Remove(tmp)
		return FileHash{}, err
	}
	return got, nil
}

func shortHash(h string) string {
	if len(h) > 8 {
		return h[:8]
	}
	return h
}

func hasReleaseFiles(release *Release, files []string) bool {
	if release == nil {
		return false
	}
	for _, name := range files {
		if _, ok := release.AssetSHA256(name); !ok {
			return false
		}
	}
	return true
}

func hashAll(dir string, files []string) (map[string]FileHash, error) {
	out := make(map[string]FileHash, len(files))
	for _, name := range files {
		hash, err := HashFile(filepath.Join(dir, name))
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return nil, err
		}
		out[name] = hash
	}
	return out, nil
}

func hasFiles(files map[string]FileHash, names []string) bool {
	for _, name := range names {
		if _, ok := files[name]; !ok {
			return false
		}
	}
	return true
}

type RestoreConfig struct {
	GameDir    string
	BackupRoot string
	Language   region.Language
	Repo       string
	Cloud      Cloud
	// Text / Font 指定这次只还原哪一组文件；两个都为 false 时还原全部（兼容旧行为）。
	Text       bool
	Font       bool
	OnProgress func(stage, detail string, done, total int64)
	Log        func(format string, args ...any)
}

func (c *RestoreConfig) log(format string, args ...any) {
	if c.Log != nil {
		c.Log(format, args...)
	}
}

// Restore 手动恢复全部 6 个核心文件为官方原版，返回真正被覆盖的文件名。
// 客户端本来就与备份一致时返回空列表，不写任何文件。
func Restore(ctx context.Context, cfg RestoreConfig) ([]string, error) {
	if cfg.GameDir == "" {
		return nil, fmt.Errorf("未设置游戏目录")
	}
	if cfg.Repo == "" {
		cfg.Repo = "Souma-Sumire/FFXIVChnTextPatch-Souma"
	}
	reg := region.Of(cfg.Language)
	backupDir := filepath.Join(cfg.BackupRoot, region.BackupFolder(cfg.Language))
	sqpackDir := filepath.Join(cfg.GameDir, "game", "sqpack", "ffxiv")

	version, err := readGameVersion(cfg.GameDir)
	if err != nil {
		return nil, fmt.Errorf("读取当前客户端版本失败: %w", err)
	}

	meta, err := readBackupMeta(backupDir)
	if err != nil {
		return nil, err
	}

	hasBackup := IsBackupComplete(meta, backupDir, version)
	if !hasBackup || !meta.IsVerifiedOriginal() {
		// 本地没有可信原版备份时，国际服优先尝试从云端下载真正官方原版。
		var release *Release
		if reg == region.Global && cfg.Cloud != nil {
			rel, relErr := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
			if relErr == nil && rel != nil && hasReleaseFiles(rel, AllFiles) {
				release = rel
			}
		}

		if release != nil {
			cfg.log("[还原] 从云端下载 %s 原版备份...", version)
			downloadedMeta, dlErr := downloadAndCreateBackup(ctx, Config{
				Cloud: cfg.Cloud,
				Repo:  cfg.Repo,
				OnEvent: func(e Event) {
					if cfg.OnProgress != nil {
						cfg.OnProgress(e.Stage, e.Detail, e.Done, e.Total)
					}
				},
				Log: cfg.Log,
			}, release, backupDir, version, string(reg), nil)
			if dlErr != nil {
				return nil, fmt.Errorf("下载原版备份失败: %w", dlErr)
			}
			meta = downloadedMeta
		} else if hasBackup {
			cfg.log("[备份] 使用本地操作前快照 (%s)", version)
		} else {
			return nil, fmt.Errorf("本地没有版本 %s 的原版备份，云端也未收录；请使用官方启动器修复客户端", version)
		}
	} else {
		cfg.log("[备份] 使用本地原版备份 (%s)", version)
	}

	// 只还原用户勾选的那一组：两个都没勾时按"全部还原"处理（旧调用方兼容）。
	files := AllFiles
	if cfg.Text || cfg.Font {
		files = nil
		if cfg.Text {
			files = append(files, TextFiles...)
		}
		if cfg.Font {
			files = append(files, FontFiles...)
		}
	}

	// 逐文件比对，只覆盖真正被改动的文件；客户端本来就是原版时返回空列表。
	restored, err := RestoreFiles(ctx, backupDir, sqpackDir, files, meta, func(name string) {
		if cfg.OnProgress != nil {
			cfg.OnProgress("restore", "正在还原 "+name, 0, 0)
		}
		cfg.log(DiagPrefix+"[还原] 覆盖 %s", name)
	})
	if err != nil {
		return nil, err
	}
	sort.Strings(restored)
	return restored, nil
}

func readBackupMeta(backupDir string) (*BackupMeta, error) {
	data, err := os.ReadFile(filepath.Join(backupDir, "meta.json"))
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, err
	}
	var meta BackupMeta
	if err := json.Unmarshal(data, &meta); err != nil {
		return nil, fmt.Errorf("解析备份清单失败: %w", err)
	}
	if meta.Files == nil {
		meta.Files = map[string]FileHash{}
	}
	return &meta, nil
}

func writeBackupMeta(backupDir string, meta *BackupMeta) error {
	meta.UpdatedAt = time.Now().UTC().Format(time.RFC3339)
	if err := os.MkdirAll(backupDir, 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(meta, "", "  ")
	if err != nil {
		return err
	}
	targetPath := filepath.Join(backupDir, "meta.json")
	tmpPath := targetPath + ".tmp"
	if err := os.WriteFile(tmpPath, append(data, '\n'), 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmpPath, targetPath); err != nil {
		_ = os.Remove(tmpPath)
		return fmt.Errorf("写入备份清单失败: %w", err)
	}
	return nil
}

// CleanNetError 将可确认的网络错误归纳为简短提示；其余错误保留精简后的原始摘要，避免把 HTTP 限流或解析失败误报成网络异常。
func CleanNetError(err error) string {
	if err == nil {
		return ""
	}
	lower := strings.ToLower(err.Error())
	var netErr net.Error
	if errors.Is(err, context.DeadlineExceeded) || (errors.As(err, &netErr) && netErr.Timeout()) ||
		strings.Contains(lower, "context deadline exceeded") ||
		strings.Contains(lower, "timed out") ||
		strings.Contains(lower, "i/o timeout") {
		return "网络超时"
	}
	if strings.Contains(lower, "connection refused") {
		return "连接被拒绝"
	}
	if strings.Contains(lower, "no such host") {
		return "域名解析失败"
	}
	if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) || strings.Contains(lower, "connection reset") {
		return "连接中断"
	}
	summary := strings.Join(strings.Fields(err.Error()), " ")
	runes := []rune(summary)
	if len(runes) > 120 {
		return string(runes[:120]) + "…"
	}
	return summary
}
