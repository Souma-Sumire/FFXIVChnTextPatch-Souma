#pragma once

namespace App {
	class BaseWindow {
		static std::set<BaseWindow*> s_windows;

	public:
		BaseWindow();

		virtual ~BaseWindow();

		virtual bool ConsumeDialogMessage(MSG& msg) = 0;

		virtual bool ConsumeAccelerator(MSG& msg) = 0;

		static bool ConsumeMessage(MSG& msg);
	};
}
