#include <locale>
#include <iostream>
#include "waffle.h"

template<class T>
struct Callback
{
	long m_total;
	long m_index{ -1 };

	void operator()(long index, IUpdate * update, T * progress)
	{
		if (m_index < index)
		{
			m_index = index++;

			std::wcout << index << L':' << progress << L' ' << update << std::endl;
		}
		else
		{
			// TODO: 仮想端末のエスケープシーケンスを利用して１行毎に引き戻す
			std::wcout << index << L':' << progress << L' ' << update << std::endl;
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

		auto session = waffle::CreateSession();
		auto updates = session.Search(_bstr_t(szCriteria), msTimeout);

		if (!updates.empty())
		{
			session.Download(updates, Callback<IDownloadProgress>{ updates.size() });
			session.Install(updates, Callback<IInstallationProgress>{ updates.size() });
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
