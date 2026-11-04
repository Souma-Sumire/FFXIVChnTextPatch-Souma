package verifier

import (
	"archive/zip"
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
	meta := &BackupMeta{Region: string(region.Of(lang)), Version: version, Files: map[string]FileHash{}}
	for name, content := range files {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, name), []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
		sum := sha256.Sum256([]byte(content))
		meta.Files[name] = FileHash{SHA256: hex.EncodeToString(sum[:]), Size: int64(len(content))}
	}
	if err := writeBackupMeta(dir, meta); err != nil {
		t.Fatal(err)
	}
}

func (e *testEnv) read(t *testing.T, name string) string {
	t.Helper()
	data, err := os.ReadFile(filepath.Join(e.sqpackDir, name))
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}

func (e *testEnv) readBackup(t *testing.T, lang region.Language, name string) string {
	t.Helper()
	data, err := os.ReadFile(filepath.Join(e.backupRoot, string(region.Of(lang)), name))
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}

func allContents(value string, names []string) map[string]string {
	out := make(map[string]string, len(names))
	for _, name := range names {
		out[name] = value + "/" + name
	}
	return out
}

func baseConfig(env *testEnv, lang region.Language) Config {
	return Config{
		GameDir:     env.gameDir,
		BackupRoot:  env.backupRoot,
		Language:    lang,
		Repo:        testRepo,
		ReplaceText: true,
	}
}

// TestMultiRegionIsolation 验证繁中服 / 韩服绝不访问国际服 GitHub Release。
func TestMultiRegionIsolation(t *testing.T) {
	for _, lang := range []region.Language{region.TC, region.KO} {
		t.Run(string(lang), func(t *testing.T) {
			version := "2026.10.16.0000.0000"
			text := allContents("tc-original", TextFiles)
			fonts := allContents("tc-font", FontFiles)
			files := map[string]string{}
			for k, v := range text {
				files[k] = v
			}
			for k, v := range fonts {
				files[k] = v
			}
			env := newTestEnv(t, version, files)
			env.seedBackup(t, lang, version, files)

			cloud := newMockServer(t, map[string]map[string]string{
				version: allContents("global-should-not-be-used", AllFiles),
			})
			cfg := baseConfig(env, lang)
			cfg.ReplaceFont = true
			cfg.Cloud = cloud.client()
			cfg.ApplyText = func(ctx context.Context) error { return nil }
			cfg.ApplyFont = func(ctx context.Context) error { return nil }

			result, err := Run(context.Background(), cfg)
			if err != nil {
				t.Fatalf("Run 失败: %v", err)
			}
			if got := cloud.requests.Load(); got != 0 {
				t.Fatalf("%s 不应访问国际服 Release，实际请求 %d 次", lang, got)
			}
			if !result.TextPatched {
				t.Fatalf("%s 文本未被汉化", lang)
			}
			if result.Plan.CloudState != CloudLocalOnly {
				t.Fatalf("%s 云端状态应为 localOnly，实际 %s", lang, result.Plan.CloudState)
			}
		})
	}
}

// TestNonGlobalWithoutBackupSucceeds 验证繁中服 / 韩服在本地无备份时，因原版哈希未知不阻断汉化，而是自动备份当前文件并汉化。
func TestNonGlobalWithoutBackupSucceeds(t *testing.T) {
	for _, lang := range []region.Language{region.TC, region.KO} {
		t.Run(string(lang), func(t *testing.T) {
			version := "2026.10.16.0000.0000"
			files := allContents("initial-game-files", AllFiles)
			env := newTestEnv(t, version, files)
			// 本地不提供任何预先备份

			cfg := baseConfig(env, lang)
			cfg.ReplaceFont = true
			cfg.ApplyText = func(ctx context.Context) error { return nil }
			cfg.ApplyFont = func(ctx context.Context) error { return nil }

			result, err := Run(context.Background(), cfg)
			if err != nil {
				t.Fatalf("%s 首次汉化不应失败: %v", lang, err)
			}
			if !result.TextPatched || !result.FontPatched {
				t.Fatalf("%s 汉化应成功完成", lang)
			}
			if result.Plan.Text.Action != ActionPatch || result.Plan.Font.Action != ActionPatch {
				t.Fatalf("%s 动作应为 ActionPatch，实际 text=%s font=%s",
					lang, result.Plan.Text.Action, result.Plan.Font.Action)
			}
			// 验证已自动生成备份
			meta, err := readBackupMeta(filepath.Join(env.backupRoot, string(region.Of(lang))))
			if err != nil || meta == nil || meta.Version != version {
				t.Fatalf("%s 应自动生成当前版本备份", lang)
			}
		})
	}
}

// TestVersionHigherThanCloud 验证本地版本高于云端时禁止下载旧版覆盖。
func TestVersionHigherThanCloud(t *testing.T) {
	localVersion := "2026.10.16.0000.0000"
	cloudVersion := "2026.10.10.0000.0000"
	files := allContents("local-original", AllFiles)
	env := newTestEnv(t, localVersion, files)

	cloud := newMockServer(t, map[string]map[string]string{
		cloudVersion: allContents("cloud-original", AllFiles),
	})
	cfg := baseConfig(env, region.EN)
	cfg.ReplaceFont = true
	cfg.Cloud = cloud.client()
	textCalls, fontCalls := 0, 0
	cfg.ApplyText = func(ctx context.Context) error { textCalls++; return nil }
	cfg.ApplyFont = func(ctx context.Context) error { fontCalls++; return nil }

	result, err := Run(context.Background(), cfg)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if cloud.downloads.Load() != 0 {
		t.Fatalf("本地版本更高时不应下载云端文件，实际下载 %d 次", cloud.downloads.Load())
	}
	if result.Plan.CloudState != CloudPending {
		t.Fatalf("云端状态应为 pending，实际 %s", result.Plan.CloudState)
	}
	if !result.TextPatched || !result.FontPatched || textCalls != 1 || fontCalls != 1 {
		t.Fatalf("应基于本地文件完成汉化 text=%v font=%v", result.TextPatched, result.FontPatched)
	}
	if len(result.Plan.Notes) == 0 {
		t.Fatal("应提示用户版本高于云端备份")
	}
	meta, err := readBackupMeta(filepath.Join(env.backupRoot, "global"))
	if err != nil || meta == nil {
		t.Fatalf("应生成本地备份清单: %v", err)
	}
	if meta.Version != localVersion {
		t.Fatalf("备份版本应为 %s，实际 %s", localVersion, meta.Version)
	}
}

// TestTextNonOriginalBackupLatest 验证本地已被修改但 backup 为最新原版时直接从 backup 还原。
func TestTextNonOriginalBackupLatest(t *testing.T) {
	version := "2026.10.16.0000.0000"
	original := map[string]string{}
	for k, v := range allContents("orig", TextFiles) {
		original[k] = v
	}
	for k, v := range allContents("orig-font", FontFiles) {
		original[k] = v
	}
	game := map[string]string{}
	for k, v := range allContents("patched", TextFiles) {
		game[k] = v
	}
	for k, v := range allContents("patched-font", FontFiles) {
		game[k] = v
	}
	env := newTestEnv(t, version, game)
	// 本地 backup 是最新原版文本。
	env.seedBackup(t, region.EN, version, original)

	cloud := newMockServer(t, map[string]map[string]string{version: original})
	cfg := baseConfig(env, region.EN)
	cfg.Cloud = cloud.client()
	textCalls := 0
	cfg.ApplyText = func(ctx context.Context) error { textCalls++; return nil }
	cfg.ApplyFont = func(ctx context.Context) error { return nil }

	result, err := Run(context.Background(), cfg)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if cloud.downloads.Load() != 0 {
		t.Fatalf("本地 backup 已是最新原版时不应下载，实际 %d 次", cloud.downloads.Load())
	}
	if result.Plan.Text.Action != ActionRestore {
		t.Fatalf("文本动作应为 restorePatch，实际 %s", result.Plan.Text.Action)
	}
	if !result.RestoredText || !result.TextPatched || textCalls != 1 {
		t.Fatal("应先从本地 backup 还原再执行文本汉化")
	}
	if got := env.read(t, TextFiles[0]); got != original[TextFiles[0]] {
		t.Fatalf("游戏文本文件未还原: %q", got)
	}
}

