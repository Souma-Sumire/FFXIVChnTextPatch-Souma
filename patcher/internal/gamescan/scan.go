package gamescan

import (
	"context"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
)

const (
	// DefaultMaxDepth 是默认扫描层级：root 的直接子目录记为第 1 层。
	DefaultMaxDepth = 4

	// scanWorkers 是单层并行读取目录的并发数。
	scanWorkers = 8
)

// skipDirs 是扫描时不再向下展开的系统与非游戏目录，避免把时间耗在无关目录树上。
var skipDirs = map[string]struct{}{
	"windows":                   {},
	"programdata":               {},
	"system volume information": {},
	"recovery":                  {},
	"config.msi":                {},
	"msocache":                  {},
	"perflogs":                  {},
	"$recycle.bin":              {},
	"users":                     {},
	"appdata":                   {},
	"node_modules":              {},
	"temp":                      {},
	"tmp":                       {},
	"windowsapps":               {},
	"wpsystem":                  {},
	"common files":              {},
	"microsoft":                 {},
	"microsoft.net":             {},
	"windows kits":              {},
	"reference assemblies":      {},
}

// Find 在 roots 下按层扫描名为 name 的目录，命中项经 accept 过滤后返回。
// root 的直接子目录算第 1 层，最多展开 maxDepth 层；ctx 被取消时立即返回已发现的结果。
func Find(ctx context.Context, roots []string, name string, maxDepth int, accept func(string) bool) []string {
	if name == "" {
		return nil
	}
	return FindAny(ctx, roots, []string{name}, maxDepth, accept)
}

// FindAny 在 roots 下按层扫描名称属于 names 的目录，命中项经 accept 过滤后返回。
func FindAny(ctx context.Context, roots []string, names []string, maxDepth int, accept func(string) bool) []string {
	if ctx == nil {
		ctx = context.Background()
	}
	nameSet := make(map[string]struct{}, len(names))
	for _, n := range names {
		if trimmed := strings.ToLower(strings.TrimSpace(n)); trimmed != "" {
			nameSet[trimmed] = struct{}{}
		}
	}
	if len(nameSet) == 0 || maxDepth <= 0 {
		return nil
	}

	var found []string
	level := dedupe(roots)

	for depth := 1; depth <= maxDepth && len(level) > 0; depth++ {
		if ctx.Err() != nil {
			break
		}
		level = scanLevel(ctx, level, nameSet, accept, &found)
	}

	if len(found) > 1 {
		sort.Strings(found)
	}
	return found
}

// FindClients 在 roots 下按层扫描客户端目录（最多 maxDepth 层），不限制客户端文件夹名称。
// 展开目录时先在内存中检查是否同时存在 boot 与 game 子目录，命中并经 accept 校验通过后立即剪枝停止向下展开。
func FindClients(ctx context.Context, roots []string, maxDepth int, accept func(string) bool) []string {
	if ctx == nil {
		ctx = context.Background()
	}
	if maxDepth <= 0 {
		return nil
	}
	level := dedupe(roots)
	if len(level) == 0 {
		return nil
	}

	var found []string
	for depth := 1; depth <= maxDepth && len(level) > 0; depth++ {
		if ctx.Err() != nil {
			break
		}
		level = scanClientLevel(ctx, level, depth == maxDepth, accept, &found)
	}

	if len(found) > 1 {
		sort.Strings(found)
	}
	return found
}

