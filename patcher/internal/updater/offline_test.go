package updater

import (
	"archive/zip"
	"os"
	"path/filepath"
	"testing"
)

func createTestZip(t *testing.T, zipPath string, files map[string]string) {
	t.Helper()
	f, err := os.Create(zipPath)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	w := zip.NewWriter(f)
	for name, content := range files {
		fw, err := w.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := fw.Write([]byte(content)); err != nil {
			t.Fatal(err)
		}
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
}

func TestImportOfflineFromZip(t *testing.T) {
	tmpDir := t.TempDir()
	zipPath := filepath.Join(tmpDir, "resource.zip")
	destDir := filepath.Join(tmpDir, "dest_resource")

	files := map[string]string{
		"resource/manifest.json": `{"generatedAt":"2026-11-02T12:00:00Z","rawexd":{"sha256":"abcd","size":100}}`,
		"resource/tables.json":   `{"files":["Item","Action"],"tables":{"Item":"道具","Action":"技能"}}`,
		"resource/rawexd/Item.csv": "key,0,1\n1,测试道具,1",
		"resource/rawexd/Action.csv": "key,0,1\n1,测试技能,1",
		"resource/font/font.fdt": "dummy font data",
	}
	createTestZip(t, zipPath, files)

	summary, err := ImportOffline(zipPath, destDir)
	if err != nil {
		t.Fatalf("ImportOffline 失败: %v", err)
	}
	if !summary.Success {
		t.Errorf("期望 Success 为 true")
	}
	if summary.TableCount != 2 {
		t.Errorf("期望 TableCount 为 2，实际为 %d", summary.TableCount)
	}
	if summary.CSVCount != 2 {
		t.Errorf("期望 CSVCount 为 2，实际为 %d", summary.CSVCount)
	}
	if !HasLocalResource(destDir) {
		t.Errorf("导入后 HasLocalResource 应返回 true")
	}
}

func TestImportOfflineNestedRawexdZip(t *testing.T) {
	tmpDir := t.TempDir()
	innerZipPath := filepath.Join(tmpDir, "rawexd.zip")
	createTestZip(t, innerZipPath, map[string]string{
		"rawexd/Item.csv": "key,0,1\n1,测试道具,1",
	})
	innerData, err := os.ReadFile(innerZipPath)
	if err != nil {
		t.Fatal(err)
	}

	outerZipPath := filepath.Join(tmpDir, "outer.zip")
	f, err := os.Create(outerZipPath)
	if err != nil {
		t.Fatal(err)
	}
	w := zip.NewWriter(f)
	fw, err := w.Create("rawexd.zip")
	if err != nil {
		t.Fatal(err)
	}
	_, _ = fw.Write(innerData)
	_ = w.Close()
	f.Close()

	destDir := filepath.Join(tmpDir, "dest_nested")
	summary, err := ImportOffline(outerZipPath, destDir)
	if err != nil {
		t.Fatalf("ImportOffline 失败: %v", err)
	}
	if summary.CSVCount != 1 {
		t.Errorf("期望解压出 1 个 CSV 文件，实际为 %d", summary.CSVCount)
	}
	if !HasLocalResource(destDir) {
		t.Errorf("导入后 HasLocalResource 应返回 true")
	}
}

func TestImportOfflineEmptyZipFails(t *testing.T) {
	tmpDir := t.TempDir()
	emptyZipPath := filepath.Join(tmpDir, "empty.zip")
	createTestZip(t, emptyZipPath, map[string]string{
		"readme.txt": "hello",
	})

	destDir := filepath.Join(tmpDir, "dest_empty")
	_, err := ImportOffline(emptyZipPath, destDir)
	if err == nil {
		t.Errorf("空包/无 CSV 包应报错，但未报错")
	}
}
