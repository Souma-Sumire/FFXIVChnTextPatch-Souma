package gamecfg

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

var (
	ErrConfigNotFound = errors.New("未找到 FFXIV.cfg 配置文件")
	ErrInvalidConfig  = errors.New("FFXIV.cfg 内容无效或损坏")
)

const (
	ConfigFileName = "FFXIV.cfg"
	SubFolder      = "FINAL FANTASY XIV - A Realm Reborn"
)

var uiAssetTypeRegex = regexp.MustCompile(`(?m)^UiAssetType[\t ]+([0-9]+)\s*$`)

func FindConfigFile() (string, error) {
	for _, candidate := range CandidatePaths() {
		if st, err := os.Stat(candidate); err == nil && !st.IsDir() {
			return candidate, nil
		}
	}
	return "", ErrConfigNotFound
}

func CandidatePaths() []string {
	var candidates []string
	seen := make(map[string]bool)

	add := func(p string) {
		if p == "" {
			return
		}
		clean := filepath.Clean(p)
		if !seen[strings.ToLower(clean)] {
			seen[strings.ToLower(clean)] = true
			candidates = append(candidates, clean)
		}
	}

	for _, docs := range findDocumentDirs() {
		add(filepath.Join(docs, "My Games", SubFolder, ConfigFileName))
	}

	return candidates
}

func ReadUiAssetType(path string) (int, bool, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return -1, false, err
	}
	if len(data) < 20 || !bytes.Contains(data, []byte("FINAL FANTASY XIV")) {
		return -1, false, ErrInvalidConfig
	}

	matches := uiAssetTypeRegex.FindSubmatch(data)
	if len(matches) < 2 {
		return -1, false, nil
	}

	val, err := strconv.Atoi(string(matches[1]))
	if err != nil {
		return -1, false, err
	}
	return val, true, nil
}

type ModifyResult struct {
	ConfigPath string `json:"configPath"`
	BackupPath string `json:"backupPath"`
	Modified   bool   `json:"modified"`
	Previous   int    `json:"previous"`
	Current    int    `json:"current"`
}

func EnsureHighResolutionUiAsset(customPath string) (ModifyResult, error) {
	cfgPath := customPath
	if cfgPath == "" {
		found, err := FindConfigFile()
		if err != nil {
			return ModifyResult{}, err
		}
		cfgPath = found
	}

	st, err := os.Stat(cfgPath)
	if err != nil {
		return ModifyResult{}, fmt.Errorf("读取配置文件失败: %w", err)
	}
	if st.IsDir() {
		return ModifyResult{}, fmt.Errorf("%s 是目录而非文件", cfgPath)
	}
	if st.Size() < 20 || st.Size() > 10*1024*1024 {
		return ModifyResult{}, ErrInvalidConfig
	}

	data, err := os.ReadFile(cfgPath)
	if err != nil {
		return ModifyResult{}, fmt.Errorf("读取配置文件失败: %w", err)
	}
	if !bytes.Contains(data, []byte("FINAL FANTASY XIV")) {
		return ModifyResult{}, ErrInvalidConfig
	}

	eol := "\r\n"
	if !bytes.Contains(data, []byte("\r\n")) && bytes.Contains(data, []byte("\n")) {
		eol = "\n"
	}

	matches := uiAssetTypeRegex.FindSubmatchIndex(data)
	var prevVal = -1
	var hasEntry = false

	if len(matches) >= 4 {
		hasEntry = true
		valBytes := data[matches[2]:matches[3]]
		if v, err := strconv.Atoi(string(valBytes)); err == nil {
			prevVal = v
		}
	}

	if prevVal == 1 {
		return ModifyResult{
			ConfigPath: cfgPath,
			Modified:   false,
			Previous:   1,
			Current:    1,
		}, nil
	}

	bakPath := cfgPath + ".bak"
	if err := createBackupFile(cfgPath, bakPath); err != nil {
		return ModifyResult{}, fmt.Errorf("创建备份失败: %w", err)
	}

	var newData []byte
	targetLine := "UiAssetType\t1"

	if hasEntry {
		var buf bytes.Buffer
		buf.Write(data[:matches[0]])
		buf.WriteString(targetLine)
		buf.Write(data[matches[1]:])
		newData = buf.Bytes()
	} else {
		displaySettingsTag := []byte("<Display Settings>")
		idx := bytes.Index(data, displaySettingsTag)
		if idx >= 0 {
			insertPos := idx + len(displaySettingsTag)
			if insertPos+len(eol) <= len(data) && string(data[insertPos:insertPos+len(eol)]) == eol {
				insertPos += len(eol)
			}
			var buf bytes.Buffer
			buf.Write(data[:insertPos])
			buf.WriteString(targetLine + eol)
			buf.Write(data[insertPos:])
			newData = buf.Bytes()
		} else {
			var buf bytes.Buffer
			buf.Write(data)
			if !bytes.HasSuffix(data, []byte(eol)) {
				buf.WriteString(eol)
			}
			buf.WriteString(targetLine + eol)
			newData = buf.Bytes()
		}
	}

	tmpPath := cfgPath + ".tmp"
	if err := atomicWriteFile(tmpPath, cfgPath, newData); err != nil {
		return ModifyResult{}, fmt.Errorf("保存新配置失败: %w", err)
	}

	return ModifyResult{
		ConfigPath: cfgPath,
		BackupPath: bakPath,
		Modified:   true,
		Previous:   prevVal,
		Current:    1,
	}, nil
}

func createBackupFile(src, dst string) error {
	if st, err := os.Stat(dst); err == nil && st.Size() > 0 {
		return nil
	}

	srcFile, err := os.Open(src)
	if err != nil {
		return err
	}
	defer srcFile.Close()

	dstFile, err := os.OpenFile(dst, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	defer dstFile.Close()

	if _, err := io.Copy(dstFile, srcFile); err != nil {
		_ = os.Remove(dst)
		return err
	}
	return dstFile.Sync()
}

func atomicWriteFile(tmpPath, dstPath string, data []byte) error {
	f, err := os.OpenFile(tmpPath, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	if _, err := f.Write(data); err != nil {
		_ = f.Close()
		_ = os.Remove(tmpPath)
		return err
	}
	if err := f.Sync(); err != nil {
		_ = f.Close()
		_ = os.Remove(tmpPath)
		return err
	}
	if err := f.Close(); err != nil {
		_ = os.Remove(tmpPath)
		return err
	}

	if err := os.Rename(tmpPath, dstPath); err != nil {
		_ = os.Remove(tmpPath)
		return fmt.Errorf("安全替换配置文件失败: %w", err)
	}
	return nil
}
