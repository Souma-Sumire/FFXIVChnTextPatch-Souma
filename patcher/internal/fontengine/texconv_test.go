package fontengine

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"testing"
)

func TestConvertTex32To16(t *testing.T) {
	tempDir := t.TempDir()
	texPath := filepath.Join(tempDir, "test.tex")

	width, height := uint16(4), uint16(4)
	pixelCount := int(width) * int(height)

	// 构造一个 32 位 B8G8R8A8 模拟数据
	rawHeader := make([]byte, TexHeaderSize)
	binary.LittleEndian.PutUint32(rawHeader[0:4], 0x800000)
	binary.LittleEndian.PutUint32(rawHeader[4:8], TexTypeB8G8R8A8)
	binary.LittleEndian.PutUint16(rawHeader[8:10], width)
	binary.LittleEndian.PutUint16(rawHeader[10:12], height)
	binary.LittleEndian.PutUint16(rawHeader[12:14], 1)
	binary.LittleEndian.PutUint16(rawHeader[14:16], 1)

	pixels := make([]byte, pixelCount*4)
	for i := 0; i < pixelCount; i++ {
		pixels[i*4+0] = 255 // B
		pixels[i*4+1] = 0   // G
		pixels[i*4+2] = 128 // R (~7.5 -> 7)
		pixels[i*4+3] = 255 // A
	}

	fullData := append(rawHeader, pixels...)
	if err := os.WriteFile(texPath, fullData, 0o644); err != nil {
		t.Fatalf("write mock tex failed: %v", err)
	}

	if err := ConvertTex32To16(texPath); err != nil {
		t.Fatalf("ConvertTex32To16 failed: %v", err)
	}

	res, err := os.ReadFile(texPath)
	if err != nil {
		t.Fatalf("read converted tex failed: %v", err)
	}

	if len(res) != TexHeaderSize+pixelCount*2 {
		t.Fatalf("expected size %d, got %d", TexHeaderSize+pixelCount*2, len(res))
	}

	convertedType := binary.LittleEndian.Uint32(res[4:8])
	if convertedType != TexTypeB4G4R4A4 {
		t.Fatalf("expected converted type 0x%X, got 0x%X", TexTypeB4G4R4A4, convertedType)
	}

	// 再次调用应该幂等成功且不改动数据
	if err := ConvertTex32To16(texPath); err != nil {
		t.Fatalf("second call failed: %v", err)
	}
	res2, _ := os.ReadFile(texPath)
	if !bytes.Equal(res, res2) {
		t.Fatalf("second call modified data")
	}
}
