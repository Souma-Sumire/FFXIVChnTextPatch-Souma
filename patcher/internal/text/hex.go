package text

import (
	"encoding/hex"
	"fmt"
	"strings"
)

// ErrNestedHexTag 表示 CSV 文本中出现了非法的嵌套 <hex:...> 标签。
var ErrNestedHexTag = fmt.Errorf("非法的嵌套 <hex:...> 标签")

// ParseTargetString 把 CSV 单元格中的 <hex:...> 标签解码为原始字节，
// 普通文本按 UTF-8 拼接。行为与旧版 ReplaceEXDF 的 CSV 分支保持一致：
// 遇到 "<hex:" 之前会先冲刷已累积的普通文本；遇到 ">" 且处于 hex 状态时解码标签内容。
func ParseTargetString(s string) ([]byte, error) {
	var out []byte
	var cur []byte
	inHex := false

	for i := 0; i < len(s); i++ {
		c := s[i]
		switch c {
		case '<':
			isHex := i+3 < len(s) && s[i+1] == 'h' && s[i+2] == 'e' && s[i+3] == 'x'
			if isHex {
				if inHex {
					return nil, fmt.Errorf("%w: %s", ErrNestedHexTag, s)
				}
				inHex = true
			}
			if len(cur) > 0 {
				out = append(out, cur...)
				cur = cur[:0]
			}
			cur = append(cur, c)
		case '>':
			cur = append(cur, c)
			if !inHex {
				continue
			}
			if len(cur) < 6 {
				return nil, fmt.Errorf("非法的 <hex:...> 标签: %s", s)
			}
			payload := string(cur[5 : len(cur)-1])
			decoded, err := decodeHex(payload)
			if err != nil {
				return nil, fmt.Errorf("%w: %s", err, s)
			}
			out = append(out, decoded...)
			cur = cur[:0]
			inHex = false
		default:
			cur = append(cur, c)
		}
	}
	if len(cur) > 0 {
		out = append(out, cur...)
	}
	return out, nil
}

func decodeHex(s string) ([]byte, error) {
	s = strings.ReplaceAll(s, " ", "")
	if len(s)%2 != 0 {
		return nil, fmt.Errorf("十六进制字符串长度为奇数")
	}
	out := make([]byte, len(s)/2)
	if _, err := hex.Decode(out, []byte(s)); err != nil {
		return nil, fmt.Errorf("十六进制字符串非法: %w", err)
	}
	return out, nil
}

// TransformPlain 只对 <hex:...> 标签之外的普通文本执行转换，标签与载荷保持原样。
func TransformPlain(s string, fn func(string) string) string {
	var b strings.Builder
	last := 0
	for i := 0; i+5 <= len(s); i++ {
		if s[i] != '<' || !strings.HasPrefix(s[i:], "<hex:") {
			continue
		}
		end := strings.IndexByte(s[i:], '>')
		if end < 0 {
			break
		}
		b.WriteString(fn(s[last:i]))
		b.WriteString(s[i : i+end+1])
		i += end
		last = i + 1
	}
	b.WriteString(fn(s[last:]))
	return b.String()
}
