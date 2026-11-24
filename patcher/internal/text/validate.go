package text

import (
	"errors"
	"fmt"
)

var (
	// ErrInvalidSeString 表示编码后的字节流不符合 FFXIV SeString 二进制结构。
	ErrInvalidSeString = errors.New("SeString 控制码结构非法")
)

const maxSeStringDepth = 256

// ValidateSeString 校验字节流中的 FFXIV SeString 控制码（0x02 ... 0x03）
// 与内嵌字符串表达式（0xFF ...）长度是否与实际边界匹配，防止越界或截断导致游戏崩溃。
func ValidateSeString(data []byte) error {
	return validateSeStringDepth(data, 0)
}

func validateSeStringDepth(data []byte, depth int) error {
	if depth > maxSeStringDepth {
		return fmt.Errorf("%w: 控制码嵌套层数超过上限 (%d)", ErrInvalidSeString, maxSeStringDepth)
	}
	for i := 0; i < len(data); {
		b := data[i]
		if b == 0x00 {
			return fmt.Errorf("%w: 偏移 %d 处存在非法 NUL 字节 0x00", ErrInvalidSeString, i)
		}
		if b == 0x03 {
			return fmt.Errorf("%w: 偏移 %d 处存在孤立的结束符 0x03", ErrInvalidSeString, i)
		}
		if b != 0x02 {
			i++
			continue
		}

		if i+2 >= len(data) {
			return fmt.Errorf("%w: 偏移 %d 处的控制码过短（缺少类型或长度）", ErrInvalidSeString, i)
		}
		macroCode := data[i+1]
		if macroCode == 0x00 || macroCode >= 0xCF {
			return fmt.Errorf("%w: 偏移 %d 处存在非法控制码类型 0x%02X", ErrInvalidSeString, i, macroCode)
		}

		bodyLen, lenBytes, err := decodeSeInt(data[i+2:])
		if err != nil {
			return fmt.Errorf("%w: 偏移 %d 处解析控制码 0x%02X 长度失败: %v", ErrInvalidSeString, i, macroCode, err)
		}
		if bodyLen < 0 {
			return fmt.Errorf("%w: 偏移 %d 处控制码 0x%02X 长度为负 (%d)", ErrInvalidSeString, i, macroCode, bodyLen)
		}

		bodyStart := i + 2 + lenBytes
		if bodyStart >= len(data) || bodyLen > len(data)-1-bodyStart {
			return fmt.Errorf("%w: 偏移 %d 处控制码 0x%02X 长度过长（声明 %d 字节，总长仅 %d）",
				ErrInvalidSeString, i, macroCode, bodyLen, len(data)-bodyStart)
		}
		bodyEnd := bodyStart + bodyLen
		if data[bodyEnd] != 0x03 {
			return fmt.Errorf("%w: 偏移 %d 处控制码 0x%02X 长度不匹配（声明 %d 字节，末尾字节为 0x%02X 而非 0x03）",
				ErrInvalidSeString, i, macroCode, bodyLen, data[bodyEnd])
		}
		for k := bodyStart; k < bodyEnd; k++ {
			if data[k] == 0x00 {
				return fmt.Errorf("%w: 偏移 %d 处控制码 0x%02X 内部存在非法 NUL 字节 0x00", ErrInvalidSeString, k, macroCode)
			}
		}

		if macroCode != 0x22 { // Format 指令末尾含非表达式原始字节，只校验外层包络
			if err := validateMacroBody(data[bodyStart:bodyEnd], macroCode, depth+1); err != nil {
				return err
			}
		}

		i = bodyEnd + 1
	}
	return nil
}

func validateMacroBody(body []byte, macroCode byte, depth int) error {
	pos := 0
	for pos < len(body) {
		consumed, ok, err := validateSeExpr(body[pos:], depth)
		if err != nil {
			return fmt.Errorf("%w (外层控制码 0x%02X)", err, macroCode)
		}
		if !ok {
			// 遇到非标准表达式字节（如部分指令的原始参数），外层 0x03 已校验对齐，停止深层扫描。
			break
		}
		pos += consumed
	}
	return nil
}