// TestTextNonOriginalBackupOld 验证本地 backup 也是旧版时自动下载最新原版。
func TestTextNonOriginalBackupOld(t *testing.T) {
	version := "2026.10.16.0000.0000"
	latest := map[string]string{}
	for k, v := range allContents("latest", TextFiles) {
		latest[k] = v
	}
	for k, v := range allContents("latest-font", FontFiles) {
		latest[k] = v
	}
	old := map[string]string{}
	for k, v := range allContents("old", TextFiles) {
		old[k] = v
	}
	game := map[string]string{}
	for k, v := range allContents("patched", TextFiles) {
		game[k] = v
	}
	for k, v := range allContents("patched-font", FontFiles) {
		game[k] = v
	}
	env := newTestEnv(t, version, game)
	env.seedBackup(t, region.EN, version, old)

	cloud := newMockServer(t, map[string]map[string]string{version: latest})
	cfg := baseConfig(env, region.EN)
	cfg.Cloud = cloud.client()
	cfg.ApplyText = func(ctx context.Context) error { return nil }
	cfg.ApplyFont = func(ctx context.Context) error { return nil }

	result, err := Run(context.Background(), cfg)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}
	if result.Plan.Text.Action != ActionDownload {
		t.Fatalf("文本动作应为 downloadRestorePatch，实际 %s", result.Plan.Text.Action)
	}
	if cloud.downloads.Load() != int32(len(TextFiles)) {
		t.Fatalf("应下载 %d 个文本原文件，实际 %d", len(TextFiles), cloud.downloads.Load())
	}
	if !result.RestoredText || !result.TextPatched {
		t.Fatal("下载后应还原并汉化")
	}
	for _, name := range TextFiles {
		if got := env.read(t, name); got != latest[name] {
			t.Fatalf("游戏文件 %s 未更新为最新原版", name)
		}
		if got := env.readBackup(t, region.EN, name); got != latest[name] {
			t.Fatalf("备份文件 %s 未更新为最新原版", name)
		}
	}
}

// TestFontFourBranches 覆盖 000000 字体的 4 分支状态机。
func TestFontFourBranches(t *testing.T) {
	version := "2026.10.16.0000.0000"
	original := map[string]string{}
	for k, v := range allContents("orig", TextFiles) {
		original[k] = v
	}
	for k, v := range allContents("orig-font", FontFiles) {
		original[k] = v
	}

	t.Run("原版且勾选替换字体", func(t *testing.T) {
		env := newTestEnv(t, version, original)
		cloud := newMockServer(t, map[string]map[string]string{version: original})
		cfg := baseConfig(env, region.EN)
		cfg.ReplaceFont = true
		cfg.Cloud = cloud.client()
		cfg.ApplyText = func(ctx context.Context) error { return nil }
		fontCalls := 0
		cfg.ApplyFont = func(ctx context.Context) error { fontCalls++; return nil }

		result, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}
		if result.Plan.Font.Action != ActionPatch || fontCalls != 1 || !result.FontPatched {
			t.Fatal("原版字体应备份后直接写入")
		}
		if _, err := os.Stat(filepath.Join(env.backupRoot, "global", FontFiles[0])); err != nil {
			t.Fatal("替换字体前应先备份原版字体")
		}
	})

	t.Run("非原版且勾选替换字体", func(t *testing.T) {
		game := map[string]string{}
		for k, v := range original {
			game[k] = v
		}
		for k, v := range allContents("patched-font", FontFiles) {
			game[k] = v
		}
		env := newTestEnv(t, version, game)
		env.seedBackup(t, region.EN, version, original)
		cloud := newMockServer(t, map[string]map[string]string{version: original})
		cfg := baseConfig(env, region.EN)
		cfg.ReplaceFont = true
		cfg.Cloud = cloud.client()
		cfg.ApplyText = func(ctx context.Context) error { return nil }
		cfg.ApplyFont = func(ctx context.Context) error { return nil }

		result, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}
		if result.Plan.Font.Action != ActionRestore || !result.RestoredFont || !result.FontPatched {
			t.Fatalf("非原版字体应从 backup 还原后再写入，实际 action=%s", result.Plan.Font.Action)
		}
		if cloud.downloads.Load() != 0 {
			t.Fatalf("backup 已是最新原版时不应下载，实际 %d", cloud.downloads.Load())
		}
	})

	t.Run("原版且未勾选替换字体需确认", func(t *testing.T) {
		env := newTestEnv(t, version, original)
		cloud := newMockServer(t, map[string]map[string]string{version: original})
		cfg := baseConfig(env, region.EN)
		cfg.ReplaceFont = false
		cfg.Cloud = cloud.client()
		textCalls := 0
		cfg.ApplyText = func(ctx context.Context) error { textCalls++; return nil }
		cfg.ApplyFont = func(ctx context.Context) error { return nil }
		confirmCalls := 0
		cfg.ConfirmFont = func(ctx context.Context) (bool, error) { confirmCalls++; return true, nil }

		result, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}
		if confirmCalls != 1 || !result.Plan.NeedFontConfirm {
			t.Fatal("原版字体未勾选替换时应触发确认")
		}
		if !result.TextPatched || textCalls != 1 || result.FontPatched {
			t.Fatal("确认后应继续文本汉化且不替换字体")
		}
	})

	t.Run("原版且未勾选替换字体取消", func(t *testing.T) {
		env := newTestEnv(t, version, original)
		cloud := newMockServer(t, map[string]map[string]string{version: original})
		cfg := baseConfig(env, region.EN)
		cfg.ReplaceFont = false
		cfg.Cloud = cloud.client()
		textCalls := 0
		cfg.ApplyText = func(ctx context.Context) error { textCalls++; return nil }
		cfg.ApplyFont = func(ctx context.Context) error { return nil }
		cfg.ConfirmFont = func(ctx context.Context) (bool, error) { return false, nil }

		result, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}
		if !result.Cancelled || textCalls != 0 {
			t.Fatal("用户取消后不应执行任何汉化")
		}
	})

	t.Run("非原版且未勾选替换字体静默跳过", func(t *testing.T) {
		game := map[string]string{}
		for k, v := range original {
			game[k] = v
		}
		for k, v := range allContents("patched-font", FontFiles) {
			game[k] = v
		}
		env := newTestEnv(t, version, game)
		cloud := newMockServer(t, map[string]map[string]string{version: original})
		cfg := baseConfig(env, region.EN)
		cfg.ReplaceFont = false
		cfg.Cloud = cloud.client()
		cfg.ApplyText = func(ctx context.Context) error { return nil }
		fontCalls := 0
		cfg.ApplyFont = func(ctx context.Context) error { fontCalls++; return nil }
		confirmCalls := 0
		cfg.ConfirmFont = func(ctx context.Context) (bool, error) { confirmCalls++; return true, nil }

		result, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}
		if confirmCalls != 0 || fontCalls != 0 || result.FontPatched {
			t.Fatal("非原版字体且未勾选替换时应静默跳过，0 副作用")
		}
		if !result.TextPatched {
			t.Fatal("文本汉化应继续执行")
		}
		if got := env.read(t, FontFiles[0]); got != game[FontFiles[0]] {
			t.Fatal("字体文件不应被修改")
		}
	})

	t.Run("云端Release缺少字体附件时基于本地文件打补丁", func(t *testing.T) {
		// 模拟实际生产环境：GitHub Release 只有文本文件，缺少字体附件
		cloudFiles := allContents("cloud-text-only", TextFiles)
		gameFiles := allContents("orig-text", TextFiles)
		for k, v := range allContents("orig-font", FontFiles) {
			gameFiles[k] = v
		}
		env := newTestEnv(t, version, gameFiles)
		cloud := newMockServer(t, map[string]map[string]string{version: cloudFiles})
		cfg := baseConfig(env, region.EN)
		cfg.ReplaceFont = true
		cfg.Cloud = cloud.client()
		cfg.ApplyText = func(ctx context.Context) error { return nil }
		cfg.ApplyFont = func(ctx context.Context) error { return nil }

		result, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败，不应因云端缺少字体附件而报错: %v", err)
		}
		if result.Plan.Font.Action != ActionPatch {
			t.Fatalf("云端无字体附件时应直接 ActionPatch，实际 %s", result.Plan.Font.Action)
		}
		if !result.FontPatched {
			t.Fatal("字体应成功打上补丁")
		}
	})
}

