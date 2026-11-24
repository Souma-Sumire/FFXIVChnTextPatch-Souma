package main

import (
	"context"
	"embed"
	"io/fs"
	"log"
	"runtime"

	"github.com/egoist/mygo"
)

//go:embed all:frontend/dist
var assets embed.FS

func main() {
	distFS, err := fs.Sub(assets, "frontend/dist")
	if err != nil {
		log.Fatal(err)
	}
	appIcon, _ := fs.ReadFile(distFS, "appicon.png")

	mygo.App.SetName("FFXIV 汉化器")
	mygo.App.SetVersion(appVersion)
	mygo.Theme.SetSource(mygo.ThemeDark)
	if runtime.GOOS == "windows" {
		mygo.App.SetMenu(nil)
	}
	mygo.SetFrontend(distFS)

	app := NewApp()
	mygo.Bind(app)
	mygo.App.OnBeforeQuit(func(_ *mygo.QuitEvent) {
		app.prepareQuit()
	})
	mygo.App.OnQuit(func() {
		app.prepareQuit()
	})

	mygo.App.WhenReady(func() {
		appCtx, appCancel := context.WithCancel(context.Background())
		mainWin := mygo.NewWindow(mygo.WindowOptions{
			Title:           "FFXIV 汉化器",
			URL:             "/",
			Width:           1060,
			Height:          720,
			MinWidth:        840,
			MinHeight:       580,
			BackgroundColor: "#16171a",
			Page: mygo.PageOptions{
				DevTools: mygo.DevToolsAuto,
			},
		})
		if len(appIcon) > 0 {
			_ = mainWin.SetIcon(appIcon)
		}
		mainWin.OnClose(func(_ *mygo.CloseEvent) {
			app.prepareQuit()
		})
		app.initRuntime(appCtx, appCancel, mainWin, appIcon)
	})

	if err := mygo.App.Run(); err != nil {
		log.Fatal(err)
	}
}
