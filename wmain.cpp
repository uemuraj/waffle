#include <locale>
#include <iostream>
#include "waffle.h"

// TODO: 仮想端末のエスケープシーケンスを利用する？

void DownloadCallback(long index, IUpdate * update, IDownloadProgress * progress)
{
	std::wcout << index << L':' << progress << L' ' << update << std::endl;
}

void InstallCallback(long index, IUpdate * update, IInstallationProgress * progress)
{
	std::wcout << index << L':' << progress << L' ' << update << std::endl;
}

int wmain()
{
	// https://learn.microsoft.com/ja-jp/windows/win32/api/wuapi/nf-wuapi-iupdatesearcher-search#remarks
	// https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/guidelines-for-asynchronous-wua-operations

	auto criteria = L"IsInstalled=0 and Type='Software' and IsHidden=0";
	auto timeout = 1000 * 60 * 3UL;

	try
	{
		std::locale::global(std::locale(""));

		auto session = waffle::CreateSession();
		auto updates = session.Search(criteria, timeout);

		if (!updates.empty())
		{
			session.Download(updates, DownloadCallback);
			session.Install(updates, InstallCallback);
		}

		if (!session.RebootRequired())
		{
			return 0;
		}

		std::wcerr << L"Reboot Required." << std::endl;
		return 1;
	}
	catch (const std::exception & e)
	{
		std::wcerr << e.what() << std::endl;
		return -1;
	}
}
