package updater

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// verifyStampName 记录最近一次完整校验通过时的云端清单与本地文件指纹。
// 它放在资源目录内，随资源一起被用户看到的概率很低，写失败也不影响功能。
const verifyStampName = ".verify.json"

// verifyStamp 用来跳过重复的全量哈希校验。
// 只要云端清单没变、本地文件的数量/大小/修改时间之和也没变，
// 就说明内容没有被改动过：7114 个 CSV 的 CRC 核对可以退化成一次目录遍历（约 15ms），
// 目录被改过或清单更新时才回到完整校验。
type verifyStamp struct {
	Manifest string    `json:"manifest"`
	Zip      fileStamp `json:"zip"`
	CSV      treeStamp `json:"csv"`
}

type fileStamp struct {
	Size    int64 `json:"size"`
	ModTime int64 `json:"mtime"`
}

type treeStamp struct {
	Files      int   `json:"files"`
	Size       int64 `json:"size"`
	ModTimeSum int64 `json:"mtimeSum"`
	// EntriesDigest 是「相对路径|大小|mtime」按路径排序后的清单摘要。
	// 单靠文件数/总大小/mtime 之和，删一个文件再补一个同大小同时间的文件就能绕过；
	// 逐文件指纹让增删、互换立刻现形，代价仍只是一次目录遍历。
	EntriesDigest string `json:"entriesDigest"`
}

// manifestIdentity 取清单中决定资源内容的四个哈希，任一变化都视为云端资源已更新。
func manifestIdentity(m *Manifest) string {
	return m.RawEXD.SHA256 + "|" + m.Rules.SHA256 + "|" + m.Tables.SHA256
}

func readVerifyStamp(resourceDir string) *verifyStamp {
	data, err := os.ReadFile(filepath.Join(resourceDir, verifyStampName))
	if err != nil {
		return nil
	}
	var s verifyStamp
	if err := json.Unmarshal(data, &s); err != nil {
		return nil
	}
	return &s
}

func writeVerifyStamp(resourceDir string, s verifyStamp) {
	data, err := json.Marshal(s)
	if err != nil {
		return
	}
	path := filepath.Join(resourceDir, verifyStampName)
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
	}
}

// buildVerifyStamp 在完整校验通过或更新完成后重新采集指纹。
func buildVerifyStamp(resourceDir string, remote *Manifest) verifyStamp {
	s := verifyStamp{Manifest: manifestIdentity(remote)}
	if st, err := os.Stat(filepath.Join(resourceDir, "rawexd.zip")); err == nil {
		s.Zip = fileStamp{Size: st.Size(), ModTime: st.ModTime().UnixNano()}
	}
	s.CSV, _ = scanTreeStamp(filepath.Join(resourceDir, "rawexd"))
	return s
}

// matches 判断本地资源是否仍是上次校验通过时的状态。
func (s verifyStamp) matches(resourceDir string, remote *Manifest) bool {
	if s.Manifest != manifestIdentity(remote) {
		return false
	}
	st, err := os.Stat(filepath.Join(resourceDir, "rawexd.zip"))
	if err != nil || s.Zip.Size != st.Size() || s.Zip.ModTime != st.ModTime().UnixNano() {
		return false
	}
	csv, ok := scanTreeStamp(filepath.Join(resourceDir, "rawexd"))
	return ok && csv == s.CSV
}

// scanTreeStamp 汇总目录下所有文件的数量、总大小、修改时间之和与逐文件指纹；
// 内联修改不改变父目录时间戳，因此只能逐文件取元数据，好在不读文件内容。
func scanTreeStamp(root string) (treeStamp, bool) {
	var s treeStamp
	exists := false
	entries := make([]string, 0, 256)
	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if path == root {
				exists = true
			}
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return nil
		}
		s.Files++
		s.Size += info.Size()
		s.ModTimeSum += info.ModTime().UnixNano()
		rel, relErr := filepath.Rel(root, path)
		if relErr != nil {
			rel = path
		}
		entries = append(entries, fmt.Sprintf("%s|%d|%d", rel, info.Size(), info.ModTime().UnixNano()))
		return nil
	})
	if err == nil && len(entries) > 0 {
		sort.Strings(entries)
		sum := sha256.Sum256([]byte(strings.Join(entries, "\n")))
		s.EntriesDigest = hex.EncodeToString(sum[:])
	}
	return s, exists && err == nil
}
