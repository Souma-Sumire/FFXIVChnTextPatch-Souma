#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <fstream>
#include <mutex>
#include <numbers>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>

#include <Windows.h>

#include <comdef.h>
#include <dwrite.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_BITMAP_H
#include FT_OUTLINE_H
#include FT_GLYPH_H

#include <nlohmann/json.hpp>

#include "FontChanger.FixedSizeFont/directwrite_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/fontdata_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/fontdata_packer.h"
#include "FontChanger.FixedSizeFont/freetype_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/merged_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/text_measurer.h"
#include "FontChanger.FixedSizeFont/glyph_merging_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/image_fixed_size_font.h"
#include "FontChanger.FixedSizeFont/wrapping_fixed_size_font.h"
#include "xivres/fontdata.h"
#include "xivres/installation.h"
#include "xivres/texture.h"
#include "xivres/texture.mipmap_stream.h"
#include "xivres/util.on_dtor.h"
#include "xivres/util.pixel_formats.h"

#include "FontChanger.Presets/HResult.h"

using FontChanger::SuccessOrThrow;
