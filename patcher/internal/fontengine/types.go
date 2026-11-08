package fontengine

// PresetSummary 预设简要信息
type PresetSummary struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	Description string `json:"description"`
	Builtin     bool   `json:"builtin"`
	// Size/UpdatedAt 只对自定义方案有意义（内置方案随程序发布，不展示文件元信息）。
	Size      int64  `json:"size,omitempty"`
	UpdatedAt string `json:"updatedAt,omitempty"`
}

// MultiFontSet 完整字体方案配置（与 FFXIV-FontChanger 完全兼容）
type MultiFontSet struct {
	ExportMapChnAxisToFont   bool       `json:"exportMapChnAxisToFont,omitempty"`
	ExportMapFontLobbyToFont bool       `json:"exportMapFontLobbyToFont,omitempty"`
	ExportMapKrnAxisToFont   bool       `json:"exportMapKrnAxisToFont,omitempty"`
	ExportMapTcAxisToFont    bool       `json:"exportMapTcAxisToFont,omitempty"`
	FontSets                 []*FontSet `json:"fontSets,omitempty"`
}

// FontSet 一组字体集合
type FontSet struct {
	DiscardStep       int      `json:"discardStep,omitempty"`
	ExpectedTexCount  int      `json:"expectedTexCount,omitempty"`
	SideLength        int      `json:"sideLength,omitempty"`
	TexFilenameFormat string   `json:"texFilenameFormat,omitempty"`
	Faces             []*Face  `json:"faces,omitempty"`
	Fonts             []*Face  `json:"fonts,omitempty"`
}

// Face 单个字体（如 AXIS_12, AXIS_18, Jupiter_16 等）
type Face struct {
	Name     string         `json:"name"`
	Elements []*FontElement `json:"elements"`
}

// FontElement 字体图层元素
type FontElement struct {
	Size             float64           `json:"size"`
	Gamma            float64           `json:"gamma"`
	Renderer         int               `json:"renderer"` // 0: Empty, 1: Game, 2: DirectWrite, 3: FreeType, 4: GlyphImages
	MergeMode        int               `json:"mergeMode"` // 0: Default, 1: Fallback, etc.
	Lookup           Lookup            `json:"lookup"`
	Transform        Transform         `json:"transform"`
	WrapModifiers    WrapModifiers     `json:"wrapModifiers"`
	RenderSpecific   RenderSpecific    `json:"renderSpecific"`
}

// Lookup 字体查找标识
type Lookup struct {
	Name     string   `json:"name"`
	Style    int      `json:"style"`
	Weight   int      `json:"weight"`
	Stretch  int      `json:"stretch"`
	Features []string `json:"features,omitempty"`
	Filename string   `json:"filename,omitempty"`
}

// Transform 变换参数
type Transform struct {
	Rotation float64 `json:"rotation"`
	ScaleX   float64 `json:"scaleX"`
	ScaleY   float64 `json:"scaleY"`
	Skew     float64 `json:"skew"`
}

// WrapModifiers 排版与字形修饰
type WrapModifiers struct {
	BaselineShift         float64            `json:"baselineShift"`
	HorizontalOffset      float64            `json:"horizontalOffset"`
	LetterSpacing         float64            `json:"letterSpacing"`
	Codepoints            [][]int            `json:"codepoints,omitempty"`
	CodepointReplacements map[string]string  `json:"codepointReplacements,omitempty"`
}

// RenderSpecific 渲染器特定参数
type RenderSpecific struct {
	DirectWrite DirectWriteSpecific `json:"directwrite"`
	FreeType    FreeTypeSpecific    `json:"freetype"`
	Empty       EmptySpecific       `json:"empty"`
}

type DirectWriteSpecific struct {
	GridFitMode int `json:"gridFitMode"`
	MeasureMode int `json:"measureMode"`
	RenderMode  int `json:"renderMode"`
}

type FreeTypeSpecific struct {
	ForceAutohint bool `json:"forceAutohint"`
	NoAutohint    bool `json:"noAutohint"`
	NoBitmap      bool `json:"noBitmap"`
	NoHinting     bool `json:"noHinting"`
	RenderMode    int  `json:"renderMode"`
}

type EmptySpecific struct {
	Ascent     float64 `json:"ascent"`
	LineHeight float64 `json:"lineHeight"`
}
