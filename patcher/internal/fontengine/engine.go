package fontengine

import (
	"bufio"
	"context"
	"encoding/json"
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
	filepath.Join("fontchanger", CLIBinaryName),
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
	gameDirs      map[string]string
}

func NewEngine(customBinPath string) *Engine {
	return &Engine{
		customBinPath: customBinPath,
	}
}

// isValidCLI 校验路径是否为有效的字体编译组件可执行文件（存在、非目录、体积大于 1MB）。
func isValidCLI(path string) bool {
	clean := strings.TrimSpace(path)
	if clean == "" {
		return false
	}
	st, err := os.Stat(clean)
	if err != nil {
		return false
	}
	if st.IsDir() {
		return false
	}
	return st.Size() > 1<<20
}

// SetBinPath 指定编译组件路径（运行时按需下载到资源目录后调用），并清掉解析缓存。
func (e *Engine) SetBinPath(path string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	clean := strings.TrimSpace(path)
	if clean != "" {
		if abs, err := filepath.Abs(clean); err == nil {
			clean = abs
		}
	}
	e.customBinPath = clean
	e.cachedBin = ""
}

func (e *Engine) SetGameDir(dir string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.gameDir = strings.TrimSpace(dir)
}

func (e *Engine) SetGameDirs(primaryDir string, dirsByRegion map[string]string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.gameDir = strings.TrimSpace(primaryDir)
	copied := make(map[string]string, len(dirsByRegion))
	for k, v := range dirsByRegion {
		if clean := strings.TrimSpace(v); clean != "" {
			copied[strings.ToUpper(strings.TrimSpace(k))] = clean
		}
	}
	e.gameDirs = copied
}

func (e *Engine) getGameDir() string {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.gameDir
}

func expandClientGameSubdirs(dirs ...string) []string {
	seen := make(map[string]bool, len(dirs)*2)
	out := make([]string, 0, len(dirs)*2)
	add := func(p string) {
		clean := filepath.Clean(strings.TrimSpace(p))
		if clean == "" || clean == "." {
			return
		}
		key := strings.ToLower(clean)
		if seen[key] {
			return
		}
		seen[key] = true
		out = append(out, clean)
	}
	for _, raw := range dirs {
		d := strings.TrimSpace(raw)
		if d == "" {
			continue
		}
		if strings.EqualFold(filepath.Base(d), "game") {
			add(d)
			continue
		}
		gameSub := filepath.Join(d, "game")
		if st, err := os.Stat(gameSub); err == nil && st.IsDir() {
			add(gameSub)
		}
		add(d)
	}
	return out
}

func (e *Engine) syncCliConfig(binPath string) bool {
	e.mu.Lock()
	primary := e.gameDir
	dirs := make(map[string]string, len(e.gameDirs))
	for k, v := range e.gameDirs {
		dirs[k] = v
	}
	e.mu.Unlock()

	if primary == "" && len(dirs) == 0 {
		return false
	}

	cfgPath := filepath.Join(filepath.Dir(binPath), "config.json")
	doc := make(map[string]any)
	if raw, err := os.ReadFile(cfgPath); err == nil && len(raw) > 0 {
		if err := json.Unmarshal(raw, &doc); err != nil {
			// 配置已损坏时放弃写入：用空文档覆盖会让用户丢失全部设置。
			// 返回 false 后调用方改用 --game-path 参数传目录。
			return false
		}
	}

	doc["global"] = expandClientGameSubdirs(
		dirs["GLOBAL"],
		primary,
	)
	doc["china"] = expandClientGameSubdirs(dirs["CHS"])
	doc["korea"] = expandClientGameSubdirs(dirs["KO"])
	doc["traditionalchinese"] = expandClientGameSubdirs(dirs["TC"])

	encoded, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return false
	}
	tmpPath := cfgPath + ".fc.tmp"
	if err := os.WriteFile(tmpPath, append(encoded, '\n'), 0o644); err != nil {
		return false
	}
	if err := os.Rename(tmpPath, cfgPath); err != nil {
		_ = os.Remove(tmpPath)
		if err := os.WriteFile(cfgPath, append(encoded, '\n'), 0o644); err != nil {
			return false
		}
	}
	return true
}

func (e *Engine) prepareGamePathArgs(binPath string) []string {
	if e.syncCliConfig(binPath) {
		return nil
	}
	if gd := e.getGameDir(); gd != "" {
		return []string{"--game-path", gd}
	}
	return nil
}

func (e *Engine) ResolveBinary() (string, error) {
	e.mu.Lock()
	defer e.mu.Unlock()

	if e.cachedBin != "" && isValidCLI(e.cachedBin) {
		if abs, err := filepath.Abs(e.cachedBin); err == nil {
			return abs, nil
		}
		return e.cachedBin, nil
	}

	if e.customBinPath != "" && isValidCLI(e.customBinPath) {
		if abs, err := filepath.Abs(e.customBinPath); err == nil {
			e.cachedBin = abs
			return abs, nil
		}
		e.cachedBin = e.customBinPath
		return e.customBinPath, nil
	}

	if path, ok := externalCLIExists(""); ok {
		e.cachedBin = path
		return path, nil
	}

	return "", ErrCliNotFound
}

