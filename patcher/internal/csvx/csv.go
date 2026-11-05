// Package csvx 提供与旧版 Univocity CsvParser 默认行为兼容的 CSV 解析。
//
// 关键兼容点（旧版 ReplaceEXDF 依赖）：
//   - '#' 开头的行按注释跳过（因此 "#,Name,Description" 行不会被当作数据）。
//   - 未加引号的字段会去除首尾空格 / 制表符。
//   - 加引号的字段保留内部空白，支持 "" 转义。
//   - 空行自动跳过。
package csvx

import (
	"bytes"
	"fmt"
	"os"
	"strings"
)

// ParseFile 读取并解析 CSV 文件（UTF-8，自动去除 BOM）。
// 引号未闭合等结构错误会返回 error，由调用方决定是否跳过该表。
func ParseFile(path string) ([][]string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("读取 CSV 失败: %w", err)
	}
	rows, err := parse(data, true)
	if err != nil {
		return nil, fmt.Errorf("解析 CSV 失败: %w", err)
	}
	return rows, nil
}

// Parse 解析 CSV 字节流。结构错误按当前行截断，不返回错误；
// 需要严格校验时用 ParseFile。
func Parse(data []byte) [][]string {
	rows, _ := parse(data, false)
	return rows
}

func parse(data []byte, strict bool) ([][]string, error) {
	s := string(bytes.TrimPrefix(data, []byte{0xEF, 0xBB, 0xBF}))
	var rows [][]string
	row := make([]string, 0, 16)
	i := 0
	n := len(s)

	for i < n {
		// 行首：判断注释行与空行。
		j := i
		for j < n && (s[j] == ' ' || s[j] == '\t') {
			j++
		}
		if j < n && s[j] == '#' {
			for i < n && s[i] != '\n' {
				i++
			}
			if i < n {
				i++
			}
			continue
		}
		if j >= n || s[j] == '\r' || s[j] == '\n' {
			for i < n && (s[i] == '\r' || s[i] == '\n') {
				i++
			}
			continue
		}

		row = row[:0]
		for {
			// 未加引号字段：先跳过前导空白。
			for i < n && (s[i] == ' ' || s[i] == '\t') {
				i++
			}
			var val string
			if i < n && s[i] == '"' {
				quoteStart := i
				i++
				var b strings.Builder
				closed := false
				for i < n {
					if s[i] == '"' {
						if i+1 < n && s[i+1] == '"' {
							b.WriteByte('"')
							i += 2
							continue
						}
						i++
						closed = true
						break
					}
					b.WriteByte(s[i])
					i++
				}
				if !closed {
					if strict {
						return nil, fmt.Errorf("第 %d 行存在未闭合的引号字段", len(rows)+1)
					}
					// 宽松模式只保留到本行结尾，避免把后续行吞进同一个字段。
					if nl := strings.IndexByte(s[quoteStart:], '\n'); nl >= 0 {
						val = strings.TrimRight(s[quoteStart+1:quoteStart+nl], "\r")
						i = quoteStart + nl
					} else {
						val = b.String()
						i = n
					}
				} else {
					val = b.String()
				}
				for i < n && (s[i] == ' ' || s[i] == '\t') {
					i++
				}
			} else {
				start := i
				for i < n && s[i] != ',' && s[i] != '\r' && s[i] != '\n' {
					i++
				}
				val = strings.TrimRight(s[start:i], " \t")
			}
			row = append(row, val)

			if i < n && s[i] == ',' {
				i++
				continue
			}
			break
		}
		if i < n && s[i] == '\r' {
			i++
		}
		if i < n && s[i] == '\n' {
			i++
		}
		rows = append(rows, append([]string(nil), row...))
	}
	return rows, nil
}
