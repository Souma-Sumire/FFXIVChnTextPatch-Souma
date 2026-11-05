#pragma once

namespace App {
	// Renders a text with a font of a configuration and saves it as a PNG file, when the arguments ask to:
	//
	//   FontChanger.FontEditor64.exe <config.json> --render-text <text> --output <image.png> [--font <name>] [--max-width <px>]
	//
	// In the text, \n starts a new line, and \\ is a backslash. The font is the first of the configuration if not given.
	//
	// Also exports the glyphs of a font, or of one of its elements by its index, as files:
	//
	//   FontChanger.FontEditor64.exe <config.json> --export-glyphs <folder> [--font <name>] [--element <index> [--with-adjustments]] [--no-svg] [--no-png]
	//
	// Returns the exit code of the process, or nothing if the arguments do not ask for rendering.
	std::optional<int> RunCommandLineRender(const std::vector<std::wstring>& args);
}
