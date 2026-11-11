package verifier

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

const testRepo = "owner/repo"

type mockServer struct {
	srv       *httptest.Server
	releases  map[string]map[string]string // tag -> 文件名 -> 内容
	requests  atomic.Int32
	downloads atomic.Int32
}

func newMockServer(t *testing.T, releases map[string]map[string]string) *mockServer {
	t.Helper()
	m := &mockServer{releases: releases}
	m.srv = httptest.NewServer(http.HandlerFunc(m.handle))
	t.Cleanup(m.srv.Close)
	return m
}

func (m *mockServer) client() *GitHubClient {
	return &GitHubClient{HTTP: m.srv.Client(), APIBase: m.srv.URL}
}

func (m *mockServer) tags() []string {
	tags := make([]string, 0, len(m.releases))
	for tag := range m.releases {
		tags = append(tags, tag)
	}
	sort.Slice(tags, func(i, j int) bool { return CompareVersion(tags[i], tags[j]) > 0 })
	return tags
}

func (m *mockServer) releaseJSON(tag string) githubRelease {
	files := m.releases[tag]
	out := githubRelease{TagName: tag}
	names := make([]string, 0, len(files))
	for name := range files {
		names = append(names, name)
	}
	sort.Strings(names)
	for _, name := range names {
		sum := sha256.Sum256([]byte(files[name]))
		out.Assets = append(out.Assets, githubAsset{
			Name:               name,
			Digest:             "sha256:" + hex.EncodeToString(sum[:]),
			BrowserDownloadURL: m.srv.URL + "/dl/" + tag + "/" + name,
		})
	}
	return out
}

