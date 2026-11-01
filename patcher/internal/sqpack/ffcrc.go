package sqpack

import "strings"

// ComputeCRC 移植自旧版 FFCRC.ComputeCRC。
//
// 注意：
//  1. 这不是标准 IEEE CRC32，最终结果没有做 ^0xFFFFFFFF 取反。
//  2. 初始值为 0xFFFFFFFF（Java 中的 -1）。
//  3. 计算路径 / 文件名 CRC 之前必须先转小写。
func ComputeCRC(data []byte) int32 {
	dwCRC := int32(-1)
	cbRunningLength := 0
	if len(data) >= 4 {
		cbRunningLength = len(data) / 4 * 4
	}

	for i := 0; i < cbRunningLength/4; i++ {
		v := int32(uint32(data[i*4]) |
			uint32(data[i*4+1])<<8 |
			uint32(data[i*4+2])<<16 |
			uint32(data[i*4+3])<<24)
		dwCRC ^= v
		dwCRC = crc_table_0f091d0[byte(dwCRC)] ^
			crc_table_0f08dd0[byte(dwCRC>>8)] ^
			crc_table_0f089d0[byte(dwCRC>>16)] ^
			crc_table_0f085d0[byte(dwCRC>>24)]
	}

	for i := cbRunningLength; i < len(data); i++ {
		dwCRC = crc_table_0f085d0[byte(dwCRC^int32(data[i]))] ^ int32(uint32(dwCRC)>>8)
	}
	return dwCRC
}

// ComputeCRCString 先转小写再计算 CRC，用于 SqPack 路径与文件名。
func ComputeCRCString(s string) int32 {
	return ComputeCRC([]byte(strings.ToLower(s)))
}
