package zipatch

import (
	"bytes"
	"compress/flate"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"path/filepath"
)

const (
	uncompressedBlockSentinel = 32000
	blockAlign                = 128
	emptyBlockHeaderSize      = 24
)

var zeroScratch = make([]byte, 64*1024)

type patchApplier struct {
	outDir   string
	targets  map[string]struct{}
	platform string
	files    map[string]*os.File
}

func newPatchApplier(outDir string, targets []string) *patchApplier {
	set := make(map[string]struct{}, len(targets))
	for _, t := range targets {
		set[normalizePath(t)] = struct{}{}
	}
	return &patchApplier{
		outDir:   outDir,
		targets:  set,
		platform: "win32",
		files:    make(map[string]*os.File),
	}
}

func (a *patchApplier) wants(relPath string) bool {
	if len(a.targets) == 0 {
		return true
	}
	_, ok := a.targets[relPath]
	return ok
}

func (a *patchApplier) openFile(relPath string, trunc bool) (*os.File, error) {
	if trunc {
		if f, ok := a.files[relPath]; ok {
			_ = f.Close()
			delete(a.files, relPath)
		}
	} else if f, ok := a.files[relPath]; ok {
		return f, nil
	}

	fullPath := filepath.Join(a.outDir, filepath.FromSlash(relPath))
	if err := os.MkdirAll(filepath.Dir(fullPath), 0o755); err != nil {
		return nil, err
	}
	flags := os.O_RDWR | os.O_CREATE
	if trunc {
		flags |= os.O_TRUNC
	}
	f, err := os.OpenFile(fullPath, flags, 0o644)
	if err != nil {
		return nil, err
	}
	a.files[relPath] = f
	return f, nil
}

func (a *patchApplier) closeAll() error {
	var firstErr error
	for rel, f := range a.files {
		if err := f.Sync(); err != nil && firstErr == nil {
			firstErr = err
		}
		if err := f.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
		delete(a.files, rel)
	}
	return firstErr
}

func (a *patchApplier) applyChunk(fourcc string, body []byte) (bool, error) {
	switch fourcc {
	case "EOF_":
		return true, nil
	case "SQPK":
		return false, a.applySQPK(body)
	case "FHDR", "APLY", "APFS", "ADIR", "DELD", "XXXX":
		return false, nil
	default:
		return false, fmt.Errorf("不支持的 ZiPatch 块类型: %s", fourcc)
	}
}

func (a *patchApplier) applySQPK(body []byte) error {
	if len(body) < 5 {
		return fmt.Errorf("SQPK 块过短: %d", len(body))
	}
	innerSize := int(binary.BigEndian.Uint32(body[0:4]))
	if innerSize != len(body) {
		return fmt.Errorf("SQPK 长度不匹配: %d != %d", innerSize, len(body))
	}

	cmd := body[4]
	switch cmd {
	case 'T':
		if len(body) < 10 {
			return fmt.Errorf("SQPK TargetInfo 长度不足: %d", len(body))
		}
		a.platform = platformName(binary.BigEndian.Uint16(body[8:10]))
		return nil
	case 'I', 'X':
		return nil
	case 'H':
		return a.applyHeader(body)
	case 'A':
		return a.applyAddData(body)
	case 'D', 'E':
		return a.applyEmptyData(body)
	case 'F':
		return a.applyFile(body)
	default:
		return fmt.Errorf("不支持的 SQPK 指令: %q", cmd)
	}
}

func (a *patchApplier) applyHeader(body []byte) error {
	if len(body) < 16+HeaderSize {
		return fmt.Errorf("SQPK Header 长度不足: %d", len(body))
	}
	fileKind := body[5]
	headerKind := body[6]
	var rawTarget string
	if fileKind == 'D' {
		rawTarget = readDatPath(body[8:16])
	} else {
		rawTarget = readIndexPath(body[8:16])
	}
	rel := resolvePlatform(rawTarget, a.platform)
	if !a.wants(rel) {
		return nil
	}
	offset := int64(HeaderSize)
	if headerKind == 'V' {
		offset = 0
	}
	f, err := a.openFile(rel, false)
	if err != nil {
		return err
	}
	_, err = f.WriteAt(body[16:16+HeaderSize], offset)
	return err
}

func (a *patchApplier) applyAddData(body []byte) error {
	if len(body) < 28 {
		return fmt.Errorf("SQPK AddData 长度不足: %d", len(body))
	}
	rel := resolvePlatform(readDatPath(body[8:16]), a.platform)
	if !a.wants(rel) {
		return nil
	}
	blockOffset := int64(binary.BigEndian.Uint32(body[16:20])) << 7
	blockNumber := int64(binary.BigEndian.Uint32(body[20:24])) << 7
	blockDeleteNumber := int64(binary.BigEndian.Uint32(body[24:28])) << 7

	payload := body[28:]
	if int64(len(payload)) < blockNumber {
		return fmt.Errorf("SQPK AddData 载荷不足: need=%d got=%d", blockNumber, len(payload))
	}
	f, err := a.openFile(rel, false)
	if err != nil {
		return err
	}
	if blockNumber > 0 {
		if _, err := f.WriteAt(payload[:blockNumber], blockOffset); err != nil {
			return err
		}
	}
	if blockDeleteNumber > 0 {
		if err := writeZerosAt(f, blockOffset+blockNumber, blockDeleteNumber); err != nil {
			return err
		}
	}
	return nil
}

