package gameproc

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	goruntime "runtime"
	"strings"
)

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

var procLister = findRunningProcesses

func checkFileLocks(gameDir string) error {
	if gameDir == "" {
		return nil
	}
	sqpackDir := filepath.Join(gameDir, "game", "sqpack", "ffxiv")
	testFiles := []string{
		filepath.Join(sqpackDir, "0a0000.win32.index"),
		filepath.Join(sqpackDir, "000000.win32.index"),
	}
	for _, p := range testFiles {
		if _, err := os.Stat(p); err == nil {
			f, err := os.OpenFile(p, os.O_RDWR, 0)
			if err != nil {
				if os.IsPermission(err) || strings.Contains(strings.ToLower(err.Error()), "used by another process") {
					return errors.New("检测到游戏客户端数据文件正在被占用，请先退出游戏和启动器后再试")
				}
			} else {
				_ = f.Close()
			}
		}
	}
	return nil
}

func CheckGameNotRunning(gameDir string) error {
	if procs, err := procLister(); err == nil && len(procs) > 0 {
		conflicts := filterConflicting(procs, gameDir)
		if len(conflicts) > 0 {
			return formatConflictError(conflicts)
		}
	}
	return checkFileLocks(gameDir)
}
