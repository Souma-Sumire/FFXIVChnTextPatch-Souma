package sqpack

import (
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// writeDat 构造一个最小 dat 文件：24 字节文件头 + 附加字节。
func writeDat(t *testing.T, headerLength, contentType, fileSize, blockCount int32, tail []byte) string {
	t.Helper()
	buf := make([]byte, 24)
	binary.LittleEndian.PutUint32(buf[0:], uint32(headerLength))
	binary.LittleEndian.PutUint32(buf[4:], uint32(contentType))
	binary.LittleEndian.PutUint32(buf[8:], uint32(fileSize))
	binary.LittleEndian.PutUint32(buf[20:], uint32(blockCount))
	buf = append(buf, tail...)

	path := filepath.Join(t.TempDir(), "test.win32.dat0")
	if err := os.WriteFile(path, buf, 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func openTestDat(t *testing.T, path string) *DatFile {
	t.Helper()
	d, err := OpenDat(path)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = d.Close() })
	return d
}

// 损坏的头部不应导致按 int32 上限分配内存。
func TestExtractFileRejectsOversizedFileSize(t *testing.T) {
	path := writeDat(t, 0, 2, 1<<30, 1, make([]byte, 32))
	_, err := openTestDat(t, path).ExtractFile(0)
	if err == nil {
		t.Fatal("解包尺寸超过上限时应报错")
	}
	if !strings.Contains(err.Error(), "超过上限") {
		t.Fatalf("错误信息不符: %v", err)
	}
}

// 块数远超文件尺寸时应在读取块表前拦下，而不是按块数分配内存。
func TestExtractFileRejectsOversizedBlockCount(t *testing.T) {
	path := writeDat(t, 0, 2, 64, 1<<20, nil)
	_, err := openTestDat(t, path).ExtractFile(0)
	if err == nil {
		t.Fatal("块数超出文件尺寸时应报错")
	}
	if !strings.Contains(err.Error(), "块数") {
		t.Fatalf("错误信息不符: %v", err)
	}
}

// 负的子块数量会让 make 直接 panic，必须在解包前拒绝。
func TestExtractType4RejectsNegativeSubBlocks(t *testing.T) {
	table := make([]byte, 20)
	negOne := int32(-1)
	binary.LittleEndian.PutUint32(table[16:], uint32(negOne))
	path := writeDat(t, 0, 4, 64, 1, table)

	_, err := openTestDat(t, path).ExtractFile(0)
	if err == nil {
		t.Fatal("子块数量为负时应报错")
	}
	if !strings.Contains(err.Error(), "子块数量") {
		t.Fatalf("错误信息不符: %v", err)
	}
}

// 负的 fileSize 早于任何分配就被拒绝。
func TestExtractFileRejectsNegativeHeader(t *testing.T) {
	path := writeDat(t, 0, 2, -1, -1, nil)
	_, err := openTestDat(t, path).ExtractFile(0)
	if err == nil {
		t.Fatal("负的文件尺寸与块数应报错")
	}
	if !strings.Contains(err.Error(), "文件头非法") {
		t.Fatalf("错误信息不符: %v", err)
	}
}

// 空占位块仍按约定返回 ErrEmptyDatBlock，不受新增边界检查影响。
func TestExtractFileKeepsEmptyBlockSentinel(t *testing.T) {
	path := writeDat(t, 0, 0, 0, 0, nil)
	_, err := openTestDat(t, path).ExtractFile(0)
	if err != ErrEmptyDatBlock {
		t.Fatalf("空块应返回 ErrEmptyDatBlock，got=%v", err)
	}
}
