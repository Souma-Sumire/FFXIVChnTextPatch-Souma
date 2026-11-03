//go:build windows

package main

import (
	"os"
	"path/filepath"

	"golang.org/x/sys/windows"
)

func getScanDriveRoots() []string {
	mask, err := windows.GetLogicalDrives()
	if err == nil && mask != 0 {
		var roots []string
		for i := 0; i < 26; i++ {
			if (mask & (1 << i)) == 0 {
				continue
			}
			drive := string(rune('A'+i)) + ":\\"
			pDrive, err := windows.UTF16PtrFromString(drive)
			if err != nil {
				continue
			}
			dt := windows.GetDriveType(pDrive)
			// 只扫描固定硬盘、移动硬盘与内存盘；网络盘（DRIVE_REMOTE）断连时
			// os.Stat 与目录读取会阻塞到 SMB 超时，而游戏装在网络共享上不具备
			// 可用的汉化场景，因此不纳入自动探测。光驱与无介质卷同样过滤。
			if dt == windows.DRIVE_FIXED || dt == windows.DRIVE_REMOVABLE || dt == windows.DRIVE_RAMDISK {
				roots = append(roots, drive)
			}
		}
		if len(roots) > 0 {
			return roots
		}
	}

	// 极端异常兜底：动态获取当前系统安装盘符（如 "C:\"），不使用任何假定盘符
	if sysDrive := os.Getenv("SystemDrive"); sysDrive != "" {
		return []string{filepath.Clean(sysDrive) + string(os.PathSeparator)}
	}
	if vol := filepath.VolumeName(os.TempDir()); vol != "" {
		return []string{vol + string(os.PathSeparator)}
	}
	return nil
}
