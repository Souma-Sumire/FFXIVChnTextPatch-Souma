package replace

import (
	"bytes"
	"encoding/binary"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/exd"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
)

func bePut32(v uint32) []byte {
	buf := make([]byte, 4)
	binary.BigEndian.PutUint32(buf, v)
	return buf
}

func singleStringTable(chunkSize int, stringOffsetColumn int) *exd.EXHF {
	return &exd.EXHF{
		DatasetChunkSize: uint16(chunkSize),
		Datasets: []exd.Dataset{
			{Type: 1, Offset: 0},
			{Type: 0, Offset: uint16(stringOffsetColumn)},
		},
		Pages: []exd.Page{{PageNum: 0, NumEntries: 1}},
		Langs: []int{0},
	}
}

func runBuild(t *testing.T, exh *exd.EXHF, entryData []byte, offsetMap map[int]int, row []string) []byte {
	t.Helper()
	file := &exd.File{Entries: map[int32][]byte{1: entryData}}
	dataMap := map[int][]string{}
	if row != nil {
		dataMap[1] = row
	}
	out, _, err := buildEntries(exh, file, "TestTable", offsetMap, dataMap, pipeline.NewRegistry(nil))
	if err != nil {
		t.Fatalf("buildEntries 失败: %v", err)
	}
	return out[1]
}

// TestEmptyCellFallsBackToOriginal 验证 CSV 空单元格严格保留原语言字节，绝不变成空字符串。
func TestEmptyCellFallsBackToOriginal(t *testing.T) {
	exh := singleStringTable(8, 4)
	chunk := make([]byte, 8)
	binary.BigEndian.PutUint32(chunk[4:], 0)
	ja := []byte("日本語")
	entry := append(append(append([]byte{}, chunk...), ja...), 0)

	got := runBuild(t, exh, entry, map[int]int{4: 0}, []string{""})

	if !bytes.Equal(got[:8], chunk) {
		t.Fatalf("定长 chunk 被破坏: % x", got[:8])
	}
	stringArea := got[8:]
	if !bytes.HasPrefix(stringArea, append(append([]byte{}, ja...), 0)) {
		t.Fatalf("空单元格应回退原文字节，实际 % x", stringArea)
	}
	// 8 + 9 + 1 = 18，4 字节对齐需补 2 个 0。
	if len(got) != 20 || !bytes.Equal(got[18:], []byte{0, 0}) {
		t.Fatalf("空单元格回退后的 Padding 不正确: len=%d % x", len(got), got[18:])
	}
}

func TestNonEmptyCellReplacesString(t *testing.T) {
	exh := singleStringTable(8, 4)
	chunk := make([]byte, 8)
	ja := []byte("日本語")
	entry := append(append(append([]byte{}, chunk...), ja...), 0)

	got := runBuild(t, exh, entry, map[int]int{4: 0}, []string{"中文"})

	if !bytes.Equal(got[:8], chunk) {
		t.Fatalf("定长 chunk 被破坏: % x", got[:8])
	}
	wantString := append([]byte("中文"), 0)
	if !bytes.Equal(got[8:8+len(wantString)], wantString) {
		t.Fatalf("字符串区不正确: % x", got[8:])
	}
	// 8 + 6 + 1 = 15，需补 1 个 0。
	if len(got) != 16 || got[15] != 0 {
		t.Fatalf("Padding 不正确: len=%d", len(got))
	}
}

// TestNonStringColumnsPreserved 验证多列（Int32 / Bool / Byte 与多个 String）混合时，
// 修改 String Offset 不会破坏非 String 列的定长 chunk 数据。
func TestNonStringColumnsPreserved(t *testing.T) {
	exh := &exd.EXHF{
		DatasetChunkSize: 12,
		Datasets: []exd.Dataset{
			{Type: 1, Offset: 0},
			{Type: 0, Offset: 4},
			{Type: 0, Offset: 8},
		},
		Pages: []exd.Page{{PageNum: 0}},
		Langs: []int{0},
	}
	chunk := make([]byte, 12)
	copy(chunk[0:], []byte{0x11, 0x22, 0x33, 0x44})
	binary.BigEndian.PutUint32(chunk[4:], 0)
	binary.BigEndian.PutUint32(chunk[8:], 3) // "AA\0" 的起始
	entry := append(append([]byte{}, chunk...), []byte("AA\x00BB\x00")...)

	got := runBuild(t, exh, entry, map[int]int{4: 0, 8: 1}, []string{"甲", "乙"})

	if !bytes.Equal(got[0:4], []byte{0x11, 0x22, 0x33, 0x44}) {
		t.Fatalf("非 String 列被修改: % x", got[0:4])
	}
	if off := binary.BigEndian.Uint32(got[4:]); off != 0 {
		t.Fatalf("第一字符串偏移应为 0，实际 %d", off)
	}
	if off := binary.BigEndian.Uint32(got[8:]); off != 4 {
		t.Fatalf("第二字符串偏移应为 4，实际 %d", off)
	}
	wantString := []byte("甲\x00乙\x00")
	if !bytes.Equal(got[12:12+len(wantString)], wantString) {
		t.Fatalf("多字符串区不正确: % x", got[12:])
	}
}

