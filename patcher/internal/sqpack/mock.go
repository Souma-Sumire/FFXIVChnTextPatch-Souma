package sqpack

import (
	"encoding/binary"
)

// CreateMockIndex 构造一个包含指定文件夹和文件列表的合法 SqPack 索引数据。
func CreateMockIndex(folder string, files ...string) []byte {
	data := make([]byte, 4096)
	copy(data[0:6], "SqPack")
	binary.LittleEndian.PutUint32(data[0x0C:0x10], 1024)
	data[0x14] = 0x02

	seg3Pos := 1028 + 32 + 4 + 40 + 32 + 40 + 32 + 40
	folderOffset := uint32(2048)
	fileOffset := folderOffset + 16
	numFiles := uint32(len(files))

	binary.LittleEndian.PutUint32(data[seg3Pos+4:seg3Pos+8], folderOffset)
	binary.LittleEndian.PutUint32(data[seg3Pos+8:seg3Pos+12], 16)

	folderID := uint32(ComputeCRCString(folder))
	binary.LittleEndian.PutUint32(data[folderOffset:folderOffset+4], folderID)
	binary.LittleEndian.PutUint32(data[folderOffset+4:folderOffset+8], fileOffset)
	binary.LittleEndian.PutUint32(data[folderOffset+8:folderOffset+12], numFiles*16)

	pos := int(fileOffset)
	for _, f := range files {
		fileID := uint32(ComputeCRCString(f))
		binary.LittleEndian.PutUint32(data[pos:pos+4], fileID)
		pos += 16
	}

	return data
}
