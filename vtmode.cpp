#include "vtmode.h"

#include <iostream>
#include <system_error>

VirtualTerminalMode::VirtualTerminalMode()
{
	m_cout = ::GetStdHandle(STD_OUTPUT_HANDLE);

	if (m_cout == INVALID_HANDLE_VALUE)
	{
		throw std::system_error(::GetLastError(), std::system_category(), "GetStdHandle()");
	}

	if (!::GetConsoleMode(m_cout, &m_mode))
	{
		throw std::system_error(::GetLastError(), std::system_category(), "GetConsoleMode()");
	}

	if (!::SetConsoleMode(m_cout, m_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
	{
		throw std::system_error(::GetLastError(), std::system_category(), "SetConsoleMode()");
	}
}

VirtualTerminalMode::~VirtualTerminalMode() noexcept
{
	std::wcout << L"\x1b[!p";

	::SetConsoleMode(m_cout, m_mode);
}