// TestEntryPaddingBoundaries 覆盖 len(chunk+strings) % 4 == 0/1/2/3 的 4 种 Padding 边界。
func TestEntryPaddingBoundaries(t *testing.T) {
	exh := singleStringTable(8, 4)
	cases := []struct {
		target      string
		wantPadding int
	}{
		{"aaa", 4},    // 8+3+1 = 12 -> 余 0 -> 补 4
		{"aaaa", 3},   // 13 -> 余 1 -> 补 3
		{"aaaaa", 2},  // 14 -> 余 2 -> 补 2
		{"aaaaaa", 1}, // 15 -> 余 3 -> 补 1
	}
	for _, c := range cases {
		chunk := make([]byte, 8)
		entry := append(append([]byte{}, chunk...), 0)
		got := runBuild(t, exh, entry, map[int]int{4: 0}, []string{c.target})
		if len(got) != 16 {
			t.Fatalf("目标 %q 的输出长度应为 16，实际 %d", c.target, len(got))
		}
		padding := len(got) - (8 + len(c.target) + 1)
		if padding != c.wantPadding {
			t.Fatalf("目标 %q 的 Padding 应为 %d，实际 %d", c.target, c.wantPadding, padding)
		}
		for i := len(got) - padding; i < len(got); i++ {
			if got[i] != 0 {
				t.Fatalf("目标 %q 的 Padding 字节非 0", c.target)
			}
		}
	}
}

// TestMultiPageEXHF 验证包含多个分页的 EXHF 能完整解析出所有分页。
func TestMultiPageEXHF(t *testing.T) {
	buf := make([]byte, 32)
	copy(buf[0:], []byte("EXHF"))
	binary.BigEndian.PutUint16(buf[4:], 3)
	binary.BigEndian.PutUint16(buf[6:], 12)
	binary.BigEndian.PutUint16(buf[8:], 1)
	binary.BigEndian.PutUint16(buf[10:], 3)
	binary.BigEndian.PutUint16(buf[12:], 2)
	binary.BigEndian.PutUint32(buf[20:], 125)

	buf = append(buf, bePut32(0)...) // dataset type=0, offset=0
	for _, p := range [][2]uint32{{0, 100}, {500, 20}, {1, 5}} {
		buf = append(buf, bePut32(p[0])...)
		buf = append(buf, bePut32(p[1])...)
	}
	buf = append(buf, 0, 0, 1, 0) // 两个语言

	e, err := exd.ParseEXHF(buf)
	if err != nil {
		t.Fatalf("ParseEXHF 失败: %v", err)
	}
	if len(e.Pages) != 3 {
		t.Fatalf("分页数量应为 3，实际 %d", len(e.Pages))
	}
	wantPages := []uint32{0, 500, 1}
	for i, want := range wantPages {
		if e.Pages[i].PageNum != want {
			t.Fatalf("第 %d 个分页号应为 %d，实际 %d", i, want, e.Pages[i].PageNum)
		}
	}
	if e.StringDatasetCount() != 1 {
		t.Fatalf("String 列数量应为 1，实际 %d", e.StringDatasetCount())
	}
	if len(e.Langs) != 2 {
		t.Fatalf("语言数量应为 2，实际 %d", len(e.Langs))
	}
}

// TestEXDFParseBuildRoundTrip 验证 EXDF 解析与重建的无损往返。
func TestEXDFParseBuildRoundTrip(t *testing.T) {
	entries := map[int32][]byte{
		0: {1, 2, 3, 0},
		3: {9, 9},
		7: {},
	}
	data, err := exd.BuildEXDF(entries)
	if err != nil {
		t.Fatalf("BuildEXDF 失败: %v", err)
	}
	parsed, err := exd.ParseEXDF(data)
	if err != nil {
		t.Fatalf("ParseEXDF 失败: %v", err)
	}
	if len(parsed.Entries) != len(entries) {
		t.Fatalf("条目数量不一致: %d != %d", len(parsed.Entries), len(entries))
	}
	for k, want := range entries {
		if !bytes.Equal(parsed.Entries[k], want) {
			t.Fatalf("条目 %d 数据不一致: % x != % x", k, parsed.Entries[k], want)
		}
	}
	rebuilt, err := exd.BuildEXDF(parsed.Entries)
	if err != nil {
		t.Fatalf("重建 EXDF 失败: %v", err)
	}
	if !bytes.Equal(data, rebuilt) {
		t.Fatal("EXDF 往返重建字节不一致")
	}
}

func TestKTGCheatRuleInBuildEntries(t *testing.T) {
	exh := &exd.EXHF{
		DatasetChunkSize: 8,
		Datasets: []exd.Dataset{
			{Type: 0, Offset: 0},
			{Type: 0, Offset: 4},
		},
		Pages: []exd.Page{{PageNum: 0, NumEntries: 1}},
		Langs: []int{0},
	}
	chunk := make([]byte, 8)
	ja := []byte("orig")
	entry := append(append(append([]byte{}, chunk...), ja...), 0)

	file := &exd.File{Entries: map[int32][]byte{0: entry}}
	dataMap := map[int][]string{
		0: {"col0_text", ""}, // 第 2 列 (colIndex 1, offset 4) 为空字符串
	}

	reg := pipeline.NewRegistry([]pipeline.Rule{pipeline.NewKTGCheatRule()})
	reg.SetEnabled("ktg_cheat", true)

	out, replaced, err := buildEntries(exh, file, "KTGTypeWordTextData", map[int]int{0: 0, 4: 1}, dataMap, reg)
	if err != nil {
		t.Fatalf("buildEntries 失败: %v", err)
	}
	if replaced == 0 {
		t.Fatal("应用 KTGCheatRule 后应当标记 replaced > 0")
	}

	// 对于 rowKey=0, (0%9)+1 = 1, colIndex 1 的值应为 "1"
	gotEntry := out[0]
	// 检查 offset 4 指向的字符串
	strOffset := binary.BigEndian.Uint32(gotEntry[4:8])
	targetStrBytes := gotEntry[8+strOffset:]
	if !bytes.HasPrefix(targetStrBytes, []byte("1\x00")) {
		t.Fatalf("期望得到 '1\\x00'，实际: %q", string(targetStrBytes))
	}
}