func (a *patchApplier) applyEmptyData(body []byte) error {
	if len(body) < 24 {
		return fmt.Errorf("SQPK Delete/Expand 长度不足: %d", len(body))
	}
	rel := resolvePlatform(readDatPath(body[8:16]), a.platform)
	if !a.wants(rel) {
		return nil
	}
	blockOffset := int64(binary.BigEndian.Uint32(body[16:20])) << 7
	blockCount := binary.BigEndian.Uint32(body[20:24])

	// 官方 SqPack 24 字节空块头全为 uint32；block_count - 1 位于 +0x0C（u32）。
	var hdr [emptyBlockHeaderSize]byte
	binary.LittleEndian.PutUint32(hdr[0:4], blockAlign)
	binary.LittleEndian.PutUint32(hdr[12:16], blockCount-1)

	f, err := a.openFile(rel, false)
	if err != nil {
		return err
	}
	if _, err := f.WriteAt(hdr[:], blockOffset); err != nil {
		return err
	}
	if totalBytes := int64(blockCount) << 7; totalBytes > emptyBlockHeaderSize {
		if err := writeZerosAt(f, blockOffset+emptyBlockHeaderSize, totalBytes-emptyBlockHeaderSize); err != nil {
			return err
		}
	}
	return nil
}

func (a *patchApplier) applyFile(body []byte) error {
	if len(body) < 32 {
		return fmt.Errorf("SQPK File 长度不足: %d", len(body))
	}
	op := body[5]
	fileOffset := int64(binary.BigEndian.Uint64(body[8:16]))
	pathLen := int(int32(binary.BigEndian.Uint32(body[24:28])))
	expacID := binary.BigEndian.Uint16(body[28:30])

	if op == 'R' {
		if expacID == 0 {
			for rel := range a.targets {
				if f, ok := a.files[rel]; ok {
					_ = f.Close()
					delete(a.files, rel)
				}
				_ = os.Remove(filepath.Join(a.outDir, filepath.FromSlash(rel)))
			}
		}
		return nil
	}
	if pathLen < 0 || 32+pathLen > len(body) {
		return fmt.Errorf("SQPK File 路径长度非法: %d", pathLen)
	}
	rel := resolvePlatform(trimZeroASCII(body[32:32+pathLen]), a.platform)
	if !a.wants(rel) {
		return nil
	}

	switch op {
	case 'M':
		return nil
	case 'D':
		if f, ok := a.files[rel]; ok {
			_ = f.Close()
			delete(a.files, rel)
		}
		if err := os.Remove(filepath.Join(a.outDir, filepath.FromSlash(rel))); err != nil && !os.IsNotExist(err) {
			return err
		}
		return nil
	case 'A':
		f, err := a.openFile(rel, fileOffset == 0)
		if err != nil {
			return err
		}
		pos := 32 + pathLen
		writeOff := fileOffset
		for pos < len(body) {
			if pos+16 > len(body) {
				return fmt.Errorf("SQPK FileAdd 块头越界")
			}
			headerSize := int(int32(binary.LittleEndian.Uint32(body[pos : pos+4])))
			compressedSize := int(int32(binary.LittleEndian.Uint32(body[pos+8 : pos+12])))
			dataSize := int(int32(binary.LittleEndian.Uint32(body[pos+12 : pos+16])))
			if headerSize < 16 || dataSize < 0 {
				return fmt.Errorf("SQPK FileAdd 块参数非法: header=%d data=%d", headerSize, dataSize)
			}
			payloadLen := compressedSize
			isCompressed := compressedSize != uncompressedBlockSentinel
			if !isCompressed {
				payloadLen = dataSize
			}
			if payloadLen < 0 || pos+headerSize+payloadLen > len(body) {
				return fmt.Errorf("SQPK FileAdd 块载荷越界")
			}
			payload := body[pos+headerSize : pos+headerSize+payloadLen]
			var out []byte
			if isCompressed {
				r := flate.NewReader(bytes.NewReader(payload))
				out = make([]byte, dataSize)
				_, err := io.ReadFull(r, out)
				_ = r.Close()
				if err != nil {
					return fmt.Errorf("解压 SQPK FileAdd 块失败: %w", err)
				}
			} else {
				out = payload
			}
			if _, err := f.WriteAt(out, writeOff); err != nil {
				return err
			}
			writeOff += int64(dataSize)
			blockTotal := headerSize + payloadLen
			pos += ((blockTotal + blockAlign - 1) / blockAlign) * blockAlign
		}
		return nil
	default:
		return fmt.Errorf("不支持的 SQPK File 操作: %q", op)
	}
}

func writeZerosAt(f *os.File, offset, length int64) error {
	for length > 0 {
		n := length
		if n > int64(len(zeroScratch)) {
			n = int64(len(zeroScratch))
		}
		written, err := f.WriteAt(zeroScratch[:n], offset)
		if err != nil {
			return err
		}
		offset += int64(written)
		length -= int64(written)
	}
	return nil
}