func scanClientLevel(ctx context.Context, parents []string, isLeafLevel bool, accept func(string) bool, found *[]string) []string {
	var (
		mu   sync.Mutex
		wg   sync.WaitGroup
		next []string
	)
	seen := make(map[string]struct{}, len(parents)*4)
	sem := make(chan struct{}, scanWorkers)

	for _, parent := range parents {
		if ctx.Err() != nil {
			break
		}
		wg.Add(1)
		sem <- struct{}{}
		go func(dir string) {
			defer wg.Done()
			defer func() { <-sem }()

			if ctx.Err() != nil {
				return
			}
			entries, err := readDir(dir)
			if err != nil {
				return
			}

			hasBoot, hasGame := false, false
			for _, entry := range entries {
				if !entry.IsDir() {
					continue
				}
				switch strings.ToLower(entry.Name()) {
				case "boot":
					hasBoot = true
				case "game":
					hasGame = true
				}
				if hasBoot && hasGame {
					break
				}
			}
			if hasBoot && hasGame && (accept == nil || accept(dir)) {
				mu.Lock()
				*found = append(*found, dir)
				mu.Unlock()
				return
			}

			var hits, subs []string
			for _, entry := range entries {
				if !entry.IsDir() || entry.Type()&os.ModeSymlink != 0 {
					continue
				}
				name := entry.Name()
				lower := strings.ToLower(name)
				if strings.HasPrefix(lower, "$") || strings.HasPrefix(lower, ".") {
					continue
				}
				if _, skip := skipDirs[lower]; skip {
					continue
				}
				child := filepath.Join(dir, name)
				if isLeafLevel {
					if accept != nil && accept(child) {
						hits = append(hits, child)
					}
					continue
				}
				subs = append(subs, child)
			}
			if len(hits) == 0 && len(subs) == 0 {
				return
			}

			mu.Lock()
			defer mu.Unlock()
			*found = append(*found, hits...)
			for _, sub := range subs {
				key := strings.ToLower(sub)
				if _, ok := seen[key]; ok {
					continue
				}
				seen[key] = struct{}{}
				next = append(next, sub)
			}
		}(parent)
	}
	wg.Wait()
	return next
}

func scanLevel(ctx context.Context, parents []string, nameSet map[string]struct{}, accept func(string) bool, found *[]string) []string {
	var (
		mu   sync.Mutex
		wg   sync.WaitGroup
		next []string
	)
	seen := make(map[string]struct{}, len(parents)*4)
	sem := make(chan struct{}, scanWorkers)

	for _, parent := range parents {
		if ctx.Err() != nil {
			break
		}
		wg.Add(1)
		sem <- struct{}{}
		go func(dir string) {
			defer wg.Done()
			defer func() { <-sem }()

			if ctx.Err() != nil {
				return
			}
			entries, err := readDir(dir)
			if err != nil {
				return
			}

			var hits, subs []string
			for _, entry := range entries {
				if !entry.IsDir() || entry.Type()&os.ModeSymlink != 0 {
					continue
				}
				child := filepath.Join(dir, entry.Name())
				lower := strings.ToLower(entry.Name())
				if _, ok := nameSet[lower]; ok {
					hits = append(hits, child)
					continue
				}
				if strings.HasPrefix(lower, "$") {
					continue
				}
				if _, skip := skipDirs[lower]; skip {
					continue
				}
				subs = append(subs, child)
			}
			if len(hits) == 0 && len(subs) == 0 {
				return
			}

			mu.Lock()
			defer mu.Unlock()
			for _, hit := range hits {
				if accept == nil || accept(hit) {
					*found = append(*found, hit)
				}
			}
			for _, sub := range subs {
				key := strings.ToLower(sub)
				if _, ok := seen[key]; ok {
					continue
				}
				seen[key] = struct{}{}
				next = append(next, sub)
			}
		}(parent)
	}
	wg.Wait()
	return next
}

func readDir(dir string) ([]os.DirEntry, error) {
	f, err := os.Open(dir)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return f.ReadDir(-1)
}

func dedupe(paths []string) []string {
	seen := make(map[string]struct{}, len(paths))
	out := make([]string, 0, len(paths))
	for _, p := range paths {
		if p == "" {
			continue
		}
		clean := filepath.Clean(p)
		key := strings.ToLower(clean)
		if _, ok := seen[key]; ok {
			continue
		}
		seen[key] = struct{}{}
		out = append(out, clean)
	}
	return out
}
