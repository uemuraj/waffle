//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include <locale>
#include <format>
#include <iostream>
#include "waffle.h"

struct Callback
{
	long m_total;
	long m_index{ -1 };

	void operator()(long index, OperationResultCode code, IUpdate * update, IUpdateDownloadResult * result, IDownloadProgress * progress)
	{
		std::wcout << index << ' ' << update << progress << ' ' << result;
	}

	void operator()(long index, OperationResultCode code, IUpdate * update, IUpdateInstallationResult * result, IInstallationProgress * progress)
	{
		std::wcout << index << ' ' << update << progress << ' ' << result;
	}
};

int wmain()
{
	// https://learn.microsoft.com/ja-jp/windows/win32/api/wuapi/nf-wuapi-iupdatesearcher-search#remarks
	// https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/guidelines-for-asynchronous-wua-operations

	auto szCriteria = L"IsInstalled=0 and Type='Software' and IsHidden=0";
	auto msTimeout = 3 * 60 * 1000UL;

	try
	{
		std::locale::global(std::locale(""));

		std::wcout << std::format(L"Searching for updates... {} sec", msTimeout / 1000) << std::endl;

		auto session = waffle::CreateSession();
		auto updates = session.Search(_bstr_t(szCriteria), msTimeout);

		if (!updates.empty())
		{
			session.Download(updates, Callback{ updates.size() });
			session.Install(updates, Callback{ updates.size() });
		}

		if (!session.RebootRequired())
		{
			return 0;
		}

		std::wcout << L"Reboot Required." << std::endl;
		return 1;
	}
	catch (const std::exception & e)
	{
		std::wcout << e.what() << std::endl;
		return -1;
	}
}
