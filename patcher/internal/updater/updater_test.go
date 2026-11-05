package updater

import (
	"archive/zip"
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func buildTestZip(t *testing.T, files map[string]string) []byte {
	t.Helper()
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)
	for name, content := range files {
		w, err := zw.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := w.Write([]byte(content)); err != nil {
			t.Fatal(err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func TestExtractZipRejectsTraversal(t *testing.T) {
	data := buildTestZip(t, map[string]string{"../evil.txt": "evil"})
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "bad.zip")
	if err := os.WriteFile(zipPath, data, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ExtractZip(zipPath, filepath.Join(dir, "out")); err == nil {
		t.Fatal("应拒绝包含路径穿越的 zip 条目")
	}
}

func TestExtractZipWritesFiles(t *testing.T) {
	data := buildTestZip(t, map[string]string{"rawexd/A.csv": "a", "rawexd/quest/B.csv": "b"})
	dir := t.TempDir()
	zipPath := filepath.Join(dir, "ok.zip")
	if err := os.WriteFile(zipPath, data, 0o644); err != nil {
		t.Fatal(err)
	}
	out := filepath.Join(dir, "out")
	if err := ExtractZip(zipPath, out); err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(filepath.Join(out, "rawexd", "quest", "B.csv"))
	if err != nil || string(got) != "b" {
		t.Fatalf("解压结果不正确: %v %q", err, got)
	}
}

// 一个节点挂了要能换下一个：并发请求里只要有节点返回合法清单就继续。
func TestFetchManifestFallsBackToOtherNodes(t *testing.T) {
	bad := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.NotFound(w, r)
	}))
	defer bad.Close()
	good := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/manifest.json" {
			http.NotFound(w, r)
			return
		}
		_, _ = w.Write([]byte(`{"generatedAt":"2026-11-05T00:00:00Z","rawexd":{"sha256":"aa"}}`))
	}))
	defer good.Close()

	u := &Updater{HTTP: http.DefaultClient, Nodes: []string{bad.URL + "/", good.URL + "/"}}
	m, err := u.FetchManifest(context.Background())
	if err != nil {
		t.Fatalf("有可用节点时不应失败: %v", err)
	}
	if m.RawEXD.SHA256 != "aa" {
		t.Fatalf("取到的清单不正确: %+v", m)
	}
}

// 全部节点都失败时，错误信息要列出每个节点，而不是只报最后一个。
func TestFetchManifestReportsEveryFailedNode(t *testing.T) {
	newBroken := func() *httptest.Server {
		return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			http.NotFound(w, r)
		}))
	}
	a, b := newBroken(), newBroken()
	defer a.Close()
	defer b.Close()

	u := &Updater{HTTP: http.DefaultClient, Nodes: []string{a.URL + "/", b.URL + "/"}}
	_, err := u.FetchManifest(context.Background())
	if err == nil {
		t.Fatal("所有节点都失败时应返回错误")
	}
	msg := err.Error()
	for _, want := range []string{"全部 2 个下载节点", "404", "资源分支或路径不存在"} {
		if !strings.Contains(msg, want) {
			t.Fatalf("错误信息缺少 %q: %s", want, msg)
		}
	}
	if strings.Count(msg, "404") < 2 {
		t.Fatalf("错误信息应列出每个节点的结果: %s", msg)
	}
}
