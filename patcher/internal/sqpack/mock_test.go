package sqpack

import (
	"os"
	"path/filepath"
	"testing"
)

func TestCreateMockIndex(t *testing.T) {
	data := CreateMockIndex("exd", "addon_0_chs.exd", "addon_0_ja.exd")
	tmpFile := filepath.Join(t.TempDir(), "0a0000.win32.index")
	if err := os.WriteFile(tmpFile, data, 0o644); err != nil {
		t.Fatal(err)
	}

	idx, err := OpenIndex(tmpFile)
	if err != nil {
		t.Fatalf("OpenIndex 失败: %v", err)
	}

	if idx.Lookup("exd", "addon_0_chs.exd") == nil {
		t.Fatal("未能找到 addon_0_chs.exd")
	}
	if idx.Lookup("exd", "addon_0_ja.exd") == nil {
		t.Fatal("未能找到 addon_0_ja.exd")
	}
	if idx.Lookup("exd", "addon_0_ko.exd") != nil {
		t.Fatal("不应找到 addon_0_ko.exd")
	}
}