func (m *mockServer) handle(w http.ResponseWriter, r *http.Request) {
	m.requests.Add(1)
	switch {
	case strings.HasPrefix(r.URL.Path, "/repos/"+testRepo+"/releases/tags/"):
		tag := strings.TrimPrefix(r.URL.Path, "/repos/"+testRepo+"/releases/tags/")
		if _, ok := m.releases[tag]; !ok {
			http.NotFound(w, r)
			return
		}
		writeJSON(w, m.releaseJSON(tag))
	case r.URL.Path == "/repos/"+testRepo+"/releases":
		list := make([]githubRelease, 0, len(m.releases))
		for _, tag := range m.tags() {
			list = append(list, m.releaseJSON(tag))
		}
		writeJSON(w, list)
	case strings.HasPrefix(r.URL.Path, "/dl/"):
		m.downloads.Add(1)
		parts := strings.SplitN(strings.TrimPrefix(r.URL.Path, "/dl/"), "/", 2)
		if len(parts) != 2 {
			http.NotFound(w, r)
			return
		}
		content, ok := m.releases[parts[0]][parts[1]]
		if !ok {
			http.NotFound(w, r)
			return
		}
		_, _ = w.Write([]byte(content))
	default:
		http.NotFound(w, r)
	}
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

type testEnv struct {
	root       string
	gameDir    string
	backupRoot string
	sqpackDir  string
}

func newTestEnv(t *testing.T, version string, files map[string]string) *testEnv {
	t.Helper()
	root := t.TempDir()
	env := &testEnv{
		root:       root,
		gameDir:    filepath.Join(root, "game-root"),
		backupRoot: filepath.Join(root, "backup"),
	}
	env.sqpackDir = filepath.Join(env.gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(env.sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(env.gameDir, "game", "ffxivgame.ver"), []byte(version+"\r\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	for name, content := range files {
		if err := os.WriteFile(filepath.Join(env.sqpackDir, name), []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return env
}

func (e *testEnv) seedBackup(t *testing.T, lang region.Language, version string, files map[string]string) {
	t.Helper()
	dir := filepath.Join(e.backupRoot, string(region.Of(lang)))
	meta := &BackupMeta{Region: string(region.Of(lang)), Version: version, Source: BackupSourceCloud, Files: map[string]FileHash{}}
	for name, content := range files {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
		path := filepath.Join(dir, name)
		if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
		sum := sha256.Sum256([]byte(content))
		meta.Files[name] = FileHash{SHA256: hex.EncodeToString(sum[:]), Size: int64(len(content))}
	}
	if err := writeBackupMeta(dir, meta); err != nil {
		t.Fatal(err)
	}
}

func canonicalFiles(prefix string) map[string]string {
	m := make(map[string]string)
	for _, f := range AllFiles {
		m[f] = prefix + ":" + f
	}
	return m
}

func logContains(logs []string, sub string) bool {
	for _, line := range logs {
		if strings.Contains(line, sub) {
			return true
		}
	}
	return false
}

// setModTime 给 6 个核心文件打上固定旧时间，用来断言它们没有被重写。
func setModTime(t *testing.T, dir string, mod time.Time) {
	t.Helper()
	for _, name := range AllFiles {
		if err := os.Chtimes(filepath.Join(dir, name), mod, mod); err != nil {
			t.Fatal(err)
		}
	}
}

func requireModTime(t *testing.T, dir string, want time.Time) {
	t.Helper()
	for _, name := range AllFiles {
		fi, err := os.Stat(filepath.Join(dir, name))
		if err != nil {
			t.Fatal(err)
		}
		if !fi.ModTime().Equal(want) {
			t.Fatalf("%s 被重写了（mtime=%s），原版文件不该被覆盖", name, fi.ModTime())
		}
	}
}

// 场景 1：本地已存在该版本的可信原版备份。
// 汉化前先从备份还原回原版，然后再写入。
func TestRun_WithExistingValidBackup(t *testing.T) {
	orig := canonicalFiles("original-v1")
	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	env.seedBackup(t, region.GLOBAL, "2026.10.16.0000.0000", orig)

	// 将游戏目录的文本文件弄脏（模拟已汉化或被修改）
	_ = os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("dirty-text-index"), 0o644)

	var logs []string
	textPatched := false
	fontPatched := false

	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		ReplaceFont: true,
		Log:         func(format string, args ...any) { logs = append(logs, fmt.Sprintf(format, args...)) },
		ApplyText: func(ctx context.Context) error {
			// 汉化替换执行时，验证文件已被先还原为原版
			cur, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
			if string(cur) != orig["0a0000.win32.index"] {
				t.Fatalf("替换文本前未还原至官方原版，当前内容: %s", string(cur))
			}
			textPatched = true
			return os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("patched-text"), 0o644)
		},
		ApplyFont: func(ctx context.Context) error {
			fontPatched = true
			return os.WriteFile(filepath.Join(env.sqpackDir, "000000.win32.index"), []byte("patched-font"), 0o644)
		},
	})

	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	// 只有文本被改动过；字体本来是原版，不需要还原。
	if !res.RestoredText || res.RestoredFont {
		t.Fatalf("应只标记文本已还原，实际: RestoredText=%v, RestoredFont=%v", res.RestoredText, res.RestoredFont)
	}
	if !textPatched || !fontPatched {
		t.Fatalf("汉化替换函数未被调用: text=%v, font=%v", textPatched, fontPatched)
	}
	if !logContains(logs, "[文本] 还原 0a0000.win32.index") {
		t.Fatalf("被改动的文本文件应记录还原动作，实际日志: %v", logs)
	}
	if !logContains(logs, DiagPrefix+"[字体] 客户端字体已是原版，无需还原") {
		t.Fatalf("字体未改动时应记录无需还原，实际日志: %v", logs)
	}
}

// 场景 2：本地无备份，但云端已收录该版本原版。
// 自动从云端下载建立本地备份并还原，然后写入。
func TestRun_NoBackup_CloudAvailable(t *testing.T) {
	orig := canonicalFiles("cloud-original-v2")
	srv := newMockServer(t, map[string]map[string]string{
		"2026.10.16.0000.0000": orig,
	})

	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	// 本地故意不放备份

	textPatched := false
	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		ReplaceFont: false,
		Cloud:       srv.client(),
		Repo:        testRepo,
		ApplyText: func(ctx context.Context) error {
			textPatched = true
			return nil
		},
	})

	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if len(res.Downloaded) != 6 {
		t.Fatalf("从云端建立备份应下载全部 6 个文件，实际下载: %d", len(res.Downloaded))
	}
	if !textPatched {
		t.Fatalf("文本替换未执行")
	}

	// 确认本地备份已生成且版本正确
	backupDir := filepath.Join(env.backupRoot, "global")
	meta, err := readBackupMeta(backupDir)
	if err != nil || meta == nil || meta.Version != "2026.10.16.0000.0000" {
		t.Fatalf("本地备份清单未正确生成: %+v, err=%v", meta, err)
	}
	if !meta.IsVerifiedOriginal() {
		t.Fatalf("云端下载创建的备份应标记为可信来源 (%s)，实际 source=%q", BackupSourceCloud, meta.Source)
	}
	if !IsBackupComplete(meta, backupDir, "2026.10.16.0000.0000") {
		t.Fatalf("生成的本地备份应为完整有效备份")
	}
}

