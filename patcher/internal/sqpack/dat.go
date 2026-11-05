package sqpack

import (
	"bytes"
	"compress/flate"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
)

// ErrEmptyDatBlock 表示该偏移处的 dat 块未填充数据（全 0 占位符）。
var ErrEmptyDatBlock = errors.New("dat 块为空占位符")

// maxDecompressedFileSize 是单个文件解包结果的上限。损坏的头部会把 fileSize
// 报成接近 int32 上限，直接按它分配内存会瞬间吃掉 2GB。
const maxDecompressedFileSize = 512 << 20

// maxBlockEntrySize 是单个块表项的最大字节数（type4 为 20，type2 为 8），
// 用于在读取块表前拦掉远超文件实际尺寸的块数。
const maxBlockEntrySize = 20

// DatFile 对应旧版 SqPackDatFile，只实现本项目需要的类型 2 / 类型 4 解包。
type DatFile struct {
	f    *os.File
	size int64
}

// OpenDat 打开 SqPack dat 文件。
func OpenDat(path string) (*DatFile, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("打开 dat 文件失败: %w", err)
	}
	st, err := f.Stat()
	if err != nil {
		_ = f.Close()
		return nil, fmt.Errorf("获取 dat 文件信息失败 (%s): %w", path, err)
	}
	return &DatFile{f: f, size: st.Size()}, nil
}

// Close 关闭 dat 文件。
func (d *DatFile) Close() error {
	return d.f.Close()
}

// ReadAt 从 dat 文件绝对偏移处读取原始字节（用于读取块头）。
func (d *DatFile) ReadAt(off int64, n int) ([]byte, error) {
	return d.readAt(off, n)
}

func (d *DatFile) readAt(off int64, n int) ([]byte, error) {
	if n < 0 {
		return nil, fmt.Errorf("非法读取长度 (%s): %d", d.f.Name(), n)
	}
	buf := make([]byte, n)
	if _, err := d.f.ReadAt(buf, off); err != nil {
		return nil, fmt.Errorf("读取 dat (%s) 失败 (off=%d len=%d): %w", d.f.Name(), off, n, err)
	}
	return buf, nil
}

func le32(b []byte) int32 { return int32(binary.LittleEndian.Uint32(b)) }

// ExtractFile 解包指定偏移处的完整文件字节流。
func (d *DatFile) ExtractFile(fileOffset int64) ([]byte, error) {
	header, err := d.readAt(fileOffset, 24)
	if err != nil {
		return nil, err
	}
	headerLength := le32(header[0:])
	contentType := le32(header[4:])
	fileSize := le32(header[8:])
	blockCount := le32(header[20:])

	if headerLength == 0 && contentType == 0 && fileSize == 0 {
		return nil, ErrEmptyDatBlock
	}

	if fileSize < 0 || blockCount < 0 {
		return nil, fmt.Errorf("dat (%s, off=%d) 文件头非法: fileSize=%d blocks=%d", d.f.Name(), fileOffset, fileSize, blockCount)
	}
	if int64(fileSize) > maxDecompressedFileSize {
		return nil, fmt.Errorf("dat (%s, off=%d) 文件头非法: 解包尺寸 %d 超过上限 %d", d.f.Name(), fileOffset, fileSize, maxDecompressedFileSize)
	}
	if blockCount > 0 && int64(blockCount)*maxBlockEntrySize > d.size {
		return nil, fmt.Errorf("dat (%s, off=%d) 文件头非法: 块数 %d 超出文件尺寸 %d", d.f.Name(), fileOffset, blockCount, d.size)
	}

	switch contentType {
	case 2:
		return d.extractType2(fileOffset, int64(headerLength), fileSize, blockCount)
	case 4:
		return d.extractType4(fileOffset, int64(headerLength), fileSize, blockCount)
	default:
		return nil, fmt.Errorf("dat (%s, off=%d) 暂不支持的 dat 内容类型: %d", d.f.Name(), fileOffset, contentType)
	}
}

// extractType2 处理 EXDF、fdt 等二进制块。
func (d *DatFile) extractType2(fileOffset, headerLength int64, fileSize, blockCount int32) ([]byte, error) {
	out := make([]byte, fileSize)
	if blockCount == 0 {
		return out, nil
	}
	table, err := d.readAt(fileOffset+24, int(blockCount)*8)
	if err != nil {
		return nil, err
	}
	pos := 0
	for i := 0; i < int(blockCount); i++ {
		p := i * 8
		offset := le32(table[p:])
		block, err := d.readBlock(fileOffset + headerLength + int64(offset))
		if err != nil {
			return nil, err
		}
		if pos+len(block) > len(out) {
			return nil, fmt.Errorf("type2 块越界: pos=%d len=%d total=%d", pos, len(block), len(out))
		}
		copy(out[pos:], block)
		pos += len(block)
	}
	return out, nil
}

type texBlock struct {
	Offset        int32
	FrameSize     int32
	BlockTableOff int32
	NumSubBlocks  int32
}

