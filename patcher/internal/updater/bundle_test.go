package updater

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func writeBundle(t *testing.T, dir string, files map[string]string) BundleInfo {
	t.Helper()
	data := buildTestZip(t, files)
	zipPath := filepath.Join(dir, "rawexd.zip")
	if err := os.WriteFile(zipPath, data, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := ExtractZip(zipPath, dir); err != nil {
		t.Fatal(err)
	}
	return bundleOfBytes(data, len(files))
}

func TestVerifyZipBundleAcceptsIntactFiles(t *testing.T) {
	dir := t.TempDir()
	want := writeBundle(t, dir, map[string]string{"rawexd/A.csv": "csv-a", "rawexd/quest/B.csv": "csv-b"})
	if err := VerifyZipBundle(dir, "rawexd.zip", want); err != nil {
		t.Fatalf("完整资源不应报错: %v", err)
	}
}

func TestVerifyZipBundleDetectsTamperedCSV(t *testing.T) {
	dir := t.TempDir()
	want := writeBundle(t, dir, map[string]string{"rawexd/A.csv": "csv-a"})
	// 同尺寸替换，绕过大小检查也必须被发现。
	if err := os.WriteFile(filepath.Join(dir, "rawexd", "A.csv"), []byte("csv-x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := VerifyZipBundle(dir, "rawexd.zip", want); err == nil {
		t.Fatal("CSV 内容被改动时应报错")
	}
}

func TestVerifyZipBundleDetectsMissingCSV(t *testing.T) {
	dir := t.TempDir()
	want := writeBundle(t, dir, map[string]string{"rawexd/A.csv": "csv-a"})
	if err := os.Remove(filepath.Join(dir, "rawexd", "A.csv")); err != nil {
		t.Fatal(err)
	}
	if err := VerifyZipBundle(dir, "rawexd.zip", want); err == nil {
		t.Fatal("解压文件缺失时应报错")
	}
}

func TestVerifyZipBundleDetectsBadZipHash(t *testing.T) {
	dir := t.TempDir()
	want := writeBundle(t, dir, map[string]string{"rawexd/A.csv": "csv-a"})
	want.SHA256 = "deadbeef"
	if err := VerifyZipBundle(dir, "rawexd.zip", want); err == nil {
		t.Fatal("资源包哈希不符时应报错")
	}
}

// 解压目录被改动时，Provision 应使用保留下来的快照包重新解压，而不是重新下载。
func TestProvisionRepairsTamperedCSVFromKeptZip(t *testing.T) {
	node, _ := provisionFixture(t)
	dir := t.TempDir()
	if _, err := node.updater().Provision(context.Background(), dir, nil); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(dir, "rawexd.zip")); err != nil {
		t.Fatalf("应保留 rawexd.zip: %v", err)
	}
	csvPath := filepath.Join(dir, "rawexd", "A.csv")
	if err := os.WriteFile(csvPath, []byte("csv-x"), 0o644); err != nil {
		t.Fatal(err)
	}
	before := node.hits.Load()

	status, err := node.updater().Provision(context.Background(), dir, nil)
	if err != nil {
		t.Fatalf("Provision 失败: %v", err)
	}
	if !status.CSVUpdated {
		t.Fatalf("应修复被改动的 CSV: %+v", status)
	}
	if got, err := os.ReadFile(csvPath); err != nil || string(got) != "csv-a" {
		t.Fatalf("CSV 未恢复: %q %v", got, err)
	}
	if got := node.hits.Load(); got != before {
		t.Fatalf("快照包完好时不应重新下载，新增请求 %d", got-before)
	}
}
