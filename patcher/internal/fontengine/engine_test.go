package fontengine

import (
	"os"
	"path/filepath"
	"testing"
)

func TestResolveBinaryInvalidOrSmallFilesIgnored(t *testing.T) {
	tmpDir := t.TempDir()

	// 1. 空文件不应被认为是有效组件
	emptyCli := filepath.Join(tmpDir, CLIBinaryName)
	if err := os.WriteFile(emptyCli, []byte{}, 0o755); err != nil {
		t.Fatal(err)
	}

	engine := NewEngine(emptyCli)
	if _, err := engine.ResolveBinary(); err != ErrCliNotFound {
		t.Fatalf("空文件不应解析成功，期望 ErrCliNotFound，实际: %v", err)
	}

	// 2. 小于 1MB 的伪文件不应被认为是有效组件
	smallCli := filepath.Join(tmpDir, "small-"+CLIBinaryName)
	if err := os.WriteFile(smallCli, make([]byte, 1024), 0o755); err != nil {
		t.Fatal(err)
	}
	engine.SetBinPath(smallCli)
	if _, err := engine.ResolveBinary(); err != ErrCliNotFound {
		t.Fatalf("小于 1MB 的文件不应解析成功，期望 ErrCliNotFound，实际: %v", err)
	}

	// 3. 大于 1MB 的有效文件应解析成功并返回绝对路径
	validCli := filepath.Join(tmpDir, "valid-"+CLIBinaryName)
	if err := os.WriteFile(validCli, make([]byte, 1<<20+100), 0o755); err != nil {
		t.Fatal(err)
	}
	engine.SetBinPath(validCli)
	resolved, err := engine.ResolveBinary()
	if err != nil {
		t.Fatalf("大于 1MB 的文件应解析成功: %v", err)
	}
	if !filepath.IsAbs(resolved) {
		t.Fatalf("解析结果必须是绝对路径，实际为: %s", resolved)
	}
}
