package gameproc

import (
	"errors"
	"fmt"
	"path/filepath"
	goruntime "runtime"
	"strconv"
	"strings"
	"time"
)

// ErrCancelled 表示用户取消了启动器退出确认。
var ErrCancelled = errors.New("操作已取消：游戏启动器正在运行")

type ProcessType int

const (
	TypeUnknown ProcessType = iota
	TypeGame
	TypeOfficialLauncher
	TypeThirdPartyLauncher
)

type ProcessInfo struct {
	PID  uint32
	Name string
	Path string
	Type ProcessType
}

var gameProcesses = map[string]bool{
	"ffxiv_dx11.exe": true,
	"ffxiv.exe":      true,
	"ffxiv_dx11":     true,
	"ffxiv":          true,
}

var officialLaunchers = map[string]bool{
	"ffxivboot64.exe":     true,
	"ffxivboot.exe":       true,
	"ffxivlauncher64.exe": true,
	"ffxivlauncher.exe":   true,
	"ffxivupdater64.exe":  true,
	"ffxivupdater.exe":    true,
	"ffxivconfig64.exe":   true,
	"ffxivconfig.exe":     true,
	"ffxivsysinfo64.exe":  true,
	"ffxivsysinfo.exe":    true,
}

var thirdPartyLaunchers = map[string]bool{
	"xivlauncher.exe":        true,
	"xivlauncher.core.exe":   true,
	"xivlaunchercn.exe":      true,
	"xivlaunchercn.core.exe": true,
	"ffxivquicklauncher.exe": true,
	"xivlauncher":            true,
	"xivlauncher.core":       true,
	"xivlaunchercn":          true,
	"xiv on mac":             true,
}

func ClassifyProcess(name string) (ProcessType, bool) {
	clean := strings.ToLower(strings.TrimSpace(name))
	if gameProcesses[clean] {
		return TypeGame, true
	}
	if officialLaunchers[clean] {
		return TypeOfficialLauncher, true
	}
	if thirdPartyLaunchers[clean] {
		return TypeThirdPartyLauncher, true
	}
	return TypeUnknown, false
}

// classifyCommandLine 在完整命令行里找已知的游戏与启动器进程名。
// Wine/CrossOver 把 Windows exe 当参数传给加载器，ps 的 comm 只显示 wine，
// 而 Windows 路径分隔符在 Unix 下不参与 filepath.Base，需要先归一化。
func classifyCommandLine(command string) (string, ProcessType, bool) {
	var fallbackName string
	var fallbackType ProcessType
	for _, token := range strings.FieldsFunc(command, func(r rune) bool {
		return r == ' ' || r == '\t' || r == '\r' || r == '\n' || r == '"' || r == '\''
	}) {
		if strings.Contains(token, "=") {
			continue
		}
		name := filepath.Base(strings.ReplaceAll(token, "\\", "/"))
		pType, ok := ClassifyProcess(name)
		if !ok {
			continue
		}
		// 一条命令行里可能同时出现 boot 与游戏本体，游戏本体必须先判定，
		// 否则会被当成启动器进入「强制结束」流程。
		if pType == TypeGame {
			return name, pType, true
		}
		if fallbackName == "" {
			fallbackName, fallbackType = name, pType
		}
	}
	if fallbackName != "" {
		return fallbackName, fallbackType, true
	}
	return "", TypeUnknown, false
}

// parseProcessLine 从 "PID 其余内容" 形式的 ps 输出里取出 PID 与剩余文本。
// PID 列通常右对齐带前导空格，comm / command 又可能含空格，所以按首段切分而不是按列宽。
func parseProcessLine(line string) (uint32, string, bool) {
	line = strings.TrimSpace(line)
	if line == "" {
		return 0, "", false
	}
	fields := strings.Fields(line)
	if len(fields) < 2 {
		return 0, "", false
	}
	pid64, err := strconv.ParseUint(fields[0], 10, 32)
	if err != nil {
		return 0, "", false
	}
	detail := strings.TrimSpace(line[len(fields[0]):])
	if detail == "" {
		return 0, "", false
	}
	return uint32(pid64), detail, true
}

func isPathUnder(childPath, parentDir string) bool {
	if childPath == "" || parentDir == "" {
		return false
	}
	cleanChild := filepath.Clean(childPath)
	cleanParent := filepath.Clean(parentDir)

	if goruntime.GOOS == "windows" {
		cleanChild = strings.ToLower(cleanChild)
		cleanParent = strings.ToLower(cleanParent)
	}

	if cleanChild == cleanParent {
		return true
	}

	rel, err := filepath.Rel(cleanParent, cleanChild)
	if err != nil {
		return false
	}
	if rel == "." {
		return true
	}
	return !strings.HasPrefix(rel, "..") && !filepath.IsAbs(rel)
}

func isConflicting(proc ProcessInfo, gameDir string) bool {
	switch proc.Type {
	case TypeGame, TypeOfficialLauncher:
		if gameDir == "" {
			return true
		}
		if proc.Path == "" {
			return true
		}
		return isPathUnder(proc.Path, gameDir)
	case TypeThirdPartyLauncher:
		return true
	default:
		return false
	}
}

