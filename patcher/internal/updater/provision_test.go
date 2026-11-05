package updater

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
)

type mockNode struct {
	srv         *httptest.Server
	manifest    Manifest
	zipData     []byte
	fontZipData []byte
	files       map[string]string
	hits        atomic.Int64
}

func newMockNode(t *testing.T, files, zipFiles map[string]string) *mockNode {
	t.Helper()
	m := &mockNode{files: files}
	if zipFiles == nil {
		zipFiles = map[string]string{}
	}
	m.zipData = buildTestZip(t, zipFiles)
	m.manifest = Manifest{
		GeneratedAt: "2026-10-29T06:00:00Z",
		RawEXD:      bundleOfBytes(m.zipData, len(zipFiles)),
		Rules:       bundleOfText(files["rules.json"]),
		Tables:      bundleOfText(files["tables.json"]),
	}
	// 文件里带了 font/ 条目就顺带提供一个字体包，供按需下载的测试使用。
	fontFiles := map[string]string{}
	for key, content := range files {
		if strings.HasPrefix(key, "font/") {
			fontFiles[key] = content
		}
	}
	if len(fontFiles) > 0 {
		m.fontZipData = buildTestZip(t, fontFiles)
		m.manifest.Font = bundleOfBytes(m.fontZipData, len(fontFiles))
	}
	m.srv = httptest.NewServer(http.HandlerFunc(m.handle))
	t.Cleanup(m.srv.Close)
	return m
}

func bundleOfBytes(data []byte, count int) BundleInfo {
	sum := sha256.Sum256(data)
	return BundleInfo{SHA256: hex.EncodeToString(sum[:]), Size: int64(len(data)), Count: count}
}

func bundleOfText(content string) BundleInfo {
	sum := sha256.Sum256([]byte(content))
	return BundleInfo{SHA256: hex.EncodeToString(sum[:]), Size: int64(len(content))}
}

func (m *mockNode) handle(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/resource/manifest.json" {
		m.hits.Add(1)
	}
	switch {
	case r.URL.Path == "/resource/manifest.json":
		_ = json.NewEncoder(w).Encode(m.manifest)
	case r.URL.Path == "/resource/rawexd.zip":
		_, _ = w.Write(m.zipData)
	case r.URL.Path == "/resource/font.zip":
		if len(m.fontZipData) == 0 {
			http.NotFound(w, r)
			return
		}
		_, _ = w.Write(m.fontZipData)
	case len(r.URL.Path) > len("/resource/"):
		key := r.URL.Path[len("/resource/"):]
		content, ok := m.files[key]
		if !ok {
			http.NotFound(w, r)
			return
		}
		_, _ = w.Write([]byte(content))
	default:
		http.NotFound(w, r)
	}
}

func (m *mockNode) updater() *Updater {
	return &Updater{HTTP: m.srv.Client(), Nodes: []string{m.srv.URL + "/resource/"}}
}

func provisionFixture(t *testing.T) (*mockNode, map[string]string) {
	t.Helper()
	files := map[string]string{
		"rules.json":  `{"specialWord":{}}`,
		"tables.json": `{"folders":{},"tables":{},"files":[]}`,
	}
	zipFiles := map[string]string{
		"rawexd/A.csv":       "csv-a",
		"rawexd/quest/B.csv": "csv-b",
	}
	return newMockNode(t, files, zipFiles), files
}

func TestProvisionDownloadsFullResource(t *testing.T) {
	node, files := provisionFixture(t)
	dir := t.TempDir()

	status, err := node.updater().Provision(context.Background(), dir, nil)
	if err != nil {
		t.Fatalf("Provision 失败: %v", err)
	}
	if !status.Updated {
		t.Fatalf("首次应下载资源: %+v", status)
	}
	for key, want := range files {
		got, err := os.ReadFile(filepath.Join(dir, filepath.FromSlash(key)))
		if err != nil {
			t.Fatalf("缺少 %s: %v", key, err)
		}
		if string(got) != want {
			t.Fatalf("%s 内容不一致: %q", key, got)
		}
	}
	for _, key := range []string{"rawexd/A.csv", "rawexd/quest/B.csv"} {
		if _, err := os.Stat(filepath.Join(dir, filepath.FromSlash(key))); err != nil {
			t.Fatalf("快照包未解压出 %s", key)
		}
	}
	if _, err := os.Stat(filepath.Join(dir, "manifest.json")); err != nil {
		t.Fatal("应写入 manifest.json")
	}
}

