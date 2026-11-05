package fontengine

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
)

// CLIBinaryName 是字体编译组件在程序内的名字，错误提示与查找路径都用它。
const CLIBinaryName = "fontchanger-cli.exe"

var (
	ErrCliNotFound = errors.New("没有找到字体编译组件 " + CLIBinaryName)
)

// cliRelativeCandidates 是外置编译组件相对「程序目录 / 当前工作目录」的常见位置。
var cliRelativeCandidates = []string{
	CLIBinaryName,
	"FontChanger.FontEditor64.exe",
	filepath.Join("tools", "fontchanger", "bin", CLIBinaryName),
	filepath.Join("tools", "fontchanger", "build", "Release", "FontChanger.FontEditor64.exe"),
	filepath.Join("tools", "fontchanger", "FontChanger.FontEditor64.exe"),
}

type Engine struct {
	customBinPath string
	mu            sync.Mutex
	cachedBin     string
	gameDir       string
}

func NewEngine(customBinPath string) *Engine {
	return &Engine{
		customBinPath: customBinPath,
	}
}

// SetBinPath 指定编译组件路径（运行时按需下载到资源目录后调用），并清掉解析缓存。
func (e *Engine) SetBinPath(path string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.customBinPath = strings.TrimSpace(path)
	e.cachedBin = ""
}

func (e *Engine) SetGameDir(dir string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.gameDir = strings.TrimSpace(dir)
}

func (e *Engine) getGameDir() string {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.gameDir
}

func (e *Engine) ResolveBinary() (string, error) {
	e.mu.Lock()
	defer e.mu.Unlock()

	if e.cachedBin != "" {
		if _, err := os.Stat(e.cachedBin); err == nil {
			return e.cachedBin, nil
		}
	}

	if e.customBinPath != "" {
		if _, err := os.Stat(e.customBinPath); err == nil {
			e.cachedBin = e.customBinPath
			return e.cachedBin, nil
		}
	}

	if path, ok := externalCLIExists(""); ok {
		e.cachedBin = path
		return path, nil
	}

	return "", ErrCliNotFound
}

// externalCLIExists 查找外置的字体编译组件。只做文件存在性检查，不解压、不执行。
func externalCLIExists(customBinPath string) (string, bool) {
	if customBinPath != "" {
		if _, err := os.Stat(customBinPath); err == nil {
			return customBinPath, true
		}
	}
	var bases []string
	if exePath, err := os.Executable(); err == nil {
		bases = append(bases, filepath.Dir(exePath))
	}
	if wd, err := os.Getwd(); err == nil {
		bases = append(bases, wd)
	}
	for _, base := range bases {
		for _, rel := range cliRelativeCandidates {
			candidate := filepath.Join(base, rel)
			if _, err := os.Stat(candidate); err == nil {
				return candidate, true
			}
		}
	}
	if path, err := exec.LookPath("fontchanger-cli"); err == nil {
		return path, true
	}
	return "", false
}

func (e *Engine) RenderPreview(ctx context.Context, configJson string, fontName string, text string, maxWidth int) ([]byte, error) {
	binPath, err := e.ResolveBinary()
	if err != nil {
		return nil, err
	}

	tempDir, err := os.MkdirTemp("", "fc-preview-*")
	if err != nil {
		return nil, fmt.Errorf("创建预览临时目录失败: %w", err)
	}
	defer os.RemoveAll(tempDir)

	configPath := filepath.Join(tempDir, "config.json")
	if err := os.WriteFile(configPath, []byte(configJson), 0o644); err != nil {
		return nil, fmt.Errorf("写入临时配置失败: %w", err)
	}

	outPngPath := filepath.Join(tempDir, "preview.png")

	args := []string{
		configPath,
		"--render-text", text,
		"--output", outPngPath,
	}
	if fontName != "" {
		args = append(args, "--font", fontName)
	}
	if maxWidth > 0 {
		args = append(args, "--max-width", strconv.Itoa(maxWidth))
	}
	if gd := e.getGameDir(); gd != "" {
		args = append(args, "--game-path", gd)
	}

	cmd := exec.CommandContext(ctx, binPath, args...)
	cmd.Dir = tempDir
	output, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("渲染预览失败 (退出码: %v, 输出: %s)", err, strings.TrimSpace(string(output)))
	}

	data, err := os.ReadFile(outPngPath)
	if err != nil {
		return nil, fmt.Errorf("读取渲染生成的预览图失败: %w", err)
	}

	return data, nil
}

