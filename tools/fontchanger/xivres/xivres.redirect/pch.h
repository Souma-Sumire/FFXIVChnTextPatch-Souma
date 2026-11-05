#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800

#include <array>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <intrin.h>
#include <map>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <thread>

#include <Windows.h>

#include <dinput.h>
#include <PathCch.h>
#include <Psapi.h>
#include <winioctl.h>

#include <MinHook.h>
#include <srell.hpp>

extern HMODULE g_hModule;
