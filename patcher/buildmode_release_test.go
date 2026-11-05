//go:build !development

package main

import "testing"

func TestReleaseBuildUsesCloudResource(t *testing.T) {
	if localResourceOnly {
		t.Fatal("发布构建不应启用本地资源模式")
	}
	app := NewApp()
	if app.updater.LocalOnly {
		t.Fatal("NewApp 不应在发布构建里关闭云端同步")
	}
}