// TestRestoreWithCloudDownload 验证本地无备份时，Restore 自动从云端 Release 下载原文件并还原。
func TestRestoreWithCloudDownload(t *testing.T) {
	version := "2026.10.16.0000.0000"
	cloudFiles := allContents("cloud-original", AllFiles)
	gameFiles := allContents("patched-content", AllFiles)

	env := newTestEnv(t, version, gameFiles)
	// 本地故意不放备份（不调用 seedBackup）
	cloud := newMockServer(t, map[string]map[string]string{version: cloudFiles})

	progressCalls := 0
	restored, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.EN,
		Repo:       testRepo,
		Cloud:      cloud.client(),
		OnProgress: func(stage, detail string, done, total int64) {
			progressCalls++
		},
	})
	if err != nil {
		t.Fatalf("本地无备份时 Restore 失败: %v", err)
	}
	if len(restored) != len(AllFiles) {
		t.Fatalf("应还原全部 %d 个文件，实际还原 %d 个", len(AllFiles), len(restored))
	}
	if cloud.downloads.Load() != int32(len(AllFiles)) {
		t.Fatalf("应从云端下载 %d 个文件，实际下载 %d 次", len(AllFiles), cloud.downloads.Load())
	}
	for _, name := range AllFiles {
		if got := env.read(t, name); got != cloudFiles[name] {
			t.Fatalf("文件 %s 还原内容不匹配: got=%q want=%q", name, got, cloudFiles[name])
		}
	}
	if progressCalls == 0 {
		t.Fatal("应触发下载进度回调")
	}
}

func TestParseExpandedAssets(t *testing.T) {
	sampleHTML := `<div data-view-component="true" class="Box Box--condensed tmp-mt-3">
  <ul>
    <li>
      <a href="/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/2026.10.16.0000.0000/000000.win32.dat0" rel="nofollow">
        <span class="text-bold">000000.win32.dat0</span>
      </a>
      <span class="Truncate-text">sha256:7bfc4a74d0c8a2e382a1914f2a5d8e03c0a8998fde711aa672eac3eb1ea53a89</span>
    </li>
    <li>
      <a href="/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/2026.10.16.0000.0000/0a0000.win32.dat0" rel="nofollow">
        <span class="text-bold">0a0000.win32.dat0</span>
      </a>
      <clipboard-copy value="sha256:c4b8fa2053b8425b329e33a5d31098248096e69c57b772e7f1fdac5934594f6c"></clipboard-copy>
    </li>
  </ul>
</div>`

	rel, err := parseExpandedAssets(sampleHTML, "2026.10.16.0000.0000")
	if err != nil {
		t.Fatalf("parseExpandedAssets 失败: %v", err)
	}
	if len(rel.Assets) != 2 {
		t.Fatalf("应解析 2 个资产，实际解析 %d 个", len(rel.Assets))
	}
	f0 := rel.Assets["000000.win32.dat0"]
	if f0.DownloadURL != "https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/2026.10.16.0000.0000/000000.win32.dat0" {
		t.Fatalf("f0 download URL 错误: %s", f0.DownloadURL)
	}
	hash0, ok := rel.AssetSHA256("000000.win32.dat0")
	if !ok || hash0 != "7bfc4a74d0c8a2e382a1914f2a5d8e03c0a8998fde711aa672eac3eb1ea53a89" {
		t.Fatalf("f0 sha256 错误: %s (ok=%v)", hash0, ok)
	}
	hashA, ok := rel.AssetSHA256("0a0000.win32.dat0")
	if !ok || hashA != "c4b8fa2053b8425b329e33a5d31098248096e69c57b772e7f1fdac5934594f6c" {
		t.Fatalf("fA sha256 错误: %s (ok=%v)", hashA, ok)
	}
}

func TestLiveFallbackWhenRateLimited(t *testing.T) {
	client := NewGitHubClient()
	rel, err := client.ReleaseByTag(context.Background(), "Souma-Sumire/FFXIVChnTextPatch-Souma", "2026.10.16.0000.0000")
	if err != nil {
		t.Fatalf("ReleaseByTag 失败: %v", err)
	}
	t.Logf("Tag: %s, Total assets found: %d", rel.Tag, len(rel.Assets))
	for _, name := range AllFiles {
		asset, ok := rel.Assets[name]
		if !ok {
			t.Errorf("缺少资产: %s", name)
			continue
		}
		hash, hasHash := rel.AssetSHA256(name)
		t.Logf("Asset %s: url=%s sha256=%s (hasHash=%v)", name, asset.DownloadURL, hash, hasHash)
	}
}

func TestCandidateURLs(t *testing.T) {
	client := NewGitHubClient()
	const raw = "https://github.com/foo/bar/releases/download/v1.0/test.dat"
	cands := client.candidateURLs(raw)
	expectedCount := len(DefaultAssetMirrors()) + 1
	if len(cands) != expectedCount {
		t.Fatalf("候选数量期望 %d，实际 %d", expectedCount, len(cands))
	}
	if cands[0].mirror != "https://gh-proxy.com/" {
		t.Errorf("首选镜像期望 gh-proxy.com，实际 %s", cands[0].mirror)
	}
	if cands[len(cands)-1].mirror != "direct" || cands[len(cands)-1].url != raw {
		t.Errorf("末尾期望 direct 原链接，实际 %v", cands[len(cands)-1])
	}

	// 设置 preferredNode
	client.SetPreferredNode("https://wget.la/")
	cands2 := client.candidateURLs(raw)
	if cands2[0].mirror != "https://wget.la/" {
		t.Errorf("设置优先节点后首选期望 wget.la，实际 %s", cands2[0].mirror)
	}
}

func TestParseRecentReleasesHTML(t *testing.T) {
	html := `
		<div><a href="/owner/repo/releases/tag/2026.10.16.0000.0000">v1</a></div>
		<div><a href="/owner/repo/releases/tag/2026.10.15.0000.0000">v2</a></div>
		<div><a href="/owner/repo/releases/tag/2026.10.16.0000.0000">dup</a></div>
	`
	rels := parseRecentReleasesHTML(html, "owner/repo", 5)
	if len(rels) != 2 {
		t.Fatalf("解析 Release 数量期望 2，实际 %d", len(rels))
	}
	if rels[0].Tag != "2026.10.16.0000.0000" || rels[1].Tag != "2026.10.15.0000.0000" {
		t.Errorf("解析结果异常: %+v", rels)
	}
	if rels[0].URL != "https://github.com/owner/repo/releases/tag/2026.10.16.0000.0000" {
		t.Errorf("Release 链接应由传入仓库拼出，实际 %q", rels[0].URL)
	}
}

type roundTripFunc func(req *http.Request) (*http.Response, error)

func (f roundTripFunc) RoundTrip(req *http.Request) (*http.Response, error) {
	return f(req)
}

func TestDownloadWithCandidateFallback(t *testing.T) {
	dir := t.TempDir()
	dst := filepath.Join(dir, "out.bin")

	attempted := make([]string, 0)
	client := &GitHubClient{
		APIBase:      "https://api.github.com",
		AssetMirrors: []string{"https://mirror1.local/", "https://mirror2.local/"},
		HTTP: &http.Client{
			Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
				attempted = append(attempted, req.URL.String())
				if strings.Contains(req.URL.String(), "mirror1.local") {
					// 模拟节点 1 失败
					return &http.Response{
						StatusCode: http.StatusBadGateway,
						Body:       io.NopCloser(strings.NewReader("bad gateway")),
						Header:     make(http.Header),
					}, nil
				}
				// 节点 2 成功返回
				content := "hello-world"
				return &http.Response{
					StatusCode:    http.StatusOK,
					Body:          io.NopCloser(strings.NewReader(content)),
					ContentLength: int64(len(content)),
					Header:        make(http.Header),
				}, nil
			}),
		},
	}

	err := client.Download(context.Background(), "https://github.com/foo/bar/releases/download/v1/test.bin", dst, nil)
	if err != nil {
		t.Fatalf("Download 失败: %v", err)
	}
	data, _ := os.ReadFile(dst)
	if string(data) != "hello-world" {
		t.Errorf("下载内容不符: %s", string(data))
	}
	if len(attempted) != 2 {
		t.Errorf("尝试次数期望 2，实际 %d: %v", len(attempted), attempted)
	}
	if client.preferred() != "https://mirror2.local/" {
		t.Errorf("优选节点未更新为成功节点，当前: %s", client.preferred())
	}
}

