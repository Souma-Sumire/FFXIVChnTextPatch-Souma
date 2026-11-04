package gameproc

import (
	"errors"
	"fmt"
	"path/filepath"
	goruntime "runtime"
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
		return nil, nil, nil
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
