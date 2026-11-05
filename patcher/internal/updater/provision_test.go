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
	fontFiles := map[string]string{}
	for k, v := range files {
		if filepath.ToSlash(k) == "font/A.fdt" || filepath.ToSlash(k) == "font/B.tex" {
			fontFiles[k] = v
		}
	}
	m.zipData = buildTestZip(t, zipFiles)
	m.fontZipData = buildTestZip(t, fontFiles)
	m.manifest = Manifest{
		GeneratedAt: "2026-10-29T06:00:00Z",
		RawEXD:      bundleOfBytes(m.zipData, len(zipFiles)),
		Font:        bundleOfBytes(m.fontZipData, len(fontFiles)),
		Rules:       bundleOfText(files["rules.json"]),
		Tables:      bundleOfText(files["tables.json"]),
		Fonts: map[string]FileInfo{
			"font/A.fdt": fileInfoOf(files["font/A.fdt"]),
			"font/B.tex": fileInfoOf(files["font/B.tex"]),
		},
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

func fileInfoOf(content string) FileInfo {
	sum := sha256.Sum256([]byte(content))
	return FileInfo{SHA256: hex.EncodeToString(sum[:]), Size: int64(len(content))}
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
		"font/A.fdt":  "font-a",
		"font/B.tex":  "font-b",
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
	if err := os.Remove(filepath.Join(dir, "font", "A.fdt")); err != nil {
		t.Fatal(err)
	}

	status, err := node.updater().Provision(context.Background(), dir, nil)
	if err != nil {
		t.Fatalf("Provision 失败: %v", err)
	}
	if !status.Updated {
		t.Fatalf("缺失文件应触发修复: %+v", status)
	}
	if data, err := os.ReadFile(filepath.Join(dir, "font", "A.fdt")); err != nil || string(data) != "font-a" {
		t.Fatalf("字体未修复: %v %q", err, data)
	}
	if _, err := os.Stat(filepath.Join(dir, "font", "B.tex")); err != nil {
		t.Fatal("已存在的字体不应被删除")
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

// 字体文件大小一致但内容损坏时，不能判定资源完整。
func TestModulesCompleteDetectsCorruptedFont(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"manifest.json", "rules.json", "tables.json"} {
		if err := os.WriteFile(filepath.Join(dir, name), []byte("{}"), 0o644); err != nil {
			t.Fatal(err)
		}
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
	fontPath := filepath.Join(dir, "font", "A.fdt")
	if err := os.MkdirAll(filepath.Dir(fontPath), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(fontPath, []byte("good"), 0o644); err != nil {
		t.Fatal(err)
	}

	goodHash := sha256.Sum256([]byte("good"))
	mf := &Manifest{
		GeneratedAt: "2026-11-01T00:00:00Z",
		RawEXD:      bundleOfBytes(zipData, 1),
		Font:        BundleInfo{SHA256: "font"},
		Rules:       BundleInfo{SHA256: "rules"},
		Tables:      BundleInfo{SHA256: "tables"},
		Fonts: map[string]FileInfo{
			"font/A.fdt": {SHA256: hex.EncodeToString(goodHash[:]), Size: 4},
		},
	}

	if !modulesComplete(dir, mf, mf) {
		t.Fatal("字体内容完好时应判定资源完整")
	}

	// 同尺寸替换为损坏内容
	if err := os.WriteFile(fontPath, []byte("evil"), 0o644); err != nil {
		t.Fatal(err)
	}
	if modulesComplete(dir, mf, mf) {
		t.Error("字体内容与哈希不符时不应判定资源完整")
	}
}
