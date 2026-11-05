package main

import (
	"bytes"
	"context"
	"os"
	"path/filepath"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/replace"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

// 汉化中途失败时，本次开始写入的每个文件组都要撤回：
// 索引回到写入前的内容，dat0 截断回写入前的长度。
func TestRevertWritesRestoresEveryGroup(t *testing.T) {
	dir := t.TempDir()
	originals := map[string][]byte{}
	datSizes := map[string]int64{}
	var undos []*replace.Undo

	for _, base := range []string{"0a0000.win32", "000000.win32"} {
		indexPath := filepath.Join(dir, base+".index")
		datPath := filepath.Join(dir, base+".dat0")
		fontDir := filepath.Join(dir, base+"-font")
		if err := os.MkdirAll(fontDir, 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(indexPath, sqpack.CreateMockIndex("common/font", "font1.fdt"), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(datPath, make([]byte, 64), 0o644); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(fontDir, "font1.fdt"), []byte("font-data-"+base), 0o644); err != nil {
			t.Fatal(err)
		}

		before, err := os.ReadFile(indexPath)
		if err != nil {
			t.Fatal(err)
		}
		originals[indexPath] = before
		datSizes[datPath] = 64

		undo, err := replace.NewUndo(indexPath)
		if err != nil {
			t.Fatalf("NewUndo(%s) 失败: %v", base, err)
		}
		if _, err := replace.ReplaceFontWithUndo(context.Background(), indexPath, fontDir, nil, undo); err != nil {
			t.Fatalf("ReplaceFontWithUndo(%s) 失败: %v", base, err)
		}
		undos = append(undos, undo)
	}

	changed, err := revertWrites(undos)
	if err != nil {
		t.Fatalf("revertWrites 失败: %v", err)
	}
	if changed != len(undos) {
		t.Fatalf("应撤回 %d 个文件组，实际 %d", len(undos), changed)
	}

	for indexPath, want := range originals {
		got, err := os.ReadFile(indexPath)
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(got, want) {
			t.Fatalf("%s 未回到写入前的内容", filepath.Base(indexPath))
		}
	}
	for datPath, want := range datSizes {
		st, err := os.Stat(datPath)
		if err != nil {
			t.Fatal(err)
		}
		if st.Size() != want {
			t.Fatalf("%s 应截断回 %d 字节，实际 %d", filepath.Base(datPath), want, st.Size())
		}
	}
}

// 什么都没写过时 revertWrites 不该报告改动，也不该让调用方以为回滚了文件。
func TestRevertWritesWithoutWrites(t *testing.T) {
	changed, err := revertWrites(nil)
	if err != nil {
		t.Fatalf("空列表不应报错: %v", err)
	}
	if changed != 0 {
		t.Fatalf("空列表不应报告改动，实际 %d", changed)
	}
}