func TestProvisionSkipsWhenUpToDate(t *testing.T) {
	node, _ := provisionFixture(t)
	dir := t.TempDir()
	if _, err := node.updater().Provision(context.Background(), dir, nil); err != nil {
		t.Fatal(err)
	}
	before := node.hits.Load()

	status, err := node.updater().Provision(context.Background(), dir, nil)
	if err != nil {
		t.Fatalf("Provision 失败: %v", err)
	}
	if status.Updated {
		t.Fatalf("资源已是最新时不应更新: %+v", status)
	}
	if got := node.hits.Load(); got != before {
		t.Fatalf("已是最新时不应下载文件，新增请求 %d", got-before)
	}
}

func TestProvisionRepairsMissingFile(t *testing.T) {
	node, _ := provisionFixture(t)
	dir := t.TempDir()
	if _, err := node.updater().Provision(context.Background(), dir, nil); err != nil {
		t.Fatal(err)
	}
	if err := os.Remove(filepath.Join(dir, "rules.json")); err != nil {
		t.Fatal(err)
	}

	status, err := node.updater().Provision(context.Background(), dir, nil)
	if err != nil {
		t.Fatalf("Provision 失败: %v", err)
	}
	if !status.Updated {
		t.Fatalf("缺失文件应触发修复: %+v", status)
	}
	if data, err := os.ReadFile(filepath.Join(dir, "rules.json")); err != nil || string(data) != `{"specialWord":{}}` {
		t.Fatalf("rules.json 未修复: %v %q", err, data)
	}
}

// 不支持离线使用：即使本地资源完整，拿不到云端清单也必须报错。
func TestProvisionOfflineWithLocalResourceFails(t *testing.T) {
	node, _ := provisionFixture(t)
	dir := t.TempDir()
	if _, err := node.updater().Provision(context.Background(), dir, nil); err != nil {
		t.Fatal(err)
	}

	offline := &Updater{
		HTTP:  node.updater().HTTP,
		Nodes: []string{"http://127.0.0.1:1/resource/"},
	}
	if _, err := offline.Provision(context.Background(), dir, nil); err == nil {
		t.Fatal("无法获取云端清单时应返回错误，不能继续使用本地资源")
	}
}

func TestProvisionOfflineWithoutLocalResource(t *testing.T) {
	offline := &Updater{Nodes: []string{"http://127.0.0.1:1/resource/"}}
	if _, err := offline.Provision(context.Background(), t.TempDir(), nil); err == nil {
		t.Fatal("无本地资源且网络不可用时应返回错误")
	}
}

