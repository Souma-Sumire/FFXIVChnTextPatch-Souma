//go:build !windows

package fontengine

// ListSystemFonts 非 Windows 环境返回基础字体推荐列表
func ListSystemFonts() ([]string, error) {
	return []string{
		"PingFang SC",
		"Hiragino Sans GB",
		"Source Han Sans SC",
		"WenQuanYi Micro Hei",
		"Arial",
		"Helvetica",
	}, nil
}
