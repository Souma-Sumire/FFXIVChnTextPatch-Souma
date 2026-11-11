//go:build windows

package main

import (
	"sync"

	"github.com/wailsapp/wails/v3/pkg/application"
	"github.com/wailsapp/wails/v3/pkg/events"
	"github.com/wailsapp/wails/v3/pkg/w32"
)

// Wails v3 仅在创建窗口时设置了 WM_SETICON(ICON_BIG)，此处补充设置 ICON_SMALL，
// 确保标题栏左上角使用清晰的小图标且不依赖资源回退链。
func setupWindowIcon(win *application.WebviewWindow, iconData []byte) {
	if win == nil || len(iconData) == 0 {
		return
	}
	var once sync.Once
	win.OnWindowEvent(events.Common.WindowShow, func(_ *application.WindowEvent) {
		once.Do(func() {
			hwnd := w32.HWND(win.NativeWindow())
			if hwnd == 0 {
				return
			}
			if smallIcon, err := w32.CreateSmallHIconFromImage(iconData); err == nil && smallIcon != 0 {
				w32.SetWindowIcon(hwnd, smallIcon)
			}
		})
	})
}
