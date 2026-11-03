package sqpack

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

// TestWriteDataOffset 校验两种条目布局写入偏移的位置：
// 16 字节条目的 .index 写 pt+8，8 字节条目的 .index2 写 pt+4，且互不越界。
func TestWriteDataOffset(t *testing.T) {
	path := filepath.Join(t.TempDir(), "0a0000.win32.index")
	buf := make([]byte, 32)
	if err := os.WriteFile(path, buf, 0o644); err != nil {
		t.Fatal(err)
	}
	f, err := os.OpenFile(path, os.O_RDWR, 0)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	indexEntry := &IndexFile{Pt: 0, IsIndex2: false}
	index2Entry := &IndexFile{Pt: 16, IsIndex2: true}
	if err := WriteDataOffset(f, indexEntry, 0x11223344); err != nil {
		t.Fatalf("写入 .index 偏移失败: %v", err)
	}
	if err := WriteDataOffset(f, index2Entry, 0x55667788); err != nil {
		t.Fatalf("写入 .index2 偏移失败: %v", err)
	}

	got := make([]byte, 32)
	if _, err := f.ReadAt(got, 0); err != nil {
		t.Fatal(err)
	}
	if v := binary.LittleEndian.Uint32(got[8:12]); v != 0x11223344 {
		t.Errorf(".index 偏移写入位置错误: pt+8 = %#x", v)
	}
	if v := binary.LittleEndian.Uint32(got[20:24]); v != 0x55667788 {
		t.Errorf(".index2 偏移写入位置错误: pt+4 = %#x", v)
	}
	// 未涉及的 4 字节字段（.index 的 id2 与 .index2 的 id）必须保持原样。
	for _, off := range []int{4, 16} {
		if v := binary.LittleEndian.Uint32(got[off : off+4]); v != 0 {
			t.Errorf("偏移字段之外的字节被改写: offset %d = %#x", off, v)
		}
	}

	if err := WriteDataOffset(f, nil, 0); err == nil {
		t.Error("空索引条目应返回错误")
	}
}
