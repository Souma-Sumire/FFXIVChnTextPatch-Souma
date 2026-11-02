//go:build !windows

package gameproc

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func findRunningProcesses() ([]ProcessInfo, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, "ps", "-A", "-o", "pid=,comm=")
	out, err := cmd.Output()
	if err != nil {
		return nil, nil
	}

	lines := strings.Split(string(out), "\n")
	var results []ProcessInfo
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		pid64, err := strconv.ParseUint(fields[0], 10, 32)
		if err != nil {
			continue
		}
		commPath := strings.TrimSpace(line[len(fields[0]):])
		baseName := filepath.Base(commPath)

		if pType, ok := ClassifyProcess(baseName); ok {
			results = append(results, ProcessInfo{
				PID:  uint32(pid64),
				Name: baseName,
				Path: commPath,
				Type: pType,
			})
		}
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
