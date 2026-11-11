package fontengine

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

func TestMain(m *testing.M) {
	if os.Getenv("FC_TEST_HELPER") == "1" {
		runHelperCLI(os.Args[1:])
		os.Exit(0)
	}
	os.Exit(m.Run())
}

func runHelperCLI(args []string) {
	for i := 0; i < len(args)-1; i++ {
		switch args[i] {
		case "--export-raw":
			outDir := args[i+1]
			if !filepath.IsAbs(outDir) {
				os.Exit(2)
			}
			if err := os.WriteFile(filepath.Join(outDir, "AXIS_12.fdt"), []byte("fdt"), 0o644); err != nil {
				os.Exit(3)
			}
		case "--export-ttmp":
			outPath := args[i+1]
			if !filepath.IsAbs(outPath) {
				os.Exit(2)
			}
			if err := os.WriteFile(outPath, []byte("ttmp"), 0o644); err != nil {
				os.Exit(3)
			}
		}
	}
}

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

func TestPromoteToFontchangerDir(t *testing.T) {
	base := t.TempDir()
	src := filepath.Join(base, CLIBinaryName)
	if err := os.WriteFile(src, make([]byte, 1<<20+100), 0o755); err != nil {
		t.Fatal(err)
	}

	want := filepath.Join(base, "fontchanger", CLIBinaryName)
	if got := promoteToFontchangerDir(base, src); got != want {
		t.Fatalf("promoteToFontchangerDir() = %s, want %s", got, want)
	}
	if _, err := os.Stat(want); err != nil {
		t.Fatalf("组件未迁入隔离目录: %v", err)
	}
	if _, err := os.Stat(src); !os.IsNotExist(err) {
		t.Fatalf("旧位置的组件应已移走，实际状态: %v", err)
	}
	if got := promoteToFontchangerDir(base, want); got != want {
		t.Fatalf("重复调用应保持幂等，实际: %s", got)
	}
}

func TestCompileRelativeOutputPathsResolvedBeforeTempDirExec(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	if !isValidCLI(exe) {
		t.Skipf("当前测试二进制小于 1MB，跳过子进程测试: %s", exe)
	}

	workDir := t.TempDir()
	origWd, err := os.Getwd()
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Chdir(workDir); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_ = os.Chdir(origWd)
	})

	t.Setenv("FC_TEST_HELPER", "1")
	engine := NewEngine(exe)

	if err := engine.CompileRaw(context.Background(), `{}`, "fontbuild.tmp", false, nil); err != nil {
		t.Fatalf("CompileRaw 传相对输出目录应成功: %v", err)
	}
	if _, err := os.Stat(filepath.Join(workDir, "fontbuild.tmp", "AXIS_12.fdt")); err != nil {
		t.Fatalf("未在工作目录找到 CompileRaw 产物: %v", err)
	}

	if err := engine.CompileTTMP(context.Background(), `{}`, filepath.Join("dist", "out.ttmp2"), nil); err != nil {
		t.Fatalf("CompileTTMP 传相对输出路径应成功: %v", err)
	}
	if _, err := os.Stat(filepath.Join(workDir, "dist", "out.ttmp2")); err != nil {
		t.Fatalf("未在工作目录找到 CompileTTMP 产物: %v", err)
	}
}
