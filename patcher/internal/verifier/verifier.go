package verifier

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

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
	Plan         Plan     `json:"plan"`
	TextPatched  bool     `json:"textPatched"`
	FontPatched  bool     `json:"fontPatched"`
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

type BackupMeta struct {
	Region    string              `json:"region"`
	Version   string              `json:"version"`
	Files     map[string]FileHash `json:"files"`
	UpdatedAt string              `json:"updatedAt"`
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
// 1. 确保是原版：有可信备份先还原对应文件为原版，无备份但云端有则下载备份并还原；
// 2. 云端未收录且无本地备份（未知版本）时，弹窗警告用户，确认后直接在当前文件写入且不建立备份；
// 3. 执行汉化替换。
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
		return nil, err
	}
	result.Plan.GameVersion = version
	cfg.log("[校验] 正在校验游戏客户端与备份完整性（版本 %s）...", version)

	meta, err := readBackupMeta(backupDir)
	if err != nil {
		return nil, err
	}

	hasBackup := IsBackupComplete(meta, backupDir, version)

	if hasBackup {
		cfg.log("[备份] 本地已存在版本 %s 的可信原版备份，汉化前先确保还原至原版...", version)
		if cfg.ReplaceText {
			cfg.log("[文本] 正在从本地备份还原官方原版文本...")
			if err := RestoreFiles(backupDir, sqpackDir, TextFiles, meta); err != nil {
				return nil, fmt.Errorf("从本地备份还原文本原文件失败: %w", err)
			}
			result.RestoredText = true
		}
		if cfg.ReplaceFont {
			cfg.log("[字体] 正在从本地备份还原官方原版字体...")
			if err := RestoreFiles(backupDir, sqpackDir, FontFiles, meta); err != nil {
				return nil, fmt.Errorf("从本地备份还原字体原文件失败: %w", err)
			}
			result.RestoredFont = true
		}
	} else {
		// 本地没有当前版本的可信备份，尝试云端
		var release *Release
		if reg == region.Global && cfg.Cloud != nil {
			rel, relErr := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
			if relErr == nil && rel != nil && hasReleaseFiles(rel, AllFiles) {
				release = rel
			}
		}

		if release != nil {
			cfg.log("[云端] 检测到云端已收录版本 %s 的官方原版，正在下载建立本地备份...", version)
			downloadedMeta, dlErr := downloadAndCreateBackup(ctx, cfg, release, backupDir, version, string(reg), result)
			if dlErr != nil {
				return nil, fmt.Errorf("从云端建立原版备份失败: %w", dlErr)
			}
			meta = downloadedMeta

			if cfg.ReplaceText {
				cfg.log("[文本] 正在还原官方原版文本...")
				if err := RestoreFiles(backupDir, sqpackDir, TextFiles, meta); err != nil {
					return nil, fmt.Errorf("还原文本原文件失败: %w", err)
				}
				result.RestoredText = true
			}
			if cfg.ReplaceFont {
				cfg.log("[字体] 正在还原官方原版字体...")
				if err := RestoreFiles(backupDir, sqpackDir, FontFiles, meta); err != nil {
					return nil, fmt.Errorf("还原字体原文件失败: %w", err)
				}
				result.RestoredFont = true
			}
		} else {
			// 云端未收录 + 游戏已更新 = 未知版本
			note := fmt.Sprintf("云端尚未收录当前游戏版本 (%s) 的官方原版备份", version)
			cfg.log("[提示] %s", note)
			result.Plan.Notes = append(result.Plan.Notes, note)

			if cfg.ConfirmUnknown == nil {
				return nil, fmt.Errorf("当前游戏版本 (%s) 无法确认是否为原版，缺少确认回调", version)
			}
			ok, confirmErr := cfg.ConfirmUnknown(ctx, version)
			if confirmErr != nil {
				return nil, confirmErr
			}
			if !ok {
				result.Cancelled = true
				return result, nil
			}
			cfg.log("[提示] 用户已确认在未知版本状态下继续汉化，本次不进行文件备份")
		}
	}

	// 执行写入
	if cfg.ReplaceText && cfg.ApplyText != nil {
		cfg.event("text", "正在替换文本", 0, 0)
		if err := cfg.ApplyText(ctx); err != nil {
			return nil, fmt.Errorf("文本汉化失败: %w", err)
		}
		result.TextPatched = true
	}

	if cfg.ReplaceFont && cfg.ApplyFont != nil {
		cfg.event("font", "正在替换字体", 0, 0)
		if err := cfg.ApplyFont(ctx); err != nil {
			return nil, fmt.Errorf("字体汉化失败: %w", err)
		}
		result.FontPatched = true
	}

	return result, nil
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
		Files:   make(map[string]FileHash),
	}

	for _, name := range AllFiles {
		cloudSHA, hasSHA := release.AssetSHA256(name)
		asset, hasAsset := release.Assets[name]
		if !hasSHA || !hasAsset || asset.DownloadURL == "" {
			return nil, fmt.Errorf("云端缺少文件 %s", name)
		}
		backupPath := filepath.Join(backupDir, name)
		cfg.event("download", "正在下载 "+name, 0, 0)
		cfg.log("[下载] 正在从云端拉取官方原版 %s...", name)

		err := cfg.Cloud.Download(ctx, asset.DownloadURL, backupPath, func(done, total int64) {
			cfg.event("download", name, done, total)
		})
		if err != nil {
			return nil, fmt.Errorf("下载原版 %s 失败: %w", name, err)
		}

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

func RestoreFiles(backupDir, sqpackDir string, files []string, meta *BackupMeta) error {
	for _, name := range restoreSequence(files) {
		src := filepath.Join(backupDir, name)
		dst := filepath.Join(sqpackDir, name)
		if _, err := os.Stat(src); err != nil {
			return fmt.Errorf("备份文件缺失 (%s): %w", name, err)
		}
		want := ""
		if meta != nil {
			if h, ok := meta.Files[name]; ok {
				want = h.SHA256
			}
		}
		if want == "" {
			return fmt.Errorf("备份清单中缺少 %s 的哈希记录", name)
		}
		if dstHash, err := HashFile(dst); err == nil && strings.EqualFold(want, dstHash.SHA256) {
			continue
		}
		if _, err := copyFile(src, dst, want); err != nil {
			return fmt.Errorf("还原 %s 失败: %w", name, err)
		}
	}
	return nil
}

func copyFile(src, dst, wantSHA string) (FileHash, error) {
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
	size, err := io.Copy(io.MultiWriter(out, h), in)
	if err != nil {
		_ = out.Close()
		_ = os.Remove(tmp)
		return FileHash{}, err
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
	OnProgress func(stage, detail string, done, total int64)
	Log        func(format string, args ...any)
}

func (c *RestoreConfig) log(format string, args ...any) {
	if c.Log != nil {
		c.Log(format, args...)
	}
}

// Restore 手动恢复全部 6 个核心文件为官方原版。
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

	if !IsBackupComplete(meta, backupDir, version) {
		// 本地没有有效备份，如果是国际服尝试从云端下载
		var release *Release
		if reg == region.Global && cfg.Cloud != nil {
			rel, relErr := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
			if relErr == nil && rel != nil && hasReleaseFiles(rel, AllFiles) {
				release = rel
			}
		}

		if release == nil {
			return nil, fmt.Errorf("本地没有版本 %s 的原版备份，云端也未收录；请使用官方启动器修复客户端", version)
		}

		cfg.log("[还原] 正在从云端拉取官方原版以建立备份并还原...")
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
	}

	cfg.log("[还原] 正在将官方原版还原至客户端...")
	if err := RestoreFiles(backupDir, sqpackDir, AllFiles, meta); err != nil {
		return nil, err
	}

	restored := make([]string, len(AllFiles))
	copy(restored, AllFiles)
	sort.Strings(restored)
	return restored, nil
}

// RestoreToGameWithContext 是外部调用的便捷封装。
func RestoreToGameWithContext(ctx context.Context, gameDir, backupRoot string, lang region.Language,
	onProgress func(stage, detail string, done, total int64), logFn func(format string, args ...any)) ([]string, error) {
	if ctx == nil {
		ctx = context.Background()
	}
	return Restore(ctx, RestoreConfig{
		GameDir:    gameDir,
		BackupRoot: backupRoot,
		Language:   lang,
		OnProgress: onProgress,
		Log:        logFn,
	})
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