// 场景 3：【云端没更新 + 游戏更新了 = 未知版本】。
// 提示用户警告，用户选择继续汉化。
// 核心规则：不备份文件，直接在当前文件写入。
func TestRun_UnknownVersion_ConfirmYes(t *testing.T) {
	srv := newMockServer(t, map[string]map[string]string{}) // 云端没有当前版本
	dirtyFiles := canonicalFiles("some-local-content")
	env := newTestEnv(t, "2026.11.01.0000.0000", dirtyFiles)

	confirmCalled := false
	textPatched := false

	_, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		Cloud:       srv.client(),
		Repo:        testRepo,
		ConfirmUnknown: func(ctx context.Context, version string) (bool, error) {
			confirmCalled = true
			return true, nil // 用户确认继续
		},
		ApplyText: func(ctx context.Context) error {
			textPatched = true
			return nil
		},
	})

	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if !confirmCalled {
		t.Fatalf("未知版本时应调用 ConfirmUnknown 回调提示用户")
	}
	if !textPatched {
		t.Fatalf("用户确认继续后应执行汉化写入")
	}

	backupDir := filepath.Join(env.backupRoot, "global")
	meta, _ := readBackupMeta(backupDir)
	if meta == nil || len(meta.Files) != len(AllFiles) {
		t.Fatalf("未知版本继续汉化时应创建本地还原基线备份，实际备份清单: %+v", meta)
	}
	if meta.Source != BackupSourceLocal || meta.IsVerifiedOriginal() {
		t.Fatalf("未知版本创建的本地备份不应标记为可信原版，实际 source=%q", meta.Source)
	}
}

func TestRun_LocalSnapshotReusedWithoutClobbering(t *testing.T) {
	srv := newMockServer(t, map[string]map[string]string{})
	initial := canonicalFiles("before-patch")
	env := newTestEnv(t, "2026.11.01.0000.0000", initial)

	confirmCalls := 0
	_, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.CHS,
		ReplaceText: true,
		Cloud:       srv.client(),
		Repo:        testRepo,
		ConfirmUnknown: func(ctx context.Context, version string) (bool, error) {
			confirmCalls++
			return true, nil
		},
		ApplyText: func(ctx context.Context) error {
			return os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("patched-once"), 0o644)
		},
	})
	if err != nil {
		t.Fatalf("首次 Run 失败: %v", err)
	}

	var logs []string
	res2, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.CHS,
		ReplaceText: true,
		Cloud:       srv.client(),
		Repo:        testRepo,
		Log:         func(format string, args ...any) { logs = append(logs, fmt.Sprintf(format, args...)) },
		ConfirmUnknown: func(ctx context.Context, version string) (bool, error) {
			confirmCalls++
			return true, nil
		},
		ApplyText: func(ctx context.Context) error {
			cur, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
			if string(cur) != initial["0a0000.win32.index"] {
				t.Fatalf("二次打补丁前应先从初次快照恢复，当前内容=%q", string(cur))
			}
			return nil
		},
	})
	if err != nil {
		t.Fatalf("第二次 Run 失败: %v", err)
	}
	if confirmCalls != 1 {
		t.Fatalf("复用同版本操作前快照时不应重复弹窗，confirmCalls=%d", confirmCalls)
	}
	if !res2.RestoredText {
		t.Fatalf("第二次 Run 应从本地快照恢复被修改的文本文件")
	}
	if !logContains(logs, "命中本地操作前快照") {
		t.Fatalf("日志应明确说明使用操作前快照，实际日志=%v", logs)
	}
}