func TestDownloadReportsNodeSwitches(t *testing.T) {
	dst := filepath.Join(t.TempDir(), "out.bin")

	var nodes []string
	client := &GitHubClient{
		APIBase:      "https://api.github.com",
		AssetMirrors: []string{"https://mirror1.local/", "https://mirror2.local/"},
		HTTP: &http.Client{
			Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
				if strings.Contains(req.URL.String(), "mirror1.local") {
					return &http.Response{
						StatusCode: http.StatusBadGateway,
						Body:       io.NopCloser(strings.NewReader("bad gateway")),
						Header:     make(http.Header),
					}, nil
				}
				content := "ok"
				return &http.Response{
					StatusCode:    http.StatusOK,
					Body:          io.NopCloser(strings.NewReader(content)),
					ContentLength: int64(len(content)),
					Header:        make(http.Header),
				}, nil
			}),
		},
	}
	client.SetNodeHook(func(node string) { nodes = append(nodes, node) })

	if err := client.Download(context.Background(), "https://github.com/foo/bar/releases/download/v1/test.bin", dst, nil); err != nil {
		t.Fatalf("Download 失败: %v", err)
	}
	want := []string{"mirror1.local", "mirror2.local"}
	if len(nodes) != len(want) || nodes[0] != want[0] || nodes[1] != want[1] {
		t.Fatalf("节点回调应只报告实际尝试过的节点，期望 %v，实际 %v", want, nodes)
	}

	// 回调清空后不应再有通知，避免界面在非更新流程里收到无关日志。
	nodes = nil
	client.SetNodeHook(nil)
	if err := client.Download(context.Background(), "https://github.com/foo/bar/releases/download/v1/test.bin", dst, nil); err != nil {
		t.Fatalf("Download 失败: %v", err)
	}
	if len(nodes) != 0 {
		t.Fatalf("清空回调后仍收到通知: %v", nodes)
	}
}

func TestZipArchiveAutoExtractionAndAlias(t *testing.T) {
	dir := t.TempDir()
	rawContent := "sqpack-raw-data-sample-content"
	expectedHash := "7b2d5f06126b4850fa104f69ad9d4439c7dc2e0e02ebf9149725f0a0d6e1564f"

	// 制作一个包含 000000.win32.dat0 的 zip 内存字节
	var zipBuf strings.Builder
	zw := zip.NewWriter(&zipBuf)
	f, err := zw.Create("000000.win32.dat0")
	if err != nil {
		t.Fatalf("创建 zip 内文件失败: %v", err)
	}
	if _, err := f.Write([]byte(rawContent)); err != nil {
		t.Fatalf("写入 zip 失败: %v", err)
	}
	_ = zw.Close()
	zipBytes := []byte(zipBuf.String())

	client := &GitHubClient{
		AssetMirrors: nil, // 仅走 direct
		HTTP: &http.Client{
			Transport: roundTripFunc(func(req *http.Request) (*http.Response, error) {
				return &http.Response{
					StatusCode:    http.StatusOK,
					Body:          io.NopCloser(strings.NewReader(string(zipBytes))),
					ContentLength: int64(len(zipBytes)),
					Header:        make(http.Header),
				}, nil
			}),
		},
	}

	dstFile := filepath.Join(dir, "000000.win32.dat0")
	if err := client.Download(context.Background(), "https://github.com/foo/bar/releases/download/v1/000000.win32.dat0.zip", dstFile, nil); err != nil {
		t.Fatalf("Download zip 自动解压失败: %v", err)
	}

	unzippedData, err := os.ReadFile(dstFile)
	if err != nil {
		t.Fatalf("读取解压后文件失败: %v", err)
	}
	if string(unzippedData) != rawContent {
		t.Errorf("解压后文件内容不符: %s", string(unzippedData))
	}

	// 验证 linkZipAliases 与 FileHashes
	rel := &Release{
		Tag: "v1.0",
		Assets: map[string]ReleaseAsset{
			"000000.win32.dat0.zip": {
				Name:        "000000.win32.dat0.zip",
				DownloadURL: "https://example.com/000000.win32.dat0.zip",
			},
		},
		FileHashes: map[string]string{
			"000000.win32.dat0": expectedHash,
		},
	}
	rel.linkZipAliases()

	asset, ok := rel.Assets["000000.win32.dat0"]
	if !ok || asset.DownloadURL != "https://example.com/000000.win32.dat0.zip" {
		t.Errorf("未能通过原文件名别名索引到 zip 资产: %+v", asset)
	}
	hash, hasHash := rel.AssetSHA256("000000.win32.dat0")
	if !hasHash || hash != expectedHash {
		t.Errorf("AssetSHA256 未能正确获取 FileHashes: %s (hasHash=%v)", hash, hasHash)
	}
}

func TestRestoreNonGlobalRules(t *testing.T) {
	for _, lang := range []region.Language{region.CHS, region.TC, region.KO} {
		t.Run(string(lang)+"本地无备份报错", func(t *testing.T) {
			env := newTestEnv(t, "2026.10.16.0000.0000", allContents("content", AllFiles))
			_, err := Restore(context.Background(), RestoreConfig{
				GameDir:    env.gameDir,
				BackupRoot: env.backupRoot,
				Language:   lang,
			})
			if err == nil {
				t.Fatalf("%s 无本地备份且无云端下载时 Restore 应返回错误", lang)
			}
			if !strings.Contains(err.Error(), "未收录云端原版下载") && !strings.Contains(err.Error(), "不存在") {
				t.Fatalf("%s 错误信息不符: %v", lang, err)
			}
		})

		t.Run(string(lang)+"游戏更新后旧版本备份无法还原", func(t *testing.T) {
			oldVersion := "2026.10.16.0000.0000"
			newVersion := "2026.10.23.0000.0000"
			original := allContents("tc-orig", AllFiles)
			patched := allContents("tc-patched", AllFiles)
			env := newTestEnv(t, newVersion, patched)
			env.seedBackup(t, lang, oldVersion, original)

			_, err := Restore(context.Background(), RestoreConfig{
				GameDir:    env.gameDir,
				BackupRoot: env.backupRoot,
				Language:   lang,
			})
			if err == nil {
				t.Fatalf("%s 游戏更新后旧备份 Restore 应报错", lang)
			}
			if !strings.Contains(err.Error(), "不一致") && !strings.Contains(err.Error(), "已更新") {
				t.Fatalf("%s 错误信息不符: %v", lang, err)
			}
		})

		t.Run(string(lang)+"备份文件被外部修改后无法还原", func(t *testing.T) {
			version := "2026.10.16.0000.0000"
			original := allContents("tc-orig", AllFiles)
			patched := allContents("tc-patched", AllFiles)
			env := newTestEnv(t, version, patched)
			env.seedBackup(t, lang, version, original)

			// 模拟备份文件被外部修改
			backupFile := filepath.Join(env.backupRoot, string(region.Of(lang)), AllFiles[0])
			_ = os.WriteFile(backupFile, []byte("externally-modified-content"), 0o644)

			_, err := Restore(context.Background(), RestoreConfig{
				GameDir:    env.gameDir,
				BackupRoot: env.backupRoot,
				Language:   lang,
			})
			if err == nil {
				t.Fatalf("%s 备份文件被修改后 Restore 应报错", lang)
			}
			if !strings.Contains(err.Error(), "不一致") && !strings.Contains(err.Error(), "修改") {
				t.Fatalf("%s 错误信息不符: %v", lang, err)
			}
		})

		t.Run(string(lang)+"本地有备份且未被修改可成功还原", func(t *testing.T) {
			version := "2026.10.16.0000.0000"
			original := allContents("tc-orig", AllFiles)
			patched := allContents("tc-patched", AllFiles)
			env := newTestEnv(t, version, patched)
			env.seedBackup(t, lang, version, original)

			restored, err := Restore(context.Background(), RestoreConfig{
				GameDir:    env.gameDir,
				BackupRoot: env.backupRoot,
				Language:   lang,
			})
			if err != nil {
				t.Fatalf("%s 有本地备份时 Restore 失败: %v", lang, err)
			}
			if len(restored) == 0 {
				t.Fatalf("%s 未还原任何文件", lang)
			}
			if got := env.read(t, AllFiles[0]); got != original[AllFiles[0]] {
				t.Fatalf("%s 游戏文件未成功从本地备份还原", lang)
			}
		})
	}
}

