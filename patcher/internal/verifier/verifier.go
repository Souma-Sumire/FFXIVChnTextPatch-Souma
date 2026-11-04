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

	// PatchedIndexHashes 记录本工具上次汉化后的索引哈希（来自 patched_records.json）。
	// 非国际服没有云端原文件与官方哈希，只能靠它区分“官方原版”与“已汉化”，
	// 避免把汉化结果当成原版备份。调用方应只传入与当前版本一致的记录。
	PatchedIndexHashes map[string]string

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
	cfg.log("[校验] 正在校验游戏客户端与备份完整性（版本 %s）...", version)

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

	originalText, originalFont := determineOriginal(reg, cloudState, version, meta, gameHashes, release, cfg.PatchedIndexHashes)

	// 非国际服没有云端原文件，也没有官方哈希可供核对，只能以当前客户端为基线。
	// 把这条能力边界明确告知用户，避免“备份的其实是汉化后的文件”这种误解。
	if reg != region.Global && (meta == nil || meta.Version != version) {
		if originalText && originalFont {
			cfg.log("[提示] 非国际服不提供云端原文件，将以当前客户端文件作为官方原版基线建立备份。")
		} else {
			cfg.log("[提示] 客户端仍是上次汉化后的状态，本地原版备份缺失或版本不符，无法安全还原。")
		}
	}

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
			return nil, fmt.Errorf("文本数据已被修改或汉化，本地和云端都没有 %s 版本的官方原版备份，无法安全汉化；请先用官方启动器修复客户端，再试", version)
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
			return nil, fmt.Errorf("字体数据已被修改或汉化，本地和云端都没有 %s 版本的官方原版备份，无法安全汉化；请先用官方启动器修复客户端，再试", version)
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
	meta, err = ensureAllOriginalFilesBackedUp(ctx, cfg, result, reg, cloudState, version, meta, backupDir, sqpackDir, gameHashes, release, originalText, originalFont)
	if err != nil {
		return nil, err
	}

	// 先完成文本与字体的原文件准备（还原），再统一写入汉化数据，
	// 避免出现“备份文本 → 改文本 → 备份字体 → 改字体”的交叉流程。
	if err := prepareTextFiles(cfg, result, backupDir, sqpackDir, meta); err != nil {
		return nil, err
	}
	if err := prepareFontFiles(cfg, result, backupDir, sqpackDir, meta); err != nil {
		return nil, err
	}

	// 原文件全部就绪后再写入汉化，中途失败不会留下“文本已改、字体未备份”的中间态。
	if err := applyTextData(ctx, cfg, result); err != nil {
		return nil, err
	}
	if err := applyFontData(ctx, cfg, result); err != nil {
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
		if errors.Is(err, ErrNotFound) {
			note := fmt.Sprintf("云端尚未收录当前游戏版本 (%s) 的原文件备份，已直接基于本地文件汉化", version)
			cfg.log("[提示] %s", note)
			result.Plan.Notes = append(result.Plan.Notes, note)
			return nil, CloudPending
		}
		cfg.log("[警告] 按版本 (%s) 查询云端备份失败: %v", version, err)
		result.Plan.Notes = append(result.Plan.Notes, fmt.Sprintf("无法访问云端备份（%v），已直接基于本地文件处理", err))
		return nil, CloudLocalOnly
	}

	if CompareVersion(version, release.Tag) == 0 {
		return release, CloudMatched
	}
	note := fmt.Sprintf("当前游戏版本 (%s) 与云端备份版本 (%s) 不一致，已直接基于本地文件汉化", version, release.Tag)
	cfg.log("[提示] %s", note)
	result.Plan.Notes = append(result.Plan.Notes, note)
	return release, CloudPending
}

// DetectClientRegion 根据客户端文件结构特征探测所属区服。
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

