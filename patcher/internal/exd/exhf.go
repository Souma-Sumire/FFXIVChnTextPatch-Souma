package exd

import (
	"encoding/binary"
	"fmt"
)

// Dataset 对应旧版 EXDFDataset（EXHF 中的列定义）。
type Dataset struct {
	Type   uint16
	Offset uint16
}

// Page 对应旧版 EXDFPage（EXHF 中的分页定义）。
type Page struct {
	PageNum    uint32
	NumEntries uint32
}

// EXHF 对应旧版 EXHFFile，内部全部使用 Big-Endian。
type EXHF struct {
	DatasetChunkSize uint16
	Datasets         []Dataset
	Pages            []Page
	Langs            []int
	NumEntries       uint32
}

const (
	exhfMagic   = 0x45584846 // "EXHF"
	exhfVersion = 3
)

// ParseEXHF 解析 .exh 头部。
func ParseEXHF(data []byte) (*EXHF, error) {
	if len(data) < 32 {
		return nil, fmt.Errorf("EXHF 数据过短: %d", len(data))
	}
	if binary.BigEndian.Uint32(data[0:]) != exhfMagic {
		return nil, fmt.Errorf("不是 EXHF 文件")
	}
	if v := binary.BigEndian.Uint16(data[4:]); v != exhfVersion {
		return nil, fmt.Errorf("EXHF 版本不支持: %d", v)
	}

	e := &EXHF{
		DatasetChunkSize: binary.BigEndian.Uint16(data[6:]),
		NumEntries:       binary.BigEndian.Uint32(data[20:]),
	}
	numDataSetTable := int(binary.BigEndian.Uint16(data[8:]))
	numPageTable := int(binary.BigEndian.Uint16(data[10:]))
	numLangTable := int(binary.BigEndian.Uint16(data[12:]))

	pos := 32
	if pos+numDataSetTable*4 > len(data) {
		return nil, fmt.Errorf("EXHF 列定义越界")
	}
	e.Datasets = make([]Dataset, numDataSetTable)
	for i := 0; i < numDataSetTable; i++ {
		e.Datasets[i] = Dataset{
			Type:   binary.BigEndian.Uint16(data[pos:]),
			Offset: binary.BigEndian.Uint16(data[pos+2:]),
		}
		pos += 4
	}

	if pos+numPageTable*8 > len(data) {
		return nil, fmt.Errorf("EXHF 分页定义越界")
	}
	e.Pages = make([]Page, numPageTable)
	for i := 0; i < numPageTable; i++ {
		e.Pages[i] = Page{
			PageNum:    binary.BigEndian.Uint32(data[pos:]),
			NumEntries: binary.BigEndian.Uint32(data[pos+4:]),
		}
		pos += 8
	}

	if pos+numLangTable*2 > len(data) {
		return nil, fmt.Errorf("EXHF 语言表越界")
	}
	e.Langs = make([]int, numLangTable)
	for i := 0; i < numLangTable; i++ {
		e.Langs[i] = int(data[pos])
		pos += 2
	}
	return e, nil
}

// StringDatasetCount 统计 type == 0（String 列）的数量。
func (e *EXHF) StringDatasetCount() int {
	count := 0
	for _, d := range e.Datasets {
		if d.Type == 0 {
			count++
		}
	}
	return count
}