// TestFontConfirmRuleOnlyChinaSkips 验证字体弹窗规则：只有国服不需要字体弹窗，其他服（国际服、繁中服、韩服）都需要。
func TestFontConfirmRuleOnlyChinaSkips(t *testing.T) {
	version := "2026.10.16.0000.0000"
	files := allContents("orig", AllFiles)

	// 1. 国服不弹窗
	t.Run("国服不需要弹窗", func(t *testing.T) {
		env := newTestEnv(t, version, files)
		cfg := baseConfig(env, region.CHS)
		cfg.ReplaceText = true
		cfg.ReplaceFont = false
		confirmCalled := false
		cfg.ConfirmFont = func(ctx context.Context) (bool, error) {
			confirmCalled = true
			return true, nil
		}
		cfg.ApplyText = func(ctx context.Context) error { return nil }

		res, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}
		if res.Plan.NeedFontConfirm || confirmCalled {
			t.Fatalf("国服不应触发字体确认弹窗")
		}
	})

	// 2. 其他服（国际服、繁中服、韩服）都需要弹窗
	for _, lang := range []region.Language{region.GLOBAL, region.TC, region.KO} {
		t.Run(string(lang)+"需要字体弹窗", func(t *testing.T) {
			env := newTestEnv(t, version, files)
			cfg := baseConfig(env, lang)
			cfg.ReplaceText = true
			cfg.ReplaceFont = false
			confirmCalled := false
			cfg.ConfirmFont = func(ctx context.Context) (bool, error) {
				confirmCalled = true
				return true, nil
			}
			cfg.ApplyText = func(ctx context.Context) error { return nil }

			res, err := Run(context.Background(), cfg)
			if err != nil {
				t.Fatalf("%s Run 失败: %v", lang, err)
			}
			if !res.Plan.NeedFontConfirm || !confirmCalled {
				t.Fatalf("%s 应触发字体确认弹窗", lang)
			}
		})
	}
}

// TestNoRedundantBackupWhenAlreadyBackedUp 验证当前版本已有备份时汉化不会进行无意义的重复备份。
func TestNoRedundantBackupWhenAlreadyBackedUp(t *testing.T) {
	version := "2026.10.16.0000.0000"
	files := allContents("original-data", AllFiles)

	t.Run("原版客户端且已有备份不重复写备份", func(t *testing.T) {
		env := newTestEnv(t, version, files)
		env.seedBackup(t, region.GLOBAL, version, files)
		backupDir := filepath.Join(env.backupRoot, "global")

		modTimes := map[string]time.Time{}
		for _, name := range AllFiles {
			fi, err := os.Stat(filepath.Join(backupDir, name))
			if err != nil {
				t.Fatalf("读取备份文件失败: %v", err)
			}
			modTimes[name] = fi.ModTime()
		}

		time.Sleep(15 * time.Millisecond)

		cfg := baseConfig(env, region.GLOBAL)
		cfg.ReplaceText = true
		cfg.ReplaceFont = true
		cfg.ApplyText = func(ctx context.Context) error { return nil }
		cfg.ApplyFont = func(ctx context.Context) error { return nil }

		_, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}

		for _, name := range AllFiles {
			fi, err := os.Stat(filepath.Join(backupDir, name))
			if err != nil {
				t.Fatalf("读取备份文件失败: %v", err)
			}
			if !fi.ModTime().Equal(modTimes[name]) {
				t.Errorf("文件 %s 被重复覆盖写入了", name)
			}
		}
	})

	t.Run("已汉化客户端二次汉化直接从现有备份还原不产生新备份", func(t *testing.T) {
		patchedGame := allContents("patched-content", AllFiles)
		env := newTestEnv(t, version, patchedGame)
		env.seedBackup(t, region.GLOBAL, version, files)
		backupDir := filepath.Join(env.backupRoot, "global")

		metaBefore, err := readBackupMeta(backupDir)
		if err != nil {
			t.Fatalf("读取 meta 失败: %v", err)
		}

		cfg := baseConfig(env, region.GLOBAL)
		cfg.ReplaceText = true
		cfg.ReplaceFont = true
		cfg.ApplyText = func(ctx context.Context) error { return nil }
		cfg.ApplyFont = func(ctx context.Context) error { return nil }

		res, err := Run(context.Background(), cfg)
		if err != nil {
			t.Fatalf("Run 失败: %v", err)
		}

		if res.Plan.Text.Action != ActionRestore || res.Plan.Font.Action != ActionRestore {
			t.Fatalf("应为 ActionRestore，实际 Text=%s Font=%s", res.Plan.Text.Action, res.Plan.Font.Action)
		}

		metaAfter, err := readBackupMeta(backupDir)
		if err != nil {
			t.Fatalf("读取 meta 失败: %v", err)
		}

		for _, name := range AllFiles {
			if metaBefore.Files[name].SHA256 != metaAfter.Files[name].SHA256 {
				t.Fatalf("备份中的文件 %s 哈希被意外修改", name)
			}
		}
	})
}

// TestRunBacksUpAllSixFilesEvenWhenOnlyTextPatched 验证仅勾选文本汉化时，仍会备份全部 6 个核心原版文件。
func TestRunBacksUpAllSixFilesEvenWhenOnlyTextPatched(t *testing.T) {
	version := "2026.10.16.0000.0000"
	origFiles := allContents("original-data", AllFiles)

	env := newTestEnv(t, version, origFiles)
	cloud := newMockServer(t, map[string]map[string]string{version: origFiles})

	cfg := baseConfig(env, region.GLOBAL)
	cfg.ReplaceText = true
	cfg.ReplaceFont = false
	cfg.Cloud = cloud.client()
	cfg.ConfirmFont = func(ctx context.Context) (bool, error) { return true, nil }
	cfg.ApplyText = func(ctx context.Context) error { return nil }

	_, err := Run(context.Background(), cfg)
	if err != nil {
		t.Fatalf("Run 失败: %v", err)
	}

	backupDir := filepath.Join(env.backupRoot, "global")
	meta, err := readBackupMeta(backupDir)
	if err != nil || meta == nil {
		t.Fatalf("读取备份 meta 失败: %v", err)
	}

	if len(meta.Files) != len(AllFiles) {
		t.Fatalf("备份中应包含全部 %d 个核心文件，实际包含 %d 个", len(AllFiles), len(meta.Files))
	}

	for _, name := range AllFiles {
		if _, ok := meta.Files[name]; !ok {
			t.Fatalf("备份缺少核心文件记录: %s", name)
		}
		if _, err := os.Stat(filepath.Join(backupDir, name)); err != nil {
			t.Fatalf("备份物理文件缺失: %s", name)
		}
	}
}

