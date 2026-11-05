package main

import (
	"context"
	"os"
	"path/filepath"
	goruntime "runtime"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/fontengine"
)

// writeFontSourceConfig 在数据根目录写配置，模拟界面选择的字体来源。
func writeFontSourceConfig(t *testing.T, root, fontSource string) {
	t.Helper()
	cfg := `{"fontSource":"` + fontSource + `"}`
	if err := os.WriteFile(filepath.Join(root, "config.json"), []byte(cfg), 0o644); err != nil {
		t.Fatal(err)
	}
}

// 内置字体包来源不需要编译组件：预检必须直接放行。
func TestCheckFontSourcePackageNeedsNoCompiler(t *testing.T) {
	resDir := filepath.Join(t.TempDir(), "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeFontSourceConfig(t, filepath.Dir(resDir), "package")

	app := NewApp()
	app.resourceDirOverride = resDir
	if err := app.checkFontSource(context.Background(), resDir, t.TempDir()); err != nil {
		t.Fatalf("内置字体包来源不应依赖编译组件: %v", err)
	}
}

// 自定义方案来源：装好编译组件只是前提，方案本身没编译过时仍要拦住，
// 否则用户会一路跑到字体那一步才发现。
func TestCheckFontSourceCompileRequiresBuild(t *testing.T) {
	if goruntime.GOOS != "windows" {
		t.Skip("现场编译只在 Windows 上可用")
	}
	root := t.TempDir()
	resDir := filepath.Join(root, "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeFontSourceConfig(t, root, "compile")
	if err := os.WriteFile(filepath.Join(root, fontengine.CLIBinaryName), make([]byte, 1<<20+1), 0o755); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.resourceDirOverride = resDir
	err := app.checkFontSource(context.Background(), resDir, t.TempDir())
	if err == nil {
		t.Fatal("方案尚未编译时不应放行")
	}
	if !strings.Contains(err.Error(), "尚未编译") {
		t.Fatalf("组件可用时应卡在未编译，而不是组件问题: %v", err)
	}
}

// 自定义方案来源拿不到编译组件时必须报错中止，不再回落到内置字体包。
func TestCheckFontSourceCompileWithoutCompilerFails(t *testing.T) {
	root := t.TempDir()
	resDir := filepath.Join(root, "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeFontSourceConfig(t, root, "compile")
	// 清空 PATH，避免本机恰好装了同名程序影响断言。
	t.Setenv("PATH", "")

	app := NewApp()
	app.resourceDirOverride = resDir
	// 预取消的 ctx 让下载循环立刻退出，测试不触网。
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := app.checkFontSource(ctx, resDir, t.TempDir()); err == nil {
		t.Fatal("选自定义方案却拿不到编译组件时必须报错，不能静默改用内置字体包")
	}
}