// 规则文件损坏时，不能判定资源完整。
func TestModulesCompleteDetectsCorruptedRules(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"manifest.json", "tables.json"} {
		if err := os.WriteFile(filepath.Join(dir, name), []byte("{}"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	rulesPath := filepath.Join(dir, "rules.json")
	if err := os.WriteFile(rulesPath, []byte("good-rules"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "rawexd"), 0o755); err != nil {
		t.Fatal(err)
	}
	zipData := buildTestZip(t, map[string]string{"rawexd/A.csv": "csv-a"})
	zipPath := filepath.Join(dir, "rawexd.zip")
	if err := os.WriteFile(zipPath, zipData, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ExtractZip(zipPath, dir); err != nil {
		t.Fatal(err)
	}

	goodHash := sha256.Sum256([]byte("good-rules"))
	mf := &Manifest{
		GeneratedAt: "2026-11-01T00:00:00Z",
		RawEXD:      bundleOfBytes(zipData, 1),
		Rules:       BundleInfo{SHA256: hex.EncodeToString(goodHash[:]), Size: int64(len("good-rules"))},
		Tables:      BundleInfo{SHA256: "tables"},
	}

	if !modulesComplete(dir, mf, mf) {
		t.Fatal("内容完好时应判定资源完整")
	}

	// 删除 rules.json 后应判定不完整
	if err := os.Remove(rulesPath); err != nil {
		t.Fatal(err)
	}
	if modulesComplete(dir, mf, mf) {
		t.Error("缺失必要元数据文件时不应判定资源完整")
	}
}

// 本地开发构建只校验工作区资源，绝不发网络请求，也不写入任何文件。
func TestProvisionLocalOnlyValidatesWithoutNetwork(t *testing.T) {
	dir := t.TempDir()
	u := &Updater{LocalOnly: true, Nodes: []string{"http://127.0.0.1:1/resource/"}}

	if _, err := u.Provision(context.Background(), dir, nil); err == nil {
		t.Fatal("缺少 rawexd 时应返回错误")
	}
	if err := os.MkdirAll(filepath.Join(dir, "rawexd", "quest"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "rawexd", "quest", "A.csv"), []byte("a"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "tables.json"), []byte(`{"files":["quest/A"]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "rules.json"), []byte(`{}`), 0o644); err != nil {
		t.Fatal(err)
	}

	status, err := u.Provision(context.Background(), dir, nil)
	if err != nil {
		t.Fatalf("本地资源完整时不应失败: %v", err)
	}
	if status.Updated || status.DownloadedFiles != 0 {
		t.Fatalf("本地开发构建不应下载或改动资源: %+v", status)
	}
	if _, err := os.Stat(filepath.Join(dir, "manifest.json")); !os.IsNotExist(err) {
		t.Fatalf("本地开发构建不应写 manifest.json: %v", err)
	}
}

func TestSyncTableDocLocalOnly(t *testing.T) {
	dir := t.TempDir()
	u := &Updater{LocalOnly: true}
	if _, err := u.SyncTableDoc(context.Background(), dir); err == nil {
		t.Fatal("缺少 tables.json 时应返回错误")
	}
	if err := os.WriteFile(filepath.Join(dir, "tables.json"), []byte(`{"files":["A"]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	updated, err := u.SyncTableDoc(context.Background(), dir)
	if err != nil || updated {
		t.Fatalf("本地 tables.json 可用时应直接返回: updated=%v err=%v", updated, err)
	}
}

func TestEnsureFontPackLocalOnly(t *testing.T) {
	dir := t.TempDir()
	u := &Updater{LocalOnly: true, Nodes: []string{"http://127.0.0.1:1/resource/"}}
	if err := u.EnsureFontPack(context.Background(), dir, nil); err == nil {
		t.Fatal("缺少 font 目录时应返回错误")
	}
	if err := os.MkdirAll(filepath.Join(dir, "font"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := u.EnsureFontPack(context.Background(), dir, nil); err != nil {
		t.Fatalf("本地 font 目录存在时不应联网: %v", err)
	}
}

// manifest.json 只是同步元数据；本地开发构建没有它，内置词条同样可用。
func TestHasLocalResourceWithoutManifest(t *testing.T) {
	dir := t.TempDir()
	if HasLocalResource(dir) {
		t.Fatal("空目录不应判定为可用")
	}
	if err := os.WriteFile(filepath.Join(dir, "tables.json"), []byte(`{"files":["A"]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "rawexd"), 0o755); err != nil {
		t.Fatal(err)
	}
	if !HasLocalResource(dir) {
		t.Fatal("rawexd 与 tables.json 齐全时应判定为可用")
	}
}

// 字体包不参与启动同步，只在真正需要字体替换时按需下载。
func TestEnsureFontPackDownloadsOnDemand(t *testing.T) {
	files := map[string]string{
		"rules.json":  `{"specialWord":{}}`,
		"tables.json": `{"folders":{},"tables":{},"files":[]}`,
		"font/A.fdt":  "font-a",
		"font/B.tex":  "font-b",
	}
	node := newMockNode(t, files, map[string]string{"rawexd/A.csv": "csv-a"})
	dir := t.TempDir()

	if _, err := node.updater().Provision(context.Background(), dir, nil); err != nil {
		t.Fatalf("Provision 失败: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "font")); err == nil {
		t.Fatal("启动同步不应下载字体包")
	}

	if err := node.updater().EnsureFontPack(context.Background(), dir, nil); err != nil {
		t.Fatalf("按需下载字体包失败: %v", err)
	}
	for key, want := range map[string]string{"font/A.fdt": "font-a", "font/B.tex": "font-b"} {
		got, err := os.ReadFile(filepath.Join(dir, filepath.FromSlash(key)))
		if err != nil || string(got) != want {
			t.Fatalf("%s 内容不正确: %v %q", key, err, got)
		}
	}
	if _, err := os.Stat(filepath.Join(dir, "font.zip")); err != nil {
		t.Fatal("应保留 font.zip 作为可信来源")
	}

	before := node.hits.Load()
	if err := node.updater().EnsureFontPack(context.Background(), dir, nil); err != nil {
		t.Fatal(err)
	}
	if node.hits.Load() != before {
		t.Fatalf("已校验通过时不应重新下载，新增请求 %d", node.hits.Load()-before)
	}
}
