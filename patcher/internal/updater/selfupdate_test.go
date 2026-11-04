package updater

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

func writeTestFile(t *testing.T, path string, data []byte) {
	t.Helper()
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}

// fakeDownloadCloud 把固定内容写进目标文件，避免测试依赖网络。
type fakeDownloadCloud struct {
	payload []byte
	err     error
}

func (f *fakeDownloadCloud) ReleaseByTag(ctx context.Context, repo, tag string) (*verifier.Release, error) {
	return nil, verifier.ErrNotFound
}

func (f *fakeDownloadCloud) RecentReleases(ctx context.Context, repo string, limit int) ([]*verifier.Release, error) {
	return nil, nil
}

func (f *fakeDownloadCloud) Download(ctx context.Context, url, dst string, progress func(done, total int64)) error {
	if f.err != nil {
		return f.err
	}
	if progress != nil {
		progress(int64(len(f.payload)), int64(len(f.payload)))
	}
	return os.WriteFile(dst, f.payload, 0o755)
}

func (f *fakeDownloadCloud) MirrorDownloadURL(raw string) string { return "" }

func TestVerifyAppUpdate(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "app.exe")
	payload := append([]byte("MZ"), make([]byte, 4096)...)
	writeTestFile(t, path, payload)

	sum := sha256.Sum256(payload)
	goodHash := hex.EncodeToString(sum[:])

	if err := verifyAppUpdate(path, goodHash, int64(len(payload))); err != nil {
		t.Fatalf("哈希匹配时应通过校验，实际报错: %v", err)
	}
	if err := verifyAppUpdate(path, "", int64(len(payload))); err != nil {
		t.Fatalf("无哈希时应只做基础校验，实际报错: %v", err)
	}
	if err := verifyAppUpdate(path, goodHash, int64(len(payload))+1); err == nil {
		t.Fatal("体积不足时应报错")
	}
	if err := verifyAppUpdate(path, hex.EncodeToString(make([]byte, 32)), int64(len(payload))); err == nil {
		t.Fatal("哈希不匹配时应报错")
	}
}

func TestSwapExecutableAndRollback(t *testing.T) {
	dir := t.TempDir()
	target := filepath.Join(dir, "FFXIVChnTextPatch-SM-v2.exe")
	newPath := filepath.Join(dir, ".app-update-123")
	writeTestFile(t, target, []byte("old"))
	writeTestFile(t, newPath, []byte("new"))

	backup, err := SwapExecutable(newPath, target)
	if err != nil {
		t.Fatalf("替换失败: %v", err)
	}
	if got, _ := os.ReadFile(target); string(got) != "new" {
		t.Fatalf("目标文件应为新版本，实际为 %q", string(got))
	}
	if got, _ := os.ReadFile(backup); string(got) != "old" {
		t.Fatalf("备份文件应为旧版本，实际为 %q", string(got))
	}
	if _, err := os.Stat(newPath); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("替换后临时文件应已不存在")
	}

	if err := RollbackExecutable(target, backup); err != nil {
		t.Fatalf("回滚失败: %v", err)
	}
	if got, _ := os.ReadFile(target); string(got) != "old" {
		t.Fatalf("回滚后应恢复旧版本，实际为 %q", string(got))
	}
}

func TestPrepareAppUpdateValidatesInput(t *testing.T) {
	if !SelfUpdateSupported() {
		t.Skip("仅 Windows 支持就地替换")
	}
	target := filepath.Join(t.TempDir(), "app.exe")
	if _, err := PrepareAppUpdate(context.Background(), nil, &AppUpdateInfo{}, target, nil, nil); err == nil {
		t.Fatal("缺少下载地址时应报错")
	}

	cloud := &fakeDownloadCloud{err: errors.New("network down")}
	_, err := PrepareAppUpdate(context.Background(), cloud, &AppUpdateInfo{DownloadURL: "https://github.com/o/r/releases/download/t/app.exe"}, target, nil, nil)
	if err == nil {
		t.Fatal("下载失败时应把错误返回给调用方")
	}
	if _, statErr := os.Stat(target); !errors.Is(statErr, os.ErrNotExist) {
		t.Fatal("下载失败不应留下目标文件")
	}
}

func TestPrepareAppUpdateRejectsBadPayload(t *testing.T) {
	if !SelfUpdateSupported() {
		t.Skip("仅 Windows 支持就地替换")
	}
	dir := t.TempDir()
	target := filepath.Join(dir, "app.exe")

	// 体积过小（非完整程序）
	cloud := &fakeDownloadCloud{payload: []byte("MZ")}
	if _, err := PrepareAppUpdate(context.Background(), cloud, &AppUpdateInfo{DownloadURL: "https://github.com/o/r/releases/download/t/app.exe"}, target, nil, nil); err == nil {
		t.Fatal("体积异常的更新文件应被拒绝")
	}

	// 体积达标但哈希不符
	payload := append([]byte("MZ"), make([]byte, minAppUpdateBytes)...)
	cloud = &fakeDownloadCloud{payload: payload}
	wrong := hex.EncodeToString(make([]byte, 32))
	if _, err := PrepareAppUpdate(context.Background(), cloud, &AppUpdateInfo{
		DownloadURL:    "https://github.com/o/r/releases/download/t/app.exe",
		DownloadSHA256: wrong,
	}, target, nil, nil); err == nil {
		t.Fatal("哈希不符的更新文件应被拒绝")
	}

	sum := sha256.Sum256(payload)
	cloud = &fakeDownloadCloud{payload: payload}
	got, err := PrepareAppUpdate(context.Background(), cloud, &AppUpdateInfo{
		DownloadURL:    "https://github.com/o/r/releases/download/t/app.exe",
		DownloadSHA256: hex.EncodeToString(sum[:]),
	}, target, nil, nil)
	if err != nil {
		t.Fatalf("合法更新包应通过校验: %v", err)
	}
	defer os.Remove(got)
	if st, err := os.Stat(got); err != nil || st.Size() != int64(len(payload)) {
		t.Fatalf("临时文件内容不完整: %v", err)
	}
}

func TestPrepareAppUpdateUnsupportedPlatform(t *testing.T) {
	if SelfUpdateSupported() {
		t.Skip("Windows 支持就地替换")
	}
	_, err := PrepareAppUpdate(context.Background(), &fakeDownloadCloud{}, &AppUpdateInfo{DownloadURL: "https://x/y.exe"}, "app", nil, nil)
	if !errors.Is(err, ErrSelfUpdateUnsupported) {
		t.Fatalf("非 Windows 平台应返回 ErrSelfUpdateUnsupported，实际: %v", err)
	}
}