func (e *Engine) CompileRaw(ctx context.Context, configJson string, outDir string, progressFn func(percent int, msg string)) error {
	binPath, err := e.ResolveBinary()
	if err != nil {
		return err
	}

	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return fmt.Errorf("创建字体输出目录失败: %w", err)
	}

	tempDir, err := os.MkdirTemp("", "fc-compile-*")
	if err != nil {
		return fmt.Errorf("创建编译临时目录失败: %w", err)
	}
	defer os.RemoveAll(tempDir)

	configPath := filepath.Join(tempDir, "config.json")
	if err := os.WriteFile(configPath, []byte(configJson), 0o644); err != nil {
		return fmt.Errorf("写入临时配置失败: %w", err)
	}

	args := []string{
		configPath,
		"--export-raw", outDir,
	}
	if gd := e.getGameDir(); gd != "" {
		args = append(args, "--game-path", gd)
	}

	cmd := exec.CommandContext(ctx, binPath, args...)
	cmd.Dir = tempDir

	stderr, err := cmd.StderrPipe()
	if err != nil {
		return fmt.Errorf("获取标准错误流失败: %w", err)
	}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("启动字体编译工具失败: %w", err)
	}

	var errLines []string
	scanner := bufio.NewScanner(stderr)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(line, "PROGRESS:") {
			parts := strings.SplitN(strings.TrimPrefix(line, "PROGRESS:"), ":", 2)
			if len(parts) == 2 {
				pct, _ := strconv.Atoi(strings.TrimSpace(parts[0]))
				msg := strings.TrimSpace(parts[1])
				if progressFn != nil {
					progressFn(pct, msg)
				}
			}
		} else if line != "" {
			errLines = append(errLines, line)
		}
	}

	if err := cmd.Wait(); err != nil {
		if len(errLines) > 0 {
			return fmt.Errorf("字体编译异常退出 (%w): %s", err, strings.Join(errLines, "; "))
		}
		return fmt.Errorf("字体编译异常退出: %w", err)
	}

	return nil
}

func (e *Engine) CompileTTMP(ctx context.Context, configJson string, outPath string, progressFn func(percent int, msg string)) error {
	binPath, err := e.ResolveBinary()
	if err != nil {
		return err
	}

	if err := os.MkdirAll(filepath.Dir(outPath), 0o755); err != nil {
		return fmt.Errorf("创建目标目录失败: %w", err)
	}

	tempDir, err := os.MkdirTemp("", "fc-ttmp-*")
	if err != nil {
		return fmt.Errorf("创建临时目录失败: %w", err)
	}
	defer os.RemoveAll(tempDir)

	configPath := filepath.Join(tempDir, "config.json")
	if err := os.WriteFile(configPath, []byte(configJson), 0o644); err != nil {
		return fmt.Errorf("写入临时配置失败: %w", err)
	}

	args := []string{
		configPath,
		"--export-ttmp", outPath,
	}
	if gd := e.getGameDir(); gd != "" {
		args = append(args, "--game-path", gd)
	}

	cmd := exec.CommandContext(ctx, binPath, args...)
	cmd.Dir = tempDir

	stderr, err := cmd.StderrPipe()
	if err != nil {
		return fmt.Errorf("获取标准错误流失败: %w", err)
	}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("启动字体编译工具失败: %w", err)
	}

	var errLines []string
	scanner := bufio.NewScanner(stderr)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(line, "PROGRESS:") {
			parts := strings.SplitN(strings.TrimPrefix(line, "PROGRESS:"), ":", 2)
			if len(parts) == 2 {
				pct, _ := strconv.Atoi(strings.TrimSpace(parts[0]))
				msg := strings.TrimSpace(parts[1])
				if progressFn != nil {
					progressFn(pct, msg)
				}
			}
		} else if line != "" {
			errLines = append(errLines, line)
		}
	}

	if err := cmd.Wait(); err != nil {
		if len(errLines) > 0 {
			return fmt.Errorf("导出 TTMP 异常退出 (%w): %s", err, strings.Join(errLines, "; "))
		}
		return fmt.Errorf("导出 TTMP 异常退出: %w", err)
	}

	return nil
}
