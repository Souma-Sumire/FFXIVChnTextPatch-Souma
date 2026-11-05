//go:build darwin

package netproxy

import (
	"net/http"
	"net/url"
	"os/exec"
	"sync"
	"time"
)

// 代理软件可能在程序运行期间改端口，缓存一小段时间后重读。
const scutilConfigTTL = 30 * time.Second

var (
	scutilMu     sync.Mutex
	scutilCached *scutilProxy
	scutilLoaded time.Time
)

// systemProxyFunc 读取 macOS 的系统代理设置。
// 从 Finder 启动的 .app 不继承 shell 里的 HTTP_PROXY，而代理软件通常只写
// SCDynamicStore，所以标准库的 http.ProxyFromEnvironment 在这里读不到代理。
func systemProxyFunc() func(*http.Request) (*url.URL, error) {
	return func(req *http.Request) (*url.URL, error) {
		return currentScutilProxy().forRequest(req)
	}
}

func currentScutilProxy() *scutilProxy {
	scutilMu.Lock()
	defer scutilMu.Unlock()
	if scutilCached != nil && time.Since(scutilLoaded) < scutilConfigTTL {
		return scutilCached
	}

	cfg := &scutilProxy{byScheme: make(map[string]*url.URL)}
	if out, err := readScutilProxy(); err == nil {
		if parsed := parseScutilProxy(string(out)); parsed != nil {
			cfg = parsed
		}
	}
	scutilCached = cfg
	scutilLoaded = time.Now()
	return cfg
}

func readScutilProxy() ([]byte, error) {
	if out, err := exec.Command("/usr/sbin/scutil", "--proxy").Output(); err == nil {
		return out, nil
	}
	return exec.Command("scutil", "--proxy").Output()
}
