package text

import (
	"errors"
	"fmt"
)

var (
	// ErrInvalidSeString 表示编码后的字节流不符合 FFXIV SeString 二进制结构。
	ErrInvalidSeString = errors.New("SeString 控制码结构非法")
)

// ValidateSeString 校验字节流中的 FFXIV SeString 控制码（0x02 ... 0x03）
// 与内嵌字符串表达式（0xFF ...）长度是否与实际边界匹配，防止越界或截断导致游戏崩溃。
func ValidateSeString(data []byte) error {
	for i := 0; i < len(data); {
		b := data[i]
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
		bodyEnd := bodyStart + bodyLen
		if bodyEnd >= len(data) {
			return fmt.Errorf("%w: 偏移 %d 处控制码 0x%02X 长度过长（声明 %d 字节，总长仅 %d）",
				ErrInvalidSeString, i, macroCode, bodyLen, len(data)-bodyStart)
		}
		if data[bodyEnd] != 0x03 {
			return fmt.Errorf("%w: 偏移 %d 处控制码 0x%02X 长度不匹配（声明 %d 字节，末尾字节为 0x%02X 而非 0x03）",
				ErrInvalidSeString, i, macroCode, bodyLen, data[bodyEnd])
		}

		if macroCode != 0x22 { // Format 指令末尾含非表达式原始字节，只校验外层包络
			if err := validateMacroBody(data[bodyStart:bodyEnd], macroCode); err != nil {
				return err
			}
		}

		i = bodyEnd + 1
	}
	return nil
}

func validateMacroBody(body []byte, macroCode byte) error {
	pos := 0
	for pos < len(body) {
		consumed, ok, err := validateSeExpr(body[pos:])
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

func validateSeExpr(data []byte) (int, bool, error) {
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
		op1Len, ok1, err := validateSeExpr(data[1:])
		if err != nil || !ok1 {
			return 0, false, err
		}
		op2Len, ok2, err := validateSeExpr(data[1+op1Len:])
		if err != nil || !ok2 {
			return 0, false, err
		}
		return 1 + op1Len + op2Len, true, nil
	case b >= 0xE8 && b <= 0xEB:
		opLen, ok, err := validateSeExpr(data[1:])
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
		if strLen < 0 || 1+lenBytes+strLen > len(data) {
			return 0, false, fmt.Errorf("%w: 内嵌字符串长度越界（声明 %d 字节，可用 %d）",
				ErrInvalidSeString, strLen, len(data)-1-lenBytes)
		}
		inner := data[1+lenBytes : 1+lenBytes+strLen]
		if err := ValidateSeString(inner); err != nil {
			return 0, false, err
		}
		return 1 + lenBytes + strLen, true, nil
	default:
		return 0, false, nil
	}
}

func seIntExtraBytes(b byte) (int, bool) {
	switch b {
	case 0xF0:
		return 1, true
	case 0xF1, 0xF2, 0xF4:
		return 2, true
	case 0xF5, 0xF6, 0xFA, 0xFD:
		return 3, true
	case 0xFE:
		return 4, true
	default:
		return 0, false
	}
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
	switch b {
	case 0xF0:
		return int(data[1]), 2, nil
	case 0xF1:
		return (int(data[1])<<8 | int(data[2])) - 1, 3, nil
	case 0xF2, 0xF4:
		return int(data[1])<<8 | int(data[2]), 3, nil
	case 0xF5:
		return (int(data[1])<<16 | int(data[2])<<8 | int(data[3])) - 1, 4, nil
	case 0xF6:
		return int(data[1])<<16 | int(data[2])<<8 | int(data[3]), 4, nil
	case 0xFA:
		var v int
		if data[1] != 0xFF {
			v |= int(data[1]) << 16
		}
		if data[2] != 0xFF {
			v |= int(data[2]) << 8
		}
		if data[3] != 0xFF {
			v |= int(data[3])
		}
		return v, 4, nil
	case 0xFD:
		return (int(data[1])<<16 | int(data[2])<<8 | int(data[3])) << 8, 4, nil
	case 0xFE:
		return int(data[1])<<24 | int(data[2])<<16 | int(data[3])<<8 | int(data[4]), 5, nil
	default:
		return 0, 0, fmt.Errorf("非法长度编码字节 0x%02X", b)
	}
}
