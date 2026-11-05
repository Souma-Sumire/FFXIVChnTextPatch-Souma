//go:build windows

package netproxy

import (
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"golang.org/x/sys/windows/registry"
)

const (
	internetSettingsKey = `Software\Microsoft\Windows\CurrentVersion\Internet Settings`
	// 代理设置可能在运行期间被改动（用户开关代理软件、切换端口），
	// 缓存一小段时间后重读注册表，避免每个请求都去读一次。
	configTTL = 30 * time.Second
)

type proxyConfig struct {
	enable   bool
	byScheme map[string]*url.URL
	bypass   []string
	loadedAt time.Time
}

var (
	configMu     sync.Mutex
	cachedConfig *proxyConfig
)

// systemProxyFunc 读取 Windows 的静态代理设置（ProxyEnable / ProxyServer）。
// 仅配置了 PAC（AutoConfigURL）而没有静态代理时无法解析，直接走直连：
// 执行 PAC 需要 WinHTTP 的 WinHttpGetProxyForUrl，标准库不具备该能力。
func systemProxyFunc() func(*http.Request) (*url.URL, error) {
	return func(req *http.Request) (*url.URL, error) {
		return proxyFor(currentConfig(), req)
	}
}

// proxyFor 按配置为单个请求选择代理，命中 ProxyOverride 或无需代理时返回 nil。
func proxyFor(cfg *proxyConfig, req *http.Request) (*url.URL, error) {
	if cfg == nil || !cfg.enable || req == nil || req.URL == nil {
		return nil, nil
	}
	if bypassHost(cfg.bypass, req.URL.Hostname()) {
		return nil, nil
	}
	if proxy, ok := cfg.byScheme[strings.ToLower(req.URL.Scheme)]; ok {
		return proxy, nil
	}
	if proxy, ok := cfg.byScheme["*"]; ok {
		return proxy, nil
	}
	return nil, nil
}

func currentConfig() *proxyConfig {
	configMu.Lock()
	defer configMu.Unlock()
	if cachedConfig != nil && time.Since(cachedConfig.loadedAt) < configTTL {
		return cachedConfig
	}
	cachedConfig = readConfig()
	return cachedConfig
}

func readConfig() *proxyConfig {
	key, err := registry.OpenKey(registry.CURRENT_USER, internetSettingsKey, registry.QUERY_VALUE)
	if err != nil {
		return nil
	}
	defer key.Close()

	enable, _, err := key.GetIntegerValue("ProxyEnable")
	if err != nil {
		return nil
	}
	server, _, _ := key.GetStringValue("ProxyServer")
	override, _, _ := key.GetStringValue("ProxyOverride")

	byScheme := parseProxyServer(server)
	return &proxyConfig{
		enable:   enable == 1 && len(byScheme) > 0,
		byScheme: byScheme,
		bypass:   strings.Split(override, ";"),
		loadedAt: time.Now(),
	}
}

// parseProxyServer 解析 ProxyServer 的两种写法：
//   - "127.0.0.1:7897"：所有协议共用一个代理
//   - "http=127.0.0.1:7897;https=127.0.0.1:7897;socks=127.0.0.1:1080"：按协议区分
func parseProxyServer(raw string) map[string]*url.URL {
	out := make(map[string]*url.URL)
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return out
	}

	if strings.Contains(raw, "=") {
		for _, part := range strings.Split(raw, ";") {
			pair := strings.SplitN(part, "=", 2)
			if len(pair) != 2 {
				continue
			}
			scheme := strings.ToLower(strings.TrimSpace(pair[0]))
			proxy, ok := toProxyURL(scheme, pair[1])
			if !ok {
				continue
			}
			if scheme == "socks" {
				// Windows 用 socks= 表示全部协议走 SOCKS
				out["*"] = proxy
				continue
			}
			out[scheme] = proxy
		}
		return out
	}

	if proxy, ok := toProxyURL("", raw); ok {
		out["*"] = proxy
	}
	return out
}

func toProxyURL(scheme, value string) (*url.URL, bool) {
	value = strings.TrimSpace(value)
	if value == "" {
		return nil, false
	}
	if !strings.Contains(value, "://") {
		if scheme == "socks" {
			value = "socks5://" + value
		} else {
			value = "http://" + value
		}
	}
	u, err := url.Parse(value)
	if err != nil || u.Hostname() == "" {
		return nil, false
	}
	return u, true
}

// bypassHost 判断目标主机是否应跳过代理（ProxyOverride）。
// 支持 "*"、"<local>"（无点号的局域网主机名）与 "*.example.com" 形式的通配。
func bypassHost(entries []string, host string) bool {
	host = strings.ToLower(strings.TrimSpace(host))
	if host == "" || isLoopback(host) {
		return true
	}
	for _, entry := range entries {
		entry = strings.ToLower(strings.TrimSpace(entry))
		switch {
		case entry == "":
			continue
		case entry == "*":
			return true
		case entry == "<local>":
			if !strings.Contains(host, ".") && !strings.Contains(host, ":") {
				return true
			}
		default:
			if matchHostPattern(entry, host) {
				return true
			}
		}
	}
	return false
}

func matchHostPattern(pattern, host string) bool {
	return wildcardMatch(pattern, host)
}

// wildcardMatch 按 Windows ProxyOverride 的习惯把 '*' 当作任意长度的通配符，
// 因此 "*.example.com"（后缀）、"192.168.*"（前缀）、"10.*" 都能命中。
func wildcardMatch(pattern, host string) bool {
	// "*.example.com" 同时匹配子域与裸域，这符合用户书写绕过列表的预期。
	if strings.HasPrefix(pattern, "*.") && host == pattern[2:] {
		return true
	}
	star := strings.IndexByte(pattern, '*')
	if star < 0 {
		return pattern == host
	}
	prefix := pattern[:star]
	if !strings.HasPrefix(host, prefix) {
		return false
	}
	pattern = pattern[star+1:]
	host = host[len(prefix):]
	for {
		if wildcardMatch(pattern, host) {
			return true
		}
		if host == "" {
			return false
		}
		host = host[1:]
	}
}

func isLoopback(host string) bool {
	if host == "localhost" {
		return true
	}
	if ip := net.ParseIP(host); ip != nil {
		return ip.IsLoopback()
	}
	return false
}
