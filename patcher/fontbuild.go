package main

import (
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	goruntime "runtime"
	"strings"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/fontengine"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/replace"
)

// fontBuildVersion 在编译参数变化时递增，让旧产物自动作废。
const fontBuildVersion = 1

// FontBuildInfo 是界面判断「编译」按钮与开始汉化放行条件的依据。
type FontBuildInfo struct {
	Ready      bool   `json:"ready"`
	Reason     string `json:"reason"`
	PresetID   string `json:"presetId"`
	TexCount   int    `json:"texCount"`
	SideLength int    `json:"sideLength"`
	CompiledAt string `json:"compiledAt"`
}

// fontBuildMeta 与产物同目录存放，记录这份贴图是按哪套输入编译出来的。
// 任何一项不一致都说明产物已失效，必须重新编译。
type fontBuildMeta struct {
	Version    int    `json:"version"`
	PresetID   string `json:"presetId"`
	Content    string `json:"content"`
	Use16Bit   bool   `json:"use16Bit"`
	Compiler   string `json:"compiler"`
	CompiledAt string `json:"compiledAt"`
	TexCount   int    `json:"texCount"`
	SideLength int    `json:"sideLength"`
}

func fontBuildDir(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "fontbuild")
}

// fontBuildMetaPath 放在产物目录之外：产物目录要能整目录交给替换流程，
// 里面只能有贴图和 fdt，多一个 JSON 会被当成字体文件去索引里查而报错。
func fontBuildMetaPath(resourceDir string) string {
	return filepath.Join(dataRootFor(resourceDir), "fontbuild.json")
}

func hashText(s string) string {
	sum := sha256.Sum256([]byte(s))
	return hex.EncodeToString(sum[:])
}

// compilerStamp 用大小与修改时间标识编译器：换了组件，旧产物就不可信了。
func (a *App) compilerStamp() string {
	path, err := a.fontEngine.ResolveBinary()
	if err != nil || path == "" {
		return ""
	}
	st, err := os.Stat(path)
	if err != nil {
		return ""
	}
	return fmt.Sprintf("%d-%d", st.Size(), st.ModTime().Unix())
}

// resolveFontPresetContent 取当前区服在用的方案，做区服校验与边长约束。
// 编译按钮与汉化流程共用同一条路径，避免两边拿到的方案不一致。
func (a *App) resolveFontPresetContent(resourceDir, regionKey string, targetReg region.Region) (string, string, int, error) {
	presetID := a.getActiveFontPreset(regionKey)
	configJson, err := fontengine.GetPresetContent(presetID, presetsDirFor(resourceDir))
	if err != nil {
		defaultID := defaultPresetForRegionWithDirs(targetReg, a.loadConfig(resourceDir).GameDirs)
		a.emitPatchLog(fmt.Sprintf("[字体] 方案 %s 不可用（%v），改用默认方案 %s", presetID, err, defaultID))
		configJson, err = fontengine.GetPresetContent(defaultID, "")
		if err != nil {
			return "", "", 0, fmt.Errorf("读取字体配置失败: %w", err)
		}
		presetID = defaultID
	}
	if err := fontengine.ValidatePresetForRegion(configJson, targetReg); err != nil {
		return "", "", 0, fmt.Errorf("字体配置与当前区服不匹配: %w", err)
	}
	constrained, changed, err := constrainPresetForCompile(presetID, configJson)
	if err != nil {
		return "", "", 0, err
	}
	if changed > 0 {
		configJson = constrained
	}
	return presetID, configJson, changed, nil
}