func TestRun_GlobalLocalSnapshotUpgradesToCloudWhenAvailable(t *testing.T) {
	cloudOrig := canonicalFiles("official-global-v1")
	srv := newMockServer(t, map[string]map[string]string{
		"2026.11.01.0000.0000": cloudOrig,
	})
	localModified := canonicalFiles("preexisting-mod")
	env := newTestEnv(t, "2026.11.01.0000.0000", localModified)

	// 预先放一份未知版本时期保存的不可信本地快照
	backupDir := filepath.Join(env.backupRoot, "global")
	if _, err := createLocalBackupFromGameDir(context.Background(), Config{}, env.sqpackDir, backupDir, "2026.11.01.0000.0000", "global"); err != nil {
		t.Fatalf("创建本地快照失败: %v", err)
	}

	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		Cloud:       srv.client(),
		Repo:        testRepo,
		ApplyText: func(ctx context.Context) error {
			cur, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
			if string(cur) != cloudOrig["0a0000.win32.index"] {
				t.Fatalf("云端已收录后应下载并恢复到云端原版，当前=%q", string(cur))
			}
			return nil
		},
	})
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if len(res.Downloaded) != len(AllFiles) {
		t.Fatalf("云端已收录时应自动下载升级全部原版文件，实际=%d", len(res.Downloaded))
	}
	meta, err := readBackupMeta(backupDir)
	if err != nil || !meta.IsVerifiedOriginal() {
		t.Fatalf("升级后的备份应标记为 %s，实际=%+v, err=%v", BackupSourceCloud, meta, err)
	}
}

func TestRestore_LocalSnapshotFallbackAndCloudUpgrade(t *testing.T) {
	cloudOrig := canonicalFiles("official-global-restore")
	srv := newMockServer(t, map[string]map[string]string{
		"2026.11.01.0000.0000": cloudOrig,
	})
	localSnap := canonicalFiles("local-snap")
	env := newTestEnv(t, "2026.11.01.0000.0000", localSnap)

	globalBackup := filepath.Join(env.backupRoot, "global")
	if _, err := createLocalBackupFromGameDir(context.Background(), Config{}, env.sqpackDir, globalBackup, "2026.11.01.0000.0000", "global"); err != nil {
		t.Fatal(err)
	}
	chsBackup := filepath.Join(env.backupRoot, "chs")
	if _, err := createLocalBackupFromGameDir(context.Background(), Config{}, env.sqpackDir, chsBackup, "2026.11.01.0000.0000", "chs"); err != nil {
		t.Fatal(err)
	}

	// 先改脏游戏文件
	_ = os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("patched"), 0o644)

	// 国服无云端基准，使用本地快照兜底还原
	restoredCHS, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.CHS,
		Text:       true,
	})
	if err != nil || len(restoredCHS) == 0 {
		t.Fatalf("国服应可用本地快照还原: restored=%v err=%v", restoredCHS, err)
	}
	cur, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
	if string(cur) != localSnap["0a0000.win32.index"] {
		t.Fatalf("国服还原结果应匹配本地快照，实际=%q", string(cur))
	}

	// 国际服有云端收录时，应自动升级到云端官方原版还原
	restoredGlobal, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.GLOBAL,
		Cloud:      srv.client(),
		Repo:       testRepo,
		Text:       true,
	})
	if err != nil || len(restoredGlobal) == 0 {
		t.Fatalf("国际服应从云端升级并还原: restored=%v err=%v", restoredGlobal, err)
	}
	curGlobal, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
	if string(curGlobal) != cloudOrig["0a0000.win32.index"] {
		t.Fatalf("国际服应还原为云端官方原版，实际=%q", string(curGlobal))
	}
	meta, _ := readBackupMeta(globalBackup)
	if meta == nil || !meta.IsVerifiedOriginal() {
		t.Fatalf("国际服备份应升级为 %s，实际=%+v", BackupSourceCloud, meta)
	}
}

