//go:build windows

package gamecfg

import (
	"os"
	"path/filepath"

	"golang.org/x/sys/windows/registry"
)

func findDocumentDirs() []string {
	var dirs []string
	seen := make(map[string]bool)

	add := func(d string) {
		if d == "" {
			return
		}
		clean := filepath.Clean(d)
		if !seen[clean] {
			seen[clean] = true
			dirs = append(dirs, clean)
		}
	}

	if k, err := registry.OpenKey(registry.CURRENT_USER, `Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders`, registry.QUERY_VALUE); err == nil {
		defer k.Close()
		if val, _, err := k.GetStringValue("Personal"); err == nil && val != "" {
			add(os.ExpandEnv(val))
		}
	}

	if home, err := os.UserHomeDir(); err == nil && home != "" {
		add(filepath.Join(home, "Documents"))
		add(filepath.Join(home, "OneDrive", "Documents"))
		add(filepath.Join(home, "OneDrive", "我的文档"))
		add(filepath.Join(home, "我的文档"))
	}

	return dirs
}
