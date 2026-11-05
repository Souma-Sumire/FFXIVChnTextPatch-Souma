package sqpack

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"
)

// IndexFile 对应旧版 SqPackIndexFile，保留 Pt 以便回写新的 dat0 偏移量。
type IndexFile struct {
	Pt         int64 // 该条目在 index 文件中的绝对位置
	ID         int32 // 文件名 CRC（或 index2 中的 folder CRC）
	ID2        int32 // 文件夹 CRC（index2 中为 -1）
	DataOffset int32 // dat 文件中的偏移量（以 8 字节为单位，低 4 位编码 dat 序号）
	IsIndex2   bool  // 是否来自 index2
}

// Folder 对应旧版 SqPackIndexFolder。
type Folder struct {
	ID              int32
	NumFiles        int32
	FileIndexOffset int32
	Files           map[int32]*IndexFile
}

// Index 对应旧版 SqPackIndex。
type Index struct {
	Path     string
	IsIndex2 bool
	Folders  map[int32]*Folder
}

type dataSegment struct {
	FirstVal int32
	Offset   int32
	Size     int32
	SHA1     [20]byte
}

// OpenIndex 解析 SqPack index 或 index2 文件。
func OpenIndex(path string) (*Index, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("读取索引文件失败: %w", err)
	}
	if len(data) < 24 {
		return nil, fmt.Errorf("索引文件过短: %s", path)
	}
	if string(data[0:6]) != "SqPack" {
		return nil, fmt.Errorf("不是 SqPack 文件: %s", path)
	}
	headerLength := int32(binary.LittleEndian.Uint32(data[0x0C:]))
	if headerLength <= 0 || int(headerLength)+24 > len(data) {
		return nil, fmt.Errorf("索引头长度非法 (%d): %s", headerLength, path)
	}
	if typ := int32(binary.LittleEndian.Uint32(data[0x14:])); typ != 2 {
		return nil, fmt.Errorf("不是索引文件 (type=%d): %s", typ, path)
	}

	segments, err := resolveSegments(data, int(headerLength))
	if err != nil {
		return nil, fmt.Errorf("解析索引段失败 (%s): %w", path, err)
	}

	idx := &Index{
		Path:     path,
		IsIndex2: strings.Contains(path, "index2"),
		Folders:  make(map[int32]*Folder),
	}

	if segments[3].Offset != 0 {
		offset := int(segments[3].Offset)
		size := int(segments[3].Size)
		numFolders := size / 16
		for i := 0; i < numFolders; i++ {
			pos := offset + i*16
			if pos+16 > len(data) {
				return nil, fmt.Errorf("索引目录表越界: %s", path)
			}
			id := int32(binary.LittleEndian.Uint32(data[pos:]))
			fileIndexOffset := int32(binary.LittleEndian.Uint32(data[pos+4:]))
			folderSize := int32(binary.LittleEndian.Uint32(data[pos+8:]))
			if fileIndexOffset < 0 || folderSize < 0 {
				return nil, fmt.Errorf("索引目录表条目非法 (%s): offset=%d size=%d", path, fileIndexOffset, folderSize)
			}
			numFiles := folderSize / 16
			folder := &Folder{ID: id, NumFiles: numFiles, FileIndexOffset: fileIndexOffset}
			if err := folder.readFiles(data, false); err != nil {
				return nil, fmt.Errorf("读取索引文件表失败 (%s): %w", path, err)
			}
			idx.Folders[id] = folder
		}
	} else {
		multiplier := int32(1)
		if idx.IsIndex2 {
			multiplier = 2
		}
		numFiles := int32(int64(multiplier) * int64(segments[0].Size) / 16)
		folder := &Folder{ID: 0, NumFiles: numFiles, FileIndexOffset: segments[0].Offset}
		if err := folder.readFiles(data, idx.IsIndex2); err != nil {
			return nil, fmt.Errorf("读取索引文件表失败 (%s): %w", path, err)
		}
		idx.Folders[0] = folder
	}
	return idx, nil
}