// 场景 4：未知版本，用户选择取消汉化。
func TestRun_UnknownVersion_ConfirmNo(t *testing.T) {
	srv := newMockServer(t, map[string]map[string]string{})
	env := newTestEnv(t, "2026.11.01.0000.0000", canonicalFiles("local"))

	textPatched := false
	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		Cloud:       srv.client(),
		Repo:        testRepo,
		ConfirmUnknown: func(ctx context.Context, version string) (bool, error) {
			return false, nil // 用户点击取消
		},
		ApplyText: func(ctx context.Context) error {
			textPatched = true
			return nil
		},
	})

	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if !res.Cancelled {
		t.Fatalf("用户拒绝后应标记 Cancelled = true")
	}
	if textPatched {
		t.Fatalf("用户取消时不应执行汉化写入")
	}
}

// 场景 5：手动还原 - 本地已有备份。
func TestRestore_WithBackup(t *testing.T) {
	orig := canonicalFiles("original-data")
	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	env.seedBackup(t, region.GLOBAL, "2026.10.16.0000.0000", orig)

	// 修改客户端
	_ = os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("modified-index"), 0o644)

	restored, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.GLOBAL,
	})
	if err != nil {
		t.Fatalf("Restore 失败: %v", err)
	}
	if len(restored) != 1 || restored[0] != "0a0000.win32.index" {
		t.Fatalf("只有被改动的索引文件需要还原，实际: %v", restored)
	}

	content, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
	if string(content) != orig["0a0000.win32.index"] {
		t.Fatalf("客户端文件未恢复为原版: %s", string(content))
	}
}

// 场景 6：手动还原 - 本地无备份，云端已收录。
func TestRestore_NoBackup_CloudAvailable(t *testing.T) {
	orig := canonicalFiles("cloud-original")
	srv := newMockServer(t, map[string]map[string]string{
		"2026.10.16.0000.0000": orig,
	})
	env := newTestEnv(t, "2026.10.16.0000.0000", canonicalFiles("dirty"))

	restored, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.GLOBAL,
		Cloud:      srv.client(),
		Repo:       testRepo,
	})
	if err != nil {
		t.Fatalf("Restore 失败: %v", err)
	}
	if len(restored) != 6 {
		t.Fatalf("客户端 6 个文件全被改动，还原应覆盖 6 个，实际: %d", len(restored))
	}

	content, _ := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
	if string(content) != orig["0a0000.win32.index"] {
		t.Fatalf("客户端文件未恢复为云端原版: %s", string(content))
	}
}

// 场景 7：手动还原 - 本地无备份且云端也未收录。
func TestRestore_NoBackup_NoCloud(t *testing.T) {
	srv := newMockServer(t, map[string]map[string]string{})
	env := newTestEnv(t, "2026.10.16.0000.0000", canonicalFiles("dirty"))

	_, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.GLOBAL,
		Cloud:      srv.client(),
		Repo:       testRepo,
	})
	if err == nil {
		t.Fatalf("无本地备份且云端未收录时，Restore 应报错")
	}
	if !strings.Contains(err.Error(), "官方启动器修复") {
		t.Fatalf("错误信息应提示官方启动器修复，实际: %v", err)
	}
}

