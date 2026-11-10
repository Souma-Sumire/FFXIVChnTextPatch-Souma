//go:build !windows && !darwin

package netproxy

import (
	"net/http"
	"net/url"
)

// systemProxyFunc 在 Windows 与 macOS 之外的平台没有可读的系统代理设置，返回 nil 表示交给环境变量处理。
func systemProxyFunc() func(*http.Request) (*url.URL, error) {
	return nil
}
