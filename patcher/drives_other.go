//go:build !windows

package main

import "os"

// getScanDriveRoots 在非 Windows 平台只提供 Wine 前缀作为扫描根。
// 游戏的其他常见位置（XIV on Mac / Bottles 等）已由 DetectGamePaths 显式枚举，
// 不再对 "/" 做全盘 3 层遍历，避免拖慢探测并触发系统目录权限提示。
func getScanDriveRoots() []string {
	if winePrefix := os.Getenv("WINEPREFIX"); winePrefix != "" {
		return []string{winePrefix}
	}
	return nil
}
