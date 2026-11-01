package updater

import (
	"archive/zip"
	"bytes"
	"os"
	"path/filepath"
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
