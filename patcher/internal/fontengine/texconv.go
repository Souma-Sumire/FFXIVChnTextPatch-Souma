package fontengine

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// TexHeader 对应 FFXIV .tex 贴图文件的前部元数据（80 字节）
type TexHeader struct {
	Attribute   uint32
	Type        uint32
	Width       uint16
	Height      uint16
	Depth       uint16
	MipmapCount uint16
	LodOffsets  [3]uint32
}

const (
	TexTypeB4G4R4A4 = 0x1440
	TexTypeB8G8R8A8 = 0x1450
	TexHeaderSize   = 80
)

// ConvertTex32To16 将单个 32 位 (0x1450 B8G8R8A8) 的 .tex 贴图转换为 16 位 (0x1440 B4G4R4A4)。
// 若输入已经是 16 位，则直接返回 nil。
func ConvertTex32To16(texPath string) error {
	data, err := os.ReadFile(texPath)
	if err != nil {
		return fmt.Errorf("读取贴图文件失败: %w", err)
	}
	if len(data) < TexHeaderSize {
		return fmt.Errorf("贴图文件头数据不足: %s", texPath)
	}

	var h TexHeader
	if err := binary.Read(bytes.NewReader(data[:binary.Size(h)]), binary.LittleEndian, &h); err != nil {
		return fmt.Errorf("解析贴图头失败: %w", err)
	}

	if h.Type == TexTypeB4G4R4A4 {
		return nil
	}
	if h.Type != TexTypeB8G8R8A8 {
		return fmt.Errorf("不支持的贴图像素格式 0x%X (仅支持 0x1450 转 0x1440)", h.Type)
	}

	pixelData := data[TexHeaderSize:]
	pixelCount := int(h.Width) * int(h.Height)
	if len(pixelData) < pixelCount*4 {
		return fmt.Errorf("贴图像素数据不完整 (期望至少 %d 字节, 实际 %d 字节)", pixelCount*4, len(pixelData))
	}

	outPixels := make([]byte, pixelCount*2)
	for i := 0; i < pixelCount; i++ {
		srcIdx := i * 4
		b := uint16(pixelData[srcIdx+0]) * 15 / 255
		g := uint16(pixelData[srcIdx+1]) * 15 / 255
		r := uint16(pixelData[srcIdx+2]) * 15 / 255
		a := uint16(pixelData[srcIdx+3]) * 15 / 255

		val := (a << 12) | (r << 8) | (g << 4) | b
		binary.LittleEndian.PutUint16(outPixels[i*2:], val)
	}

	outData := make([]byte, TexHeaderSize+len(outPixels))
	copy(outData[:TexHeaderSize], data[:TexHeaderSize])
	binary.LittleEndian.PutUint32(outData[4:8], TexTypeB4G4R4A4)
	copy(outData[TexHeaderSize:], outPixels)

	return os.WriteFile(texPath, outData, 0o644)
}

// ConvertTexDirTo16Bit 将指定目录下所有的 .tex 贴图文件转换为 16 位 (B4G4R4A4)。
func ConvertTexDirTo16Bit(dir string) error {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return fmt.Errorf("读取贴图目录失败: %w", err)
	}

	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".tex") {
			continue
		}
		path := filepath.Join(dir, entry.Name())
		if err := ConvertTex32To16(path); err != nil {
			return fmt.Errorf("转换贴图 %s 为 16 位失败: %w", entry.Name(), err)
		}
	}
	return nil
}
