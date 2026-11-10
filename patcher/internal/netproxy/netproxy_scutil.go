package netproxy

import (
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
)

// scutilProxy 是 macOS `scutil --proxy` 输出的一份系统代理配置。
// 解析与选路放在无平台标记的文件里，便于在没有 Mac 的环境用单元测试验证。
type scutilProxy struct {
	enable   bool
	byScheme map[string]*url.URL
	bypass   []string
}

// parseScutilProxy 解析 scutil 的 <dictionary> 输出。
// ExceptionsList 里的 <array> 元素以数字下标出现，一并当作绕过规则收集。
func parseScutilProxy(text string) *scutilProxy {
	fields := make(map[string]string)
	var bypass []string
	for _, line := range strings.Split(text, "\n") {
		line = strings.TrimSpace(line)
		key, value, found := strings.Cut(line, " : ")
		if !found {
			continue
		}
		key = strings.TrimSpace(key)
		value = strings.TrimSpace(value)
		if _, err := strconv.ParseUint(key, 10, 32); err == nil {
			bypass = append(bypass, value)
			continue
		}
		fields[key] = value
	}

	cfg := &scutilProxy{byScheme: make(map[string]*url.URL), bypass: bypass}
	if fields["HTTPSEnable"] == "1" {
		if u := scutilProxyURL("http", fields["HTTPSProxy"], fields["HTTPSPort"]); u != nil {
			cfg.byScheme["https"] = u
		}
	}
	if fields["HTTPEnable"] == "1" {
		if u := scutilProxyURL("http", fields["HTTPProxy"], fields["HTTPPort"]); u != nil {
			cfg.byScheme["http"] = u
		}
	}
	if fields["SOCKSEnable"] == "1" {
		if u := scutilProxyURL("socks5", fields["SOCKSProxy"], fields["SOCKSPort"]); u != nil {
			cfg.byScheme["*"] = u
		}
	}
	if fields["ExcludeSimpleHostnames"] == "1" {
		cfg.bypass = append(cfg.bypass, "<local>")
	}
	cfg.enable = len(cfg.byScheme) > 0
	return cfg
}

func scutilProxyURL(scheme, host, port string) *url.URL {
	host = strings.Trim(strings.TrimSpace(host), "[]")
	port = strings.TrimSpace(port)
	if host == "" || port == "" {
		return nil
	}
	if _, err := strconv.ParseUint(port, 10, 16); err != nil {
		return nil
	}
	u, err := url.Parse(scheme + "://" + net.JoinHostPort(host, port))
	if err != nil || u.Hostname() == "" {
		return nil
	}
	return u
}

func (cfg *scutilProxy) forRequest(req *http.Request) (*url.URL, error) {
	if cfg == nil || !cfg.enable || req == nil || req.URL == nil {
		return nil, nil
	}
	if scutilBypass(cfg.bypass, req.URL.Hostname()) {
		return nil, nil
	}
	if u, ok := cfg.byScheme[strings.ToLower(req.URL.Scheme)]; ok {
		return u, nil
	}
	return cfg.byScheme["*"], nil
}

func scutilBypass(entries []string, host string) bool {
	host = strings.ToLower(strings.TrimSpace(host))
	if host == "" || host == "localhost" {
		return true
	}
	if ip := net.ParseIP(host); ip != nil && ip.IsLoopback() {
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
		case strings.Contains(entry, "/"):
			if ip := net.ParseIP(host); ip != nil {
				if _, ipnet, err := net.ParseCIDR(scutilCIDR(entry)); err == nil && ipnet.Contains(ip) {
					return true
				}
			}
		case strings.HasPrefix(entry, "*."):
			if host == entry[2:] || strings.HasSuffix(host, entry[1:]) {
				return true
			}
		case strings.HasSuffix(entry, "*"):
			if strings.HasPrefix(host, strings.TrimSuffix(entry, "*")) {
				return true
			}
		case entry == host:
			return true
		}
	}
	return false
}

// scutilCIDR 把系统的 169.254/16 这类简写补成 net.ParseCIDR 能接受的 IPv4 网段。
func scutilCIDR(entry string) string {
	addr, bits, found := strings.Cut(entry, "/")
	if !found || strings.Contains(addr, ":") {
		return entry
	}
	parts := strings.Split(addr, ".")
	for len(parts) < 4 {
		parts = append(parts, "0")
	}
	return strings.Join(parts, ".") + "/" + bits
}