// 场景 8：客户端区服探测。
func TestDetectClientRegion(t *testing.T) {
	tmpDir := t.TempDir()
	gameDir := filepath.Join(tmpDir, "game")
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	textIndex := filepath.Join(sqpackDir, "0a0000.win32.index")

	_ = os.WriteFile(textIndex, sqpack.CreateMockIndex("exd", "addon_0_chs.exd"), 0o644)
	reg, err := DetectClientRegion(gameDir)
	if err != nil || reg != region.China {
		t.Fatalf("国服客户端探测错误: reg=%s, err=%v", reg, err)
	}

	_ = os.WriteFile(textIndex, sqpack.CreateMockIndex("exd", "addon_0_ja.exd"), 0o644)
	reg, err = DetectClientRegion(gameDir)
	if err != nil || reg != region.Global {
		t.Fatalf("国际服客户端探测错误: reg=%s, err=%v", reg, err)
	}

	_ = os.WriteFile(textIndex, sqpack.CreateMockIndex("exd", "addon_0_tc.exd"), 0o644)
	reg, err = DetectClientRegion(gameDir)
	if err != nil || reg != region.Taiwan {
		t.Fatalf("繁中服客户端探测错误: reg=%s, err=%v", reg, err)
	}
}

// 场景 9：客户端本来就是官方原版。
// 汉化前的「确保原版」不该写任何文件，日志也不能声称正在还原。
func TestRun_AlreadyOriginal_SkipsRestore(t *testing.T) {
	orig := canonicalFiles("already-original")
	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	env.seedBackup(t, region.GLOBAL, "2026.10.16.0000.0000", orig)
	old := time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC)
	setModTime(t, env.sqpackDir, old)

	var logs []string
	textPatched := false
	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		ReplaceFont: true,
		Log:         func(format string, args ...any) { logs = append(logs, fmt.Sprintf(format, args...)) },
		ApplyText: func(ctx context.Context) error {
			textPatched = true
			return nil
		},
		ApplyFont: func(ctx context.Context) error { return nil },
	})
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if !textPatched {
		t.Fatalf("客户端已是原版时仍应继续汉化")
	}
	if res.RestoredText || res.RestoredFont {
		t.Fatalf("客户端已是原版，不应标记还原，实际: text=%v, font=%v", res.RestoredText, res.RestoredFont)
	}
	requireModTime(t, env.sqpackDir, old)

	if !logContains(logs, DiagPrefix+"[文本] 客户端文本已是原版，无需还原") ||
		!logContains(logs, DiagPrefix+"[字体] 客户端字体已是原版，无需还原") {
		t.Fatalf("日志应记录文本与字体已是原版，实际: %v", logs)
	}
	for _, line := range logs {
		if strings.Contains(line, "] 还原 ") || strings.Contains(line, "已从备份还原") {
			t.Fatalf("客户端已是原版时不应提示正在还原，实际: %s", line)
		}
	}
}

// 场景 10：手动还原时客户端已是官方原版，不应覆盖任何文件。
func TestRestore_AlreadyOriginal(t *testing.T) {
	orig := canonicalFiles("already-original")
	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	env.seedBackup(t, region.GLOBAL, "2026.10.16.0000.0000", orig)
	old := time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC)
	setModTime(t, env.sqpackDir, old)

	var logs []string
	restored, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.GLOBAL,
		Log:        func(format string, args ...any) { logs = append(logs, fmt.Sprintf(format, args...)) },
	})
	if err != nil {
		t.Fatalf("Restore 失败: %v", err)
	}
	if len(restored) != 0 {
		t.Fatalf("客户端已是原版时不应还原任何文件，实际: %v", restored)
	}
	requireModTime(t, env.sqpackDir, old)
	if logContains(logs, "[还原] 覆盖") {
		t.Fatalf("没有文件被改动时不应提示覆盖还原，实际: %v", logs)
	}
}

