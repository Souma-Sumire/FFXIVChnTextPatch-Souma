package text

import (
	"encoding/hex"
	"errors"
	"fmt"
	"strings"
)

// ErrNestedHexTag 表示 CSV 文本中出现了非法的嵌套 <hex:...> 标签。
var ErrNestedHexTag = errors.New("非法的嵌套 <hex:...> 标签")

// ErrUnclosedHexTag 表示 CSV 文本中的 <hex:...> 标签缺少闭合的 '>'。
// 这种情况下标签会连同原文一起被当成普通文本写进游戏，因此一律报错。
var ErrUnclosedHexTag = errors.New("未闭合的 <hex:...> 标签")

// ParseTargetString 把 CSV 单元格中的 <hex:...> 标签解码为原始字节，
// 普通文本按 UTF-8 拼接，与旧版 ReplaceEXDF 的 CSV 分支输出一致。区别在标签的
// 识别与错误处理：标签必须以完整的 "<hex:" 开头（只认小写），"<hexabc>" 这类
// 没有冒号的写法按普通文本处理；标签没有闭合、嵌套或载荷不是合法十六进制时
// 返回错误，避免把半截标签当文本写进游戏。
func ParseTargetString(s string) ([]byte, error) {
	var out []byte
	for i := 0; i < len(s); {
		rel := strings.Index(s[i:], "<hex:")
		if rel < 0 {
			out = append(out, s[i:]...)
			break
		}
		start := i + rel
		out = append(out, s[i:start]...)

		end := strings.IndexByte(s[start+len("<hex:"):], '>')
		if end < 0 {
			return nil, fmt.Errorf("%w: %s", ErrUnclosedHexTag, s)
		}
		payload := s[start+len("<hex:") : start+len("<hex:")+end]
		if strings.Contains(payload, "<hex:") {
			return nil, fmt.Errorf("%w: %s", ErrNestedHexTag, s)
		}
		decoded, err := decodeHex(payload)
		if err != nil {
			return nil, fmt.Errorf("%w: %s", err, s)
		}
		out = append(out, decoded...)
		i = start + len("<hex:") + end + 1
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
