// Package netproxy 提供 HTTP 客户端使用的代理函数。
// 与标准库的 http.ProxyFromEnvironment 不同，它在环境变量缺省时会读取
// 操作系统（Windows「Internet 选项」）的代理配置，覆盖「只开了系统代理、
// 没设 HTTP_PROXY」这一国内最常见的代理软件工作方式。
package netproxy

import (
	"net/http"
	"net/url"
	"os"
	"strings"
)

// 环境变量优先级高于系统设置：显式设置的 HTTP(S)_PROXY 是调用方（脚本、CI、
// 容器）明确指定的出口，不应被桌面上的代理软件设置覆盖。
var envKeys = []string{
	"HTTPS_PROXY", "https_proxy",
	"HTTP_PROXY", "http_proxy",
	"ALL_PROXY", "all_proxy",
}

// Func 返回可直接赋给 http.Transport.Proxy 的函数。
func Func() func(*http.Request) (*url.URL, error) {
	if envProxyConfigured() {
		return http.ProxyFromEnvironment
	}
	if fn := systemProxyFunc(); fn != nil {
		return fn
	}
	return http.ProxyFromEnvironment
}

func envProxyConfigured() bool {
	for _, key := range envKeys {
		if strings.TrimSpace(os.Getenv(key)) != "" {
			return true
		}
	}
	return false
}
