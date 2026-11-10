//go:build windows

package main

import "golang.org/x/sys/windows"

func processPrivilege() string {
	if windows.GetCurrentProcessToken().IsElevated() {
		return "管理员"
	}
	return "普通用户"
}
