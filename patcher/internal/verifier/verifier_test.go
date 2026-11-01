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