func determineOriginal(reg region.Region, cloudState CloudState, version string, meta *BackupMeta,
	gameHashes map[string]FileHash, release *Release, patched map[string]string) (bool, bool) {
	if reg == region.Global {
		if meta != nil && meta.Version == version && hasFiles(meta.Files, TextFiles) {
			textOriginal := allMatchMeta(gameHashes, meta, TextFiles)
			fontOriginal := true
			if hasFiles(meta.Files, FontFiles) {
				fontOriginal = allMatchMeta(gameHashes, meta, FontFiles)
			} else if cloudState == CloudMatched && release != nil && hasReleaseFiles(release, FontFiles) {
				fontOriginal = allMatchRelease(gameHashes, release, FontFiles)
			} else {
				fontOriginal = !indexMatchesPatched(patched, gameHashes, "000000.win32.index")
			}
			return textOriginal, fontOriginal
		}
		if cloudState == CloudMatched && release != nil {
			textOrig := !indexMatchesPatched(patched, gameHashes, "0a0000.win32.index")
			if hasReleaseFiles(release, TextFiles) {
				textOrig = allMatchRelease(gameHashes, release, TextFiles)
			}
			fontOrig := !indexMatchesPatched(patched, gameHashes, "000000.win32.index")
			if hasReleaseFiles(release, FontFiles) {
				fontOrig = allMatchRelease(gameHashes, release, FontFiles)
			}
			return textOrig, fontOrig
		}
	}
	return !indexMatchesPatched(patched, gameHashes, "0a0000.win32.index"),
		!indexMatchesPatched(patched, gameHashes, "000000.win32.index")
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

// prepareTextFiles 完成文本原文件的还原，不写入汉化数据。
// 文本与字体的准备阶段全部结束后才进入写入阶段，避免还原与修改交叉。
func prepareTextFiles(cfg Config, result *Result, backupDir, sqpackDir string, meta *BackupMeta) error {
	plan := result.Plan.Text
	if !plan.Enabled || plan.Action == ActionNone || plan.Action == ActionPatch {
		return nil
	}
	cfg.event("text", "正在准备文本原文件")
	switch plan.Action {
	case ActionRestore:
		cfg.log("[文本] 正在从本地备份还原官方原版文本...")
		if err := restoreFromBackup(backupDir, sqpackDir, TextFiles, meta); err != nil {
			return fmt.Errorf("从本地备份还原文本原文件失败: %w", err)
		}
		result.RestoredText = true
	case ActionDownload:
		cfg.log("[文本] 正在还原已下载的官方原版文本数据包...")
		if err := restoreFromBackup(backupDir, sqpackDir, TextFiles, meta); err != nil {
			return fmt.Errorf("还原下载的文本原文件失败: %w", err)
		}
		result.RestoredText = true
	}
	return nil
}

// prepareFontFiles 完成字体原文件的还原，不写入汉化数据。
func prepareFontFiles(cfg Config, result *Result, backupDir, sqpackDir string, meta *BackupMeta) error {
	plan := result.Plan.Font
	if !plan.Enabled || plan.Action == ActionNone || plan.Action == ActionPatch {
		return nil
	}
	cfg.event("font", "正在准备字体原文件")
	switch plan.Action {
	case ActionRestore:
		cfg.log("[字体] 正在从本地备份还原官方原版字体...")
		if err := restoreFromBackup(backupDir, sqpackDir, FontFiles, meta); err != nil {
			return fmt.Errorf("从本地备份还原字体原文件失败: %w", err)
		}
		result.RestoredFont = true
	case ActionDownload:
		cfg.log("[字体] 正在还原已下载的官方原版字体数据包...")
		if err := restoreFromBackup(backupDir, sqpackDir, FontFiles, meta); err != nil {
			return fmt.Errorf("还原下载的字体原文件失败: %w", err)
		}
		result.RestoredFont = true
	}
	return nil
}

// applyTextData 在文本原文件就绪后写入汉化数据。
func applyTextData(ctx context.Context, cfg Config, result *Result) error {
	plan := result.Plan.Text
	if !plan.Enabled || plan.Action == ActionNone {
		return nil
	}
	if cfg.ApplyText == nil {
		return fmt.Errorf("未配置文本汉化执行器")
	}
	cfg.event("text", "正在替换文本")
	if err := cfg.ApplyText(ctx); err != nil {
		return fmt.Errorf("文本汉化失败: %w", err)
	}
	result.TextPatched = true
	return nil
}

// applyFontData 在字体原文件就绪后写入汉化字体。
func applyFontData(ctx context.Context, cfg Config, result *Result) error {
	if !result.Plan.Font.Enabled {
		return nil
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
	gameHashes map[string]FileHash, release *Release, originalText, originalFont bool) (*BackupMeta, error) {
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
		gh, hasGameFile := gameHashes[name]
		isOrig := false
		if reg == region.Global && cloudState == CloudMatched && release != nil {
			if cloudSHA, ok := release.AssetSHA256(name); ok && hasGameFile {
				isOrig = strings.EqualFold(gh.SHA256, cloudSHA)
			} else if strings.HasPrefix(name, "0a0000.") {
				isOrig = originalText
			} else {
				isOrig = originalFont
			}
		} else if strings.HasPrefix(name, "0a0000.") {
			isOrig = originalText
		} else {
			isOrig = originalFont
		}

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
				} else if isOrig && hasGameFile && !strings.EqualFold(bh.SHA256, gh.SHA256) {
					return nil, fmt.Errorf("本地备份中的 %s 与游戏目录内容不一致（备份 %s，游戏 %s），无法确定哪一份是官方原版；请先使用官方启动器修复客户端，或删除该备份文件后重试",
						name, shortHash(bh.SHA256), shortHash(gh.SHA256))
				} else {
					continue
				}
			}
		}

		// 2. 本地无有效备份，尝试从游戏目录提取官方原版
		if hasGameFile && isOrig {
			cfg.log("[备份] 正在将官方原版 %s 备份至本地...", name)
			got, err := copyFile(filepath.Join(sqpackDir, name), backupPath, gh.SHA256)
			if err != nil {
				return nil, fmt.Errorf("备份 %s 失败: %w", name, err)
			}
			meta.Files[name] = got
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
					return nil, fmt.Errorf("下载原版 %s 失败: %w", name, err)
				}
				got, err := HashFile(backupPath)
				if err != nil {
					_ = os.Remove(backupPath)
					return nil, fmt.Errorf("计算下载原版 %s 哈希失败: %w", name, err)
				}
				if !strings.EqualFold(got.SHA256, cloudSHA) {
					_ = os.Remove(backupPath)
					return nil, fmt.Errorf("下载原版 %s 后 SHA256 校验失败（期望 %s，实际 %s，大小 %d）",
						name, shortHash(cloudSHA), shortHash(got.SHA256), got.Size)
				}
				meta.Files[name] = got
				result.Downloaded = append(result.Downloaded, name)
				continue
			}
		}

		// 4. 若无法获取该文件的原版备份，且当前操作勾选了修改该文件，则报错终止
		if (strings.HasPrefix(name, "0a0000.") && cfg.ReplaceText) || (strings.HasPrefix(name, "000000.") && cfg.ReplaceFont) {
			return nil, fmt.Errorf("文件 %s 已被修改或汉化，本地和云端都没有 %s 版本的官方原版备份，无法安全汉化；请先用官方启动器修复客户端，再试", name, version)
		}
	}

	if err := writeBackupMeta(backupDir, meta); err != nil {
		return nil, err
	}
	return meta, nil
}