func filterConflicting(procs []ProcessInfo, gameDir string) []ProcessInfo {
	var conflicts []ProcessInfo
	for _, p := range procs {
		if isConflicting(p, gameDir) {
			conflicts = append(conflicts, p)
		}
	}
	return conflicts
}

func uniqueNames(procs []ProcessInfo) []string {
	var names []string
	seen := make(map[string]bool)
	for _, p := range procs {
		if !seen[p.Name] {
			seen[p.Name] = true
			names = append(names, p.Name)
		}
	}
	return names
}

func formatConflictError(conflicts []ProcessInfo) error {
	if len(conflicts) == 0 {
		return nil
	}
	var gameNames []string
	var launcherNames []string
	seen := make(map[string]bool)

	for _, p := range conflicts {
		if seen[p.Name] {
			continue
		}
		seen[p.Name] = true
		if p.Type == TypeGame {
			gameNames = append(gameNames, p.Name)
		} else {
			launcherNames = append(launcherNames, p.Name)
		}
	}

	if len(gameNames) > 0 && len(launcherNames) > 0 {
		allNames := append(gameNames, launcherNames...)
		return fmt.Errorf("检测到游戏及启动器正在运行（%s），为防止文件损坏，请先退出后再试", strings.Join(allNames, "、"))
	} else if len(gameNames) > 0 {
		return fmt.Errorf("检测到游戏客户端正在运行（%s），请先退出游戏后再试", strings.Join(gameNames, "、"))
	}
	return fmt.Errorf("检测到游戏启动器正在运行（%s），为防止文件冲突或更新损坏，请先关闭启动器后再试", strings.Join(launcherNames, "、"))
}

var (
	procLister = findRunningProcesses
	procKiller = killProcess
)

// KillProcesses 强制结束给定的进程列表，并在退出后做缓冲等待。
func KillProcesses(procs []ProcessInfo) ([]string, error) {
	var killed []string
	var lastErr error
	for _, p := range procs {
		if err := procKiller(p.PID); err == nil {
			killed = append(killed, p.Name)
		} else {
			lastErr = fmt.Errorf("%s (PID %d): %w", p.Name, p.PID, err)
		}
	}
	if len(killed) == 0 && lastErr != nil {
		return nil, fmt.Errorf("结束进程失败: %w", lastErr)
	}
	time.Sleep(200 * time.Millisecond)
	return killed, nil
}

// KillLauncherProcesses 扫描并强制结束当前运行中的冲突启动器进程。
func KillLauncherProcesses(gameDir string) ([]string, error) {
	_, launcherConflicts, err := InspectConflicts(gameDir)
	if err != nil {
		return nil, err
	}
	if len(launcherConflicts) == 0 {
		return nil, nil
	}
	return KillProcesses(launcherConflicts)
}

// InspectConflicts 检查与指定游戏目录相关的进程冲突，分别返回游戏和启动器冲突列表。
func InspectConflicts(gameDir string) ([]ProcessInfo, []ProcessInfo, error) {
	procs, err := procLister()
	if err != nil {
		return nil, nil, fmt.Errorf("枚举系统进程失败: %w", err)
	}
	conflicts := filterConflicting(procs, gameDir)
	var gameConflicts []ProcessInfo
	var launcherConflicts []ProcessInfo
	for _, p := range conflicts {
		if p.Type == TypeGame {
			gameConflicts = append(gameConflicts, p)
		} else if p.Type == TypeOfficialLauncher || p.Type == TypeThirdPartyLauncher {
			launcherConflicts = append(launcherConflicts, p)
		}
	}
	return gameConflicts, launcherConflicts, nil
}

// LauncherConfirmFunc 用于向用户请求是否强制结束启动器的确认回调。
type LauncherConfirmFunc func(launcherNames []string) (bool, error)

// CheckOptions 控制冲突校验与自动恢复选项。
type CheckOptions struct {
	GameDir           string
	OnLauncherConfirm LauncherConfirmFunc
	OnLaunchersKilled func(killed []string)
}

// CheckGameNotRunningWithOpts 校验游戏与启动器进程，并支持在启动器运行时交互确认并强制终止。
func CheckGameNotRunningWithOpts(opts CheckOptions) error {
	gameConflicts, launcherConflicts, err := InspectConflicts(opts.GameDir)
	if err != nil {
		return err
	}

	// 1. 如果游戏本体正在运行，必须先由用户退出游戏本体
	if len(gameConflicts) > 0 {
		return formatConflictError(gameConflicts)
	}

	// 2. 如果仅启动器正在运行
	if len(launcherConflicts) > 0 {
		if opts.OnLauncherConfirm != nil {
			names := uniqueNames(launcherConflicts)
			confirmed, err := opts.OnLauncherConfirm(names)
			if err != nil {
				return err
			}
			if !confirmed {
				return ErrCancelled
			}

			killed, err := KillProcesses(launcherConflicts)
			if err != nil {
				return fmt.Errorf("强制结束启动器失败: %w", err)
			}
			if opts.OnLaunchersKilled != nil {
				opts.OnLaunchersKilled(killed)
			}
			return nil
		}
		return formatConflictError(launcherConflicts)
	}

	return nil
}
