package replace

import (
	"bytes"
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

// newUndoFixture 造一个只有一个字体文件的最小 sqpack：index 用 sqpack.CreateMockIndex，
// dat0 预置 64 字节。返回 index 路径、dat 路径和字体目录。
func newUndoFixture(t *testing.T) (indexPath, datPath, fontDir string) {
	t.Helper()
	dir := t.TempDir()
	indexPath = filepath.Join(dir, "000000.win32.index")
	datPath = filepath.Join(dir, "000000.win32.dat0")
	if err := os.WriteFile(indexPath, sqpack.CreateMockIndex("common/font", "font1.fdt"), 0o644); err != nil {
		t.Fatalf("写测试 index 失败: %v", err)
	}
	if err := os.WriteFile(datPath, make([]byte, 64), 0o644); err != nil {
		t.Fatalf("写测试 dat0 失败: %v", err)
	}
	fontDir = filepath.Join(dir, "font")
	if err := os.MkdirAll(fontDir, 0o755); err != nil {
		t.Fatalf("建字体目录失败: %v", err)
	}
	if err := os.WriteFile(filepath.Join(fontDir, "font1.fdt"), bytes.Repeat([]byte{1, 2, 3, 4}, 64), 0o644); err != nil {
		t.Fatalf("写测试字体失败: %v", err)
	}
	return indexPath, datPath, fontDir
}

// 写入成功后调用 Revert，应把索引偏移改回原值、把 dat0 截断回写入前长度——
// 也就是精确回到「本次写入之前」的字节状态，而不是去比对官方原版。
func TestUndoRevertsCommittedWrite(t *testing.T) {
	indexPath, datPath, fontDir := newUndoFixture(t)
	before, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatal(err)
	}

	undo, err := NewUndo(indexPath)
	if err != nil {
		t.Fatalf("NewUndo 失败: %v", err)
	}
	stats, err := ReplaceFontWithUndo(context.Background(), indexPath, fontDir, nil, undo)
	if err != nil {
		t.Fatalf("ReplaceFontWithUndo 失败: %v", err)
	}
	if stats.AppendedBytes == 0 || !undo.Touched() {
		t.Fatalf("测试前提不成立：应写入数据并记录偏移，实际 bytes=%d touched=%v", stats.AppendedBytes, undo.Touched())
	}

	after, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Equal(before, after) {
		t.Fatal("测试前提不成立：写入后索引应发生变化")
	}
	if st, err := os.Stat(datPath); err != nil {
		t.Fatal(err)
	} else if st.Size() == 64 {
		t.Fatal("测试前提不成立：dat0 应被追加数据")
	}

	changed, err := undo.Revert()
	if err != nil {
		t.Fatalf("Revert 失败: %v", err)
	}
	if !changed {
		t.Fatal("Revert 应报告确实改动了客户端文件")
	}

	restored, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(restored, before) {
		t.Fatal("索引没有回到写入前的字节内容")
	}
	st, err := os.Stat(datPath)
	if err != nil {
		t.Fatal(err)
	}
	if st.Size() != 64 {
		t.Fatalf("dat0 应截断回写入前的 64 字节，实际 %d", st.Size())
	}
}

// 什么都没写就回滚：不应改动任何文件，也不该声称回滚过。
func TestUndoRevertWithoutWriteIsNoop(t *testing.T) {
	indexPath, datPath, _ := newUndoFixture(t)
	before, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatal(err)
	}

	undo, err := NewUndo(indexPath)
	if err != nil {
		t.Fatalf("NewUndo 失败: %v", err)
	}
	changed, err := undo.Revert()
	if err != nil {
		t.Fatalf("Revert 失败: %v", err)
	}
	if changed {
		t.Fatal("没有写入时 Revert 不应报告改动")
	}
	after, err := os.ReadFile(indexPath)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(before, after) {
		t.Fatal("没有写入时索引不应被改动")
	}
	st, err := os.Stat(datPath)
	if err != nil {
		t.Fatal(err)
	}
	if st.Size() != 64 {
		t.Fatalf("没有写入时 dat0 长度不该变化，实际 %d", st.Size())
	}
}

// dat0 比记录的长度还短说明文件在外部被动过，回滚必须报错而不是硬截断。
func TestUndoRevertRejectsShrunkDat(t *testing.T) {
	indexPath, datPath, _ := newUndoFixture(t)
	undo, err := NewUndo(indexPath)
	if err != nil {
		t.Fatalf("NewUndo 失败: %v", err)
	}
	if err := os.Truncate(datPath, 8); err != nil {
		t.Fatal(err)
	}
	if _, err := undo.Revert(); err == nil || !strings.Contains(err.Error(), "更短") {
		t.Fatalf("应报告 dat0 比写入前更短，实际: %v", err)
	}
}
