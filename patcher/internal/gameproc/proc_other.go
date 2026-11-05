//go:build !windows

package gameproc

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

// findRunningProcesses 在非 Windows 平台枚举进程。
// comm 能识别 "XIV on Mac" 这类原生启动器；command 用来识别 Wine/CrossOver
// 把 ffxiv_dx11.exe 作为参数启动的进程，后者在 ps 里只显示 wine 加载器。
// 非 Windows 平台拿不到可靠的客户端绝对路径（comm 可能是加载器路径或
// Windows 风格路径），所以 Path 留空，让 isConflicting 一律判冲突，避免漏检。
func findRunningProcesses() ([]ProcessInfo, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	procs, err := listByComm(ctx)
	if err != nil {
		return nil, err
	}
	seen := make(map[uint32]bool, len(procs))
	for _, p := range procs {
		seen[p.PID] = true
	}

	if wineProcs, err := listByCommand(ctx); err == nil {
		for _, p := range wineProcs {
			if !seen[p.PID] {
				procs = append(procs, p)
				seen[p.PID] = true
			}
		}
	}
	return procs, nil
}

// runPS 用绝对路径优先：GUI 启动的 .app 继承的 PATH 可能不含 /bin。
func runPS(ctx context.Context, columns string) ([]byte, error) {
	args := []string{"-A", "-o", columns}
	if out, err := exec.CommandContext(ctx, "/bin/ps", args...).Output(); err == nil {
		return out, nil
	}
	return exec.CommandContext(ctx, "ps", args...).Output()
}

func listByComm(ctx context.Context) ([]ProcessInfo, error) {
	out, err := runPS(ctx, "pid=,comm=")
	if err != nil {
		return nil, err
	}

	var results []ProcessInfo
	for _, line := range strings.Split(string(out), "\n") {
		pid, commPath, ok := parseProcessLine(line)
		if !ok {
			continue
		}
		baseName := filepath.Base(commPath)
		if pType, ok := ClassifyProcess(baseName); ok {
			results = append(results, ProcessInfo{
				PID:  pid,
				Name: baseName,
				Type: pType,
			})
		}
	}
	return results, nil
}

func listByCommand(ctx context.Context) ([]ProcessInfo, error) {
	out, err := runPS(ctx, "pid=,command=")
	if err != nil {
		return nil, err
	}

	var results []ProcessInfo
	for _, line := range strings.Split(string(out), "\n") {
		pid, command, ok := parseProcessLine(line)
		if !ok {
			continue
		}
		name, pType, ok := classifyCommandLine(command)
		if !ok {
			continue
		}
		results = append(results, ProcessInfo{
			PID:  pid,
			Name: name,
			Type: pType,
		})
	}
	return results, nil
}

func killProcess(pid uint32) error {
	p, err := os.FindProcess(int(pid))
	if err != nil {
		return err
	}
	if err := p.Kill(); err != nil {
		if strings.Contains(err.Error(), "already finished") || strings.Contains(err.Error(), "process already finished") {
			return nil
		}
		return err
	}
	_, _ = p.Wait()
	return nil
}
