//go:build !windows

package main

import "os"

func processPrivilege() string {
	if os.Geteuid() == 0 {
		return "root"
	}
	return "普通用户"
}
