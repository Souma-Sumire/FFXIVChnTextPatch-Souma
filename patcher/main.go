package main

import (
	"embed"
	"log"

	"github.com/wailsapp/wails/v3/pkg/application"
	"github.com/wailsapp/wails/v3/pkg/events"
)

//go:embed all:frontend/dist
var assets embed.FS

func main() {
	appIcon, _ := assets.ReadFile("frontend/dist/appicon.png")
	app := NewApp()
	wailsApp := application.New(application.Options{
		Name:        "FFXIV 汉化器",
		Description: "最终幻想14 国际服汉化器",
		Icon:        appIcon,
		Services: []application.Service{
			application.NewService(app),
		},
		Assets: application.AssetOptions{
			Handler: application.AssetFileServerFS(assets),
		},
		Mac: application.MacOptions{
			ApplicationShouldTerminateAfterLastWindowClosed: true,
		},
	})

	mainWin := wailsApp.Window.NewWithOptions(application.WebviewWindowOptions{
		Name:             "main",
		Title:            "FFXIV 汉化器",
		Width:            1060,
		Height:           720,
		MinWidth:         840,
		MinHeight:        580,
		InitialPosition:  application.WindowCentered,
		BackgroundColour: application.NewRGB(22, 23, 26),
		Windows: application.WindowsWindow{
			Theme:       application.Dark,
			DisableMenu: true,
		},
	})
	setupWindowIcon(mainWin, appIcon)

	mainWin.RegisterHook(events.Common.WindowClosing, func(_ *application.WindowEvent) {
		app.BeginQuit()
	})

	app.SetRuntime(wailsApp, mainWin)

	if err := wailsApp.Run(); err != nil {
		log.Fatal(err)
	}
}
