//go:build !development

package main

// localResourceOnly 在发布构建中恒为 false：内置词条必须向云端确认最新版本。
const localResourceOnly = false
