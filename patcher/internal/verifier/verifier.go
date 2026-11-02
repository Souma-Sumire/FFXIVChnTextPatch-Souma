package verifier

import (
	"context"
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

// 需要管理的 6 个客户端原文件。
var (
	TextFiles = []string{"0a0000.win32.dat0", "0a0000.win32.index", "0a0000.win32.index2"}
	FontFiles = []string{"000000.win32.dat0", "000000.win32.index", "000000.win32.index2"}
	AllFiles  = append(append([]string{}, TextFiles...), FontFiles...)
)

// CloudState 描述本地版本与云端备份版本的关系。
type CloudState string

const (
	CloudMatched   CloudState = "matched"   // 本地版本 == 云端备份版本
	CloudPending   CloudState = "pending"   // 云端尚未备份当前版本（本地更新或更旧）
	CloudLocalOnly CloudState = "localOnly" // 无法访问云端，按本地文件处理
)

// Action 描述文件处理动作。
type Action string

const (
	ActionNone     Action = "none"
	ActionPatch    Action = "patch"
	ActionRestore  Action = "restorePatch"
	ActionDownload Action = "downloadRestorePatch"
	ActionSkip     Action = "skip"
)

// FilePlan 描述一组文件的处理计划。
type FilePlan struct {
	Enabled bool     `json:"enabled"`
	Action  Action   `json:"action"`
	Files   []string `json:"files"`
}

// Plan 是前端可见的处理计划。
type Plan struct {
	Region          region.Region `json:"region"`
	GameVersion     string        `json:"gameVersion"`
	CloudTag        string        `json:"cloudTag"`
	CloudState      CloudState    `json:"cloudState"`
	Text            FilePlan      `json:"text"`
	Font            FilePlan      `json:"font"`
	NeedFontConfirm bool          `json:"needFontConfirm"`
	Notes           []string      `json:"notes"`
}

// Result 是执行结果。
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

// Config 是校验与汉化流程的配置。
type Config struct {
	GameDir     string
	BackupRoot  string
	Language    region.Language
	ReplaceText bool
	ReplaceFont bool
	Repo        string // Souma-Sumire/FFXIVChnTextPatch-Souma
	Cloud       Cloud

	ApplyText func(ctx context.Context) error
	ApplyFont func(ctx context.Context) error

	// ConfirmFont 用于“官方原版字体但未勾选替换字体”的确认弹窗。
	ConfirmFont func(ctx context.Context) (bool, error)
	OnEvent     func(Event)
	Log         func(format string, args ...any)
}

// Event 是进度事件。
type Event struct {
	Stage  string `json:"stage"`
	Detail string `json:"detail"`
	Done   int64  `json:"done"`
	Total  int64  `json:"total"`
}

// BackupMeta 是本地备份清单。
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

func (c *Config) event(stage, detail string) {
	if c.OnEvent != nil {
		c.OnEvent(Event{Stage: stage, Detail: detail})
	}
}

// Run 执行完整的版本校验、原文件还原与汉化流程。
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
	cfg.log("[校验] 正在校验游戏客户端与备份完整性 (版本: %s)...", version)

	gameHashes, err := hashAll(sqpackDir, AllFiles)
	if err != nil {
		return nil, err
	}
	meta, err := readBackupMeta(backupDir)
	if err != nil {
		return nil, err
	}

	var release *Release
	cloudState := CloudLocalOnly
	if reg == region.Global && cfg.Cloud != nil {
		release, cloudState = resolveCloud(ctx, cfg, version, result)
	} else {
		result.Plan.CloudState = CloudLocalOnly
	}

	originalText, originalFont := determineOriginal(reg, cloudState, version, meta, gameHashes, release, sqpackDir)

	// 计划文本动作。
	textPlan := FilePlan{Enabled: cfg.ReplaceText, Action: ActionNone, Files: TextFiles}
	if cfg.ReplaceText {
		switch {
		case originalText:
			textPlan.Action = ActionPatch
		case reg == region.Global && cloudState == CloudMatched && metaMatchesRelease(meta, release, TextFiles):
			textPlan.Action = ActionRestore
		case reg == region.Global && cloudState == CloudMatched && hasReleaseFiles(release, TextFiles):
			textPlan.Action = ActionDownload
		case meta != nil && meta.Version == version && hasFiles(meta.Files, TextFiles):
			textPlan.Action = ActionRestore
		default:
			return nil, fmt.Errorf("当前游戏文本数据已被修改或汉化，且本地/云端暂无当前版本 (%s) 的官方原版备份，无法安全汉化；请使用官方启动器修复客户端后再试", version)
		}
	}

	// 计划字体动作。
	fontPlan := FilePlan{Enabled: cfg.ReplaceFont, Action: ActionNone, Files: FontFiles}
	if cfg.ReplaceFont {
		switch {
		case originalFont:
			fontPlan.Action = ActionPatch
		case reg == region.Global && cloudState == CloudMatched && metaMatchesRelease(meta, release, FontFiles):
			fontPlan.Action = ActionRestore
		case reg == region.Global && cloudState == CloudMatched && hasReleaseFiles(release, FontFiles):
			fontPlan.Action = ActionDownload
		case meta != nil && meta.Version == version && hasFiles(meta.Files, FontFiles):
			fontPlan.Action = ActionRestore
		default:
			return nil, fmt.Errorf("当前游戏字体数据已被修改或汉化，且本地/云端暂无当前版本 (%s) 的官方原版备份，无法安全汉化；请使用官方启动器修复客户端后再试", version)
		}
	} else if reg != region.China && originalFont && cfg.ReplaceText {
		fontPlan.Action = ActionSkip
		result.Plan.NeedFontConfirm = true
	} else {
		fontPlan.Action = ActionSkip
	}

	result.Plan.Text = textPlan
	result.Plan.Font = fontPlan
	result.Plan.CloudState = cloudState
	if release != nil {
		result.Plan.CloudTag = release.Tag
	}

	if result.Plan.NeedFontConfirm {
		cfg.event("confirm", "等待用户确认是否在不替换字体的情况下继续")
		if cfg.ConfirmFont == nil {
			return nil, fmt.Errorf("需要用户确认字体处理方式，但未提供确认回调")
		}
		ok, err := cfg.ConfirmFont(ctx)
		if err != nil {
			return nil, err
		}
		if !ok {
			result.Cancelled = true
			return result, nil
		}
	}

	// 汉化前保障：确保全部 6 个官方原版文件均已在本地安全备份且哈希经过验证
	if err := ensureAllOriginalFilesBackedUp(ctx, cfg, result, reg, cloudState, version, meta, backupDir, sqpackDir, gameHashes, release, originalText, originalFont); err != nil {
		return nil, err
	}
	if refreshed, err := readBackupMeta(backupDir); err == nil && refreshed != nil {
		meta = refreshed
	}

	// 执行文本动作。
	if err := executeText(ctx, cfg, result, reg, cloudState, version, meta, backupDir, sqpackDir, gameHashes, release); err != nil {
		return nil, err
	}

	// 执行字体动作。
	if refreshed, err := readBackupMeta(backupDir); err == nil && refreshed != nil {
		meta = refreshed
	}
	if err := executeFont(ctx, cfg, result, reg, cloudState, version, meta, backupDir, sqpackDir, gameHashes, release); err != nil {
		return nil, err
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

func resolveCloud(ctx context.Context, cfg Config, version string, result *Result) (*Release, CloudState) {
	release, err := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
	if err != nil {
		if !errors.Is(err, ErrNotFound) {
			cfg.log("按版本查询 Release 失败: %v", err)
			result.Plan.Notes = append(result.Plan.Notes, "无法访问云端备份，已直接基于本地文件处理")
			return nil, CloudLocalOnly
		}
		recent, err := cfg.Cloud.RecentReleases(ctx, cfg.Repo, 5)
		if err != nil || len(recent) == 0 {
			result.Plan.Notes = append(result.Plan.Notes, "云端暂无可用原文件备份，已直接基于本地文件处理")
			return nil, CloudLocalOnly
		}
		best := recent[0]
		for _, r := range recent[1:] {
			if CompareVersion(r.Tag, best.Tag) > 0 {
				best = r
			}
		}
		release = best
	}

	if CompareVersion(version, release.Tag) == 0 {
		return release, CloudMatched
	}
	result.Plan.Notes = append(result.Plan.Notes,
		fmt.Sprintf("当前游戏版本 (%s) 与云端备份版本 (%s) 不一致，已直接基于本地文件汉化", version, release.Tag))
	return release, CloudPending
}

// DetectModifiedSqPack 检查客户端数据包是否存在已被修改/汉化的特征。
func DetectModifiedSqPack(sqpackDir string, reg region.Region) bool {
	if st, err := os.Stat(filepath.Join(sqpackDir, "000000.win32.dat0")); err == nil {
		if st.Size() > 90*1024*1024 {
			return true
		}
	}
	if reg == region.Global {
		idxPath := filepath.Join(sqpackDir, "0a0000.win32.index")
		idx, err := sqpack.OpenIndex(idxPath)
		if err != nil {
			return false
		}
		entry := idx.Lookup("exd", "addon_0_en.exd")
		if entry == nil {
			return false
		}
		datNum, off := sqpack.DatNumAndAlignedOffset(entry.DataOffset)
		datPath := filepath.Join(sqpackDir, fmt.Sprintf("0a0000.win32.dat%d", datNum))
		dat, err := sqpack.OpenDat(datPath)
		if err != nil {
			return false
		}
		defer dat.Close()
		content, err := dat.ExtractFile(off)
		if err != nil {
			return false
		}
		for _, r := range string(content) {
			if r >= 0x4e00 && r <= 0x9fa5 {
				return true
			}
		}
	}
	return false
}

// DetectClientRegion 根据客户端文件结构特征探测所属区服。
func DetectClientRegion(gameDir string) (region.Region, error) {
	if gameDir == "" {
		return "", errors.New("游戏目录为空")
	}
	dx11 := filepath.Join(gameDir, "game", "ffxiv_dx11.exe")
	dx9 := filepath.Join(gameDir, "game", "ffxiv.exe")
	if _, err := os.Stat(dx11); err != nil {
		if _, err := os.Stat(dx9); err != nil {
			return "", errors.New("无效的游戏目录：未找到 game/ffxiv_dx11.exe")
		}
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

func determineOriginal(reg region.Region, cloudState CloudState, version string, meta *BackupMeta,
	gameHashes map[string]FileHash, release *Release, sqpackDir string) (bool, bool) {
	if reg == region.Global {
		if meta != nil && meta.Version == version && hasFiles(meta.Files, TextFiles) {
			textOriginal := allMatchMeta(gameHashes, meta, TextFiles)
			fontOriginal := true
			if hasFiles(meta.Files, FontFiles) {
				fontOriginal = allMatchMeta(gameHashes, meta, FontFiles)
			} else if cloudState == CloudMatched && release != nil && hasReleaseFiles(release, FontFiles) {
				fontOriginal = allMatchRelease(gameHashes, release, FontFiles)
			}
			return textOriginal, fontOriginal
		}
		if cloudState == CloudMatched && release != nil {
			textOrig := true
			if hasReleaseFiles(release, TextFiles) {
				textOrig = allMatchRelease(gameHashes, release, TextFiles)
			} else {
				textOrig = !DetectModifiedSqPack(sqpackDir, reg)
			}
			fontOrig := true
			if hasReleaseFiles(release, FontFiles) {
				fontOrig = allMatchRelease(gameHashes, release, FontFiles)
			} else {
				fontOrig = !DetectModifiedSqPack(sqpackDir, reg)
			}
			return textOrig, fontOrig
		}
		if DetectModifiedSqPack(sqpackDir, reg) {
			return false, false
		}
		return true, true
	}
	if meta == nil || meta.Version != version {
		if DetectModifiedSqPack(sqpackDir, reg) {
			return false, false
		}
		return true, true
	}
	textOriginal := allMatchMeta(gameHashes, meta, TextFiles)
	fontOriginal := true
	if hasFiles(meta.Files, FontFiles) {
		fontOriginal = allMatchMeta(gameHashes, meta, FontFiles)
	}
	return textOriginal, fontOriginal
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

func executeText(ctx context.Context, cfg Config, result *Result, reg region.Region, cloudState CloudState,
	version string, meta *BackupMeta, backupDir, sqpackDir string, gameHashes map[string]FileHash, release *Release) error {
	plan := result.Plan.Text
	if !plan.Enabled || plan.Action == ActionNone {
		return nil
	}
	cfg.event("text", "正在准备文本原文件")
	switch plan.Action {
	case ActionPatch:
		cfg.log("[文本] 正在建立文本官方原版备份...")
		if reg == region.Global && cloudState == CloudMatched {
			if err := ensureBackupFromGame(cfg, backupDir, sqpackDir, version, meta, TextFiles, gameHashes); err != nil {
				return err
			}
		} else if reg == region.Global {
			if err := backupFromGameOverwrite(cfg, backupDir, sqpackDir, version, meta, AllFiles, gameHashes); err != nil {
				return err
			}
		} else {
			if err := ensureBackupFromGame(cfg, backupDir, sqpackDir, version, meta, TextFiles, gameHashes); err != nil {
				return err
			}
		}
	case ActionRestore:
		cfg.log("[文本] 正在从本地备份还原官方原版文本...")
		if err := restoreFromBackup(backupDir, sqpackDir, TextFiles); err != nil {
			return fmt.Errorf("从本地备份还原文本原文件失败: %w", err)
		}
		result.RestoredText = true
	case ActionDownload:
		cfg.log("[文本] 正在从云端拉取官方原版文本数据包...")
		downloaded, err := ensureBackupForRelease(ctx, cfg, result, backupDir, sqpackDir, version, meta, TextFiles, gameHashes, release)
		if err != nil {
			return err
		}
		result.Downloaded = append(result.Downloaded, downloaded...)
		if err := restoreFromBackup(backupDir, sqpackDir, TextFiles); err != nil {
			return fmt.Errorf("还原下载的文本原文件失败: %w", err)
		}
		result.RestoredText = true
	}

	if plan.Action == ActionPatch || plan.Action == ActionRestore || plan.Action == ActionDownload {
		if cfg.ApplyText == nil {
			return fmt.Errorf("未配置文本汉化执行器")
		}
		cfg.event("text", "正在替换文本")
		if err := cfg.ApplyText(ctx); err != nil {
			return fmt.Errorf("文本汉化失败: %w", err)
		}
		result.TextPatched = true
	}
	return nil
}

func executeFont(ctx context.Context, cfg Config, result *Result, reg region.Region, cloudState CloudState,
	version string, meta *BackupMeta, backupDir, sqpackDir string, gameHashes map[string]FileHash, release *Release) error {
	plan := result.Plan.Font
	if !plan.Enabled {
		return nil
	}
	cfg.event("font", "正在准备字体原文件")
	switch plan.Action {
	case ActionPatch:
		cfg.log("[字体] 正在建立字体官方原版备份...")
		if reg == region.Global && cloudState == CloudMatched {
			if err := ensureBackupFromGame(cfg, backupDir, sqpackDir, version, meta, FontFiles, gameHashes); err != nil {
				return err
			}
		} else if reg == region.Global {
			if err := backupFromGameOverwrite(cfg, backupDir, sqpackDir, version, meta, FontFiles, gameHashes); err != nil {
				return err
			}
		} else {
			if err := ensureBackupFromGame(cfg, backupDir, sqpackDir, version, meta, FontFiles, gameHashes); err != nil {
				return err
			}
		}
	case ActionRestore:
		cfg.log("[字体] 正在从本地备份还原官方原版字体...")
		if err := restoreFromBackup(backupDir, sqpackDir, FontFiles); err != nil {
			return fmt.Errorf("从本地备份还原字体原文件失败: %w", err)
		}
		result.RestoredFont = true
	case ActionDownload:
		cfg.log("[字体] 正在从云端拉取官方原版字体数据包...")
		downloaded, err := ensureBackupForRelease(ctx, cfg, result, backupDir, sqpackDir, version, meta, FontFiles, gameHashes, release)
		if err != nil {
			return err
		}
		result.Downloaded = append(result.Downloaded, downloaded...)
		if err := restoreFromBackup(backupDir, sqpackDir, FontFiles); err != nil {
			return fmt.Errorf("还原下载的字体原文件失败: %w", err)
		}
		result.RestoredFont = true
	}

	if cfg.ApplyFont == nil {
		return fmt.Errorf("未配置字体汉化执行器")
	}
	cfg.event("font", "正在替换字体")
	if err := cfg.ApplyFont(ctx); err != nil {
		return fmt.Errorf("字体汉化失败: %w", err)
	}
	result.FontPatched = true
	return nil
}

// ensureAllOriginalFilesBackedUp 在汉化前确保全部 6 个核心文件的官方原版均已被安全备份且哈希经过验证。
func ensureAllOriginalFilesBackedUp(ctx context.Context, cfg Config, result *Result, reg region.Region,
	cloudState CloudState, version string, meta *BackupMeta, backupDir, sqpackDir string,
	gameHashes map[string]FileHash, release *Release, originalText, originalFont bool) error {
	if meta == nil || meta.Version != version {
		diskMeta, _ := readBackupMeta(backupDir)
		if diskMeta != nil && diskMeta.Version == version {
			meta = diskMeta
		} else {
			meta = &BackupMeta{Region: filepath.Base(backupDir), Version: version, Files: map[string]FileHash{}}
		}
	}
	if meta.Files == nil {
		meta.Files = map[string]FileHash{}
	}

	for _, name := range AllFiles {
		backupPath := filepath.Join(backupDir, name)
		// 1. 检查本地备份是否已有当前版本的有效文件
		bh, hashErr := HashFile(backupPath)
		if hashErr == nil {
			if wantMeta, ok := meta.Files[name]; ok && strings.EqualFold(bh.SHA256, wantMeta.SHA256) {
				if reg == region.Global && cloudState == CloudMatched && release != nil {
					if cloudSHA, ok := release.AssetSHA256(name); ok {
						if strings.EqualFold(bh.SHA256, cloudSHA) {
							continue
						}
					} else {
						continue
					}
				} else {
					continue
				}
			}
		}

		// 2. 本地无有效备份，尝试从游戏目录提取官方原版
		gh, hasGameFile := gameHashes[name]
		isOrig := false
		if reg == region.Global && cloudState == CloudMatched && release != nil {
			if cloudSHA, ok := release.AssetSHA256(name); ok && hasGameFile {
				isOrig = strings.EqualFold(gh.SHA256, cloudSHA)
			} else {
				if strings.HasPrefix(name, "0a0000.") {
					isOrig = originalText
				} else {
					isOrig = originalFont
				}
			}
		} else {
			if strings.HasPrefix(name, "0a0000.") {
				isOrig = originalText
			} else {
				isOrig = originalFont
			}
		}

		if hasGameFile && isOrig {
			cfg.log("[备份] 正在将官方原版 %s 备份至本地...", name)
			if err := copyFile(filepath.Join(sqpackDir, name), backupPath); err != nil {
				return fmt.Errorf("备份 %s 失败: %w", name, err)
			}
			meta.Files[name] = gh
			continue
		}

		// 3. 游戏目录中已被修改（非原版）且当前操作需要修改该文件时，从云端拉取官方原版
		shouldDownload := (strings.HasPrefix(name, "0a0000.") && cfg.ReplaceText) || (strings.HasPrefix(name, "000000.") && cfg.ReplaceFont)
		if shouldDownload && reg == region.Global && cloudState == CloudMatched && release != nil {
			cloudSHA, hasSHA := release.AssetSHA256(name)
			asset, hasAsset := release.Assets[name]
			if hasSHA && hasAsset && asset.DownloadURL != "" {
				cfg.event("download", "正在下载 "+name)
				cfg.log("[备份] 正在从云端拉取官方原版 %s 并备份...", name)
				if err := cfg.Cloud.Download(ctx, asset.DownloadURL, backupPath, func(done, total int64) {
					if cfg.OnEvent != nil {
						cfg.OnEvent(Event{Stage: "download", Detail: name, Done: done, Total: total})
					}
				}); err != nil {
					return fmt.Errorf("下载原版 %s 失败: %w", name, err)
				}
				got, err := HashFile(backupPath)
				if err != nil {
					_ = os.Remove(backupPath)
					return err
				}
				if !strings.EqualFold(got.SHA256, cloudSHA) {
					_ = os.Remove(backupPath)
					return fmt.Errorf("下载原版 %s 后 SHA256 校验失败", name)
				}
				meta.Files[name] = got
				result.Downloaded = append(result.Downloaded, name)
				continue
			}
		}

		// 4. 若无法获取该文件的原版备份，且当前操作勾选了修改该文件，则报错终止
		if (strings.HasPrefix(name, "0a0000.") && cfg.ReplaceText) || (strings.HasPrefix(name, "000000.") && cfg.ReplaceFont) {
			return fmt.Errorf("当前游戏文件 %s 已被修改或汉化，且本地/云端暂无当前版本 (%s) 的官方原版备份，无法安全汉化；请使用官方启动器修复客户端后再试", name, version)
		}
	}

	return writeBackupMeta(backupDir, meta)
}

// ensureBackupFromGame 在原文件已确认可信时，把游戏目录文件复制到备份目录。
func ensureBackupFromGame(cfg Config, backupDir, sqpackDir, version string, meta *BackupMeta,
	files []string, gameHashes map[string]FileHash) error {
	if meta == nil || meta.Version != version {
		diskMeta, _ := readBackupMeta(backupDir)
		if diskMeta != nil && diskMeta.Version == version {
			meta = diskMeta
		} else {
			meta = &BackupMeta{Region: filepath.Base(backupDir), Version: version, Files: map[string]FileHash{}}
		}
	}
	if meta.Files == nil {
		meta.Files = map[string]FileHash{}
	}
	for _, name := range files {
		gh, ok := gameHashes[name]
		if !ok || gh.SHA256 == "" {
			return fmt.Errorf("游戏目录缺少文件: %s", name)
		}
		if bh, err := HashFile(filepath.Join(backupDir, name)); err == nil && bh.SHA256 == gh.SHA256 {
			meta.Files[name] = bh
			continue
		}
		if err := copyFile(filepath.Join(sqpackDir, name), filepath.Join(backupDir, name)); err != nil {
			return fmt.Errorf("备份 %s 失败: %w", name, err)
		}
		meta.Files[name] = gh
	}
	return writeBackupMeta(backupDir, meta)
}

// backupFromGameOverwrite 在云端未就绪时，用当前本地文件覆盖备份并更新版本。
func backupFromGameOverwrite(cfg Config, backupDir, sqpackDir, version string, meta *BackupMeta,
	files []string, gameHashes map[string]FileHash) error {
	if meta == nil || meta.Version != version {
		diskMeta, _ := readBackupMeta(backupDir)
		if diskMeta != nil && diskMeta.Version == version {
			meta = diskMeta
		} else {
			meta = &BackupMeta{Region: filepath.Base(backupDir), Version: version, Files: map[string]FileHash{}}
		}
	}
	if meta.Files == nil {
		meta.Files = map[string]FileHash{}
	}
	for _, name := range files {
		gh, ok := gameHashes[name]
		if !ok || gh.SHA256 == "" {
			continue
		}
		if bh, err := HashFile(filepath.Join(backupDir, name)); err == nil && bh.SHA256 == gh.SHA256 {
			meta.Files[name] = bh
			continue
		}
		if err := copyFile(filepath.Join(sqpackDir, name), filepath.Join(backupDir, name)); err != nil {
			return fmt.Errorf("备份 %s 失败: %w", name, err)
		}
		meta.Files[name] = gh
	}
	return writeBackupMeta(backupDir, meta)
}

// ensureBackupForRelease 保证备份目录中的文件与云端 Release 一致，必要时下载。
func ensureBackupForRelease(ctx context.Context, cfg Config, result *Result, backupDir, sqpackDir, version string,
	meta *BackupMeta, files []string, gameHashes map[string]FileHash, release *Release) ([]string, error) {
	if release == nil {
		return nil, fmt.Errorf("缺少云端 Release 信息")
	}
	if meta == nil || meta.Version != version {
		diskMeta, _ := readBackupMeta(backupDir)
		if diskMeta != nil && diskMeta.Version == version {
			meta = diskMeta
		} else {
			meta = &BackupMeta{Region: filepath.Base(backupDir), Version: version, Files: map[string]FileHash{}}
		}
	}
	if meta.Files == nil {
		meta.Files = map[string]FileHash{}
	}
	var downloaded []string
	for _, name := range files {
		want, ok := release.AssetSHA256(name)
		if !ok {
			return nil, fmt.Errorf("云端 Release %s 缺少文件 %s 的 SHA256", release.Tag, name)
		}
		backupPath := filepath.Join(backupDir, name)
		if bh, err := HashFile(backupPath); err == nil && bh.SHA256 == want {
			meta.Files[name] = bh
			continue
		}
		if gh, ok := gameHashes[name]; ok && gh.SHA256 == want {
			if err := copyFile(filepath.Join(sqpackDir, name), backupPath); err != nil {
				return nil, err
			}
			meta.Files[name] = gh
			continue
		}
		asset := release.Assets[name]
		if asset.DownloadURL == "" {
			return nil, fmt.Errorf("云端 Release %s 缺少文件 %s 的下载地址", release.Tag, name)
		}
		cfg.event("download", "正在下载 "+name)
		if err := cfg.Cloud.Download(ctx, asset.DownloadURL, backupPath, func(done, total int64) {
			if cfg.OnEvent != nil {
				cfg.OnEvent(Event{Stage: "download", Detail: name, Done: done, Total: total})
			}
		}); err != nil {
			return nil, fmt.Errorf("下载 %s 失败: %w", name, err)
		}
		got, err := HashFile(backupPath)
		if err != nil {
			return nil, err
		}
		if got.SHA256 != want {
			_ = os.Remove(backupPath)
			return nil, fmt.Errorf("下载 %s 后 SHA256 校验失败", name)
		}
		meta.Files[name] = got
		downloaded = append(downloaded, name)
	}
	if err := writeBackupMeta(backupDir, meta); err != nil {
		return nil, err
	}
	return downloaded, nil
}

func restoreFromBackup(backupDir, sqpackDir string, files []string) error {
	for _, name := range files {
		src := filepath.Join(backupDir, name)
		dst := filepath.Join(sqpackDir, name)
		if _, err := os.Stat(src); err != nil {
			return fmt.Errorf("备份文件缺失: %s", name)
		}
		srcHash, err := HashFile(src)
		if err == nil {
			dstHash, err := HashFile(dst)
			if err == nil && strings.EqualFold(srcHash.SHA256, dstHash.SHA256) {
				continue
			}
		}
		if err := copyFile(src, dst); err != nil {
			return err
		}
	}
	return nil
}

func copyFile(src, dst string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	tmp := dst + ".tmp"
	out, err := os.Create(tmp)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		_ = out.Close()
		_ = os.Remove(tmp)
		return err
	}
	if err := out.Close(); err != nil {
		return err
	}
	return os.Rename(tmp, dst)
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

func allMatchRelease(gameHashes map[string]FileHash, release *Release, files []string) bool {
	if release == nil {
		return false
	}
	for _, name := range files {
		want, ok := release.AssetSHA256(name)
		if !ok {
			return false
		}
		if gameHashes[name].SHA256 != want {
			return false
		}
	}
	return true
}

func allMatchMeta(gameHashes map[string]FileHash, meta *BackupMeta, files []string) bool {
	if meta == nil {
		return false
	}
	for _, name := range files {
		want, ok := meta.Files[name]
		if !ok {
			return false
		}
		if gameHashes[name].SHA256 != want.SHA256 {
			return false
		}
	}
	return true
}

func metaMatchesRelease(meta *BackupMeta, release *Release, files []string) bool {
	if meta == nil || release == nil {
		return false
	}
	for _, name := range files {
		want, ok := release.AssetSHA256(name)
		if !ok {
			return false
		}
		if meta.Files[name].SHA256 != want {
			return false
		}
	}
	return true
}

func hasFiles(files map[string]FileHash, names []string) bool {
	for _, name := range names {
		if _, ok := files[name]; !ok {
			return false
		}
	}
	return true
}

// RestoreConfig 提供原文件还原的执行配置。
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

// Restore 恢复客户端全部 6 个核心文件为官方原版。
// 优先比对游戏目录文件哈希，仅还原真正被修改过的文件以减少磁盘 IO。
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

	// 1. 本地无备份时，非国际服无法还原
	if reg != region.Global && (meta == nil || len(meta.Files) == 0) {
		return nil, fmt.Errorf("%s 的本地原文件备份不存在，且未收录云端原版下载；请先使用游戏启动器修复客户端", reg)
	}

	// 2. 本地备份版本不一致时，非国际服无法还原
	if reg != region.Global && meta != nil && meta.Version != "" && version != meta.Version {
		return nil, fmt.Errorf("当前游戏版本 (%s) 与本地备份版本 (%s) 不一致，游戏已更新；无法使用旧版本备份还原，请使用官方启动器修复客户端", version, meta.Version)
	}

	// 3. 准备当前版本的云端 Release（国际服可用时）
	var release *Release
	if reg == region.Global && cfg.Cloud != nil {
		rel, err := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
		if err == nil {
			release = rel
		}
	}

	if meta == nil || meta.Version != version {
		if meta == nil {
			meta = &BackupMeta{Region: string(reg), Version: version, Files: map[string]FileHash{}}
		} else {
			meta.Version = version
		}
	}

	// 4. 确保本地备份完整具备全部 6 个经过验证的官方原版文件
	for _, name := range AllFiles {
		backupPath := filepath.Join(backupDir, name)
		want, hasMeta := meta.Files[name]
		hasValidBackup := false
		if hasMeta {
			if bh, err := HashFile(backupPath); err == nil && bh.SHA256 != "" && strings.EqualFold(bh.SHA256, want.SHA256) {
				if reg == region.Global && release != nil {
					if cloudSHA, ok := release.AssetSHA256(name); ok {
						hasValidBackup = strings.EqualFold(bh.SHA256, cloudSHA)
					} else {
						hasValidBackup = true
					}
				} else {
					hasValidBackup = true
				}
			}
		}

		if hasValidBackup {
			continue
		}

		// 本地备份缺失或失效时：优先检查当前游戏目录中的文件是否正好是原版
		gamePath := filepath.Join(sqpackDir, name)
		gh, gErr := HashFile(gamePath)
		if gErr == nil && reg == region.Global && release != nil {
			if cloudSHA, ok := release.AssetSHA256(name); ok && strings.EqualFold(gh.SHA256, cloudSHA) {
				if err := copyFile(gamePath, backupPath); err == nil {
					meta.Files[name] = gh
					continue
				}
			}
		}

		// 从云端拉取官方原版（国际服）
		if reg == region.Global && cfg.Cloud != nil && release != nil {
			cloudSHA, hasSHA := release.AssetSHA256(name)
			asset, hasAsset := release.Assets[name]
			if hasSHA && hasAsset && asset.DownloadURL != "" {
				if cfg.OnProgress != nil {
					cfg.OnProgress("download", name, 0, 0)
				}
				cfg.log("[还原] 正在从云端拉取官方原版数据包: %s...", name)
				if err := cfg.Cloud.Download(ctx, asset.DownloadURL, backupPath, func(done, total int64) {
					if cfg.OnProgress != nil {
						cfg.OnProgress("download", name, done, total)
					}
				}); err != nil {
					return nil, fmt.Errorf("下载原版 %s 失败: %w", name, err)
				}
				got, err := HashFile(backupPath)
				if err != nil {
					_ = os.Remove(backupPath)
					return nil, fmt.Errorf("计算 %s 哈希失败: %w", name, err)
				}
				if !strings.EqualFold(got.SHA256, cloudSHA) {
					_ = os.Remove(backupPath)
					return nil, fmt.Errorf("下载 %s 后 SHA256 校验失败", name)
				}
				meta.Files[name] = got
				continue
			}
		}

		// 非国际服且缺失备份
		if reg != region.Global {
			if _, err := os.Stat(backupPath); err != nil {
				return nil, fmt.Errorf("本地备份文件缺失: %s", name)
			}
			if hasMeta {
				return nil, fmt.Errorf("本地备份文件 %s 现存哈希与汉化前记录不一致（可能已被外部修改或损坏），无法还原", name)
			}
			return nil, fmt.Errorf("本地备份中未收录文件 %s，无法还原", name)
		}

		return nil, fmt.Errorf("本地原版备份不存在，且无法从云端获取版本 %s 的原文件 %s；建议使用官方启动器设置中的【检查与修复游戏文件】恢复原版客户端", version, name)
	}

	_ = writeBackupMeta(backupDir, meta)

	// 5. 比对游戏目录哈希，只还原真正被修改过的文件以减少磁盘 IO
	return restoreLocalBackup(&cfg, backupDir, sqpackDir, meta, reg)
}

func restoreLocalBackup(cfg *RestoreConfig, backupDir, sqpackDir string, meta *BackupMeta, reg region.Region) ([]string, error) {
	if meta == nil || len(meta.Files) == 0 {
		return nil, fmt.Errorf("%s 的本地备份中没有可还原的文件", reg)
	}
	// 还原前校验：现存备份文件哈希必须与之前汉化前记录的哈希完全一致（确保未被外部修改或损坏）
	for name, want := range meta.Files {
		src := filepath.Join(backupDir, name)
		if _, err := os.Stat(src); err != nil {
			return nil, fmt.Errorf("本地备份文件缺失: %s", name)
		}
		got, err := HashFile(src)
		if err != nil {
			return nil, fmt.Errorf("计算本地备份文件 %s 哈希失败: %w", name, err)
		}
		if want.SHA256 != "" && !strings.EqualFold(got.SHA256, want.SHA256) {
			return nil, fmt.Errorf("本地备份文件 %s 现存哈希与汉化前记录不一致（可能已被外部修改或损坏），无法还原", name)
		}
	}

	if cfg != nil {
		cfg.log("[还原] 正在比对客户端全部 %d 个核心文件的哈希完整性...", len(AllFiles))
	}

	restored := make([]string, 0, len(AllFiles))
	unchanged := make([]string, 0, len(AllFiles))

	for _, name := range AllFiles {
		want, ok := meta.Files[name]
		if !ok {
			continue
		}
		src := filepath.Join(backupDir, name)
		dst := filepath.Join(sqpackDir, name)

		curHash, err := HashFile(dst)
		if err == nil && want.SHA256 != "" && strings.EqualFold(curHash.SHA256, want.SHA256) {
			unchanged = append(unchanged, name)
			continue
		}

		if err := copyFile(src, dst); err != nil {
			return nil, fmt.Errorf("还原文件 %s 失败: %w", name, err)
		}
		restored = append(restored, name)
	}
	sort.Strings(restored)

	if cfg != nil {
		if len(restored) == 0 {
			cfg.log("[还原] 全部 %d 个核心文件均与官方原版哈希一致，无需写盘", len(AllFiles))
		} else {
			cfg.log("[还原] 恢复完成：已还原 %d 个被修改的文件，%d 个未变动文件已跳过写入", len(restored), len(unchanged))
		}
	}

	return restored, nil
}

// RestoreToGame 把 backup/{region} 下的原文件复制回游戏目录（兼容旧调用接口）。
func RestoreToGame(gameDir, backupRoot string, lang region.Language) ([]string, error) {
	return Restore(context.Background(), RestoreConfig{
		GameDir:    gameDir,
		BackupRoot: backupRoot,
		Language:   lang,
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
	return os.WriteFile(filepath.Join(backupDir, "meta.json"), append(data, '\n'), 0o644)
}
