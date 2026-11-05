package main

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	goruntime "runtime"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

type mockCloud struct {
	releases map[string]*verifier.Release
	recent   []*verifier.Release
}

func (m *mockCloud) ReleaseByTag(ctx context.Context, repo, tag string) (*verifier.Release, error) {
	if m.releases != nil {
		if r, ok := m.releases[tag]; ok {
			return r, nil
		}
	}
	return nil, verifier.ErrNotFound
}

func (m *mockCloud) RecentReleases(ctx context.Context, repo string, limit int) ([]*verifier.Release, error) {
	if m.recent != nil {
		return m.recent, nil
	}
	return nil, nil
}

func (m *mockCloud) Download(ctx context.Context, url, dst string, onProgress func(done, total int64)) error {
	return nil
}

func (m *mockCloud) MirrorDownloadURL(raw string) string {
	return ""
}

func TestBuildTableListFromResource(t *testing.T) {
	data, err := os.ReadFile("../resource/tables.json")
	if err != nil {
		t.Skipf("缺少 resource/tables.json: %v", err)
	}
	var doc TablesDoc
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatalf("解析 tables.json 失败: %v", err)
	}
	if len(doc.Files) == 0 {
		t.Fatal("tables.json 缺少 files 列表")
	}
	list := buildTableList(doc)
	if len(list) != len(doc.Files) {
		t.Fatalf("表数量不一致: %d != %d", len(list), len(doc.Files))
	}

	byKey := make(map[string]TableEntry, len(list))
	for _, entry := range list {
		byKey[entry.Key] = entry
	}
	if entry, ok := byKey["BNpcName"]; !ok || entry.Warning == "" {
		t.Fatalf("BNpcName 缺少兼容警告: %+v", entry)
	}
	if entry, ok := byKey["Lobby"]; !ok || entry.Warning == "" {
		t.Fatalf("Lobby 缺少兼容警告: %+v", entry)
	}
	var quest TableEntry
	for key, entry := range byKey {
		if strings.HasPrefix(key, "quest/") {
			quest = entry
			break
		}
	}
	if quest.Key == "" || quest.Folder != "quest" || !strings.Contains(quest.Name, "任务对话") {
		t.Fatalf("任务表信息不正确: %+v", quest)
	}
}

func createMinimalIndexFile(path string) error {
	data := sqpack.CreateMockIndex("exd", "addon_0_ja.exd")
	return os.WriteFile(path, data, 0o644)
}