// extractType4 处理纹理块（font*.tex）。
func (d *DatFile) extractType4(fileOffset, headerLength int64, fileSize, blockCount int32) ([]byte, error) {
	if blockCount == 0 {
		return make([]byte, fileSize), nil
	}
	table, err := d.readAt(fileOffset+24, int(blockCount)*20)
	if err != nil {
		return nil, err
	}
	blocks := make([]texBlock, blockCount)
	for i := 0; i < int(blockCount); i++ {
		p := i * 20
		blocks[i] = texBlock{
			Offset:        le32(table[p:]),
			FrameSize:     le32(table[p+4:]),
			BlockTableOff: le32(table[p+12:]),
			NumSubBlocks:  le32(table[p+16:]),
		}
	}

	dataBlocks := make([][]int32, blockCount)
	cursor := fileOffset + 24 + int64(blockCount)*20
	for i := 0; i < int(blockCount); i++ {
		n := int(blocks[i].NumSubBlocks)
		if n < 0 {
			return nil, fmt.Errorf("纹理子块数量非法: %d", n)
		}
		if int64(n)*2 > d.size {
			return nil, fmt.Errorf("纹理子块数量 %d 超出文件尺寸 %d", n, d.size)
		}
		dataBlocks[i] = make([]int32, n)
		if n == 0 {
			continue
		}
		dataBlocks[i][0] = blocks[i].Offset
		running := blocks[i].Offset
		for j := 1; j < n; j++ {
			b, err := d.readAt(cursor, 2)
			if err != nil {
				return nil, err
			}
			cursor += 2
			running += int32(binary.LittleEndian.Uint16(b))
			dataBlocks[i][j] = running
		}
		cursor += 2 // 每个块末尾的填充 short
	}

	extraHeaderSize := blocks[0].Offset
	if extraHeaderSize < 0 {
		return nil, fmt.Errorf("纹理额外头长度非法: %d", extraHeaderSize)
	}
	extraHeader, err := d.readAt(fileOffset+headerLength, int(extraHeaderSize))
	if err != nil {
		return nil, err
	}

	out := make([]byte, fileSize)
	mipmapPosTable := mipmapPositions(extraHeader, int(blockCount))
	for j := 0; j < int(blockCount); j++ {
		currentFileOffset := 0
		if mipmapPosTable != nil && j < len(mipmapPosTable) {
			currentFileOffset = mipmapPosTable[j]
		}
		for _, blockOffset := range dataBlocks[j] {
			off := fileOffset + headerLength + int64(blockOffset)
			hdr, err := d.readAt(off, 16)
			if err != nil {
				return nil, err
			}
			compressedSize := int(le32(hdr[8:]))
			decompressedSize := int(le32(hdr[12:]))
			block, err := d.readBlockAt(off+16, compressedSize, decompressedSize)
			if err != nil {
				return nil, err
			}
			if currentFileOffset < 0 || currentFileOffset+len(block) > len(out) {
				return nil, fmt.Errorf("纹理块越界: pos=%d len=%d total=%d", currentFileOffset, len(block), len(out))
			}
			copy(out[currentFileOffset:], block)
			currentFileOffset += decompressedSize
		}
	}
	copy(out, extraHeader)
	return out, nil
}

func mipmapPositions(extraHeader []byte, blockCount int) []int {
	if len(extraHeader) < 16 {
		return nil
	}
	numMipmaps := int(int16(binary.LittleEndian.Uint16(extraHeader[14:])))
	size := numMipmaps
	if numMipmaps < blockCount {
		size = blockCount
	}
	table := make([]int, size)
	for i := 0; i < numMipmaps; i++ {
		p := 28 + i*4
		if p+4 > len(extraHeader) {
			break
		}
		table[i] = int(le32(extraHeader[p:]))
	}
	return table
}

// readBlock 读取 type2 子块头并按头部尺寸解压。
func (d *DatFile) readBlock(off int64) ([]byte, error) {
	hdr, err := d.readAt(off, 16)
	if err != nil {
		return nil, err
	}
	compressedSize := int(le32(hdr[8:]))
	decompressedSize := int(le32(hdr[12:]))
	return d.readBlockAt(off+16, compressedSize, decompressedSize)
}

func (d *DatFile) readBlockAt(dataOff int64, compressedSize, decompressedSize int) ([]byte, error) {
	if decompressedSize < 0 || compressedSize < 0 {
		return nil, fmt.Errorf("子块尺寸非法: compressed=%d decompressed=%d", compressedSize, decompressedSize)
	}
	// 子块头单独损坏时不会经过文件头的 512MB 上限，这里同样要拦：否则一个把尺寸
	// 报成接近 int32 上限的块会先按声明值分配内存，再在读盘时失败。
	if compressedSize > maxDecompressedFileSize || decompressedSize > maxDecompressedFileSize {
		return nil, fmt.Errorf("dat (%s, off=%d) 子块尺寸超过上限: compressed=%d decompressed=%d limit=%d",
			d.f.Name(), dataOff, compressedSize, decompressedSize, maxDecompressedFileSize)
	}
	// 未压缩子块按 decompressedSize 原样读取，压缩子块按 compressedSize 读取。
	raw := compressedSize == 32000 || decompressedSize == 1
	readSize := compressedSize
	if raw {
		readSize = decompressedSize
	}
	if dataOff < 0 || dataOff > d.size || int64(readSize) > d.size-dataOff {
		return nil, fmt.Errorf("dat (%s, off=%d) 子块越界: size=%d fileSize=%d", d.f.Name(), dataOff, readSize, d.size)
	}
	if raw {
		return d.readAt(dataOff, readSize)
	}
	compr, err := d.readAt(dataOff, compressedSize)
	if err != nil {
		return nil, err
	}
	out := make([]byte, decompressedSize)
	r := flate.NewReader(bytes.NewReader(compr))
	defer r.Close()
	if _, err := io.ReadFull(r, out); err != nil {
		return nil, fmt.Errorf("dat (%s, off=%d) Deflate 解压失败: %w", d.f.Name(), dataOff, err)
	}
	return out, nil
}