// 场景 11：汉化在写入阶段失败。Run 必须把「先回原版」到底改过哪些文件如实回报给
// 调用方（app.go 靠它决定日志措辞），并且自己不做任何回滚——写入的撤回由调用方负责。
func TestRun_WriteFailureReportsRestoredOriginals(t *testing.T) {
	orig := canonicalFiles("write-fail")
	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	env.seedBackup(t, region.GLOBAL, "2026.10.16.0000.0000", orig)
	// 文本索引被改脏（模拟上一次汉化或其它工具），字体系文件保持原版。
	if err := os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("dirty"), 0o644); err != nil {
		t.Fatal(err)
	}

	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		ReplaceFont: true,
		ApplyText:   func(ctx context.Context) error { return errors.New("模拟写入失败") },
		ApplyFont:   func(ctx context.Context) error { return nil },
	})
	if err == nil {
		t.Fatal("写入失败时应返回错误")
	}
	if res == nil {
		t.Fatal("出错时也要返回 Result，调用方需要知道先回原版时改过哪些文件")
	}
	if !res.RestoredText {
		t.Fatal("文本索引已从备份还原，RestoredText 应为 true")
	}
	if res.RestoredFont {
		t.Fatalf("字体本来就是原版，不应标记还原，实际: RestoredText=%v RestoredFont=%v", res.RestoredText, res.RestoredFont)
	}
	content, readErr := os.ReadFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
	if readErr != nil {
		t.Fatal(readErr)
	}
	if string(content) != orig["0a0000.win32.index"] {
		t.Fatal("先回原版后的文本索引应等于备份内容")
	}
}

func TestIsValidReleaseJSON(t *testing.T) {
	cases := []struct {
		name  string
		input string
		want  bool
	}{
		{"release list", `[{"tag_name":"patcher-v2.1.0"}]`, true},
		{"release object", `{"tag_name":"patcher-v2.1.0"}`, true},
		{"api error", `{"message":"API rate limit exceeded"}`, false},
		{"empty object", `{}`, false},
		{"empty list", `[]`, false},
		{"null", `null`, false},
		{"html error page", `<html>not found</html>`, false},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := isValidReleaseJSON([]byte(tc.input)); got != tc.want {
				t.Fatalf("isValidReleaseJSON(%s) = %v, want %v", tc.input, got, tc.want)
			}
		})
	}
}

type roundTripFunc func(*http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) {
	return f(r)
}

func httpStringResponse(status int, body string) *http.Response {
	return &http.Response{
		StatusCode: status,
		Body:       httpStringBody(body),
		Header:     make(http.Header),
	}
}

func httpStringBody(s string) *nopReadCloser {
	return &nopReadCloser{Reader: strings.NewReader(s)}
}

type nopReadCloser struct {
	*strings.Reader
}

func (nopReadCloser) Close() error { return nil }

func TestRaceGetDirectNotFoundSkipsSlowMirror(t *testing.T) {
	client := &http.Client{
		Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
			if r.URL.Host == "api.github.com" {
				return httpStringResponse(http.StatusNotFound, `{"message":"Not Found"}`), nil
			}
			<-r.Context().Done()
			return nil, r.Context().Err()
		}),
	}

	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()

	start := time.Now()
	_, _, err := raceGet(
		ctx,
		client,
		"https://api.github.com/repos/owner/repo/releases/tags/2026.11.03.0000.0000",
		[]string{"https://slow.mirror/https://api.github.com/repos/owner/repo/releases/tags/2026.11.03.0000.0000"},
		nil,
		1<<20,
		isValidReleaseJSON,
	)
	if !errors.Is(err, ErrNotFound) {
		t.Fatalf("期望返回 ErrNotFound，实际: %v", err)
	}
	if elapsed := time.Since(start); elapsed > 250*time.Millisecond {
		t.Fatalf("直连已返回 404 不应等待慢镜像超时，耗时 %v", elapsed)
	}
}

