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
		"resource/manifest.json":     `{"generatedAt":"2026-11-02T12:00:00Z","rawexd":{"sha256":"abcd","size":100}}`,
		"resource/tables.json":       `{"files":["Item","Action"],"tables":{"Item":"道具","Action":"技能"}}`,
		"resource/rawexd/Item.csv":   "key,0,1\n1,测试道具,1",
		"resource/rawexd/Action.csv": "key,0,1\n1,测试技能,1",
		"resource/font/font.fdt":     "dummy font data",
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

func writeOfflineFile(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}

// 离线包只有已解压目录、没有 .zip 包文件时，不应伪造 bundle 哈希，
// 而是按内容计算本地指纹，且指纹随内容变化。
func TestImportOfflineFallbackFingerprint(t *testing.T) {
	tmpDir := t.TempDir()
	srcDir := filepath.Join(tmpDir, "pkg")
	writeOfflineFile(t, filepath.Join(srcDir, "rawexd", "Item.csv"), "key,0,1\n1,测试道具,1")
	writeOfflineFile(t, filepath.Join(srcDir, "font", "font.fdt"), "dummy font")

	destDir := filepath.Join(tmpDir, "dest")
	if _, err := ImportOffline(srcDir, destDir); err != nil {
		t.Fatalf("ImportOffline 失败: %v", err)
	}

	mf, err := ReadLocalManifest(destDir)
	if err != nil || mf == nil {
		t.Fatalf("读取本地清单失败: %v", err)
	}
	if mf.RawEXD.SHA256 != "" || mf.Font.SHA256 != "" {
		t.Errorf("无 .zip 包文件时不应写入 bundle 哈希: rawexd=%q font=%q", mf.RawEXD.SHA256, mf.Font.SHA256)
	}
	if mf.LocalFingerprint == "" {
		t.Fatal("无 .zip 包文件时应计算本地指纹")
	}

	writeOfflineFile(t, filepath.Join(destDir, "rawexd", "Item.csv"), "key,0,1\n1,改动后的文本,1")
	if err := generateFallbackManifest(destDir); err != nil {
		t.Fatalf("重新生成清单失败: %v", err)
	}
	changed, err := ReadLocalManifest(destDir)
	if err != nil || changed == nil {
		t.Fatalf("读取本地清单失败: %v", err)
	}
	if changed.LocalFingerprint == mf.LocalFingerprint {
		t.Errorf("内容变化后本地指纹应变化")
	}
}

// 有 .zip 包文件时应直接采信包哈希（与发布清单同一值域），不再计算本地指纹。
func TestFallbackManifestHashesBothZips(t *testing.T) {
	dir := t.TempDir()
	rawexdZip := filepath.Join(dir, "rawexd.zip")
	fontZip := filepath.Join(dir, "font.zip")
	createTestZip(t, rawexdZip, map[string]string{"rawexd/Item.csv": "key,0,1\n1,测试道具,1"})
	createTestZip(t, fontZip, map[string]string{"font/font.fdt": "dummy font"})

	if err := generateFallbackManifest(dir); err != nil {
		t.Fatalf("生成清单失败: %v", err)
	}
	mf, err := ReadLocalManifest(dir)
	if err != nil || mf == nil {
		t.Fatalf("读取清单失败: %v", err)
	}
	wantRaw, err := HashFile(rawexdZip)
	if err != nil {
		t.Fatal(err)
	}
	wantFont, err := HashFile(fontZip)
	if err != nil {
		t.Fatal(err)
	}
	if mf.RawEXD.SHA256 != wantRaw.SHA256 {
		t.Errorf("rawexd 哈希应取 rawexd.zip: %q != %q", mf.RawEXD.SHA256, wantRaw.SHA256)
	}
	if mf.Font.SHA256 != wantFont.SHA256 {
		t.Errorf("font 哈希应取 font.zip: %q != %q", mf.Font.SHA256, wantFont.SHA256)
	}
	if mf.LocalFingerprint != "" {
		t.Errorf("两个包哈希齐备时不应再计算本地指纹")
	}
}

// 包内自带清单声明了 bundle 哈希时，zip 必须与声明一致，否则整包拒绝导入。
func TestImportOfflineRejectsMismatchedBundle(t *testing.T) {
	tmpDir := t.TempDir()
	destDir := filepath.Join(tmpDir, "resource")

	// 先放一套可用资源，验证失败导入不会破坏它。
	writeOfflineFile(t, filepath.Join(destDir, "tables.json"), `{"files":["Item"]}`)
	writeOfflineFile(t, filepath.Join(destDir, "rawexd", "Item.csv"), "old content")

	innerZip := filepath.Join(tmpDir, "rawexd.zip")
	createTestZip(t, innerZip, map[string]string{"rawexd/Item.csv": "new content"})
	innerData, err := os.ReadFile(innerZip)
	if err != nil {
		t.Fatal(err)
	}

	pkgDir := filepath.Join(tmpDir, "pkg")
	writeOfflineFile(t, filepath.Join(pkgDir, "manifest.json"),
		`{"rawexd":{"sha256":"deadbeef","size":1}}`)
	writeOfflineFile(t, filepath.Join(pkgDir, "tables.json"), `{"files":["Item"]}`)
	if err := os.WriteFile(filepath.Join(pkgDir, "rawexd.zip"), innerData, 0o644); err != nil {
		t.Fatal(err)
	}

	if _, err := ImportOffline(pkgDir, destDir); err == nil {
		t.Fatal("bundle 哈希不匹配时应拒绝导入")
	}

	got, err := os.ReadFile(filepath.Join(destDir, "rawexd", "Item.csv"))
	if err != nil {
		t.Fatalf("旧资源应保持可读: %v", err)
	}
	if string(got) != "old content" {
		t.Errorf("导入失败后旧资源被改写: %q", got)
	}
}

// 离线包未携带的文件（如 rules.json）在切换后应保留。
func TestImportOfflinePreservesUnprovidedFiles(t *testing.T) {
	tmpDir := t.TempDir()
	destDir := filepath.Join(tmpDir, "resource")
	writeOfflineFile(t, filepath.Join(destDir, "rules.json"), `{"rules":[]}`)
	writeOfflineFile(t, filepath.Join(destDir, "rawexd", "Item.csv"), "old")

	pkgDir := filepath.Join(tmpDir, "pkg")
	writeOfflineFile(t, filepath.Join(pkgDir, "rawexd", "Item.csv"), "new")

	if _, err := ImportOffline(pkgDir, destDir); err != nil {
		t.Fatalf("ImportOffline 失败: %v", err)
	}

	if _, err := os.Stat(filepath.Join(destDir, "rules.json")); err != nil {
		t.Errorf("旧 rules.json 应被保留: %v", err)
	}
	got, err := os.ReadFile(filepath.Join(destDir, "rawexd", "Item.csv"))
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != "new" {
		t.Errorf("包内文件应覆盖旧文件，实际为 %q", got)
	}
}
