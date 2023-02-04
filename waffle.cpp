//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include <locale>
#include <iostream>
#include "waffle.h"

int main()
{
	using namespace waffle;

	try
	{
		std::locale::global(std::locale(""));

		ComInitialize com;

		Session session;

		if (auto appName = NamedParameter(L"AppName"); appName == nullptr)
		{
			session.SetClientApplicationID(L"waffle");
		}
		else
		{
			session.SetClientApplicationID(appName);
		}

		std::wcerr << L"Searching for updates..." << std::endl;

		Searcher searcher(session);

		auto criteria = NamedParameter(L"Criteria");

		auto result = searcher.Search(criteria ? criteria : L"IsInstalled=0 and Type='Software' and IsHidden=0");

		if (!result.empty())
		{
			std::wcerr << L"List of applicable items found on the machine:" << std::endl;

			int index = 0;

			for (auto update : result)
			{
				std::wcout << ++index << L"> " << update << std::endl;
			}
		}
		return 0;
	}
	catch (const std::exception & e)
	{
		std::wcerr << e << std::endl;
		return 1;
	}
}