func validateSeExpr(data []byte, depth int) (int, bool, error) {
	if depth > maxSeStringDepth {
		return 0, false, fmt.Errorf("%w: 表达式嵌套层数超过上限 (%d)", ErrInvalidSeString, maxSeStringDepth)
	}
	if len(data) == 0 {
		return 0, false, fmt.Errorf("%w: 表达式意外截断", ErrInvalidSeString)
	}
	b := data[0]
	switch {
	case b >= 0x01 && b <= 0xCF:
		return 1, true, nil
	case b >= 0xD0 && b <= 0xDF:
		return 1, true, nil
	case b >= 0xE0 && b <= 0xE5:
		op1Len, ok1, err := validateSeExpr(data[1:], depth+1)
		if err != nil || !ok1 {
			return 0, false, err
		}
		op2Len, ok2, err := validateSeExpr(data[1+op1Len:], depth+1)
		if err != nil || !ok2 {
			return 0, false, err
		}
		return 1 + op1Len + op2Len, true, nil
	case b >= 0xE8 && b <= 0xEB:
		opLen, ok, err := validateSeExpr(data[1:], depth+1)
		if err != nil || !ok {
			return 0, false, err
		}
		return 1 + opLen, true, nil
	case b == 0xEC:
		return 1, true, nil
	case b >= 0xF0 && b <= 0xFE:
		extra, ok := seIntExtraBytes(b)
		if !ok {
			return 0, false, nil
		}
		if 1+extra > len(data) {
			return 0, false, fmt.Errorf("%w: 多字节整型 0x%02X 长度不足", ErrInvalidSeString, b)
		}
		return 1 + extra, true, nil
	case b == 0xFF:
		strLen, lenBytes, err := decodeSeInt(data[1:])
		if err != nil {
			return 0, false, fmt.Errorf("%w: 内嵌字符串长度非法: %v", ErrInvalidSeString, err)
		}
		if strLen < 0 || 1+lenBytes > len(data) || strLen > len(data)-1-lenBytes {
			return 0, false, fmt.Errorf("%w: 内嵌字符串长度越界（声明 %d 字节，可用 %d）",
				ErrInvalidSeString, strLen, len(data)-1-lenBytes)
		}
		inner := data[1+lenBytes : 1+lenBytes+strLen]
		if err := validateSeStringDepth(inner, depth+1); err != nil {
			return 0, false, err
		}
		return 1 + lenBytes + strLen, true, nil
	default:
		return 0, false, nil
	}
}

// FFXIV SeString 变长整型 0xF0..0xFE 按低 4 位掩码 mask = (b & 0x0F) + 1 编码，
// bits 3..0 分别指示 32 位大端整数的 4 个非零字节是否随附。
func seIntExtraBytes(b byte) (int, bool) {
	if b < 0xF0 || b > 0xFE {
		return 0, false
	}
	mask := (b & 0x0F) + 1
	count := 0
	for m := mask; m != 0; m &= m - 1 {
		count++
	}
	return count, true
}

func decodeSeInt(data []byte) (int, int, error) {
	if len(data) == 0 {
		return 0, 0, fmt.Errorf("缺少长度字节")
	}
	b := data[0]
	if b >= 0x01 && b <= 0xCF {
		return int(b) - 1, 1, nil
	}
	extra, ok := seIntExtraBytes(b)
	if !ok {
		return 0, 0, fmt.Errorf("非法长度编码字节 0x%02X", b)
	}
	if 1+extra > len(data) {
		return 0, 0, fmt.Errorf("变长整型 0x%02X 数据截断", b)
	}
	mask := (b & 0x0F) + 1
	var v uint32
	pos := 1
	if mask&0x08 != 0 {
		v |= uint32(data[pos]) << 24
		pos++
	}
	if mask&0x04 != 0 {
		v |= uint32(data[pos]) << 16
		pos++
	}
	if mask&0x02 != 0 {
		v |= uint32(data[pos]) << 8
		pos++
	}
	if mask&0x01 != 0 {
		v |= uint32(data[pos])
		pos++
	}
	return int(v), pos, nil
}