func resolveSegments(data []byte, start int) ([4]dataSegment, error) {
	var segments [4]dataSegment
	if start+4 > len(data) {
		return segments, fmt.Errorf("索引段头越界")
	}
	pos := start + 4 // 跳过段头长度
	for i := 0; i < 4; i++ {
		if pos+32 > len(data) {
			return segments, fmt.Errorf("索引段表越界")
		}
		segments[i].FirstVal = int32(binary.LittleEndian.Uint32(data[pos:]))
		segments[i].Offset = int32(binary.LittleEndian.Uint32(data[pos+4:]))
		segments[i].Size = int32(binary.LittleEndian.Uint32(data[pos+8:]))
		copy(segments[i].SHA1[:], data[pos+12:pos+32])
		// 负数偏移会让后续切片直接 panic，畸形索引必须在解析阶段拒绝。
		if segments[i].Offset < 0 || segments[i].Size < 0 {
			return segments, fmt.Errorf("索引段 %d 的偏移或长度非法: offset=%d size=%d", i, segments[i].Offset, segments[i].Size)
		}
		pos += 32
		if i == 0 {
			pos += 4
		}
		pos += 40
	}
	return segments, nil
}

func (f *Folder) readFiles(data []byte, isIndex2 bool) error {
	pos := int(f.FileIndexOffset)
	if pos < 0 || f.NumFiles < 0 {
		return fmt.Errorf("index 文件表头非法: offset=%d files=%d", f.FileIndexOffset, f.NumFiles)
	}
	f.Files = make(map[int32]*IndexFile, f.NumFiles)
	for i := int32(0); i < f.NumFiles; i++ {
		if isIndex2 {
			if pos+8 > len(data) {
				return fmt.Errorf("index2 文件表越界")
			}
			id := int32(binary.LittleEndian.Uint32(data[pos:]))
			offset := int32(binary.LittleEndian.Uint32(data[pos+4:]))
			f.Files[id] = &IndexFile{Pt: int64(pos), ID: id, ID2: -1, DataOffset: offset, IsIndex2: true}
			pos += 8
			continue
		}
		if pos+16 > len(data) {
			return fmt.Errorf("index 文件表越界")
		}
		id := int32(binary.LittleEndian.Uint32(data[pos:]))
		id2 := int32(binary.LittleEndian.Uint32(data[pos+4:]))
		offset := int32(binary.LittleEndian.Uint32(data[pos+8:]))
		f.Files[id] = &IndexFile{Pt: int64(pos), ID: id, ID2: id2, DataOffset: offset, IsIndex2: false}
		pos += 16
	}
	return nil
}

// Lookup 按“文件夹路径 + 文件名”查找索引条目（内部自动转小写计算 CRC）。
func (i *Index) Lookup(folderPath, fileName string) *IndexFile {
	folder := i.Folders[ComputeCRCString(folderPath)]
	if folder == nil {
		return nil
	}
	return folder.Files[ComputeCRCString(fileName)]
}

// WriteDataOffset 把新的 dat 偏移量写回 index 的偏移字段位置（Little-Endian）。
// 对 16 字节条目的 .index 写入 pt+8；对 8 字节条目的 .index2 写入 pt+4。
func WriteDataOffset(w interface {
	WriteAt(p []byte, off int64) (int, error)
}, entry *IndexFile, datOffsetBytes int64) error {
	if entry == nil {
		return fmt.Errorf("索引条目为空")
	}
	offsetPos := entry.Pt + 8
	if entry.IsIndex2 {
		offsetPos = entry.Pt + 4
	}
	var buf [4]byte
	binary.LittleEndian.PutUint32(buf[:], uint32(datOffsetBytes))
	if _, err := w.WriteAt(buf[:], offsetPos); err != nil {
		return fmt.Errorf("写入索引偏移失败: %w", err)
	}
	return nil
}

// DatNumAndAlignedOffset 复刻 ReplaceEXDF.extractFile 的偏移换算。
func DatNumAndAlignedOffset(dataOffset int32) (datNum int, alignedOffset int64) {
	raw := uint64(uint32(dataOffset))
	datNum = int((raw & 0xF) / 2)
	aligned := raw &^ 0xF
	return datNum, int64(aligned * 8)
}