// TestRestoreSkipsUnchangedFilesAndRestoresOnlyModified 验证还原时优先哈希对比，仅对被修改的文件写盘。
func TestRestoreSkipsUnchangedFilesAndRestoresOnlyModified(t *testing.T) {
	version := "2026.10.16.0000.0000"
	origFiles := allContents("original-data", AllFiles)

	// 构造游戏目录：文本 3 个文件被汉化修改，字体 3 个文件保持官方原版未修改
	gameFiles := map[string]string{}
	for _, name := range FontFiles {
		gameFiles[name] = origFiles[name]
	}
	for _, name := range TextFiles {
		gameFiles[name] = "patched-text-data"
	}

	env := newTestEnv(t, version, gameFiles)
	env.seedBackup(t, region.GLOBAL, version, origFiles)

	fontModTimesBefore := map[string]time.Time{}
	for _, name := range FontFiles {
		fi, err := os.Stat(filepath.Join(env.gameDir, "game", "sqpack", "ffxiv", name))
		if err != nil {
			t.Fatalf("获取字体文件状态失败: %v", err)
		}
		fontModTimesBefore[name] = fi.ModTime()
	}

	time.Sleep(15 * time.Millisecond)

	// 第一次还原：应仅还原 3 个被修改的文本文件，3 个字体文件跳过写入
	restored, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.EN,
	})
	if err != nil {
		t.Fatalf("Restore 失败: %v", err)
	}

	if len(restored) != len(TextFiles) {
		t.Fatalf("首次还原应仅写盘 %d 个文本文件，实际 %d 个: %v", len(TextFiles), len(restored), restored)
	}

	for _, name := range FontFiles {
		fi, err := os.Stat(filepath.Join(env.gameDir, "game", "sqpack", "ffxiv", name))
		if err != nil {
			t.Fatalf("获取字体文件状态失败: %v", err)
		}
		if !fi.ModTime().Equal(fontModTimesBefore[name]) {
			t.Errorf("原版字体文件 %s 不应被重复写入", name)
		}
	}

	for _, name := range TextFiles {
		if got := env.read(t, name); got != origFiles[name] {
			t.Fatalf("文本文件 %s 还原内容不匹配", name)
		}
	}

	// 第二次还原：所有 6 个文件均已是官方原版，应全部跳过写入（返回 0 个写盘文件）
	restoredSecond, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.EN,
	})
	if err != nil {
		t.Fatalf("二次 Restore 失败: %v", err)
	}

	if len(restoredSecond) != 0 {
		t.Fatalf("二次还原时所有文件已是原版，应跳过全部写入，实际还原了 %d 个文件: %v", len(restoredSecond), restoredSecond)
	}
}

func TestDetectClientRegion(t *testing.T) {
	tmpDir := t.TempDir()

	if _, err := DetectClientRegion(""); err == nil {
		t.Fatal("空路径应报错")
	}
	invalidDir := filepath.Join(tmpDir, "invalid")
	_ = os.MkdirAll(invalidDir, 0o755)
	if _, err := DetectClientRegion(invalidDir); err == nil {
		t.Fatal("缺少可执行文件应报错")
	}

	noIndexDir := filepath.Join(tmpDir, "no_index")
	_ = os.MkdirAll(filepath.Join(noIndexDir, "game"), 0o755)
	_ = os.WriteFile(filepath.Join(noIndexDir, "game", "ffxiv_dx11.exe"), []byte("mock"), 0o755)
	if _, err := DetectClientRegion(noIndexDir); err == nil {
		t.Fatal("缺少 0a0000.win32.index 应报错")
	}

	cases := []struct {
		name     string
		exdFiles []string
		want     region.Region
	}{
		{"china_client", []string{"addon_0_chs.exd"}, region.China},
		{"korea_client", []string{"addon_0_ko.exd"}, region.Korea},
		{"taiwan_tc_client", []string{"addon_0_tc.exd"}, region.Taiwan},
		{"global_cht_client", []string{"addon_0_cht.exd"}, region.Global},
		{"global_ja_client", []string{"addon_0_ja.exd"}, region.Global},
		{"global_en_client", []string{"addon_0_en.exd"}, region.Global},
	}

	for _, tc := range cases {
		dir := filepath.Join(tmpDir, tc.name)
		sqpackDir := filepath.Join(dir, "game", "sqpack", "ffxiv")
		if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, "game", "ffxiv_dx11.exe"), []byte("mock"), 0o755); err != nil {
			t.Fatal(err)
		}
		indexBytes := sqpack.CreateMockIndex("exd", tc.exdFiles...)
		if err := os.WriteFile(filepath.Join(sqpackDir, "0a0000.win32.index"), indexBytes, 0o644); err != nil {
			t.Fatal(err)
		}

		got, err := DetectClientRegion(dir)
		if err != nil {
			t.Fatalf("DetectClientRegion(%s) 失败: %v", tc.name, err)
		}
		if got != tc.want {
			t.Fatalf("DetectClientRegion(%s) = %s, want %s", tc.name, got, tc.want)
		}
	}
}

func TestCompareVersion(t *testing.T) {
	cases := []struct {
		a    string
		b    string
		want int
	}{
		{"2.1.2", "2.1.1", 1},
		{"2.1.1", "2.1.2", -1},
		{"2.1.1", "2.1.1", 0},
		{"2.1.1", "2.1.1-8-g28272842", 0},
		{"2.1.2", "2.1.1-8-g28272842", 1},
		{"2.1.0", "2.1.1-8-g28272842", -1},
		{"2026.10.16.0000.0000", "2026.10.15.0000.0000", 1},
	}
	for _, tc := range cases {
		got := CompareVersion(tc.a, tc.b)
		if got != tc.want {
			t.Errorf("CompareVersion(%q, %q) = %d, want %d", tc.a, tc.b, got, tc.want)
		}
	}
}

// TestRestoreGlobalRejectsStaleBackupWithoutCloud 验证国际服在游戏更新后，
// 若拿不到当前版本的云端原文件，不能用旧版本备份覆盖客户端，
// 且失败时不得把备份清单的版本号改写成当前版本。
func TestRestoreGlobalRejectsStaleBackupWithoutCloud(t *testing.T) {
	oldVersion := "2026.10.16.0000.0000"
	newVersion := "2026.10.23.0000.0000"
	original := allContents("global-orig", AllFiles)
	patched := allContents("global-patched", AllFiles)

	env := newTestEnv(t, newVersion, patched)
	env.seedBackup(t, region.EN, oldVersion, original)

	_, err := Restore(context.Background(), RestoreConfig{
		GameDir:    env.gameDir,
		BackupRoot: env.backupRoot,
		Language:   region.EN,
	})
	if err == nil {
		t.Fatal("国际服旧版本备份且无云端来源时 Restore 应报错")
	}
	if !strings.Contains(err.Error(), "不一致") {
		t.Fatalf("错误信息应说明版本不一致: %v", err)
	}

	meta, readErr := readBackupMeta(filepath.Join(env.backupRoot, region.BackupFolder(region.EN)))
	if readErr != nil {
		t.Fatalf("读取备份清单失败: %v", readErr)
	}
	if meta == nil || meta.Version != oldVersion {
		t.Fatalf("失败时不应改写备份清单版本号，got=%v want=%s", meta, oldVersion)
	}
	if got := env.read(t, AllFiles[0]); got != patched[AllFiles[0]] {
		t.Fatal("还原被拒绝时不应改动游戏文件")
	}
}

// TestRestoreSequenceIndexBeforeData 保证还原时先写索引再写 dat0：
// 中途失败时索引仍指向未被截断的原数据，客户端不会读到坏表。
func TestRestoreSequenceIndexBeforeData(t *testing.T) {
	ordered := restoreSequence(AllFiles)
	if len(ordered) != len(AllFiles) {
		t.Fatalf("还原顺序应覆盖全部文件: got=%d want=%d", len(ordered), len(AllFiles))
	}
	seenData := false
	for _, name := range ordered {
		if strings.HasSuffix(name, ".dat0") {
			seenData = true
			continue
		}
		if seenData {
			t.Fatalf("索引 %s 不应排在 dat0 之后: %v", name, ordered)
		}
	}
}

// TestDetermineOriginalNonGlobal 锁定非国际服的判定规则：
// 没有可用备份时把当前客户端当原版，只用 patched_records 识别已汉化，
// 不再依赖来自国际服的 dat0 体积启发式。
func TestDetermineOriginalNonGlobal(t *testing.T) {
	hashes := map[string]FileHash{
		"0a0000.win32.index": {SHA256: "text-index-sha"},
		"000000.win32.index": {SHA256: "font-index-sha"},
	}
	empty := map[string]FileHash{}

	textOrig, fontOrig := determineOriginal(region.China, CloudLocalOnly, "v1", nil, empty, nil, nil)
	if !textOrig || !fontOrig {
		t.Fatalf("无备份且无记录时应视为原版，got text=%v font=%v", textOrig, fontOrig)
	}

	patched := map[string]string{"0a0000.win32.index": "text-index-sha"}
	textOrig, fontOrig = determineOriginal(region.China, CloudLocalOnly, "v1", nil, hashes, nil, patched)
	if textOrig {
		t.Fatal("记录命中文本索引时应判定为已汉化")
	}
	if !fontOrig {
		t.Fatal("字体索引未被记录命中，应仍视为原版")
	}
}

