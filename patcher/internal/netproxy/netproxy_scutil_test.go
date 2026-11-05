package netproxy

import (
	"net/http"
	"testing"
)

const sampleScutil = `<dictionary> {
  ExceptionsList : <array> {
    0 : *.local
    1 : 169.254/16
  }
  ExcludeSimpleHostnames : 1
  FTPPassive : 1
  HTTPEnable : 1
  HTTPPort : 7897
  HTTPProxy : 127.0.0.1
  HTTPSEnable : 1
  HTTPSPort : 7897
  HTTPSProxy : 127.0.0.1
  SOCKSEnable : 1
  SOCKSPort : 7890
  SOCKSProxy : 127.0.0.1
}`

func TestParseScutilProxy(t *testing.T) {
	cfg := parseScutilProxy(sampleScutil)
	if !cfg.enable {
		t.Fatal("配置了 HTTP/HTTPS/SOCKS 代理时应启用")
	}
	cases := map[string]string{
		"https": "http://127.0.0.1:7897",
		"http":  "http://127.0.0.1:7897",
		"*":     "socks5://127.0.0.1:7890",
	}
	for scheme, want := range cases {
		u := cfg.byScheme[scheme]
		if u == nil || u.String() != want {
			t.Errorf("byScheme[%q] = %v, want %q", scheme, u, want)
		}
	}
}

func TestParseScutilProxyDisabled(t *testing.T) {
	cfg := parseScutilProxy("<dictionary> {\n  HTTPEnable : 0\n  HTTPSEnable : 0\n  SOCKSEnable : 0\n}")
	if cfg.enable || len(cfg.byScheme) != 0 {
		t.Fatalf("代理全部关闭时不应启用: %+v", cfg)
	}
}

func TestParseScutilProxyBadPort(t *testing.T) {
	cfg := parseScutilProxy("<dictionary> {\n  HTTPEnable : 1\n  HTTPPort : 7897abc\n  HTTPProxy : 127.0.0.1\n}")
	if cfg.enable {
		t.Fatal("端口非法时不应生成代理地址")
	}
}

func TestScutilProxyForRequest(t *testing.T) {
	cfg := parseScutilProxy(sampleScutil)
	cases := []struct {
		url  string
		want string
	}{
		{"https://raw.githubusercontent.com/x", "http://127.0.0.1:7897"},
		{"http://example.com", "http://127.0.0.1:7897"},
		{"https://foo.local/x", ""},
		{"http://localhost:8080", ""},
		{"http://127.0.0.1:8080", ""},
		{"http://169.254.1.1", ""},
		{"https://printer", ""},
	}
	for _, tc := range cases {
		req, err := http.NewRequest(http.MethodGet, tc.url, nil)
		if err != nil {
			t.Fatal(err)
		}
		u, err := cfg.forRequest(req)
		if err != nil {
			t.Fatalf("%s: %v", tc.url, err)
		}
		got := ""
		if u != nil {
			got = u.String()
		}
		if got != tc.want {
			t.Errorf("%s -> %q, want %q", tc.url, got, tc.want)
		}
	}
}

func TestScutilProxySocksFallback(t *testing.T) {
	cfg := parseScutilProxy("<dictionary> {\n  SOCKSEnable : 1\n  SOCKSPort : 1080\n  SOCKSProxy : 127.0.0.1\n}")
	req, _ := http.NewRequest(http.MethodGet, "https://github.com", nil)
	u, err := cfg.forRequest(req)
	if err != nil || u == nil || u.String() != "socks5://127.0.0.1:1080" {
		t.Fatalf("只开 SOCKS 时 https 请求应走 SOCKS，实际 %v, %v", u, err)
	}
}
