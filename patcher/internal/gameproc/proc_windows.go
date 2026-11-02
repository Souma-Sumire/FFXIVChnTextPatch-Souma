//go:build windows

package gameproc

import (
	"errors"
	"path/filepath"
	"unsafe"

	"golang.org/x/sys/windows"
)

func findRunningProcesses() ([]ProcessInfo, error) {
	snap, err := windows.CreateToolhelp32Snapshot(windows.TH32CS_SNAPPROCESS, 0)
	if err != nil {
		return nil, err
	}
	defer windows.CloseHandle(snap)

	var entry windows.ProcessEntry32
	entry.Size = uint32(unsafe.Sizeof(entry))

	if err := windows.Process32First(snap, &entry); err != nil {
		return nil, err
	}

	var results []ProcessInfo
	for {
		name := windows.UTF16ToString(entry.ExeFile[:])
		if pType, ok := ClassifyProcess(name); ok {
			var fullPath string
			hProc, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, entry.ProcessID)
			if err == nil {
				buf := make([]uint16, 1024)
				size := uint32(len(buf))
				if err := windows.QueryFullProcessImageName(hProc, 0, &buf[0], &size); err == nil && size > 0 {
					fullPath = filepath.Clean(windows.UTF16ToString(buf[:size]))
				}
				windows.CloseHandle(hProc)
			}
			results = append(results, ProcessInfo{
				PID:  entry.ProcessID,
				Name: name,
				Path: fullPath,
				Type: pType,
			})
		}

		if err := windows.Process32Next(snap, &entry); err != nil {
			break
		}
	}

	return results, nil
}

func killProcess(pid uint32) error {
	hProc, err := windows.OpenProcess(windows.PROCESS_TERMINATE|windows.SYNCHRONIZE, false, pid)
	if err != nil {
		if errors.Is(err, windows.ERROR_INVALID_PARAMETER) {
			return nil
		}
		return err
	}
	defer windows.CloseHandle(hProc)

	if err := windows.TerminateProcess(hProc, 1); err != nil {
		if errors.Is(err, windows.ERROR_ACCESS_DENIED) {
			// 进程可能已经结束
			var exitCode uint32
			if getErr := windows.GetExitCodeProcess(hProc, &exitCode); getErr == nil && exitCode != 259 {
				return nil
			}
		}
		return err
	}
	_, _ = windows.WaitForSingleObject(hProc, 3000)
	return nil
}