// TestNonGlobalPatchWithLargeFontDat 验证自带大字体包的客户端不会被启发式挡住：
// 非国际服没有云端原文件，只要没有汉化记录就应照常建立备份并汉化。
func TestNonGlobalPatchWithLargeFontDat(t *testing.T) {
	version := "2026.10.16.0000.0000"
	files := allContents("initial-game-files", AllFiles)
	env := newTestEnv(t, version, files)
	// 国际服的“已修改”阈值是 90MB，自带中文字库的客户端本来就会超过它
	if err := os.Truncate(filepath.Join(env.sqpackDir, "000000.win32.dat0"), 91*1024*1024); err != nil {
		t.Fatal(err)
	}

	cfg := baseConfig(env, region.CHS)
	cfg.ReplaceText = true
	cfg.ReplaceFont = true
	cfg.ApplyText = func(ctx context.Context) error { return nil }
	cfg.ApplyFont = func(ctx context.Context) error { return nil }

	result, err := Run(context.Background(), cfg)
	if err != nil {
		t.Fatalf("非国际服不应因字体包体积被拒绝汉化: %v", err)
	}
	if result.Plan.Text.Action != ActionPatch || result.Plan.Font.Action != ActionPatch {
		t.Fatalf("动作应为 ActionPatch，实际 text=%s font=%s", result.Plan.Text.Action, result.Plan.Font.Action)
	}
}

// TestCopyFileVerifiesHash 验证复制在哈希不符时不会覆盖目标文件。
func TestCopyFileVerifiesHash(t *testing.T) {
	dir := t.TempDir()
	src := filepath.Join(dir, "src.bin")
	dst := filepath.Join(dir, "dst.bin")
	if err := os.WriteFile(src, []byte("content"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(dst, []byte("original"), 0o644); err != nil {
		t.Fatal(err)
	}

	if _, err := copyFile(src, dst, strings.Repeat("0", 64)); err == nil {
		t.Fatal("哈希不符时应报错")
	}
	data, err := os.ReadFile(dst)
	if err != nil {
		t.Fatal(err)
	}
	if string(data) != "original" {
		t.Fatalf("校验失败时不应覆盖目标文件，got=%q", data)
	}
	if _, err := os.Stat(dst + ".tmp"); err == nil {
		t.Fatal("校验失败时应清理临时文件")
	}

	got, err := copyFile(src, dst, "")
	if err != nil {
		t.Fatalf("不比对哈希时应成功: %v", err)
	}
	if got.SHA256 == "" || got.Size != int64(len("content")) {
		t.Fatalf("应返回复制结果的哈希与大小，got=%+v", got)
	}
}

// TestBackupOverwriteGuardRejectsConflict 验证备份与游戏文件内容不一致时不会静默覆盖备份。
func TestBackupOverwriteGuardRejectsConflict(t *testing.T) {
	version := "2026.10.16.0000.0000"

	env := newTestEnv(t, version, allContents("game-content", AllFiles))
	env.seedBackup(t, region.CHS, version, allContents("backup-content", AllFiles))
	srcDir := filepath.Join(env.backupRoot, region.BackupFolder(region.CHS))

	meta, err := readBackupMeta(srcDir)
	if err != nil || meta == nil {
		t.Fatalf("读取备份清单失败: %v", err)
	}
	gameHashes, err := hashAll(env.sqpackDir, AllFiles)
	if err != nil {
		t.Fatal(err)
	}
	// 用备份目录作为目标，制造“已有当前版本备份但内容不同”的冲突
	if _, err := ensureAllOriginalFilesBackedUp(context.Background(), Config{ReplaceText: true}, &Result{}, region.China, CloudLocalOnly, version, meta, srcDir, env.sqpackDir, gameHashes, nil, true, true); err == nil {
		t.Fatal("备份与游戏目录内容冲突时应拒绝覆盖")
	}
}

// TestBackupLogReflectsActualWork 验证日志与实际动作一致：备份已就绪时不再声称“正在建立备份”。
func TestBackupLogReflectsActualWork(t *testing.T) {
	version := "2026.10.16.0000.0000"
	contents := allContents("game-content", AllFiles)
	env := newTestEnv(t, version, contents)
	env.seedBackup(t, region.CHS, version, contents)
	backupDir := filepath.Join(env.backupRoot, region.BackupFolder(region.CHS))

	meta, err := readBackupMeta(backupDir)
	if err != nil || meta == nil {
		t.Fatalf("读取备份清单失败: %v", err)
	}
	gameHashes, err := hashAll(env.sqpackDir, AllFiles)
	if err != nil {
		t.Fatal(err)
	}

	var logs []string
	cfg := Config{
		ReplaceText: true,
		Log: func(format string, args ...any) {
			logs = append(logs, fmt.Sprintf(format, args...))
		},
	}

	if _, err := ensureAllOriginalFilesBackedUp(context.Background(), cfg, &Result{}, region.China, CloudLocalOnly, version, meta, backupDir, env.sqpackDir, gameHashes, nil, true, true); err != nil {
		t.Fatalf("备份已就绪时不应失败: %v", err)
	}
	for _, entry := range logs {
		if strings.Contains(entry, "正在将官方原版") {
			t.Fatalf("备份已就绪时不应提示建立备份，got=%v", logs)
		}
	}

	logs = nil
	if err := os.RemoveAll(backupDir); err != nil {
		t.Fatal(err)
	}
	if _, err := ensureAllOriginalFilesBackedUp(context.Background(), cfg, &Result{}, region.China, CloudLocalOnly, version, nil, backupDir, env.sqpackDir, gameHashes, nil, true, true); err != nil {
		t.Fatalf("备份缺失时应重建: %v", err)
	}
	if len(logs) == 0 || !strings.Contains(logs[0], "正在将官方原版") {
		t.Fatalf("备份缺失时应提示建立备份，got=%v", logs)
	}
}

// TestBackupBeforeApply 验证文本与字体的原文件备份全部完成后才写入汉化数据，
// 不会出现“备份文本 → 改文本 → 备份字体 → 改字体”的交叉顺序。
func TestBackupBeforeApply(t *testing.T) {
	version := "2026.10.16.0000.0000"
	env := newTestEnv(t, version, allContents("original", AllFiles))

	var order []string
	cfg := baseConfig(env, region.CHS)
	cfg.ReplaceFont = true
	cfg.Log = func(format string, args ...any) {
		order = append(order, fmt.Sprintf(format, args...))
	}
	cfg.ApplyText = func(ctx context.Context) error {
		order = append(order, "apply-text")
		return nil
	}
	cfg.ApplyFont = func(ctx context.Context) error {
		order = append(order, "apply-font")
		return nil
	}

	if _, err := Run(context.Background(), cfg); err != nil {
		t.Fatalf("Run 失败: %v", err)
	}

	textIdx, fontIdx, lastBackup := -1, -1, -1
	for i, entry := range order {
		switch {
		case entry == "apply-text":
			textIdx = i
		case entry == "apply-font":
			fontIdx = i
		}
		if strings.Contains(entry, "备份") {
			lastBackup = i
		}
	}
	if textIdx < 0 || fontIdx < 0 {
		t.Fatalf("应分别写入文本与字体，order=%v", order)
	}
	if lastBackup < 0 {
		t.Fatalf("应输出备份日志，order=%v", order)
	}
	if lastBackup > textIdx {
		t.Fatalf("全部备份必须早于文本写入，order=%v", order)
	}
	if textIdx > fontIdx {
		t.Fatalf("文本应先于字体写入，order=%v", order)
	}
}

// 非国际服没有官方哈希，手动还原前必须确认当前文件仍是本工具汉化的产物，
// 否则旧备份可能对应游戏更新前的内容，覆盖会破坏客户端。
func TestRestoreNonGlobalRequiresPatchedProof(t *testing.T) {
	version := "2026.10.16.0000.0000"
	original := allContents("original", AllFiles)
	patched := allContents("patched", AllFiles)
	env := newTestEnv(t, version, patched)
	env.seedBackup(t, region.CHS, version, original)

	cfg := RestoreConfig{
		GameDir:             env.gameDir,
		BackupRoot:          env.backupRoot,
		Language:            region.CHS,
		RequirePatchedState: true,
		PatchedIndexHashes: map[string]string{
			"0a0000.win32.index": strings.Repeat("0", 64),
		},
	}
	if _, err := Restore(context.Background(), cfg); err == nil {
		t.Fatal("索引哈希与汉化记录不符时应拒绝还原")
	}
	if got := env.read(t, "0a0000.win32.index"); got != "patched/0a0000.win32.index" {
		t.Fatalf("拒绝还原时不应改写游戏文件，got=%q", got)
	}

	// 缺少汉化记录时同样无法证明备份仍对应当前内容
	cfg.PatchedIndexHashes = nil
	if _, err := Restore(context.Background(), cfg); err == nil {
		t.Fatal("缺少汉化记录时应拒绝还原")
	}

	// 记录命中当前索引时允许还原
	textHash, err := HashFile(filepath.Join(env.sqpackDir, "0a0000.win32.index"))
	if err != nil {
		t.Fatal(err)
	}
	cfg.PatchedIndexHashes = map[string]string{"0a0000.win32.index": textHash.SHA256}
	if _, err := Restore(context.Background(), cfg); err != nil {
		t.Fatalf("汉化产物状态确认后应允许还原: %v", err)
	}
	if got := env.read(t, "0a0000.win32.index"); got != "original/0a0000.win32.index" {
		t.Fatalf("还原后应回到备份内容，got=%q", got)
	}
}

// 直连被阻断时，只要任一镜像可用就应返回镜像结果，而不是整体失败。
func TestRaceGetUsesMirrorWhenDirectFails(t *testing.T) {
	direct := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "blocked", http.StatusServiceUnavailable)
	}))
	defer direct.Close()
	mirror := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"ok":true}`))
	}))
	defer mirror.Close()

	body, source, err := raceGet(context.Background(), mirror.Client(), direct.URL, []string{mirror.URL}, nil, 1<<20, json.Valid)
	if err != nil {
		t.Fatalf("直连不可用时应回退到镜像: %v", err)
	}
	if source != mirror.URL {
		t.Errorf("结果应来自镜像，实际 %s", source)
	}
	if string(body) != `{"ok":true}` {
		t.Errorf("响应体异常: %s", body)
	}
}

// 直连 404 表示资源确实不存在；镜像 404 只代表该镜像不支持这个路径，不能据此下结论。
func TestRaceGetNotFoundOnlyFromDirect(t *testing.T) {
	direct404 := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.NotFound(w, r)
	}))
	defer direct404.Close()
	mirrorOK := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{}`))
	}))
	defer mirrorOK.Close()
	// 镜像返回合法 JSON 时以镜像为准，即便直连已经 404。
	if _, _, err := raceGet(context.Background(), mirrorOK.Client(), direct404.URL, []string{mirrorOK.URL}, nil, 1<<20, json.Valid); err != nil {
		t.Fatalf("镜像可用时不应返回错误: %v", err)
	}

	mirror404 := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.NotFound(w, r)
	}))
	defer mirror404.Close()
	if _, _, err := raceGet(context.Background(), mirror404.Client(), direct404.URL, []string{mirror404.URL}, nil, 1<<20, json.Valid); !errors.Is(err, ErrNotFound) {
		t.Fatalf("直连 404 应判定资源不存在，实际 %v", err)
	}

	direct500 := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "blocked", http.StatusServiceUnavailable)
	}))
	defer direct500.Close()
	if _, _, err := raceGet(context.Background(), mirror404.Client(), direct500.URL, []string{mirror404.URL}, nil, 1<<20, json.Valid); errors.Is(err, ErrNotFound) {
		t.Fatalf("只有镜像 404 时不应判定资源不存在")
	}
}