// fontBuildInfoFor 判断某份方案内容是否已有可用产物。
func (a *App) fontBuildInfoFor(resourceDir, presetID, configJson string, use16Bit bool) FontBuildInfo {
	info := FontBuildInfo{PresetID: presetID}
	dir := fontBuildDir(resourceDir)

	data, err := os.ReadFile(fontBuildMetaPath(resourceDir))
	if err != nil {
		info.Reason = "尚未编译"
		return info
	}
	var meta fontBuildMeta
	if err := json.Unmarshal(data, &meta); err != nil {
		info.Reason = "编译记录损坏，请重新编译"
		return info
	}

	switch {
	case meta.Version != fontBuildVersion:
		info.Reason = "编译格式已更新，请重新编译"
	case meta.PresetID != presetID:
		info.Reason = "方案已变更，请重新编译"
	case meta.Content != hashText(configJson):
		info.Reason = "方案内容已变更，请重新编译"
	case meta.Use16Bit != use16Bit:
		info.Reason = "贴图位数设置已变更，请重新编译"
	case meta.Compiler != a.compilerStamp():
		info.Reason = "字体编译组件已更新，请重新编译"
	case !hasFontTexture(dir):
		info.Reason = "编译产物不完整，请重新编译"
	default:
		info.Ready = true
		info.TexCount = meta.TexCount
		info.SideLength = meta.SideLength
		info.CompiledAt = meta.CompiledAt
	}
	return info
}

// currentFontBuild 按当前区服与配置计算编译状态。
func (a *App) currentFontBuild(resourceDir string) FontBuildInfo {
	lang, _ := a.resolveLanguageAndGameDir("")
	presetID, configJson, _, err := a.resolveFontPresetContent(resourceDir, normalizeRegionCode(string(lang)), region.Of(lang))
	if err != nil {
		return FontBuildInfo{Reason: err.Error()}
	}
	return a.fontBuildInfoFor(resourceDir, presetID, configJson, a.fontTexture16Bit(resourceDir))
}

// GetFontBuildInfo 供界面刷新「编译」按钮与开始汉化的可用状态。
func (a *App) GetFontBuildInfo() FontBuildInfo {
	return a.currentFontBuild(a.resourceDir())
}

// requireFontBuild 在写入客户端之前确认自定义方案已经编译好。
func (a *App) requireFontBuild(resourceDir string) (string, error) {
	info := a.currentFontBuild(resourceDir)
	if !info.Ready {
		return "", fmt.Errorf("字体方案尚未编译完成：%s。请回到工作台，在字体来源处执行「编译」，通过后再开始汉化", info.Reason)
	}
	return fontBuildDir(resourceDir), nil
}

