//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include <locale>
#include <format>
#include <iostream>
#include "waffle.h"
#include "vtmode.h"

template<class Result, class Progress>
struct Callback
{
	long m_total;
	long m_index{ -1 };

	void operator()(long index, OperationResultCode code, IUpdate * update, Result * result, Progress * progress)
	{
		if (m_index < index)
		{
			m_index = index;

			std::cout << update;
		}

		switch (code)
		{
		case orcNotStarted:
		case orcSucceeded:
		case orcInProgress:
			std::cout << "\b\b\b\b" << progress << std::endl;
			break;
		default:
			std::cout << "\b\b\b\b" << result << std::endl;
			break;
		}
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

//		VirtualTerminalMode mode;

		std::cout << std::format("Searching for updates... {} sec", msTimeout / 1000) << std::endl;

		auto session = waffle::CreateSession();
		auto updates = session.Search(_bstr_t(szCriteria), msTimeout);

		if (!updates.empty())
		{
			session.Download(updates, Callback<IUpdateDownloadResult, IDownloadProgress>{ updates.size() });
			session.Install(updates, Callback<IUpdateInstallationResult, IInstallationProgress>{ updates.size() });
		}

		if (!session.RebootRequired())
		{
			return 0;
		}

		std::cout << "Reboot Required." << std::endl;
		return 1;
	}
	catch (const std::exception & e)
	{
		std::cout << e.what() << std::endl;
		return -1;
	}
}
