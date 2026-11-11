//go:build !windows

package main

import "github.com/wailsapp/wails/v3/pkg/application"

func setupWindowIcon(_ *application.WebviewWindow, _ []byte) {}
