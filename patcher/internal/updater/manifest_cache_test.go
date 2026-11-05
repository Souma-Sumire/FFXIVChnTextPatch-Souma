package updater

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"
)

func TestFetchManifestCachesWithinTTL(t *testing.T) {
	var hits atomic.Int64
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		hits.Add(1)
		_ = json.NewEncoder(w).Encode(Manifest{RawEXD: BundleInfo{SHA256: "aa"}})
	}))
	defer srv.Close()

	u := New()
	u.Nodes = []string{srv.URL}
	for i := 0; i < 3; i++ {
		if _, err := u.FetchManifest(context.Background()); err != nil {
			t.Fatalf("第 %d 次拉取失败: %v", i+1, err)
		}
	}
	if got := hits.Load(); got != 1 {
		t.Fatalf("缓存有效期内应只请求一次，实际 %d 次", got)
	}

	// 缓存过期后必须重新请求，保证下次运行能拿到最新清单。
	u.manifestMu.Lock()
	u.manifestCached = time.Now().Add(-manifestCacheTTL - time.Second)
	u.manifestMu.Unlock()
	if _, err := u.FetchManifest(context.Background()); err != nil {
		t.Fatal(err)
	}
	if got := hits.Load(); got != 2 {
		t.Fatalf("缓存过期后应重新请求，实际 %d 次", got)
	}
}
