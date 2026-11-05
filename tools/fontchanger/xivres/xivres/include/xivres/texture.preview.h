#ifndef XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_
#define XIVRES_INTERNAL_TEXTUREPREVIEW_WINDOWS_H_
#ifdef _WINDOWS_

#include "texture.stream.h"

namespace xivres::texture {
	void preview(const stream& texStream, std::wstring title = L"Preview");
}

#endif
#endif
