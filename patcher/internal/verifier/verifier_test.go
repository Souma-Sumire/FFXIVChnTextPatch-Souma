package verifier

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync/atomic"
	"testing"

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
	meta := &BackupMeta{Region: string(region.Of(lang)), Version: version, Files: map[string]FileHash{}}
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

// 场景 1：本地已存在该版本的可信原版备份。
// 汉化前先确保从备份还原回原版，然后再写入。
func TestRun_WithExistingValidBackup(t *testing.T) {
	orig := canonicalFiles("original-v1")
	env := newTestEnv(t, "2026.10.16.0000.0000", orig)
	env.seedBackup(t, region.GLOBAL, "2026.10.16.0000.0000", orig)

	// 将游戏目录的文本文件弄脏（模拟已汉化或被修改）
	_ = os.WriteFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"), []byte("dirty-text-index"), 0o644)

	textPatched := false
	fontPatched := false

	res, err := Run(context.Background(), Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    region.GLOBAL,
		ReplaceText: true,
		ReplaceFont: true,
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
	if !res.RestoredText || !res.RestoredFont {
		t.Fatalf("应标记文本与字体均已还原，实际: RestoredText=%v, RestoredFont=%v", res.RestoredText, res.RestoredFont)
	}
	if !textPatched || !fontPatched {
		t.Fatalf("汉化替换函数未被调用: text=%v, font=%v", textPatched, fontPatched)
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

	// 核心验证：绝对不可备份不可信的文件！
	backupDir := filepath.Join(env.backupRoot, "global")
	meta, _ := readBackupMeta(backupDir)
	if meta != nil && len(meta.Files) > 0 {
		t.Fatalf("未知版本继续汉化时不应创建本地备份，但发现了备份: %+v", meta)
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
	if len(restored) != 6 {
		t.Fatalf("还原应恢复全部 6 个文件，实际: %d", len(restored))
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
		t.Fatalf("还原应恢复 6 个文件，实际: %d", len(restored))
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
