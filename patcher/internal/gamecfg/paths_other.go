//go:build !windows

package gamecfg

import (
	"os"
	"path/filepath"
)

func findDocumentDirs() []string {
	var dirs []string
	home, err := os.UserHomeDir()
	if err != nil || home == "" {
		return dirs
	}

	dirs = append(dirs,
		filepath.Join(home, "Documents"),
		filepath.Join(home, "Library", "Application Support", "FINAL FANTASY XIV ONLINE", "Bottles", "published_Final_Fantasy", "drive_c", "users", "steamuser", "Documents"),
		filepath.Join(home, "Library", "Application Support", "XIV on Mac", "wineprefix", "drive_c", "users", "crossover", "Documents"),
	)
	return dirs
}
