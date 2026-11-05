package sqpack

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// buildTestIndex 按 OpenIndex / resolveSegments 的布局拼一个最小索引文件：
// 头部 1024 字节，4 个段描述符分别落在 headerLength+{4,80,152,224}。
func buildTestIndex(t *testing.T, name string, segs [4][2]int32, size int) string {
	t.Helper()
	const headerLength = 1024
	data := make([]byte, size)
	copy(data, "SqPack")
	binary.LittleEndian.PutUint32(data[0x0C:], headerLength)
	binary.LittleEndian.PutUint32(data[0x14:], 2)
	for i, off := range []int{4, 80, 152, 224} {
		pos := headerLength + off
		binary.LittleEndian.PutUint32(data[pos+4:], uint32(segs[i][0]))
		binary.LittleEndian.PutUint32(data[pos+8:], uint32(segs[i][1]))
	}
	path := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func putFolder(t *testing.T, path string, pos int, id, fileIndexOffset, folderSize int32) {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	binary.LittleEndian.PutUint32(data[pos:], uint32(id))
	binary.LittleEndian.PutUint32(data[pos+4:], uint32(fileIndexOffset))
	binary.LittleEndian.PutUint32(data[pos+8:], uint32(folderSize))
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}

func putIndexEntry(t *testing.T, path string, pos int, id, offset int32) {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	binary.LittleEndian.PutUint32(data[pos:], uint32(id))
	binary.LittleEndian.PutUint32(data[pos+8:], uint32(offset))
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}

// 畸形索引里的负偏移过去会让 data[负数:] 直接 panic，现在必须在解析阶段报错。
func TestOpenIndexRejectsNegativeOffsets(t *testing.T) {
	t.Run("目录表段偏移为负", func(t *testing.T) {
		path := buildTestIndex(t, "0a0000.win32.index", [4][2]int32{{0, 0}, {0, 0}, {0, 0}, {-16, 32}}, 4096)
		_, err := OpenIndex(path)
		if err == nil {
			t.Fatal("负的段偏移应返回错误")
		}
		if !strings.Contains(err.Error(), "偏移或长度非法") {
			t.Fatalf("错误信息不符: %v", err)
		}
	})

	t.Run("文件表段偏移为负", func(t *testing.T) {
		path := buildTestIndex(t, "000000.win32.index2", [4][2]int32{{-8, 64}, {0, 0}, {0, 0}, {0, 0}}, 4096)
		_, err := OpenIndex(path)
		if err == nil {
			t.Fatal("负的段偏移应返回错误")
		}
		if !strings.Contains(err.Error(), "偏移或长度非法") {
			t.Fatalf("错误信息不符: %v", err)
		}
	})

	t.Run("目录条目里的文件表偏移为负", func(t *testing.T) {
		path := buildTestIndex(t, "0a0000.win32.index", [4][2]int32{{0, 0}, {0, 0}, {0, 0}, {2048, 16}}, 4096)
		putFolder(t, path, 2048, 7, -32, 0)
		_, err := OpenIndex(path)
		if err == nil {
			t.Fatal("负的文件表偏移应返回错误")
		}
		if !strings.Contains(err.Error(), "目录表条目非法") {
			t.Fatalf("错误信息不符: %v", err)
		}
	})
}

// 正常索引仍要能解析：目录表与 index2 的单文件表两条分支都覆盖一次。
func TestOpenIndexParsesValidLayout(t *testing.T) {
	path := buildTestIndex(t, "0a0000.win32.index", [4][2]int32{{0, 0}, {0, 0}, {0, 0}, {2048, 16}}, 4096)
	putFolder(t, path, 2048, 0x1234, 3072, 32)
	putIndexEntry(t, path, 3072, 0xAAAA, 0x10)
	putIndexEntry(t, path, 3088, 0xBBBB, 0x20)

	idx, err := OpenIndex(path)
	if err != nil {
		t.Fatalf("合法索引解析失败: %v", err)
	}
	folder := idx.Folders[0x1234]
	if folder == nil || len(folder.Files) != 2 {
		t.Fatalf("目录解析不正确: %+v", idx.Folders)
	}
	if e := folder.Files[0xBBBB]; e == nil || e.DataOffset != 0x20 || e.Pt != 3088 {
		t.Fatalf("文件条目解析不正确: %+v", e)
	}

	path2 := buildTestIndex(t, "000000.win32.index2", [4][2]int32{{2048, 32}, {0, 0}, {0, 0}, {0, 0}}, 4096)
	data2, err := os.ReadFile(path2)
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 4; i++ {
		binary.LittleEndian.PutUint32(data2[2048+i*8:], uint32(0x100+i))
		binary.LittleEndian.PutUint32(data2[2048+i*8+4:], uint32(i+1))
	}
	if err := os.WriteFile(path2, data2, 0o644); err != nil {
		t.Fatal(err)
	}
	idx2, err := OpenIndex(path2)
	if err != nil {
		t.Fatalf("合法 index2 解析失败: %v", err)
	}
	if f := idx2.Folders[0]; f == nil || len(f.Files) != 4 {
		t.Fatalf("index2 文件表解析不正确: %+v", f)
	}
}

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
