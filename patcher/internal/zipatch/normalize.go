package zipatch

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

const datHeaderEnd = 0x800

// NormalizeShiftedEmptyBlocks 修复由旧版 ffxiv-downloader（create_empty_file_block 误用 u64）
// 写入的 +0x10 错位 EmptyBlock 头，将其移回官方标准的 +0x0C 位置，同时避开任何索引引用的数据偏移。
func NormalizeShiftedEmptyBlocks(datPath, indexPath string) (int, error) {
	indexed := make(map[int64]struct{})
	if indexPath != "" {
		idx, err := sqpack.OpenIndex(indexPath)
		if err != nil {
			return 0, fmt.Errorf("打开索引用于空块校验失败: %w", err)
		}
		for _, folder := range idx.Folders {
			for _, file := range folder.Files {
				datNum, aligned := sqpack.DatNumAndAlignedOffset(file.DataOffset)
				if datNum == 0 {
					indexed[aligned] = struct{}{}
				}
			}
		}
	}

	f, err := os.OpenFile(datPath, os.O_RDWR, 0)
	if err != nil {
		return 0, err
	}
	defer f.Close()

	st, err := f.Stat()
	if err != nil {
		return 0, err
	}
	size := st.Size()
	if size <= datHeaderEnd {
		return 0, nil
	}

	const chunkBlocks = 4096
	buf := make([]byte, chunkBlocks*blockAlign)
	var zero108 [108]byte
	var zero16 [16]byte
	zero16[0] = blockAlign

	fixed := 0
	for off := int64(datHeaderEnd); off+blockAlign <= size; off += int64(len(buf)) {
		toRead := int64(len(buf))
		if off+toRead > size {
			toRead = ((size - off) / blockAlign) * blockAlign
		}
		if toRead < blockAlign {
			break
		}
		if _, err := f.ReadAt(buf[:toRead], off); err != nil {
			return fixed, err
		}
		for pos := int64(0); pos < toRead; pos += blockAlign {
			block := buf[pos : pos+blockAlign]
			if !bytes.Equal(block[0:16], zero16[:]) {
				continue
			}
			val := binary.LittleEndian.Uint32(block[16:20])
			if val == 0 || !bytes.Equal(block[20:128], zero108[:]) {
				continue
			}
			absOff := off + pos
			if _, isFile := indexed[absOff]; isFile {
				continue
			}
			var patched [8]byte
			binary.LittleEndian.PutUint32(patched[0:4], val)
			if _, err := f.WriteAt(patched[:], absOff+12); err != nil {
				return fixed, err
			}
			fixed++
		}
	}

	if fixed > 0 {
		if err := f.Sync(); err != nil {
			return fixed, err
		}
	}
	return fixed, nil
}