func TestRaceGetMirrorNotFoundWaitsForDirectSuccess(t *testing.T) {
	client := &http.Client{
		Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
			if r.URL.Host == "mirror.example" {
				return httpStringResponse(http.StatusNotFound, "not found"), nil
			}
			time.Sleep(30 * time.Millisecond)
			return httpStringResponse(http.StatusOK, `{"tag_name":"2026.10.16.0000.0000"}`), nil
		}),
	}

	body, source, err := raceGet(
		context.Background(),
		client,
		"https://api.github.com/repos/owner/repo/releases/tags/2026.10.16.0000.0000",
		[]string{"https://mirror.example/api"},
		nil,
		1<<20,
		isValidReleaseJSON,
	)
	if err != nil {
		t.Fatalf("镜像返回 404 不应影响直连成功: %v", err)
	}
	if !strings.Contains(string(body), "2026.10.16.0000.0000") || !strings.HasPrefix(source, "https://api.github.com/") {
		t.Fatalf("意外结果: source=%s body=%s", source, body)
	}
}

func TestReleaseByTagAPITimeoutFallsBackToExpandedAssets(t *testing.T) {
	const digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
	c := &GitHubClient{
		APIBase:         defaultAPIBase,
		AssetMirrors:    []string{"https://slow.mirror/", "https://fast.mirror/"},
		apiStageTimeout: 80 * time.Millisecond,
		HTTP: &http.Client{
			Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
				full := r.URL.String()
				if strings.Contains(full, "api.github.com") {
					if r.URL.Host == "slow.mirror" {
						<-r.Context().Done()
						return nil, r.Context().Err()
					}
					return httpStringResponse(http.StatusForbidden, `{"message":"API rate limit exceeded"}`), nil
				}
				if strings.Contains(full, "/releases/expanded_assets/") && r.URL.Host == "fast.mirror" {
					html := fmt.Sprintf(`<li><a href="/owner/repo/releases/download/2026.10.16.0000.0000/000000.win32.index">000000.win32.index</a><span>sha256:%s</span></li>`, digest)
					return httpStringResponse(http.StatusOK, html), nil
				}
				<-r.Context().Done()
				return nil, r.Context().Err()
			}),
		},
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	rel, err := c.ReleaseByTag(ctx, "owner/repo", "2026.10.16.0000.0000")
	if err != nil {
		t.Fatalf("API 阶段超时后应成功降级网页端: %v", err)
	}
	got, ok := rel.AssetSHA256("000000.win32.index")
	if !ok || got != digest {
		t.Fatalf("解析资产哈希不匹配: ok=%v got=%q want=%q", ok, got, digest)
	}
}

func TestReleaseByTagCachesSuccessResponse(t *testing.T) {
	const digest = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
	var apiHits int
	c := &GitHubClient{
		APIBase: "https://api.example.invalid",
		HTTP: &http.Client{
			Transport: roundTripFunc(func(r *http.Request) (*http.Response, error) {
				apiHits++
				payload := fmt.Sprintf(
					`{"tag_name":"2026.11.03.0000.0000","assets":[{"name":"000000.win32.index","browser_download_url":"https://example.invalid/000000.win32.index","digest":"sha256:%s"}]}`,
					digest,
				)
				return &http.Response{
					StatusCode: http.StatusOK,
					Header:     http.Header{"Content-Type": []string{"application/json"}},
					Body:       io.NopCloser(strings.NewReader(payload)),
				}, nil
			}),
		},
	}

	ctx := context.Background()
	rel1, err := c.ReleaseByTag(ctx, "owner/repo", "2026.11.03.0000.0000")
	if err != nil {
		t.Fatalf("首次 ReleaseByTag 失败: %v", err)
	}
	rel2, err := c.ReleaseByTag(ctx, "owner/repo", "2026.11.03.0000.0000")
	if err != nil {
		t.Fatalf("第二次 ReleaseByTag 失败: %v", err)
	}
	if apiHits != 1 {
		t.Fatalf("ReleaseByTag 应命中缓存，期望请求 1 次，实际 %d 次", apiHits)
	}
	if rel1 != rel2 {
		t.Fatalf("缓存命中应返回同一 Release 对象")
	}
}