func TestCheckClientStatusOriginalNoBackup(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	verPath := filepath.Join(gameDir, "game", "ffxivgame.ver")
	if err := os.WriteFile(verPath, []byte("2026.10.16.0000.0000"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := createMinimalIndexFile(filepath.Join(sqpackDir, "0a0000.win32.index")); err != nil {
		t.Fatal(err)
	}
	if err := createMinimalIndexFile(filepath.Join(sqpackDir, "000000.win32.index")); err != nil {
		t.Fatal(err)
	}

	textHash, _ := verifier.HashFile(filepath.Join(sqpackDir, "0a0000.win32.index"))
	fontHash, _ := verifier.HashFile(filepath.Join(sqpackDir, "000000.win32.index"))

	app := NewApp()
	app.skipProcessCheck = true
	app.cloudOverride = &mockCloud{
		releases: map[string]*verifier.Release{
			"2026.10.16.0000.0000": {
				Tag: "2026.10.16.0000.0000",
				FileHashes: map[string]string{
					"0a0000.win32.index": textHash.SHA256,
					"000000.win32.index": fontHash.SHA256,
				},
			},
		},
	}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	status, err := app.CheckClientStatus(gameDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "original" {
		t.Fatalf("云端有收录且哈希一致时应判定为 original，实际为 %s (label=%s, desc=%s)",
			status.Status, status.Label, status.Description)
	}
}

func TestCheckClientStatusPatched(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	verPath := filepath.Join(gameDir, "game", "ffxivgame.ver")
	if err := os.WriteFile(verPath, []byte("2026.10.16.0000.0000"), 0o644); err != nil {
		t.Fatal(err)
	}
	textIndexPath := filepath.Join(sqpackDir, "0a0000.win32.index")
	if err := createMinimalIndexFile(textIndexPath); err != nil {
		t.Fatal(err)
	}
	fontIndexPath := filepath.Join(sqpackDir, "000000.win32.index")
	if err := createMinimalIndexFile(fontIndexPath); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.skipProcessCheck = true
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	app.skipProcessCheck = true
	if err := os.MkdirAll(app.resourceDirOverride, 0o755); err != nil {
		t.Fatal(err)
	}

	meta := verifier.BackupMeta{
		Region:  "GLOBAL",
		Version: "2026.10.16.0000.0000",
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": {SHA256: "different-hash", Size: 100},
		},
	}
	backupDir := filepath.Join(backupRootFor(app.resourceDirOverride), "GLOBAL")
	_ = os.MkdirAll(backupDir, 0o755)
	metaData, _ := json.Marshal(meta)
	_ = os.WriteFile(filepath.Join(backupDir, "meta.json"), metaData, 0o644)

	status, err := app.CheckClientStatus(gameDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "modified" {
		t.Fatalf("与备份不匹配时应判定为 modified，实际为 %s (%s)", status.Status, status.Label)
	}
}

func TestCheckClientStatusModifiedWithBackup(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	verPath := filepath.Join(gameDir, "game", "ffxivgame.ver")
	if err := os.WriteFile(verPath, []byte("2026.10.16.0000.0000"), 0o644); err != nil {
		t.Fatal(err)
	}
	textIndexPath := filepath.Join(sqpackDir, "0a0000.win32.index")
	if err := createMinimalIndexFile(textIndexPath); err != nil {
		t.Fatal(err)
	}
	fontIndexPath := filepath.Join(sqpackDir, "000000.win32.index")
	if err := createMinimalIndexFile(fontIndexPath); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.skipProcessCheck = true
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	app.skipProcessCheck = true
	backupDir := filepath.Join(tmpDir, "backup", "global")
	if err := os.MkdirAll(backupDir, 0o755); err != nil {
		t.Fatal(err)
	}
	// 写入本地备份，但哈希与当前游戏目录不同（模拟被外部改动）
	meta := verifier.BackupMeta{
		Region:  "global",
		Version: "2026.10.16.0000.0000",
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": {SHA256: "different_hash_1234567890", Size: 2048},
		},
	}
	metaBytes, _ := json.Marshal(meta)
	if err := os.WriteFile(filepath.Join(backupDir, "meta.json"), metaBytes, 0o644); err != nil {
		t.Fatal(err)
	}

	status, err := app.CheckClientStatus(gameDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "modified" {
		t.Fatalf("本地备份不匹配且非本工具汉化时应判定为 modified，实际为 %s (%s)", status.Status, status.Label)
	}
}

func TestCheckClientStatusNonGlobalUnsupported(t *testing.T) {
	app := NewApp()
	for _, lang := range []string{"CHS", "TC", "KO"} {
		status, err := app.CheckClientStatus("any/path", lang)
		if err != nil {
			t.Fatalf("%s CheckClientStatus 失败: %v", lang, err)
		}
		if status.Status != "unknown" {
			t.Fatalf("%s 无备份时状态应为 unknown，实际为 %s", lang, status.Status)
		}
	}
}

func TestRestoreOriginalNonGlobalWithoutCloudDownload(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	if err := os.MkdirAll(filepath.Join(gameDir, "game"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(gameDir, "game", "ffxivgame.ver"), []byte("2026.10.16.0000.0000"), 0o644); err != nil {
		t.Fatal(err)
	}
	app := NewApp()
	app.skipProcessCheck = true
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	app.skipProcessCheck = true

	for _, lang := range []string{"CHS", "TC", "KO"} {
		_, err := app.RestoreOriginal(gameDir, lang)
		if err == nil {
			t.Fatalf("%s 本地无备份时 RestoreOriginal 应该报错，但返回成功", lang)
		}
		if !strings.Contains(err.Error(), "原版备份") {
			t.Fatalf("%s 错误信息不符合预期: %v", lang, err)
		}
	}
}

func TestGetGameInfoNonGlobalBackup(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	app := NewApp()
	app.skipProcessCheck = true
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	app.skipProcessCheck = true
	backupDir := filepath.Join(tmpDir, "backup", "tc")
	if err := os.MkdirAll(backupDir, 0o755); err != nil {
		t.Fatal(err)
	}
	meta := verifier.BackupMeta{
		Region:  "tc",
		Version: "2026.10.16.0000.0000",
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": {SHA256: "tc_hash", Size: 1024},
		},
	}
	metaBytes, _ := json.Marshal(meta)
	if err := os.WriteFile(filepath.Join(backupDir, "meta.json"), metaBytes, 0o644); err != nil {
		t.Fatal(err)
	}

	info, err := app.GetGameInfo(gameDir, "TC")
	if err != nil {
		t.Fatalf("GetGameInfo 失败: %v", err)
	}
	if !info.BackupExists || info.BackupVersion != "2026.10.16.0000.0000" {
		t.Fatalf("其他服有本地备份时应正确识别: exists=%v, ver=%s", info.BackupExists, info.BackupVersion)
	}
}

func TestCheckClientStatusCloudMismatchIsModified(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	verPath := filepath.Join(gameDir, "game", "ffxivgame.ver")
	if err := os.WriteFile(verPath, []byte("2026.10.16.0000.0000"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := createMinimalIndexFile(filepath.Join(sqpackDir, "0a0000.win32.index")); err != nil {
		t.Fatal(err)
	}
	if err := createMinimalIndexFile(filepath.Join(sqpackDir, "000000.win32.index")); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.skipProcessCheck = true
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	app.cloudOverride = &mockCloud{
		releases: map[string]*verifier.Release{
			"2026.10.16.0000.0000": {
				Tag: "2026.10.16.0000.0000",
				FileHashes: map[string]string{
					"0a0000.win32.index": "real_official_hash_diff_from_dummy",
					"000000.win32.index": "real_official_font_hash_diff",
				},
			},
		},
	}

	status, err := app.CheckClientStatus(gameDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "modified" {
		t.Fatalf("云端原版哈希不匹配时必须判定为 modified，实际判定为 %s (%s)", status.Status, status.Label)
	}
}

func TestCheckClientStatusNonGlobalPatched(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	verPath := filepath.Join(gameDir, "game", "ffxivgame.ver")
	if err := os.WriteFile(verPath, []byte("2026.10.16.0000.0000"), 0o644); err != nil {
		t.Fatal(err)
	}
	textIndexPath := filepath.Join(sqpackDir, "0a0000.win32.index")
	if err := os.WriteFile(textIndexPath, sqpack.CreateMockIndex("exd", "addon_0_tc.exd"), 0o644); err != nil {
		t.Fatal(err)
	}
	app := NewApp()
	app.skipProcessCheck = true
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	meta := verifier.BackupMeta{
		Region:  "TC",
		Version: "2026.10.16.0000.0000",
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": {SHA256: "different-hash", Size: 100},
		},
	}
	backupDir := filepath.Join(backupRootFor(app.resourceDirOverride), "TC")
	_ = os.MkdirAll(backupDir, 0o755)
	metaData, _ := json.Marshal(meta)
	_ = os.WriteFile(filepath.Join(backupDir, "meta.json"), metaData, 0o644)

	status, err := app.CheckClientStatus(gameDir, "TC")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "modified" {
		t.Fatalf("繁中服与备份不匹配时应判定为 modified，实际为 %s (%s)", status.Status, status.Label)
	}
}

func TestEnsureHighResolutionUiAssetAndGetGameInfo(t *testing.T) {
	app := NewApp()
	info, err := app.GetGameInfo("", "GLOBAL")
	if err != nil {
		t.Fatalf("GetGameInfo 失败: %v", err)
	}
	if info.UiAssetType != 0 && info.UiAssetType != 1 && info.UiAssetType != -1 {
		t.Fatalf("UiAssetType 状态异常: %d", info.UiAssetType)
	}

	res, err := app.EnsureHighResolutionUiAsset()
	if err != nil && !strings.Contains(err.Error(), "未找到") {
		t.Fatalf("EnsureHighResolutionUiAsset 失败: %v", err)
	}
	if err == nil {
		if res.Current != 1 {
			t.Fatalf("修改后当前值应为 1，实际为 %d", res.Current)
		}
	}
}

func TestMultiRegionConfigIsolation(t *testing.T) {
	tmpDir := t.TempDir()
	app := NewApp()
	app.skipProcessCheck = true
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	_ = os.MkdirAll(app.resourceDirOverride, 0o755)

	// 1. 兼容迁移旧版单个 gameDir
	legacyCfg := `{"gameDir":"D:\\Games\\SquareEnix\\FFXIV","language":"GLOBAL"}`
	cfgPath := filepath.Join(tmpDir, "config.json")
	if err := os.WriteFile(cfgPath, []byte(legacyCfg), 0o644); err != nil {
		t.Fatal(err)
	}

	loaded := app.loadConfig(app.resourceDirOverride)
	if loaded.GameDirs["GLOBAL"] != "D:\\Games\\SquareEnix\\FFXIV" {
		t.Fatalf("旧配置应自动迁移入 GameDirs[GLOBAL]，实际为: %v", loaded.GameDirs)
	}

	// 2. 多区服独立保存与读取
	loaded.Language = "CHS"
	loaded.GameDirs["CHS"] = "D:\\Games\\最终幻想XIV"
	if err := app.SaveConfig(loaded); err != nil {
		t.Fatal(err)
	}

	reloaded := app.loadConfig(app.resourceDirOverride)
	if reloaded.GameDirs["GLOBAL"] != "D:\\Games\\SquareEnix\\FFXIV" {
		t.Fatalf("GameDirs[GLOBAL] 应保留国际服路径，实际为: %s", reloaded.GameDirs["GLOBAL"])
	}
	if reloaded.GameDirs["CHS"] != "D:\\Games\\最终幻想XIV" {
		t.Fatalf("GameDirs[CHS] 应记录国服路径，实际为: %s", reloaded.GameDirs["CHS"])
	}

	// 3. 落盘只有分区表，不再写出冗余的顶层 gameDir
	raw, err := os.ReadFile(cfgPath)
	if err != nil {
		t.Fatal(err)
	}
	var persisted map[string]json.RawMessage
	if err := json.Unmarshal(raw, &persisted); err != nil {
		t.Fatal(err)
	}
	if _, ok := persisted["gameDir"]; ok {
		t.Fatalf("config.json 不应再包含顶层 gameDir 字段，实际内容: %s", raw)
	}
}

func TestTargetLanguagesPerRegion(t *testing.T) {
	tmpDir := t.TempDir()
	app := NewApp()
	app.skipProcessCheck = true
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	_ = os.MkdirAll(app.resourceDirOverride, 0o755)
	cfgPath := filepath.Join(tmpDir, "config.json")

	// 1. 旧版数组形式迁移到当前区服，且不会让整份配置解析失败
	legacy := `{"language":"GLOBAL","targetLanguages":["JA","EN","DE"],"replaceFont":false}`
	if err := os.WriteFile(cfgPath, []byte(legacy), 0o644); err != nil {
		t.Fatal(err)
	}
	cfg := app.loadConfig(app.resourceDirOverride)
	if got := strings.Join(cfg.TargetLanguages["GLOBAL"], ","); got != "JA,EN,DE" {
		t.Fatalf("旧配置应迁移为 TargetLanguages[GLOBAL]，实际为: %q", got)
	}
	if cfg.ReplaceFont {
		t.Fatal("旧配置的其它字段不应因 targetLanguages 类型变化而丢失")
	}

	// 2. 切到国服并保存，国际服的勾选必须保留，国服记录自身唯一语言
	cfg.Language = "CHS"
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatal(err)
	}
	reloaded := app.loadConfig(app.resourceDirOverride)
	if got := strings.Join(reloaded.TargetLanguages["GLOBAL"], ","); got != "JA,EN,DE" {
		t.Fatalf("切换区服后不应丢失国际服勾选，实际为: %q", got)
	}
	if got := strings.Join(reloaded.TargetLanguages["CHS"], ","); got != "CHS" {
		t.Fatalf("国服应记录唯一语言 CHS，实际为: %q", got)
	}
	for _, code := range []string{"TC", "KO"} {
		if got := strings.Join(reloaded.TargetLanguages[code], ","); got != code {
			t.Fatalf("%s 应记录唯一语言 %s，实际为: %q", code, code, got)
		}
	}

	// 3. 改回国际服并按新勾选保存，其它区服互不影响
	reloaded.Language = "GLOBAL"
	reloaded.TargetLanguages["GLOBAL"] = []string{"EN", "FR"}
	if err := app.SaveConfig(reloaded); err != nil {
		t.Fatal(err)
	}
	final := app.loadConfig(app.resourceDirOverride)
	if got := strings.Join(final.TargetLanguages["GLOBAL"], ","); got != "EN,FR" {
		t.Fatalf("国际服勾选应更新为 EN,FR，实际为: %q", got)
	}
	if got := strings.Join(final.TargetLanguages["CHS"], ","); got != "CHS" {
		t.Fatalf("保存国际服不应影响国服记录，实际为: %q", got)
	}

	// 4. 落盘为按区服的对象，而非数组
	raw, err := os.ReadFile(cfgPath)
	if err != nil {
		t.Fatal(err)
	}
	var persisted map[string]json.RawMessage
	if err := json.Unmarshal(raw, &persisted); err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(strings.TrimSpace(string(persisted["targetLanguages"])), "{") {
		t.Fatalf("targetLanguages 应为按区服的对象，实际: %s", persisted["targetLanguages"])
	}
}

func TestSaveConfigCompletesRegionLanguages(t *testing.T) {
	tmpDir := t.TempDir()
	app := NewApp()
	app.skipProcessCheck = true
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	_ = os.MkdirAll(app.resourceDirOverride, 0o755)

	cfg := defaultConfig()
	cfg.Language = "CHS"
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatal(err)
	}

	raw, err := os.ReadFile(filepath.Join(tmpDir, "config.json"))
	if err != nil {
		t.Fatal(err)
	}
	var persisted struct {
		TargetLanguages map[string][]string `json:"targetLanguages"`
	}
	if err := json.Unmarshal(raw, &persisted); err != nil {
		t.Fatal(err)
	}
	want := map[string]string{"GLOBAL": "JA,EN", "CHS": "CHS", "TC": "TC", "KO": "KO"}
	for code, expected := range want {
		if got := strings.Join(persisted.TargetLanguages[code], ","); got != expected {
			t.Fatalf("%s 的汉化语言应为 %q，实际为 %q", code, expected, got)
		}
	}
}

func TestCheckClientStatusRegionMismatch(t *testing.T) {
	tmpDir := t.TempDir()
	cnDir := filepath.Join(tmpDir, "client_cn")
	sqpackDir := filepath.Join(cnDir, "game", "sqpack", "ffxiv")
	_ = os.MkdirAll(sqpackDir, 0o755)
	_ = os.WriteFile(filepath.Join(cnDir, "game", "ffxiv_dx11.exe"), []byte("mock"), 0o755)
	_ = os.WriteFile(filepath.Join(cnDir, "game", "ffxivgame.ver"), []byte("2026.10.16.0000.0000"), 0o644)
	_ = os.WriteFile(filepath.Join(sqpackDir, "0a0000.win32.index"), sqpack.CreateMockIndex("exd", "addon_0_chs.exd"), 0o644)

	app := NewApp()
	app.skipProcessCheck = true
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")

	// 在国际服模式下检查国服客户端目录
	status, err := app.CheckClientStatus(cnDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "mismatch" {
		t.Fatalf("区服不匹配应返回 mismatch 状态，实际为 %s (%s)", status.Status, status.Label)
	}
	if !strings.Contains(status.Label, "不匹配") {
		t.Fatalf("区服不匹配标签应包含'不匹配'，实际为: %s", status.Label)
	}

	// 校验 StartPatch 拦截
	_, err = app.StartPatch(PatchRequest{
		GameDir:     cnDir,
		Language:    "GLOBAL",
		ReplaceText: true,
	})
	if err == nil || !strings.Contains(err.Error(), "区服不匹配") {
		t.Fatalf("StartPatch 在区服不匹配时应拦截并报错，实际为: %v", err)
	}

	// 校验 RestoreOriginal 拦截
	_, err = app.RestoreOriginal(cnDir, "GLOBAL")
	if err == nil || !strings.Contains(err.Error(), "区服不匹配") {
		t.Fatalf("RestoreOriginal 在区服不匹配时应拦截并报错，实际为: %v", err)
	}
}



func TestCheckGameNotRunning(t *testing.T) {
	tmpDir := t.TempDir()
	app := NewApp()
	app.skipProcessCheck = true
	if err := app.checkGameNotRunning(context.Background(), tmpDir); err != nil {
		t.Fatalf("App.checkGameNotRunning 在跳过进程检查时不应报错: %v", err)
	}
}

func TestAnswerLauncherConfirm(t *testing.T) {
	app := NewApp()
	ch := make(chan bool, 1)
	app.launcherConfirmCh = ch

	app.AnswerLauncherConfirm(true)
	select {
	case ok := <-ch:
		if !ok {
			t.Fatalf("期望得到 true，实际得到 false")
		}
	default:
		t.Fatalf("期望收到确认结果")
	}
}

func TestCancelPatch(t *testing.T) {
	app := NewApp()
	called := false
	app.cancelPatch = func() {
		called = true
	}
	app.CancelPatch()
	if !called {
		t.Fatalf("CancelPatch 未触发底层 cancel 回调")
	}
}

func TestAppCheckAppUpdate(t *testing.T) {
	app := NewApp()
	app.cloudOverride = &mockCloud{
		recent: []*verifier.Release{
			{
				Tag:   "patcher-v3.0.0",
				Title: "汉化器 v3.0.0",
				URL:   "https://example.com/v3.0.0",
			},
		},
	}

	info, err := app.CheckAppUpdate()
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if info.LatestVersion != "3.0.0" {
		t.Fatalf("期望最新版本 3.0.0，实际得到 %s", info.LatestVersion)
	}
}

func TestAppCheckAppUpdateBlockedWhileBusy(t *testing.T) {
	app := NewApp()
	app.cloudOverride = &mockCloud{
		recent: []*verifier.Release{
			{Tag: "patcher-v3.0.0"},
		},
	}
	if err := app.beginBusy(); err != nil {
		t.Fatalf("占用忙碌标记失败: %v", err)
	}
	defer app.endBusy()

	if _, err := app.CheckAppUpdate(); err == nil {
		t.Fatal("汉化进行中不应允许检查更新")
	}
}

func TestAppInstallAppUpdateBlockedWhileBusy(t *testing.T) {
	app := NewApp()
	if err := app.beginBusy(); err != nil {
		t.Fatalf("占用忙碌标记失败: %v", err)
	}
	defer app.endBusy()

	if err := app.InstallAppUpdate(); err == nil {
		t.Fatal("汉化进行中不应允许自更新")
	}
	if err := app.RestartApp(); err == nil {
		t.Fatal("任务进行中不应允许重启程序")
	}
}

func TestIsGlobalClientDir(t *testing.T) {
	root := t.TempDir()

	mk := func(rel string) string {
		p := filepath.Join(root, filepath.FromSlash(rel))
		if err := os.MkdirAll(p, 0o755); err != nil {
			t.Fatalf("创建目录 %s 失败: %v", rel, err)
		}
		return p
	}

	full := mk("full")
	mk("full/boot")
	mk("full/game")
	if err := os.WriteFile(filepath.Join(full, "game", "ffxiv_dx11.exe"), []byte("stub"), 0o644); err != nil {
		t.Fatal(err)
	}

	noBoot := mk("no-boot")
	mk("no-boot/game")
	if err := os.WriteFile(filepath.Join(noBoot, "game", "ffxiv_dx11.exe"), []byte("stub"), 0o644); err != nil {
		t.Fatal(err)
	}

	noGame := mk("no-game")
	mk("no-game/boot")

	noExe := mk("no-exe")
	mk("no-exe/boot")
	mk("no-exe/game")

	bootIsFile := mk("boot-is-file")
	if err := os.WriteFile(filepath.Join(bootIsFile, "boot"), []byte("stub"), 0o644); err != nil {
		t.Fatal(err)
	}
	mk("boot-is-file/game")
	if err := os.WriteFile(filepath.Join(bootIsFile, "game", "ffxiv_dx11.exe"), []byte("stub"), 0o644); err != nil {
		t.Fatal(err)
	}

	cases := []struct {
		desc string
		dir  string
		want bool
	}{
		{"boot 与 game 齐全且含 ffxiv_dx11.exe", full, true},
		{"缺少 boot 目录", noBoot, false},
		{"缺少 game 目录", noGame, false},
		{"缺少 ffxiv_dx11.exe", noExe, false},
		{"boot 不是目录", bootIsFile, false},
		{"目录不存在", filepath.Join(root, "missing"), false},
	}
	for _, tc := range cases {
		if got := isGlobalClientDir(tc.dir); got != tc.want {
			t.Errorf("%s: 期望 %v，实际 %v", tc.desc, tc.want, got)
		}
	}
}

func TestGetScanDriveRoots(t *testing.T) {
	roots := getScanDriveRoots()
	// 非 Windows 平台没有盘符概念，未设置 WINEPREFIX 时返回空是预期行为。
	if len(roots) == 0 {
		if goruntime.GOOS == "windows" {
			t.Fatalf("getScanDriveRoots 在 Windows 下不应返回空")
		}
		return
	}
	for _, root := range roots {
		if root == "" {
			t.Errorf("驱动器根目录不应为空")
		}
		if !filepath.IsAbs(root) {
			t.Errorf("驱动器根目录应为绝对路径: %s", root)
		}
	}
}

// 非国际服即使本地备份与当前文件完全一致，也不能据此声称「官方原版」：
// 没有官方哈希，版本号相同不代表内容相同。
func TestCheckClientStatusNonGlobalIgnoresLocalBackup(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	version := "2026.10.16.0000.0000"
	if err := os.WriteFile(filepath.Join(gameDir, "game", "ffxivgame.ver"), []byte(version), 0o644); err != nil {
		t.Fatal(err)
	}
	textIndex := filepath.Join(sqpackDir, "0a0000.win32.index")
	fontIndex := filepath.Join(sqpackDir, "000000.win32.index")
	if err := os.WriteFile(textIndex, sqpack.CreateMockIndex("exd", "addon_0_chs.exd"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := createMinimalIndexFile(fontIndex); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")

	backupDir := filepath.Join(backupRootFor(app.resourceDirOverride), "chs")
	if err := os.MkdirAll(backupDir, 0o755); err != nil {
		t.Fatal(err)
	}
	textHash, err := verifier.HashFile(textIndex)
	if err != nil {
		t.Fatal(err)
	}
	fontHash, err := verifier.HashFile(fontIndex)
	if err != nil {
		t.Fatal(err)
	}
	meta := verifier.BackupMeta{
		Region:  "china",
		Version: version,
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": textHash,
			"000000.win32.index": fontHash,
		},
	}
	data, err := json.Marshal(meta)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(backupDir, "meta.json"), data, 0o644); err != nil {
		t.Fatal(err)
	}

	status, err := app.CheckClientStatus(gameDir, "CHS")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "original" {
		t.Fatalf("非国际服有本地备份且匹配时应为 original，实际为 %s (%s)", status.Status, status.Description)
	}
}

// 规则文件损坏时必须回退内置规则并返回错误，否则用户改坏 rules.json 后只会看到"没反应"。
// 规则数据只随资源下发：文件缺失或损坏时必须显式报错并让相关规则不可用，
// 不能回退到可执行文件内的副本。
func TestLoadRulesRequiresDataFile(t *testing.T) {
	dir := t.TempDir()

	rules, err := loadRules(dir)
	if err != nil {
		t.Fatalf("资源尚未就绪时不应提示错误: %v", err)
	}
	if len(rules) != 4 {
		t.Fatalf("规则应始终列出 4 条，实际 %d", len(rules))
	}

	if err := os.MkdirAll(filepath.Join(dir, "rawexd"), 0o755); err != nil {
		t.Fatal(err)
	}
	rules, err = loadRules(dir)
	if err == nil {
		t.Fatal("资源已就绪却缺 rules.json 时应返回错误")
	}
	if len(rules) != 4 {
		t.Fatalf("缺 rules.json 时仍应返回 4 条规则，实际 %d", len(rules))
	}
	for _, rule := range rules {
		if rule.ID() != "treasure_map_alias" {
			continue
		}
		if got := rule.Meta().Status; got != pipeline.RuleStatusUnavailable {
			t.Fatalf("缺 rules.json 时藏宝图规则应不可用，实际 %q", got)
		}
	}

	valid := `{"specialWord":{"甲":"乙"},"materiaMap":{"武略":"暴击"}}`
	if err := os.WriteFile(filepath.Join(dir, "rules.json"), []byte(valid), 0o644); err != nil {
		t.Fatal(err)
	}
	rules, err = loadRules(dir)
	if err != nil {
		t.Fatalf("合法 rules.json 不应报错: %v", err)
	}
	got := ""
	for _, rule := range rules {
		if rule.ID() != "materia_stat_suffix" {
			continue
		}
		if rule.Meta().Status != pipeline.RuleStatusOK {
			t.Fatalf("有数据时魔晶石规则状态应为 ok，实际 %q", rule.Meta().Status)
		}
		got = rule.Apply(&pipeline.CellContext{TableName: "Item", TargetStr: "武略魔晶石壹型"})
	}
	if got != "武略魔晶石壹型（暴击）" {
		t.Fatalf("规则数据未生效，实际 %q", got)
	}

	if err := os.WriteFile(filepath.Join(dir, "rules.json"), []byte("{"), 0o644); err != nil {
		t.Fatal(err)
	}
	if rules, err := loadRules(dir); err == nil || len(rules) != 4 {
		t.Fatalf("损坏的 rules.json 应返回错误且保留规则清单，实际 err=%v len=%d", err, len(rules))
	}

	if err := os.WriteFile(filepath.Join(dir, "rules.json"), []byte(`{"specialWord":{},"materiaMap":{}}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if rules, err := loadRules(dir); err == nil || len(rules) != 4 {
		t.Fatalf("内容为空的 rules.json 应返回错误且保留规则清单，实际 err=%v len=%d", err, len(rules))
	}
}

// 云端客户端必须复用同一实例，否则更新检查学到的可用节点无法用于汉化和还原。
func TestGetCloudReusesClient(t *testing.T) {
	app := NewApp()
	first := app.getCloud()
	second := app.getCloud()
	if first != second {
		t.Fatal("getCloud 应复用同一个客户端实例")
	}

	client, ok := first.(*verifier.GitHubClient)
	if !ok {
		t.Fatalf("默认云端实现应为 GitHubClient，实际 %T", first)
	}
	client.SetPreferredNode("https://wget.la/")
	got := app.getCloud().(*verifier.GitHubClient).
		MirrorDownloadURL("https://github.com/foo/bar/releases/download/v1/app.exe")
	if !strings.Contains(got, "wget.la") {
		t.Errorf("节点记忆应跨调用保留，实际 %q", got)
	}
}