// CompileFontPreset 把当前方案编译成字体贴图，产物留在本地等汉化直接使用。
//
// 编译独立于汉化：字形有问题、贴图超限都在这一步暴露，不会等文本汉化跑完才报错。
func (a *App) CompileFontPreset() (FontBuildInfo, error) {
	var info FontBuildInfo
	if goruntime.GOOS != "windows" {
		return info, errors.New("字体编译组件仅支持 Windows，当前系统无法编译")
	}
	if err := a.beginBusy(); err != nil {
		return info, err
	}
	defer a.endBusy()

	ctx := context.Background()
	if a.ctx != nil {
		ctx = a.ctx
	}

	resourceDir := a.resourceDir()
	lang, gameDir := a.resolveLanguageAndGameDir("")
	targetReg := region.Of(lang)
	regionKey := normalizeRegionCode(string(lang))

	a.syncFontEngineDirs(ctx, resourceDir, gameDir)
	if err := a.ensureFontCompiler(ctx, resourceDir); err != nil {
		return info, fmt.Errorf("字体编译组件不可用：%w", err)
	}

	presetID, configJson, changed, err := a.resolveFontPresetContent(resourceDir, regionKey, targetReg)
	if err != nil {
		return info, err
	}
	if changed > 0 {
		a.emitPatchLog(fmt.Sprintf("[字体] 方案里有 %d 处贴图边长不是 %dpx，已统一为 %dpx", changed, fontengine.SideLength, fontengine.SideLength))
	}

	use16Bit := a.fontTexture16Bit(resourceDir)
	dir := fontBuildDir(resourceDir)
	tmpDir := dir + ".tmp"
	if err := os.RemoveAll(tmpDir); err != nil {
		return info, fmt.Errorf("清理上次编译残留失败: %w", err)
	}
	if err := os.MkdirAll(tmpDir, 0o755); err != nil {
		return info, fmt.Errorf("创建编译目录失败: %w", err)
	}

	a.emitPatchLog("[字体] 正在编译方案...")
	if err := a.fontEngine.CompileRaw(ctx, configJson, tmpDir, use16Bit, func(pct int, msg string) {
		a.emitFontBuildProgress(int64(pct), msg)
	}); err != nil {
		_ = os.RemoveAll(tmpDir)
		return info, fmt.Errorf("字体编译失败：%w", err)
	}
	if !hasFontTexture(tmpDir) {
		_ = os.RemoveAll(tmpDir)
		return info, errors.New("编译没有产出字体贴图，已中止")
	}

	// 编译完就按客户端索引核对张数上限：超限在这里报出来，别等写客户端。
	if gameDir != "" {
		fontIndexPath := filepath.Join(gameDir, "game", "sqpack", "ffxiv", "000000.win32.index")
		if _, statErr := os.Stat(fontIndexPath); statErr == nil {
			if err := replace.CheckFontFiles(fontIndexPath, tmpDir); err != nil {
				_ = os.RemoveAll(tmpDir)
				return info, err
			}
		}
	}

	texCount, side := fontTextureStats(tmpDir)
	meta := fontBuildMeta{
		Version:    fontBuildVersion,
		PresetID:   presetID,
		Content:    hashText(configJson),
		Use16Bit:   use16Bit,
		Compiler:   a.compilerStamp(),
		CompiledAt: time.Now().Format(time.RFC3339),
		TexCount:   texCount,
		SideLength: side,
	}
	raw, err := json.Marshal(meta)
	if err != nil {
		_ = os.RemoveAll(tmpDir)
		return info, fmt.Errorf("写入编译记录失败: %w", err)
	}

	// 新产物写好后才替换旧目录，编译失败时上一份仍可直接使用。
	if err := os.RemoveAll(dir); err != nil {
		_ = os.RemoveAll(tmpDir)
		return info, fmt.Errorf("替换旧编译产物失败: %w", err)
	}
	if err := os.Rename(tmpDir, dir); err != nil {
		_ = os.RemoveAll(tmpDir)
		return info, fmt.Errorf("保存编译产物失败: %w", err)
	}
	// 记录写在目录之外：这一步失败时贴图还在但没有记录，界面判成未编译，不会被误用。
	if err := os.WriteFile(fontBuildMetaPath(resourceDir), raw, 0o644); err != nil {
		return info, fmt.Errorf("写入编译记录失败: %w", err)
	}

	a.emitPatchLog(fmt.Sprintf("[字体] 编译完成：%d 张贴图，边长 %dpx", texCount, side))
	return a.fontBuildInfoFor(resourceDir, presetID, configJson, use16Bit), nil
}

func (a *App) emitFontBuildProgress(done int64, detail string) {
	if a.ctx == nil || a.shouldThrottleProgress("font-build", detail, done, 100) {
		return
	}
	runtime.EventsEmit(a.ctx, "font-build-progress", ProgressEvent{
		Stage: "font-build", Detail: detail, Done: done, Total: 100,
	})
}

// fontTextureStats 统计目录里的贴图数量与最大边长。
func fontTextureStats(dir string) (count, maxSide int) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return 0, 0
	}
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".tex") {
			continue
		}
		f, err := os.Open(filepath.Join(dir, entry.Name()))
		if err != nil {
			continue
		}
		var head [16]byte
		_, readErr := io.ReadFull(f, head[:])
		_ = f.Close()
		if readErr != nil {
			continue
		}
		count++
		if w := int(binary.LittleEndian.Uint16(head[8:])); w > maxSide {
			maxSide = w
		}
		if h := int(binary.LittleEndian.Uint16(head[10:])); h > maxSide {
			maxSide = h
		}
	}
	return count, maxSide
}
