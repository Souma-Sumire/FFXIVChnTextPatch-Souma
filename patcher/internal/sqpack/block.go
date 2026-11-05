package sqpack

import (
	"bytes"
	"compress/flate"
	"encoding/binary"
	"fmt"
	"math"
)

const (
	blockPartSize      = 16000
	blockAlignSize     = 128
	textureTypeDXT5    = 5184
	textureTypeB8G8R8A = 5200
	typeBinaryBlock    = 2
	typeTextureBlock   = 4
)

// CompressBlock 使用 Raw Deflate（无 zlib 头 / 无 Adler32）压缩，等价于旧版 jzlib level 9 raw deflate。
func CompressBlock(data []byte) ([]byte, error) {
	var buf bytes.Buffer
	w, err := flate.NewWriter(&buf, flate.BestCompression)
	if err != nil {
		return nil, err
	}
	if _, err := w.Write(data); err != nil {
		_ = w.Close()
		return nil, err
	}
	if err := w.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// alignBlockHeader 复刻旧版头部 128 字节对齐逻辑：
// 小于 128 直接取 128；否则补 128 - (len % 128)（当已对齐时会再加 128）。
func alignBlockHeader(length int) int {
	if length < blockAlignSize {
		return blockAlignSize
	}
	return length + (blockAlignSize - length%blockAlignSize)
}

// BuildBinaryBlock 构建 Type 2 二进制块（EXDF 与 .fdt 字体文件）。
func BuildBinaryBlock(data []byte) ([]byte, error) {
	partCount := int(math.Ceil(float64(len(data)) / float64(blockPartSize)))
	dataHeaderLength := alignBlockHeader(24 + partCount*8)

	header := make([]byte, dataHeaderLength)
	binary.LittleEndian.PutUint32(header[0:], uint32(dataHeaderLength))
	binary.LittleEndian.PutUint32(header[4:], typeBinaryBlock)
	binary.LittleEndian.PutUint32(header[8:], uint32(len(data)))
	binary.LittleEndian.PutUint32(header[12:], 0)
	binary.LittleEndian.PutUint32(header[16:], 0)
	binary.LittleEndian.PutUint32(header[20:], uint32(partCount))

	bodies := make([][]byte, 0, partCount)
	dataOffset := 0
	cursor := 0
	for i := 1; i <= partCount; i++ {
		var part []byte
		if i == partCount {
			part = data[cursor:]
		} else {
			part = data[cursor : cursor+blockPartSize]
		}
		cursor += len(part)
		compr, err := CompressBlock(part)
		if err != nil {
			return nil, err
		}
		paddingSize := blockAlignSize - (len(compr)+16)%blockAlignSize
		blockLen := len(compr) + 16 + paddingSize
		body := make([]byte, blockLen)
		binary.LittleEndian.PutUint32(body[0:], 16)
		binary.LittleEndian.PutUint32(body[4:], 0)
		binary.LittleEndian.PutUint32(body[8:], uint32(len(compr)))
		binary.LittleEndian.PutUint32(body[12:], uint32(len(part)))
		copy(body[16:], compr)
		bodies = append(bodies, body)

		entry := 24 + (i-1)*8
		binary.LittleEndian.PutUint32(header[entry:], uint32(dataOffset))
		binary.LittleEndian.PutUint16(header[entry+4:], uint16(blockLen))
		binary.LittleEndian.PutUint16(header[entry+6:], uint16(len(part)))
		dataOffset += blockLen
	}

	binary.LittleEndian.PutUint32(header[12:], uint32(dataOffset/blockAlignSize))
	binary.LittleEndian.PutUint32(header[16:], uint32(dataOffset/blockAlignSize))

	out := make([]byte, 0, dataHeaderLength+dataOffset)
	out = append(out, header...)
	for _, body := range bodies {
		out = append(out, body...)
	}
	return out, nil
}

// BuildTexBlock 构建 Type 4 纹理块（font*.tex）。
func BuildTexBlock(data []byte) ([]byte, error) {
	if len(data) < 28 {
		return nil, fmt.Errorf("纹理数据过短: %d", len(data))
	}
	mipOffsetIndex := int(int32(binary.LittleEndian.Uint32(data[28:])))
	if mipOffsetIndex < 28 || mipOffsetIndex > len(data) {
		return nil, fmt.Errorf("纹理 mipOffsetIndex 非法: %d", mipOffsetIndex)
	}
	texHeader := data[:mipOffsetIndex]

	textureType := int(binary.LittleEndian.Uint16(texHeader[4:]))
	width := int(binary.LittleEndian.Uint16(texHeader[8:]))
	height := int(binary.LittleEndian.Uint16(texHeader[10:]))
	mipCount := int(binary.LittleEndian.Uint16(texHeader[14:]))
	if mipCount <= 0 {
		return nil, fmt.Errorf("纹理 mipCount 非法: %d", mipCount)
	}
	if mipCount != 1 {
		return nil, fmt.Errorf("当前仅支持单 mipCount 纹理构建: %d", mipCount)
	}

	uncompressedSize := len(data)
	partCount := int(math.Ceil(float64(uncompressedSize) / float64(blockPartSize)))
	dataHeaderLength := alignBlockHeader(24 + mipCount*20 + partCount*2)

	uncompMipSize := width * height
	switch textureType {
	case textureTypeDXT5:
		uncompMipSize = width * height * 2
	case textureTypeB8G8R8A:
		uncompMipSize = width * height * 4
	default:
		if rawMip := len(data) - len(texHeader); rawMip > 0 {
			uncompMipSize = rawMip
		}
	}

	header := make([]byte, dataHeaderLength)
	binary.LittleEndian.PutUint32(header[0:], uint32(dataHeaderLength))
	binary.LittleEndian.PutUint32(header[4:], typeTextureBlock)
	binary.LittleEndian.PutUint32(header[8:], uint32(uncompressedSize))
	binary.LittleEndian.PutUint32(header[12:], 0)
	binary.LittleEndian.PutUint32(header[16:], 0)
	binary.LittleEndian.PutUint32(header[20:], uint32(mipCount))

	bodies := make([][]byte, 0, partCount*mipCount)
	cursor := len(texHeader)
	for o := 0; o < mipCount; o++ {
		entryPos := 24 + o*20
		binary.LittleEndian.PutUint32(header[entryPos:], uint32(mipOffsetIndex))
		lengthPos := entryPos + 4
		binary.LittleEndian.PutUint32(header[lengthPos:], 0)
		binary.LittleEndian.PutUint32(header[entryPos+8:], uint32(uncompMipSize))
		binary.LittleEndian.PutUint32(header[entryPos+12:], 0)
		binary.LittleEndian.PutUint32(header[entryPos+16:], uint32(partCount))

		dataOffset := 0
		sizeTablePos := 24 + mipCount*20 + o*partCount*2
		for k := 1; k <= partCount; k++ {
			var part []byte
			if k == partCount {
				part = data[cursor:]
			} else {
				part = data[cursor : cursor+blockPartSize]
			}
			cursor += len(part)
			compr, err := CompressBlock(part)
			if err != nil {
				return nil, err
			}
			paddingSize := blockAlignSize - (len(compr)+16)%blockAlignSize
			blockLen := len(compr) + 16 + paddingSize
			body := make([]byte, blockLen)
			binary.LittleEndian.PutUint32(body[0:], 16)
			binary.LittleEndian.PutUint32(body[4:], 0)
			binary.LittleEndian.PutUint32(body[8:], uint32(len(compr)))
			binary.LittleEndian.PutUint32(body[12:], uint32(len(part)))
			copy(body[16:], compr)
			bodies = append(bodies, body)

			binary.LittleEndian.PutUint16(header[sizeTablePos:], uint16(blockLen))
			sizeTablePos += 2
			dataOffset += blockLen
		}

		binary.LittleEndian.PutUint32(header[12:], uint32(dataOffset/blockAlignSize))
		binary.LittleEndian.PutUint32(header[16:], uint32(dataOffset/blockAlignSize))
		binary.LittleEndian.PutUint32(header[lengthPos:], uint32(dataOffset+len(texHeader)))
	}

	out := make([]byte, 0, dataHeaderLength+len(texHeader))
	out = append(out, header...)
	out = append(out, texHeader...)
	for _, body := range bodies {
		out = append(out, body...)
	}
	paddingSize := blockAlignSize - len(out)%blockAlignSize
	out = append(out, make([]byte, paddingSize)...)
	return out, nil
}
