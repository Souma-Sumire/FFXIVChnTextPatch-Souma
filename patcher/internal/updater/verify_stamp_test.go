package updater

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func prepareStampTree(t *testing.T) (string, *Manifest) {
	t.Helper()
	root := t.TempDir()
	res := filepath.Join(root, "resource")
	if err := os.MkdirAll(filepath.Join(res, "rawexd", "content"), 0o755); err != nil {
		t.Fatal(err)
	}

	if err := os.WriteFile(filepath.Join(res, "rawexd", "content", "Item.csv"), []byte("key,0\n1,测试\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	if err := os.WriteFile(filepath.Join(res, "rawexd.zip"), []byte("zip"), 0o644); err != nil {
		t.Fatal(err)
	}
	return res, &Manifest{
		RawEXD: BundleInfo{SHA256: "aa"},
		Rules:  BundleInfo{SHA256: "cc"},
		Tables: BundleInfo{SHA256: "dd"},
	}
}

func TestVerifyStampMatches(t *testing.T) {
	res, remote := prepareStampTree(t)
	stamp := buildVerifyStamp(res, remote)
	if !stamp.matches(res, remote) {
		t.Fatal("刚采集的指纹应当匹配")
	}

	writeVerifyStamp(res, stamp)
	loaded := readVerifyStamp(res)
	if loaded == nil || *loaded != stamp {
		t.Fatalf("指纹存取不一致: %+v", loaded)
	}

	other := *remote
	other.RawEXD.SHA256 = "zz"
	if stamp.matches(res, &other) {
		t.Fatal("云端清单变化后不应匹配")
	}
}

func TestVerifyStampDetectsChanges(t *testing.T) {
	res, remote := prepareStampTree(t)

	// 内容被修改（大小与修改时间都变）
	stamp := buildVerifyStamp(res, remote)
	target := filepath.Join(res, "rawexd", "content", "Item.csv")
	if err := os.WriteFile(target, []byte("key,0\n1,AAAA\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if stamp.matches(res, remote) {
		t.Fatal("CSV 被修改后不应匹配")
	}

	// 同样大小的原地覆盖：只能靠修改时间发现
	stamp = buildVerifyStamp(res, remote)
	if err := os.WriteFile(target, []byte("key,0\n1,BBBB\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	future := time.Now().Add(2 * time.Second)
	_ = os.Chtimes(target, future, future)
	if stamp.matches(res, remote) {
		t.Fatal("CSV 大小相同但内容变化后不应匹配")
	}

	// CSV 文件被删除
	stamp = buildVerifyStamp(res, remote)
	if err := os.Remove(target); err != nil {
		t.Fatal(err)
	}
	if stamp.matches(res, remote) {
		t.Fatal("CSV 文件缺失后不应匹配")
	}

	// 快照包被替换（大小变化）
	stamp = buildVerifyStamp(res, remote)
	if err := os.WriteFile(filepath.Join(res, "rawexd.zip"), []byte("zip-new"), 0o644); err != nil {
		t.Fatal(err)
	}
	if stamp.matches(res, remote) {
		t.Fatal("快照包变化后不应匹配")
	}
}

func TestScanTreeStampMissingDir(t *testing.T) {
	root := t.TempDir()
	if _, ok := scanTreeStamp(filepath.Join(root, "absent")); ok {
		t.Fatal("目录不存在时应返回 false")
	}
	if s, ok := scanTreeStamp(root); !ok || s.Files != 0 {
		t.Fatalf("空目录应视为存在且无文件: %+v ok=%v", s, ok)
	}
}
