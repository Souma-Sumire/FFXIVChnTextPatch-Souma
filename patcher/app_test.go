package main

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

type mockCloud struct {
	releases map[string]*verifier.Release
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
	return nil, nil
}

func (m *mockCloud) Download(ctx context.Context, url, dst string, onProgress func(done, total int64)) error {
	return nil
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

	app := NewApp()
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	status, err := app.CheckClientStatus(gameDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "original" {
		t.Fatalf("无本地备份时数据包完整的客户端应判定为 original，实际为 %s (label=%s, desc=%s)",
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
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	if err := os.MkdirAll(app.resourceDirOverride, 0o755); err != nil {
		t.Fatal(err)
	}

	textHash, _ := verifier.HashFile(textIndexPath)
	fontHash, _ := verifier.HashFile(fontIndexPath)
	rec := PatchedRecord{
		GameDir:     gameDir,
		Version:     "2026.10.16.0000.0000",
		Language:    "GLOBAL",
		ReplaceText: true,
		ReplaceFont: true,
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": textHash,
			"000000.win32.index": fontHash,
		},
	}
	if err := app.savePatchedRecord(rec); err != nil {
		t.Fatal(err)
	}

	status, err := app.CheckClientStatus(gameDir, "GLOBAL")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "patched" {
		t.Fatalf("匹配汉化记录时应判定为 patched，实际为 %s (%s)", status.Status, status.Label)
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
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
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
		if status.Status != "unsupported" {
			t.Fatalf("%s 状态应为 unsupported，实际为 %s", lang, status.Status)
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
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")

	for _, lang := range []string{"CHS", "TC", "KO"} {
		_, err := app.RestoreOriginal(gameDir, lang)
		if err == nil {
			t.Fatalf("%s 本地无备份时 RestoreOriginal 应该报错，但返回成功", lang)
		}
		if !strings.Contains(err.Error(), "未收录云端原版下载") && !strings.Contains(err.Error(), "不存在") {
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
	app.cloudOverride = &mockCloud{}
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
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
	if err := createMinimalIndexFile(textIndexPath); err != nil {
		t.Fatal(err)
	}
	textHash, _ := verifier.HashFile(textIndexPath)

	app := NewApp()
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	if err := os.MkdirAll(app.resourceDirOverride, 0o755); err != nil {
		t.Fatal(err)
	}
	rec := PatchedRecord{
		GameDir:     gameDir,
		Version:     "2026.10.16.0000.0000",
		Language:    "TC",
		ReplaceText: true,
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": textHash,
		},
	}
	if err := app.savePatchedRecord(rec); err != nil {
		t.Fatal(err)
	}

	status, err := app.CheckClientStatus(gameDir, "TC")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status != "patched" {
		t.Fatalf("繁中服在已汉化时应判定为 patched，实际为 %s (%s)", status.Status, status.Label)
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
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	_ = os.MkdirAll(app.resourceDirOverride, 0o755)

	// 1. 兼容迁移旧版单个 gameDir
	legacyCfg := `{"gameDir":"D:\\Games\\SquareEnix\\FFXIV","language":"GLOBAL"}`
	cfgPath := filepath.Join(tmpDir, "config.json")
	if err := os.WriteFile(cfgPath, []byte(legacyCfg), 0o644); err != nil {
		t.Fatal(err)
	}

	loaded := app.loadConfig(app.resourceDirOverride)
	if loaded.GameDir != "D:\\Games\\SquareEnix\\FFXIV" {
		t.Fatalf("旧配置迁移后当前 GameDir 应为国际服路径，实际为: %s", loaded.GameDir)
	}
	if loaded.GameDirs["GLOBAL"] != "D:\\Games\\SquareEnix\\FFXIV" {
		t.Fatalf("旧配置应自动迁移入 GameDirs[GLOBAL]，实际为: %v", loaded.GameDirs)
	}

	// 2. 多区服独立保存与读取
	loaded.Language = "CHS"
	loaded.GameDir = "D:\\Games\\最终幻想XIV"
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
	if reloaded.GameDir != "D:\\Games\\最终幻想XIV" {
		t.Fatalf("当前选中区服应激活国服路径，实际为: %s", reloaded.GameDir)
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

func TestCheckClientStatusPatchedRecordRegionMismatchIgnored(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game_root")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	_ = os.MkdirAll(sqpackDir, 0o755)
	_ = os.WriteFile(filepath.Join(gameDir, "game", "ffxivgame.ver"), []byte("2026.10.16.0000.0000"), 0o644)
	_ = createMinimalIndexFile(filepath.Join(sqpackDir, "0a0000.win32.index"))

	textHash, _ := verifier.HashFile(filepath.Join(sqpackDir, "0a0000.win32.index"))

	app := NewApp()
	app.resourceDirOverride = filepath.Join(tmpDir, "resource")
	_ = os.MkdirAll(app.resourceDirOverride, 0o755)

	// 记录为 GLOBAL 汉化
	rec := PatchedRecord{
		GameDir:     gameDir,
		Version:     "2026.10.16.0000.0000",
		Language:    "GLOBAL",
		ReplaceText: true,
		Files: map[string]verifier.FileHash{
			"0a0000.win32.index": textHash,
		},
	}
	_ = app.savePatchedRecord(rec)

	// 切换到 CHS 查看，不应被识别为已汉化
	status, err := app.CheckClientStatus(gameDir, "CHS")
	if err != nil {
		t.Fatalf("CheckClientStatus 失败: %v", err)
	}
	if status.Status == "patched" {
		t.Fatalf("跨区服时不应套用其他区服的汉化记录，但返回了 patched: %+v", status)
	}
}

func TestCheckGameNotRunning(t *testing.T) {
	tmpDir := t.TempDir()
	if err := checkGameNotRunning(tmpDir); err != nil {
		t.Fatalf("空目录未运行游戏时不应报错: %v", err)
	}
}

