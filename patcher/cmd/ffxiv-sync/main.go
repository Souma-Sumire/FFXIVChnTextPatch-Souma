package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/zipatch"
)

func main() {
	dir := flag.String("dir", "", "目标 sqpack 根目录（内含 sqpack/ffxiv）")
	patchesJSON := flag.String("patches-json", "", "包含补丁 URL 或本地路径数组的 JSON 文件")
	normalizeEmpty := flag.Bool("normalize-empty-blocks", false, "将旧版 ffxiv-downloader 错位的 +0x10 空块头修复为官方 +0x0C 格式")
	flag.Parse()

	if *dir == "" {
		fatalf("必须指定 -dir")
	}

	var patches []string
	if *patchesJSON != "" {
		raw, err := os.ReadFile(*patchesJSON)
		if err != nil {
			fatalf("读取 patches-json 失败: %v", err)
		}
		if err := json.Unmarshal(raw, &patches); err != nil {
			fatalf("解析 patches-json 失败: %v", err)
		}
	}
	patches = append(patches, flag.Args()...)

	client := &http.Client{
		Transport: &http.Transport{
			Proxy:                 http.ProxyFromEnvironment,
			MaxIdleConns:          8,
			IdleConnTimeout:       60 * time.Second,
			ResponseHeaderTimeout: 30 * time.Second,
		},
	}

	ctx := context.Background()
	for i, src := range patches {
		var (
			stats zipatch.ApplyStats
			err   error
		)
		start := time.Now()
		if strings.HasPrefix(src, "http://") || strings.HasPrefix(src, "https://") {
			stats, err = zipatch.ApplyHTTP(ctx, client, src, *dir, zipatch.DefaultTargets)
		} else {
			stats, err = zipatch.ApplyFile(ctx, src, *dir, zipatch.DefaultTargets)
		}
		if err != nil {
			fatalf("应用补丁 #%d (%s) 失败: %v", i+1, src, err)
		}
		fmt.Printf("[%d/%d] %s (applied=%d skipped=%d skipped=%.2fMiB downloaded=%.2fMiB elapsed=%s)\n",
			i+1, len(patches), src,
			stats.AppliedChunks, stats.SkippedChunks,
			float64(stats.SkippedBytes)/(1024*1024),
			float64(stats.DownloadedBytes)/(1024*1024),
			time.Since(start).Round(time.Millisecond),
		)
	}

	if *normalizeEmpty {
		sqpackDir := filepath.Join(*dir, "sqpack", "ffxiv")
		for _, base := range []string{"000000.win32", "0a0000.win32"} {
			datPath := filepath.Join(sqpackDir, base+".dat0")
			idxPath := filepath.Join(sqpackDir, base+".index")
			if _, err := os.Stat(datPath); err != nil {
				continue
			}
			if _, err := os.Stat(idxPath); err != nil {
				idxPath = ""
			}
			fixed, err := zipatch.NormalizeShiftedEmptyBlocks(datPath, idxPath)
			if err != nil {
				fatalf("规范化空块失败 (%s): %v", datPath, err)
			}
			fmt.Printf("normalize %s.dat0: fixed=%d\n", base, fixed)
		}
	}
}

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "ffxiv-sync: "+format+"\n", args...)
	os.Exit(1)
}
