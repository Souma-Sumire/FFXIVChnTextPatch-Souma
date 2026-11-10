package updater

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	goruntime "runtime"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

// ErrSelfUpdateUnsupported 表示当前平台的发布包无法就地替换（macOS 的 .app 需要走手动替换）。
var ErrSelfUpdateUnsupported = errors.New("当前平台不支持自动更新，请前往发布页面手动下载")

// minAppUpdateBytes 是下载结果的下限。发布包约 14MB，明显偏小说明拿到的是错误页或断流。
const minAppUpdateBytes = 1 << 20

// appUpdateTempPattern 是自更新临时文件名，前缀固定以便启动时清理中断的残留。
const appUpdateTempPattern = ".app-update-*"

// nodeHookSetter 由支持镜像切换的 Cloud 实现提供（即 GitHub 客户端）。
type nodeHookSetter interface {
	SetNodeHook(func(node string))
}

// SelfUpdateSupported 判断当前平台是否支持下载后替换并重启自身。
// Windows 单文件 exe 可以直接换名替换；macOS 发布包是 .app 压缩包，整体替换需要重新签名，改走手动下载。
func SelfUpdateSupported() bool {
	return goruntime.GOOS == "windows"
}

// PrepareAppUpdate 把最新版本下载到 target 同目录的临时文件并校验内容。
// 下载与目标同卷，后续替换只是一次 rename，不会出现跨盘复制的半成品。
// onNode 非空时在每个下载节点（加速镜像或直连）尝试前回调节点名称，供界面展示进度来源。
func PrepareAppUpdate(ctx context.Context, cloud verifier.Cloud, info *AppUpdateInfo, target string, progress func(done, total int64), onNode func(label string)) (string, error) {
	if !SelfUpdateSupported() {
		return "", ErrSelfUpdateUnsupported
	}
	if info == nil || info.DownloadURL == "" {
		return "", errors.New("未找到可用的更新下载地址")
	}
	if info.DownloadSHA256 == "" && info.LatestVersion != "" {
		tag := "patcher-v" + info.LatestVersion
		if rel, err := cloud.ReleaseByTag(ctx, "Souma-Sumire/FFXIVChnTextPatch-Souma", tag); err == nil && rel != nil {
			assetName := pickReleaseAssetName(goruntime.GOOS, rel.Assets)
			if digest, ok := rel.AssetSHA256(assetName); ok {
				info.DownloadSHA256 = digest
			}
		}
	}
	if target == "" {
		return "", errors.New("无法定位当前程序所在路径")
	}
	if cloud == nil {
		cloud = verifier.NewGitHubClient()
	}

	dir := filepath.Dir(target)
	if err := ensureWritableDir(dir); err != nil {
		return "", fmt.Errorf("程序所在目录不可写，无法自动更新。请将程序移至可写目录后重试，或手动下载最新版（%w）", err)
	}

	tmp, err := os.CreateTemp(dir, appUpdateTempPattern)
	if err != nil {
		return "", err
	}
	tmpPath := tmp.Name()
	_ = tmp.Close()

	// 下载能力由 Cloud 提供，镜像切换只在 GitHub 客户端里有；按需挂上节点回调，
	// 让界面能看到当前走的是哪个加速节点，而不是只显示「正在下载」。
	if setter, ok := cloud.(nodeHookSetter); ok && onNode != nil {
		setter.SetNodeHook(onNode)
		defer setter.SetNodeHook(nil)
	}

	if err := cloud.Download(ctx, info.DownloadURL, tmpPath, progress); err != nil {
		_ = os.Remove(tmpPath)
		return "", err
	}
	if err := verifyAppUpdate(tmpPath, info.DownloadSHA256, minAppUpdateBytes); err != nil {
		_ = os.Remove(tmpPath)
		return "", err
	}
	return tmpPath, nil
}

// verifyAppUpdate 校验下载到的程序：体积下限、Windows 的 PE 头，以及发布方给出的 SHA256。
func verifyAppUpdate(path, wantSHA string, minBytes int64) error {
	st, err := os.Stat(path)
	if err != nil {
		return err
	}
	if st.Size() < minBytes {
		return errors.New("下载的更新文件不完整，请重试")
	}

	if goruntime.GOOS == "windows" {
		f, err := os.Open(path)
		if err != nil {
			return err
		}
		head := make([]byte, 2)
		_, readErr := io.ReadFull(f, head)
		_ = f.Close()
		if readErr != nil {
			return fmt.Errorf("读取更新文件失败: %w", readErr)
		}
		if string(head) != "MZ" {
			return errors.New("下载的更新文件无效，请重试或手动下载最新版本")
		}
	}

	if wantSHA == "" {
		return nil
	}
	got, err := verifier.HashFile(path)
	if err != nil {
		return err
	}
	if !strings.EqualFold(got.SHA256, wantSHA) {
		return errors.New("更新文件校验未通过，请重试或手动下载最新版本")
	}
	return nil
}

// SwapExecutable 用 newPath 替换 target，返回旧程序的备份路径。
// Windows 下正在运行的程序无法删除但可以改名，所以先把旧程序改名让出文件名，
// 再放入新文件；任何一步失败都把旧程序放回原位，绝不留一个坏掉的目标文件。
func SwapExecutable(newPath, target string) (string, error) {
	backup := target + ".old"
	_ = os.Remove(backup)

	if err := os.Rename(target, backup); err != nil {
		return "", fmt.Errorf("无法替换正在运行的程序: %w", err)
	}
	if err := os.Rename(newPath, target); err != nil {
		if rbErr := os.Rename(backup, target); rbErr != nil {
			return "", fmt.Errorf("替换程序失败: %v；恢复原程序也失败: %v", err, rbErr)
		}
		return "", fmt.Errorf("替换程序失败: %w", err)
	}
	return backup, nil
}

// RollbackExecutable 在启动新版本失败时把旧程序放回原位。
func RollbackExecutable(target, backup string) error {
	_ = os.Remove(target)
	return os.Rename(backup, target)
}

// LaunchExecutable 启动新版本进程，工作目录跟随程序所在目录。
func LaunchExecutable(path string) error {
	cmd := exec.Command(path)
	cmd.Dir = filepath.Dir(path)
	return cmd.Start()
}

// CleanupStaleAppFiles 后台清理上一次自更新留下的旧程序与中断的下载文件。
// 旧程序可能仍被即将退出的进程占用，因此按间隔重试一段时间后放弃。
func CleanupStaleAppFiles(target string) {
	if target == "" {
		return
	}
	stale := []string{target + ".old"}
	if matches, err := filepath.Glob(filepath.Join(filepath.Dir(target), appUpdateTempPattern)); err == nil {
		stale = append(stale, matches...)
	}

	go func() {
		for _, path := range stale {
			for i := 0; i < 20; i++ {
				err := os.Remove(path)
				if err == nil || errors.Is(err, os.ErrNotExist) {
					break
				}
				time.Sleep(300 * time.Millisecond)
			}
		}
	}()
}

func ensureWritableDir(dir string) error {
	f, err := os.CreateTemp(dir, ".write-test-*")
	if err != nil {
		return err
	}
	name := f.Name()
	_ = f.Close()
	return os.Remove(name)
}
