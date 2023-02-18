//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include "waffle.h"

#define MACRO_SOURCE_LOCATION() __FILE__ "(" _CRT_STRINGIZE(__LINE__) ")"

namespace waffle
{
	struct ComInitialized
	{
		ComInitialized()
		{
			if (auto hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
			}
		}

		~ComInitialized()
		{
			::CoUninitialize();
		}
	};

	Session CreateSession()
	{
		static thread_local ComInitialized com;

		return Session();
	}

	Updates::Updates(com_ptr_t<ISearchResult> result) : m_count(0)
	{
		if (auto hr = result->get_Updates(&m_updates); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (auto hr = m_updates->get_Count(&m_count); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}
	}

	Session::Session() : m_rebootRequired(false)
	{
		if (auto hr = m_session.CreateInstance("Microsoft.Update.Session"); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (auto hr = m_session->put_ClientApplicationID(_bstr_t("waffle")); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}
	}

	Updates Session::Search(BSTR criteria, unsigned long timeout)
	{
		com_ptr_t<IUpdateSearcher> searcher;

		if (auto hr = m_session->CreateUpdateSearcher(&searcher); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		Asynchronous asynchronous(&IUpdateSearcher::BeginSearch, &IUpdateSearcher::EndSearch, &ISearchCompletedCallback::Invoke);

		return asynchronous.Wait(timeout, searcher, criteria);
	}

	void Session::Download(Updates & updates, DownloadCallback callback)
	{
		com_ptr_t<IUpdateDownloader> donwloader;

		if (auto hr = m_session->CreateUpdateDownloader(&donwloader); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (auto hr = donwloader->put_Updates(updates); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		Asynchronous asynchronous(&IUpdateDownloader::BeginDownload, &IUpdateDownloader::EndDownload, &IDownloadProgressChangedCallback::Invoke);

		auto result = asynchronous.Wait(INFINITE, donwloader, DownloadProgressChangedCallback{ updates, callback });

		// TODO: ダウンロードの結果を確認して異常があれば例外を投げる
	}

	void Session::Install(Updates & updates, InstallationCallback callback)
	{
		// TODO: Wait を上手く書きたいとこ
	}

	bool Session::RebootRequired()
	{
		// TODO: 自分でダウンロードしていないくても、再起動が必要な状態か調べておく必要がある
		return m_rebootRequired;
	}

	CompleteEvent::CompleteEvent()
	{
		m_event = ::CreateEvent(nullptr, true, false, nullptr);

		if (m_event == nullptr)
		{
			throw std::system_error(::GetLastError(), std::system_category(), MACRO_SOURCE_LOCATION());
		}
	}

	CompleteEvent::~CompleteEvent()
	{
		::CloseHandle(m_event);
	}

	void CompleteEvent::Wait(unsigned long timeout)
	{
		auto wait = ::WaitForSingleObject(m_event, timeout);

		if (wait == WAIT_TIMEOUT)
		{
			throw std::runtime_error("timeout");
		}

		if (wait != WAIT_OBJECT_0)
		{
			throw std::system_error(::GetLastError(), std::system_category(), MACRO_SOURCE_LOCATION());
		}
	}

	void CompleteEvent::Notify()
	{
		if (!::SetEvent(m_event))
		{
			throw std::system_error(::GetLastError(), std::system_category(), MACRO_SOURCE_LOCATION());
		}
	}
}

std::wostream & operator<<(std::wostream & out, const char * mbs)
{
	wchar_t wc{};

	for (auto count = std::mbtowc(&wc, mbs, MB_CUR_MAX); count > 0; count = std::mbtowc(&wc, mbs += count, MB_CUR_MAX))
	{
		out << wc;
	}

	return out;
}

std::wostream & operator<<(std::wostream & out, IUpdate * update)
{
	// TODO: 名前と KB だけ実装する？
	return out;
}

std::wostream & operator<<(std::wostream & out, IDownloadProgress * update)
{
	// TODO: 適当な進行表示を実装
	return out;
}

std::wostream & operator<<(std::wostream & out, IInstallationProgress * update)
{
	// TODO: 適当な進行表示を実装
	return out;
}
