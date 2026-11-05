#pragma once

#include <initializer_list>

#include <Windows.h>

namespace FontChanger {
	HRESULT SuccessOrThrow(HRESULT hr, std::initializer_list<HRESULT> acceptables = {});
}
