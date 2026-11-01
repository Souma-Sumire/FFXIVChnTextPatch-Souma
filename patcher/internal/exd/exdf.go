package exd

import (
	"encoding/binary"
	"fmt"
	"sort"
)

// Entry 对应旧版 EXDFEntry：定长 chunk + 变长字符串区。
type Entry struct {
	Chunk []byte
	Data  []byte
}

// NewEntry 按 datasetChunkSize 切分一条 EXDF 行数据。
func NewEntry(data []byte, datasetChunkSize int) *Entry {
	if datasetChunkSize < 0 || len(data) < datasetChunkSize {
		return &Entry{Chunk: []byte{}, Data: []byte{}}
	}
	chunk := make([]byte, datasetChunkSize)
	copy(chunk, data[:datasetChunkSize])
	full := make([]byte, len(data))
	copy(full, data)
	return &Entry{Chunk: chunk, Data: full}
}

// GetString 读取 chunk 中 offset 处的 Big-Endian 字符串偏移，并返回对应的 UTF-8 字节（不含结尾 0）。
func (e *Entry) GetString(offset int) []byte {
	if offset < 0 || offset+4 > len(e.Chunk) {
		return []byte{}
	}
	stringOffset := int(int32(binary.BigEndian.Uint32(e.Chunk[offset:])))
	start := len(e.Chunk) + stringOffset
	if start < 0 || start >= len(e.Data) {
		return []byte{}
	}
	end := start
	for end < len(e.Data) && e.Data[end] != 0 {
		end++
	}
	out := make([]byte, end-start)
	copy(out, e.Data[start:end])
	return out
}

// File 对应旧版 EXDFFile。
type File struct {
	Entries map[int32][]byte
}

const (
	exdfMagic   = 0x45584446 // "EXDF"
	exdfVersion = 2
)

// ParseEXDF 解析 .exd 文件，保留每条行的原始字节。
func ParseEXDF(data []byte) (*File, error) {
	if len(data) < 32 {
		return nil, fmt.Errorf("EXDF 数据过短: %d", len(data))
	}
	if binary.BigEndian.Uint32(data[0:]) != exdfMagic {
		return nil, fmt.Errorf("不是 EXDF 文件")
	}
	if v := binary.BigEndian.Uint16(data[4:]); v != exdfVersion {
		return nil, fmt.Errorf("EXDF 版本不支持: %d", v)
	}
	offsetTableSize := int(binary.BigEndian.Uint32(data[8:]))
	if offsetTableSize < 0 || 32+offsetTableSize > len(data) {
		return nil, fmt.Errorf("EXDF 偏移表越界: %d", offsetTableSize)
	}
	f := &File{Entries: make(map[int32][]byte, offsetTableSize/8)}
	pos := 32
	for i := 0; i < offsetTableSize/8; i++ {
		if pos+8 > len(data) {
			return nil, fmt.Errorf("EXDF 偏移表越界")
		}
		rowIndex := int32(binary.BigEndian.Uint32(data[pos:]))
		rowOffset := int(binary.BigEndian.Uint32(data[pos+4:]))
		pos += 8
		if rowOffset < 0 || rowOffset+6 > len(data) {
			return nil, fmt.Errorf("EXDF 行偏移越界: %d", rowOffset)
		}
		size := int(binary.BigEndian.Uint32(data[rowOffset:]))
		start := rowOffset + 6
		if size < 0 || start+size > len(data) {
			return nil, fmt.Errorf("EXDF 行数据越界: row=%d size=%d", rowIndex, size)
		}
		entry := make([]byte, size)
		copy(entry, data[start:start+size])
		f.Entries[rowIndex] = entry
	}
	return f, nil
}

// BuildEXDF 按 rowIndex 升序组装 .exd 文件（Big-Endian）。
func BuildEXDF(entries map[int32][]byte) ([]byte, error) {
	keys := make([]int32, 0, len(entries))
	headerSize := 0
	bodySize := 0
	for k, v := range entries {
		keys = append(keys, k)
		headerSize += 8
		bodySize += len(v) + 6
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })

	out := make([]byte, 32+headerSize+bodySize)
	copy(out[0:4], []byte{'E', 'X', 'D', 'F'})
	out[5] = exdfVersion
	binary.BigEndian.PutUint32(out[8:], uint32(headerSize))
	binary.BigEndian.PutUint32(out[12:], uint32(bodySize))

	dataOffset := 32 + headerSize
	tablePos := 32
	bodyPos := 32 + headerSize
	for _, k := range keys {
		v := entries[k]
		binary.BigEndian.PutUint32(out[tablePos:], uint32(k))
		binary.BigEndian.PutUint32(out[tablePos+4:], uint32(dataOffset))
		tablePos += 8

		binary.BigEndian.PutUint32(out[bodyPos:], uint32(len(v)))
		binary.BigEndian.PutUint16(out[bodyPos+4:], 1)
		copy(out[bodyPos+6:], v)
		bodyPos += 6 + len(v)
		dataOffset += 6 + len(v)
	}
	return out, nil
}