// restoreSequence 返回还原写入顺序：先索引、后数据。
// 汉化只向 dat0 尾部追加，原偏移处的字节始终完好，所以先写索引能保证中途失败时
// 客户端仍指向可用的原数据；反过来先替换 dat0 会让汉化版索引指向被截断的位置。
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

// restoreFromBackup 把备份文件写回游戏目录。
// 期望哈希取自备份清单而不是备份文件本身：这样被外部篡改或截断的备份会被拒绝，
// 而不是把坏内容当成“原版”写回客户端。
func restoreFromBackup(backupDir, sqpackDir string, files []string, meta *BackupMeta) error {
	for _, name := range restoreSequence(files) {
		src := filepath.Join(backupDir, name)
		dst := filepath.Join(sqpackDir, name)
		if _, err := os.Stat(src); err != nil {
			return fmt.Errorf("备份文件缺失或不可访问 (%s): %w", name, err)
		}
		want := ""
		if meta != nil {
			if h, ok := meta.Files[name]; ok {
				want = h.SHA256
			}
		}
		if want == "" {
			return fmt.Errorf("备份清单中缺少 %s 的哈希记录，拒绝使用来源不明的备份还原", name)
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

// copyFile 复制文件，并在落到目标位置之前校验内容哈希。
// 边写边算 SHA256，一致才 rename：磁盘错误、写入截断或源文件被替换都会在这里被拦下，
// 目标文件保持原样，不会出现“备份/还原写进去的是一份坏数据”。
// wantSHA 为空时只计算不比对（仅用于调用方确实没有期望值的场景）。
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
		return FileHash{}, fmt.Errorf("复制 %s 后哈希校验失败（期望 %s，实际 %s），已放弃写入 %s",
			filepath.Base(src), shortHash(wantSHA), shortHash(got.SHA256), filepath.Base(dst))
	}
	if err := os.Rename(tmp, dst); err != nil {
		_ = os.Remove(tmp)
		return FileHash{}, err
	}
	return got, nil
}

// shortHash 用于错误信息，保留前 8 位便于比对。
func shortHash(h string) string {
	if len(h) > 8 {
		return h[:8]
	}
	return h
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

// indexMatchesPatched 判断某个索引是否正好是本工具上次汉化写入的版本。
func indexMatchesPatched(patched map[string]string, gameHashes map[string]FileHash, name string) bool {
	want := patched[name]
	if want == "" {
		return false
	}
	return strings.EqualFold(gameHashes[name].SHA256, want)
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
	// RequirePatchedState 要求先确认当前文件仍是本工具汉化的产物，才允许非国际服还原。
	// 非国际服没有官方哈希，这是「本地备份仍对应当前客户端内容」的唯一可用依据。
	RequirePatchedState bool
	// PatchedIndexHashes 是本工具上次汉化后的索引哈希（来自 patched_records）。
	PatchedIndexHashes map[string]string
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
		return nil, fmt.Errorf("%s 没有本地原文件备份，云端也没有该版本的原文件；请先用游戏启动器修复客户端", reg)
	}

	// 2. 准备当前版本的云端 Release（国际服可用时）
	var release *Release
	var cloudErr error
	if reg == region.Global && cfg.Cloud != nil {
		rel, err := cfg.Cloud.ReleaseByTag(ctx, cfg.Repo, version)
		if err == nil {
			release = rel
		} else {
			cloudErr = err
			if !errors.Is(err, ErrNotFound) {
				cfg.log("[警告] 获取云端原文件清单 (%s) 失败: %v", version, err)
			}
		}
	}

	// 3. 本地备份版本与当前客户端不一致时，旧版本文件不可用于还原
	if meta != nil && meta.Version != "" && meta.Version != version {
		if reg != region.Global {
			return nil, fmt.Errorf("游戏已更新到 %s，本地备份还是 %s，旧备份不能用于还原；请用官方启动器修复客户端", version, meta.Version)
		}
		// 国际服可借当前版本的云端 Release 逐文件核对，取不到就只能拒绝：
		// 用旧版本文件覆盖新版客户端会缺新增条目，且这份错误基线会被后续汉化沿用。
		if release == nil {
			if cloudErr != nil {
				return nil, fmt.Errorf("本地备份是 %s，当前游戏是 %s，获取 %s 的云端原文件失败（%v）；请联网后重试，或用官方启动器修复客户端", meta.Version, version, version, cloudErr)
			}
			return nil, fmt.Errorf("本地备份是 %s，当前游戏是 %s，取不到 %s 的云端原文件；请联网后重试，或用官方启动器修复客户端", meta.Version, version, version)
		}
	}

	if reg != region.Global && cfg.RequirePatchedState {
		if len(cfg.PatchedIndexHashes) == 0 {
			return nil, fmt.Errorf("无法确认当前客户端是否仍为本工具汉化的状态（缺少汉化记录）；本地备份可能对应游戏更新前的内容，已中止还原。请使用官方启动器修复客户端")
		}
		for name, want := range cfg.PatchedIndexHashes {
			if want == "" {
				continue
			}
			got, err := HashFile(filepath.Join(sqpackDir, name))
			if err != nil {
				return nil, fmt.Errorf("读取 %s 失败，无法确认客户端状态: %w", name, err)
			}
			if !strings.EqualFold(got.SHA256, want) {
				return nil, fmt.Errorf("当前 %s 哈希 (%s) 已与汉化记录 (%s) 不一致（游戏更新或第三方修改）；为避免用旧备份覆盖新内容，已中止还原。请使用官方启动器修复客户端",
					name, shortHash(got.SHA256), shortHash(want))
			}
		}
	}

	if meta == nil {
		meta = &BackupMeta{Region: string(reg), Version: version, Files: map[string]FileHash{}}
	}

	// 4. 确保本地备份完整具备全部 6 个经过验证的官方原版文件
	// 这是唯一会读取备份文件哈希的环节，用于保证不会用损坏或被篡改的备份覆盖客户端。
	cfg.log("[还原] 正在核对本地备份中的官方原版文件...")
	verified := make(map[string]bool, len(AllFiles))
	for _, name := range AllFiles {
		backupPath := filepath.Join(backupDir, name)
		want, hasMeta := meta.Files[name]
		hasValidBackup := false
		var bh FileHash
		var bhErr error
		if hasMeta {
			bh, bhErr = HashFile(backupPath)
			if bhErr == nil && bh.SHA256 != "" && strings.EqualFold(bh.SHA256, want.SHA256) {
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
			verified[name] = true
			continue
		}

		// 本地备份缺失或失效时：优先检查当前游戏目录中的文件是否正好是原版
		gamePath := filepath.Join(sqpackDir, name)
		gh, gErr := HashFile(gamePath)
		if gErr == nil && reg == region.Global && release != nil {
			if cloudSHA, ok := release.AssetSHA256(name); ok && strings.EqualFold(gh.SHA256, cloudSHA) {
				if _, err := copyFile(gamePath, backupPath, cloudSHA); err == nil {
					meta.Files[name] = gh
					verified[name] = true
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
					return nil, fmt.Errorf("下载 %s 后 SHA256 校验失败（期望 %s，实际 %s，大小 %d）",
						name, shortHash(cloudSHA), shortHash(got.SHA256), got.Size)
				}
				meta.Files[name] = got
				verified[name] = true
				continue
			}
		}

		// 非国际服且缺失备份
		if reg != region.Global {
			if _, err := os.Stat(backupPath); err != nil {
				return nil, fmt.Errorf("本地备份文件缺失或不可访问 (%s): %w", name, err)
			}
			if bhErr != nil {
				return nil, fmt.Errorf("读取本地备份文件 %s 失败: %w", name, bhErr)
			}
			if hasMeta {
				return nil, fmt.Errorf("本地备份 %s 的哈希 (%s) 与汉化前记录 (%s) 不一致（可能被外部修改或已损坏），无法还原",
					name, shortHash(bh.SHA256), shortHash(want.SHA256))
			}
			return nil, fmt.Errorf("本地备份里没有文件 %s，无法还原", name)
		}

		if cloudErr != nil {
			return nil, fmt.Errorf("本地没有原版备份，云端也取不到 %s 版本的 %s（%v）；建议使用官方启动器设置中的【检查与修复游戏文件】恢复原版客户端", version, name, cloudErr)
		}
		return nil, fmt.Errorf("本地没有原版备份，云端也取不到 %s 版本的 %s；建议使用官方启动器设置中的【检查与修复游戏文件】恢复原版客户端", version, name)
	}

	// 只有全部文件都通过验证，才把备份清单标记为当前版本。
	// 否则保留原版本号，避免未核对的旧文件被当成新版本原版沿用。
	if len(verified) == len(AllFiles) && (reg != region.Global || release != nil || meta.Version == version) {
		meta.Version = version
	}
	if err := writeBackupMeta(backupDir, meta); err != nil {
		cfg.log("[警告] 更新备份清单失败: %v", err)
	}

	// 5. 比对游戏目录哈希，只还原真正被修改过的文件以减少磁盘 IO
	return restoreLocalBackup(&cfg, backupDir, sqpackDir, meta, reg)
}

func restoreLocalBackup(cfg *RestoreConfig, backupDir, sqpackDir string, meta *BackupMeta, reg region.Region) ([]string, error) {
	if meta == nil || len(meta.Files) == 0 {
		return nil, fmt.Errorf("%s 的本地备份中没有可还原的文件", reg)
	}

	// 备份文件的哈希已在上一步逐文件校验过，这里只比对客户端与备份，
	// 跳过未被修改的文件以减少整包写入（copyFile 仍会校验写入内容的 SHA256）。
	if cfg != nil {
		cfg.log("[还原] 正在比对客户端与备份，仅覆盖被修改过的文件...")
	}

	restored := make([]string, 0, len(AllFiles))

	for _, name := range restoreSequence(AllFiles) {
		want, ok := meta.Files[name]
		if !ok {
			continue
		}
		src := filepath.Join(backupDir, name)
		dst := filepath.Join(sqpackDir, name)

		curHash, err := HashFile(dst)
		if err == nil && want.SHA256 != "" && strings.EqualFold(curHash.SHA256, want.SHA256) {
			continue
		}

		if _, err := copyFile(src, dst, want.SHA256); err != nil {
			return nil, fmt.Errorf("还原文件 %s 失败: %w", name, err)
		}
		restored = append(restored, name)
	}
	sort.Strings(restored)
	return restored, nil
}

// RestoreToGameWithContext 把 backup/{region} 下的原文件复制回游戏目录，可取消并向界面回报进度。
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
		// 覆盖失败时保留旧文件（还原的唯一依据），避免连旧清单一起丢失。
		_ = os.Remove(tmpPath)
		return fmt.Errorf("写入备份清单失败: %w", err)
	}
	return nil
}
