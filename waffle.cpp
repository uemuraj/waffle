//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include <locale>
#include <iostream>
#include <system_error>

#include "waffle.h"

using namespace waffle;

int main()
{
	try
	{
		std::locale::global(std::locale(""));

		ComInitialized com;

		com_ptr_t<IUpdateSession> session;

		if (auto hr = session.CreateInstance("Microsoft.Update.Session"); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		if (auto hr = session->put_ClientApplicationID(_bstr_t(L"waffle")); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		com_ptr_t<IUpdateSearcher> searcher;

		if (auto hr = session->CreateUpdateSearcher(&searcher); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		std::wcerr << L"Searching for updates..." << std::endl;

		com_ptr_t<ISearchResult> result;

		_bstr_t criteria(L"IsInstalled=0 and Type='Software' and IsHidden=0");

		if (auto hr = searcher->Search(criteria, &result); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		com_ptr_t<IUpdateCollection> updates;

		if (auto hr = result->get_Updates(&updates); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		LONG count = 0;

		if (auto hr = updates->get_Count(&count); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		if (count > 0)
		{
			std::wcerr << L"List of applicable items found on the machine:" << std::endl;

			for (LONG index = 0; index < count; ++index)
			{
				com_ptr_t<IUpdate> update;

				if (auto hr = updates->get_Item(index, &update); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), __func__);
				}

				std::wcout << (index + 1) << L"> " << update << std::endl;
			}
		}
	}
	catch (const std::exception & e)
	{
		std::wstring what;

		if (int cch = ::MultiByteToWideChar(CP_THREAD_ACP, 0, e.what(), -1, nullptr, 0); cch > 0)
		{
			what.resize(cch);

			::MultiByteToWideChar(CP_THREAD_ACP, 0, e.what(), -1, what.data(), cch);
		}

		std::wcerr << what << std::endl;
	}
}

namespace waffle
{
	ComInitialized::ComInitialized()
	{
		if (auto hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}
	}

	ComInitialized::~ComInitialized()
	{
		::CoUninitialize();
	}

	std::wostream & operator<<(std::wostream & out, com_ptr_t<IUpdate> & update)
	{
		_bstr_t title;

		if (auto hr = update->get_Title(title.GetAddress()); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		com_ptr_t<IUpdateIdentity> identity;

		if (auto hr = update->get_Identity(&identity); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		_bstr_t id;

		if (auto hr = identity->get_UpdateID(id.GetAddress()); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		LONG num = 0;

		if (auto hr = identity->get_RevisionNumber(&num); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		out << (const wchar_t *) title << L" {" << (const wchar_t *) id << L'.' << num << L'}';

		VARIANT_BOOL hidden = VARIANT_FALSE;

		if (auto hr = update->get_IsHidden(&hidden); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		if (hidden == VARIANT_TRUE)
		{
			out << L" (hidden)";
		}

		com_ptr_t<IStringCollection> articles;

		if (auto hr = update->get_KBArticleIDs(&articles); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		LONG count = 0;

		if (auto hr = articles->get_Count(&count); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		if (count > 0)
		{
			out << L" (";

			for (LONG index = 0; index < count; ++index)
			{
				_bstr_t article;

				if (auto hr = articles->get_Item(index, article.GetAddress()); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), __func__);
				}

				if (index > 0)
				{
					out << L',';
				}

				out << L"KB" << (const wchar_t *) article;
			}

			out << L')';
		}

		DeploymentAction action{};

		if (auto hr = update->get_DeploymentAction(&action); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __func__);
		}

		return out << L" Deployment action: " << action;
	}

	std::wostream & operator<<(std::wostream & out, DeploymentAction action)
	{
		switch (action)
		{
		case daNone:
			return out << L"None (Inherit)";
		case daInstallation:
			return out << L"Installation";
		case daUninstallation:
			return out << L"Uninstallation";
		case daDetection:
			return out << L"Detection";
		case daOptionalInstallation:
			return out << L"Optional Installation";
		default:
			return out << L"Unexpected (" << action << L')';
		}
	}
}