// 慢节点不能拖住整体流程：镜像先返回时应立即结束，不等待直连超时。
func TestRaceGetDoesNotWaitForSlowDirect(t *testing.T) {
	slow := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(1500 * time.Millisecond)
		_, _ = w.Write([]byte(`{"slow":true}`))
	}))
	defer slow.Close()
	fast := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"fast":true}`))
	}))
	defer fast.Close()

	start := time.Now()
	body, _, err := raceGet(context.Background(), fast.Client(), slow.URL, []string{fast.URL}, nil, 1<<20, json.Valid)
	if err != nil {
		t.Fatalf("应返回快速节点的结果: %v", err)
	}
	if !strings.Contains(string(body), "fast") {
		t.Errorf("应使用快速节点的响应，实际 %s", body)
	}
	if elapsed := time.Since(start); elapsed > time.Second {
		t.Errorf("不应等待慢节点，实际耗时 %v", elapsed)
	}
}

func TestMirrorDownloadURL(t *testing.T) {
	client := NewGitHubClient()
	const raw = "https://github.com/foo/bar/releases/download/v1.0/app.exe"

	got := client.MirrorDownloadURL(raw)
	want := mirrorURL(DefaultAssetMirrors()[0], raw)
	if got != want {
		t.Errorf("未确定节点时应使用首个镜像，期望 %q 实际 %q", want, got)
	}

	client.SetPreferredNode("https://wget.la/")
	if got := client.MirrorDownloadURL(raw); got != mirrorURL("https://wget.la/", raw) {
		t.Errorf("应优先使用已确认的节点，实际 %q", got)
	}

	client.SetPreferredNode("direct")
	if got := client.MirrorDownloadURL(raw); got != "" {
		t.Errorf("确认可直连时不应再提供镜像地址，实际 %q", got)
	}

	if got := client.MirrorDownloadURL("https://example.com/app.exe"); got != "" {
		t.Errorf("非 GitHub 地址不应套镜像，实际 %q", got)
	}
}

// 国内常见的组合是：api.github.com 能连、Release 附件直连很慢。
// 因此只有实测过大文件直连速度才允许隐藏镜像入口；小文件下载成功不算数。
func TestDirectSpeedGateKeepsMirrorForSlowDirect(t *testing.T) {
	c := NewGitHubClient()
	raw := "https://github.com/o/r/releases/download/v1/app.exe"
	if c.MirrorDownloadURL(raw) == "" {
		t.Fatal("默认应提供镜像下载地址")
	}

	// 几十 KB 的文件在慢速链路上也会瞬间完成，不能据此认定直连可用。
	c.rememberDirectSpeed(300<<10, 10*time.Millisecond)
	if c.MirrorDownloadURL(raw) == "" {
		t.Error("小样本不应判定直连可用")
	}

	// 大文件但速度慢（约 200 KB/s）
	c.rememberDirectSpeed(4<<20, 20*time.Second)
	if c.MirrorDownloadURL(raw) == "" {
		t.Error("慢速直连应保留镜像入口")
	}

	// 大文件且实测达标
	c.rememberDirectSpeed(8<<20, 500*time.Millisecond)
	if c.MirrorDownloadURL(raw) != "" {
		t.Error("实测直连够快时不应再提供镜像入口")
	}
}

func TestCandidateOrderPutsDirectLastUntilMeasuredFast(t *testing.T) {
	c := NewGitHubClient()
	raw := "https://github.com/o/r/releases/download/v1/app.exe"

	got := c.candidateURLs(raw)
	if len(got) == 0 || got[len(got)-1].mirror != "direct" {
		t.Fatalf("未确认直连速度时应把直连排在最后，实际 %+v", got)
	}
	if got[0].mirror == "direct" {
		t.Error("未确认直连速度时不应首选直连")
	}

	c.rememberDirectSpeed(8<<20, 500*time.Millisecond)
	got = c.candidateURLs(raw)
	if got[0].mirror != "direct" {
		t.Errorf("实测直连够快时应首选直连，实际 %+v", got)
	}
}

// 端到端：小文件走直连成功后，仍然要给出镜像地址。
func TestSmallDownloadDoesNotMarkDirectFast(t *testing.T) {
	payload := strings.Repeat("x", 1024)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(payload))
	}))
	defer srv.Close()

	c := NewGitHubClient()
	c.HTTP = srv.Client()
	dst := filepath.Join(t.TempDir(), "small.bin")
	if err := c.Download(context.Background(), srv.URL+"/small.bin", dst, nil); err != nil {
		t.Fatalf("下载失败: %v", err)
	}
	if c.isDirectFast() {
		t.Error("小文件下载成功不应标记直连可用")
	}
	if c.MirrorDownloadURL("https://github.com/o/r/releases/download/v1/app.exe") == "" {
		t.Error("应继续提供镜像下载地址")
	}
}
