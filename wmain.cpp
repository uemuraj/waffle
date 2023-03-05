//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include <locale>
#include <format>
#include <iostream>
#include "waffle.h"

std::wstring FormatToatalBytes(auto bytes, auto total)
{
	const auto KB = 1024ULL;
	const auto MB = 1024 * KB;
	const auto GB = 1024 * MB;

	if (auto value = (double) total / KB; value < 9.95L)
		return std::format(L"{:.1F}/{:.1F}KB ", (double) bytes / KB, value); // 9.9KB
	else if (value < 10.0L)
		return std::format(L"{:2d}/10KB ", bytes / KB); // !!!

	if (total <= (99 * KB))
		return std::format(L"{:2d}/{:d}KB ", bytes / KB, total / KB); // 99KB 

	if (total <= (999 * KB))
		return std::format(L"{:3d}/{:d}KB ", bytes / KB, total / KB); // 999KB 

	if (auto value = (double) total / MB; value < 9.95L)
		return std::format(L"{:.1F}/{:.1F}MB ", (double) bytes / MB, value); // 9.9MB 
	else if (value < 10.0L)
		return std::format(L"{:2d}/10MB ", bytes / MB); // !!!

	if (total <= (99 * MB))
		return std::format(L"{:2d}/{:d}MB ", bytes / MB, total / MB); // 99MB 

	if (total <= (999 * MB))
		return std::format(L"{:3d}/{:d}MB ", bytes / MB, total / MB); // 999MB 

	return std::format(L"{:.1F}/{:.1F}GB ", (double) bytes / GB, (double) total / GB); // 9.9GB 
}

struct Callback
{
	void operator()(long index, OperationResultCode code, IUpdate * update, IUpdateDownloadResult * result, IDownloadProgress * progress)
	{
		if (code >= orcSucceeded)
		{
			auto [total, bytes] = waffle::GetTotalBytes(progress);

			if (code == orcSucceeded)
				std::wcout << FormatToatalBytes(bytes, total) << update << std::endl;
			else
				std::wcout << FormatToatalBytes(bytes, total) << update << L' ' << result << std::endl;
		}
	}

	void operator()(long index, OperationResultCode code, IUpdate * update, IUpdateInstallationResult * result, IInstallationProgress * progress)
	{
		if (code >= orcSucceeded)
		{
			auto percent = waffle::GetPercentComplete(progress);

			if (code == orcSucceeded)
				std::wcout << std::format(L"{:3d}% ", percent) << update << std::endl;
			else
				std::wcout << std::format(L" {:3d}% ", percent) << update << L' ' << result << std::endl;
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

		std::wcout << std::format(L"Searching for updates... {} sec", msTimeout / 1000) << std::endl;

		auto session = waffle::CreateSession();
		auto updates = session.Search(_bstr_t(szCriteria), msTimeout);

		if (!updates.empty())
		{
			session.Download(updates, Callback{});
			session.Install(updates, Callback{});
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
