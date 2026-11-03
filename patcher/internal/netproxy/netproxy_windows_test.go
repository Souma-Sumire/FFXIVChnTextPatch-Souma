package netproxy

import (
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"sync/atomic"
	"testing"
)

func TestParseProxyServer(t *testing.T) {
	cases := []struct {
		name string
		raw  string
		want map[string]string
	}{
		{"单一地址", "127.0.0.1:7897", map[string]string{"*": "http://127.0.0.1:7897"}},
		{"带协议", "http://127.0.0.1:7897", map[string]string{"*": "http://127.0.0.1:7897"}},
		{"按协议区分", "http=127.0.0.1:7897;https=127.0.0.1:7897",
			map[string]string{"http": "http://127.0.0.1:7897", "https": "http://127.0.0.1:7897"}},
		{"socks 覆盖全部", "socks=127.0.0.1:1080", map[string]string{"*": "socks5://127.0.0.1:1080"}},
		{"空值", "   ", map[string]string{}},
		{"非法项被忽略", "http=:0;https=127.0.0.1:7897",
			map[string]string{"https": "http://127.0.0.1:7897"}},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got := parseProxyServer(c.raw)
			if len(got) != len(c.want) {
				t.Fatalf("解析结果数量不符: got=%v want=%v", got, c.want)
			}
			for scheme, wantRaw := range c.want {
				u, ok := got[scheme]
				if !ok {
					t.Fatalf("缺少 scheme %q，got=%v", scheme, got)
				}
				if u.String() != wantRaw {
					t.Errorf("scheme %q 期望 %q 实际 %q", scheme, wantRaw, u.String())
				}
			}
		})
	}
}

func TestBypassHost(t *testing.T) {
	// 真实的 ProxyOverride 形如 localhost;127.*;192.168.*;*.local;<local>
	entries := strings.Split("*.local;*.corp.example;<local>;intranet;192.168.*;10.*", ";")
	cases := []struct {
		host string
		want bool
	}{
		{"github.com", false},
		{"api.github.com", false},
		{"printer.local", true},
		{"a.b.corp.example", true},
		{"corp.example", true},
		{"build-server", true}, // <local>：无点号的局域网主机名
		{"intranet", true},
		{"192.168.1.10", true},
		{"10.0.0.5", true},
		{"11.0.0.5", false},
		{"127.0.0.1", true}, // 回环永远直连
		{"localhost", true},
		{"", true},
	}
	for _, c := range cases {
		if got := bypassHost(entries, c.host); got != c.want {
			t.Errorf("bypassHost(%q) = %v, want %v", c.host, got, c.want)
		}
	}

	if !bypassHost([]string{"*"}, "github.com") {
		t.Error("ProxyOverride=* 时应全部直连")
	}
}

func TestProxyForSelectsByScheme(t *testing.T) {
	cfg := &proxyConfig{
		enable: true,
		byScheme: map[string]*url.URL{
			"http":  mustURL(t, "http://127.0.0.1:7897"),
			"https": mustURL(t, "http://127.0.0.1:7897"),
		},
		bypass: []string{"*.local"},
	}

	if proxy, _ := proxyFor(cfg, requestTo(t, "https://api.github.com/repos/x/y")); proxy == nil || proxy.Host != "127.0.0.1:7897" {
		t.Errorf("HTTPS 请求应走系统代理，实际 %v", proxy)
	}
	if proxy, _ := proxyFor(cfg, requestTo(t, "http://example.com/a")); proxy == nil {
		t.Error("HTTP 请求应走系统代理")
	}
	if proxy, _ := proxyFor(cfg, requestTo(t, "https://printer.local/")); proxy != nil {
		t.Errorf("命中 ProxyOverride 的不应走代理，实际 %v", proxy)
	}
	if proxy, _ := proxyFor(cfg, requestTo(t, "http://localhost:8080/")); proxy != nil {
		t.Errorf("回环地址不应走代理，实际 %v", proxy)
	}

	// 只配置了 http 代理时，https 请求不应被错误地套上
	httpOnly := &proxyConfig{enable: true, byScheme: map[string]*url.URL{"http": mustURL(t, "http://127.0.0.1:7897")}}
	if proxy, _ := proxyFor(httpOnly, requestTo(t, "https://example.com/")); proxy != nil {
		t.Errorf("未配置 https 代理时应直连，实际 %v", proxy)
	}

	// 关闭系统代理后直连
	cfg.enable = false
	if proxy, _ := proxyFor(cfg, requestTo(t, "https://api.github.com/")); proxy != nil {
		t.Errorf("ProxyEnable=0 时应直连，实际 %v", proxy)
	}
}

func mustURL(t *testing.T, raw string) *url.URL {
	t.Helper()
	u, err := url.Parse(raw)
	if err != nil {
		t.Fatal(err)
	}
	return u
}

func requestTo(t *testing.T, raw string) *http.Request {
	t.Helper()
	req, err := http.NewRequest(http.MethodGet, raw, nil)
	if err != nil {
		t.Fatal(err)
	}
	return req
}

// 端到端：系统代理配置为某个地址时，Transport 必须真的把请求送过去。
func TestTransportUsesSystemProxy(t *testing.T) {
	var hits int32
	proxySrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		atomic.AddInt32(&hits, 1)
		if r.URL.Host != "example.invalid" {
			t.Errorf("代理收到的目标主机异常: %q", r.URL.Host)
		}
		_, _ = w.Write([]byte("ok"))
	}))
	defer proxySrv.Close()

	proxyURL, err := url.Parse(proxySrv.URL)
	if err != nil {
		t.Fatal(err)
	}
	cfg := &proxyConfig{enable: true, byScheme: map[string]*url.URL{"http": proxyURL}}
	client := &http.Client{Transport: &http.Transport{
		Proxy: func(req *http.Request) (*url.URL, error) { return proxyFor(cfg, req) },
	}}

	resp, err := client.Get("http://example.invalid/path")
	if err != nil {
		t.Fatalf("请求失败: %v", err)
	}
	defer resp.Body.Close()
	if atomic.LoadInt32(&hits) == 0 {
		t.Fatal("请求没有经过系统代理")
	}
}

// 读取真实注册表，确认配置能被解析（不同机器设置不同，这里只校验结构）。
func TestReadConfigFromRegistry(t *testing.T) {
	cfg := readConfig()
	if cfg == nil {
		t.Skip("当前会话读不到 Internet Settings 注册表键")
	}
	t.Logf("ProxyEnable=%v 代理项=%v 绕过规则=%d 条", cfg.enable, cfg.byScheme, len(cfg.bypass))
	if cfg.enable && len(cfg.byScheme) == 0 {
		t.Error("已启用系统代理却解析不到代理地址")
	}
}
