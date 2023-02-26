#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class VirtualTerminalMode
{
	HANDLE m_cout;
	DWORD m_mode;

public:
	VirtualTerminalMode();
	~VirtualTerminalMode() noexcept;
};