// externalCLIExists 查找外置的字体编译组件。做文件存在性与有效性检查，不解压、不执行。
// 程序目录根部的旧组件会被迁入 fontchanger/ 子目录，避免它的 config.json 与主程序配置同目录互写。
// 必须返回绝对路径，防止触发 Go 1.19+ 相对路径执行安全拦截。
func externalCLIExists(customBinPath string) (string, bool) {
	if customBinPath != "" && isValidCLI(customBinPath) {
		if abs, err := filepath.Abs(customBinPath); err == nil {
			return abs, true
		}
	}
	var bases []string
	exeBase := ""
	if exePath, err := os.Executable(); err == nil {
		exeBase = filepath.Dir(exePath)
		bases = append(bases, exeBase)
	}
	if wd, err := os.Getwd(); err == nil {
		bases = append(bases, wd)
	}
	for _, base := range bases {
		for _, rel := range cliRelativeCandidates {
			candidate := filepath.Join(base, rel)
			if isValidCLI(candidate) {
				if rel == CLIBinaryName && exeBase != "" && strings.EqualFold(filepath.Clean(base), filepath.Clean(exeBase)) {
					candidate = promoteToFontchangerDir(base, candidate)
				}
				if abs, err := filepath.Abs(candidate); err == nil {
					return abs, true
				}
			}
		}
	}
	if path, err := exec.LookPath("fontchanger-cli"); err == nil {
		if isValidCLI(path) {
			if abs, err := filepath.Abs(path); err == nil {
				return abs, true
			}
		}
	}
	return "", false
}

// promoteToFontchangerDir 把放在程序目录根部的旧组件移进 fontchanger/ 子目录。
// CLI 会在自己所在目录读写 config.json，与主程序配置同目录时两边会互相覆盖。
// 迁移失败时退回原路径，保证组件仍然可用。
func promoteToFontchangerDir(baseDir, src string) string {
	dir := filepath.Join(baseDir, "fontchanger")
	target := filepath.Join(dir, CLIBinaryName)
	if isValidCLI(target) {
		return target
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return src
	}
	if err := os.Rename(src, target); err != nil {
		data, readErr := os.ReadFile(src)
		if readErr != nil {
			return src
		}
		if writeErr := os.WriteFile(target, data, 0o755); writeErr != nil {
			return src
		}
		_ = os.Remove(src)
	}
	return target
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
	args = append(args, e.prepareGamePathArgs(binPath)...)

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

// AutoFitPreset 用组件自带的 AutoFit 拟合指定字号里的系统字体图层，
// 返回该字号拟合后的元素列表 JSON（一个图层覆盖多个脚本时会被拆成多个，数量可能变化）。
func (e *Engine) AutoFitPreset(ctx context.Context, configJson string, fontName string, elementIndices []int) (string, error) {
	binPath, err := e.ResolveBinary()
	if err != nil {
		return "", err
	}

	tempDir, err := os.MkdirTemp("", "fc-autofit-*")
	if err != nil {
		return "", fmt.Errorf("创建拟合临时目录失败: %w", err)
	}
	defer os.RemoveAll(tempDir)

	configPath := filepath.Join(tempDir, "config.json")
	if err := os.WriteFile(configPath, []byte(configJson), 0o644); err != nil {
		return "", fmt.Errorf("写入临时配置失败: %w", err)
	}

	outPath := filepath.Join(tempDir, "autofit.json")
	args := []string{configPath, "--autofit", "--output", outPath}
	if fontName != "" {
		args = append(args, "--font", fontName)
	}
	for _, index := range elementIndices {
		args = append(args, "--element", strconv.Itoa(index))
	}
	args = append(args, e.prepareGamePathArgs(binPath)...)

	cmd := exec.CommandContext(ctx, binPath, args...)
	cmd.Dir = tempDir
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("字体拟合失败 (退出码: %v, 输出: %s)", err, strings.TrimSpace(string(output)))
	}

	data, err := os.ReadFile(outPath)
	if err != nil {
		return "", fmt.Errorf("读取拟合结果失败: %w", err)
	}
	return string(data), nil
}

func (e *Engine) CompileRaw(ctx context.Context, configJson string, outDir string, use16Bit bool, progressFn func(percent int, msg string)) error {
	binPath, err := e.ResolveBinary()
	if err != nil {
		return err
	}

	// 贴图边长由调用方决定：内置方案的边长是设计好的（主集 4096、大厅 2048），
	// 不能在这里一刀切，需要固定边长的只有用户自己带来的方案。
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
	args = append(args, e.prepareGamePathArgs(binPath)...)

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

	if use16Bit {
		if progressFn != nil {
			progressFn(95, "正在执行 16 位贴图压缩...")
		}
		if err := ConvertTexDirTo16Bit(outDir); err != nil {
			return fmt.Errorf("16 位贴图转换失败: %w", err)
		}
	}

	entries, err := os.ReadDir(outDir)
	if err != nil || len(entries) == 0 {
		return fmt.Errorf("导出原始字体失败：产物目录为空")
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
	args = append(args, e.prepareGamePathArgs(binPath)...)

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

	st, err := os.Stat(outPath)
	if err != nil || st.Size() == 0 {
		return fmt.Errorf("导出 TTMP 失败：产物文件不存在或为空")
	}

	return nil
}
