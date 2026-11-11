package zipatch

import (
	"encoding/binary"
	"fmt"
	"strings"
)

const (
	Magic      = "\x91ZIPATCH\r\n\x1a\n"
	HeaderSize = 1024
)

const placeholder = "%PLACEHOLDER%"

var platformNames = map[uint16]string{
	0:   "win32",
	1:   "ps3",
	2:   "ps4",
	3:   "ps5",
	4:   "lys",
	254: placeholder,
	255: "unknown",
}

func platformName(id uint16) string {
	if name, ok := platformNames[id]; ok {
		return name
	}
	return "win32"
}

func normalizePath(p string) string {
	p = strings.ReplaceAll(p, "\\", "/")
	return strings.Trim(p, "/")
}

func resolvePlatform(p, platform string) string {
	return normalizePath(strings.ReplaceAll(p, placeholder, platform))
}

func expansionFolder(expansionID uint16) string {
	if expansionID == 0 {
		return "ffxiv"
	}
	return fmt.Sprintf("ex%d", expansionID)
}

func sqpackStem(b []byte) string {
	mainID := binary.BigEndian.Uint16(b[0:2])
	subID := binary.BigEndian.Uint16(b[2:4])
	exp := expansionFolder(subID >> 8)
	return fmt.Sprintf("sqpack/%s/%02x%04x.%s", exp, mainID, subID, placeholder)
}

func readDatPath(b []byte) string {
	fileID := binary.BigEndian.Uint32(b[4:8])
	return fmt.Sprintf("%s.dat%d", sqpackStem(b), fileID)
}

func readIndexPath(b []byte) string {
	fileID := binary.BigEndian.Uint32(b[4:8])
	stem := sqpackStem(b)
	if fileID == 0 {
		return stem + ".index"
	}
	return fmt.Sprintf("%s.index%d", stem, fileID)
}

func trimZeroASCII(b []byte) string {
	end := len(b)
	for end > 0 && b[end-1] == 0 {
		end--
	}
	return string(b[:end])
}

// inspectSqpkTarget 仅查看 SQPK 块头部前导字节并解析目标路径；
// canSkip 为 true 表示该块只影响 target 且大块载荷可直接跳过。
func inspectSqpkTarget(peek []byte, platform string) (target string, canSkip bool) {
	if len(peek) < 5 {
		return "", false
	}
	cmd := peek[4]
	switch cmd {
	case 'A':
		if len(peek) < 16 {
			return "", false
		}
		return resolvePlatform(readDatPath(peek[8:16]), platform), true
	case 'D', 'E':
		if len(peek) < 16 {
			return "", false
		}
		return resolvePlatform(readDatPath(peek[8:16]), platform), true
	case 'F':
		if len(peek) < 32 {
			return "", false
		}
		op := peek[5]
		if op == 'R' {
			return "", false
		}
		pathLen := int(int32(binary.BigEndian.Uint32(peek[24:28])))
		if pathLen < 0 || 32+pathLen > len(peek) {
			return "", false
		}
		return resolvePlatform(trimZeroASCII(peek[32:32+pathLen]), platform), true
	default:
		return "", false
	}
}
