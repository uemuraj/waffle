//
// Windows Update Agent API で Windows Update の検索、ダウンロード、インストール
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include "waffle.h"

#include <system_error>

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

	auto BeginAsynchronous(IUpdateSearcher * searcher, const wchar_t * criteria, SearchCompletedCallback & completed, _variant_t & state)
	{
		com_ptr_t<ISearchJob> job;

		if (auto hr = searcher->BeginSearch(_bstr_t(criteria), completed, state, &job); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		return job;
	}

	auto EndAsynchronous(IUpdateSearcher * searcher, ISearchJob * job)
	{
		com_ptr_t<ISearchResult> result;

		if (auto hr = searcher->EndSearch(job, &result); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		return result;
	}

	auto BeginAsynchronous(IUpdateDownloader * downloder, DownloadProgressChangedCallback & progress, DownloadCompletedCallback & completed, _variant_t & state)
	{
		com_ptr_t<IDownloadJob> job;

		if (auto hr = downloder->BeginDownload(progress, completed, state, &job); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		return job;
	}

	auto EndAsynchronous(IUpdateDownloader * downloder, IDownloadJob * job)
	{
		com_ptr_t<IDownloadResult> result;

		if (auto hr = downloder->EndDownload(job, &result); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		return result;
	}

	auto BeginAsynchronous(IUpdateInstaller * installer, InstallationProgressChangedCallback & progress, InstallationCompletedCallback & completed, _variant_t & state)
	{
		com_ptr_t<IInstallationJob> job;

		if (auto hr = installer->BeginInstall(progress, completed, state, &job); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		return job;
	}

	auto EndAsynchronous(IUpdateInstaller * installer, IInstallationJob * job)
	{
		com_ptr_t<IInstallationResult> result;

		if (auto hr = installer->EndInstall(job, &result); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		return result;
	}

	template<class Worker, class... Args>
	auto WaitAsynchronous(CompleteEvent & completeEvent, Worker worker, Args... args)
	{
		auto job = BeginAsynchronous(worker.GetInterfacePtr(), args...);

		try
		{
			completeEvent.Wait();

			auto result = EndAsynchronous(worker.GetInterfacePtr(), job);
			job->CleanUp();
			return result;
		}
		catch (const std::exception &)
		{
			job->RequestAbort();
			job->CleanUp();
			throw;
		}
	}

	Updates Session::Search(const wchar_t * criteria, unsigned long timeout)
	{
		com_ptr_t<IUpdateSearcher> searcher;

		if (auto hr = m_session->CreateUpdateSearcher(&searcher); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		CompleteEvent completeEvent(timeout);

		return WaitAsynchronous(completeEvent, searcher, criteria, SearchCompletedCallback(completeEvent), _variant_t{});
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

		CompleteEvent completeEvent(INFINITE);

		WaitAsynchronous(completeEvent, donwloader, DownloadProgressChangedCallback(updates, callback), DownloadCompletedCallback(completeEvent), _variant_t{});

		// TODO: ダウンロードの結果を確認して異常があれば例外を投げる
#if 0
		void Download()
		{

			com_ptr_t<IDownloadResult> results;

			if (auto hr = m_donwloader->Download(&results); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
			}

			// TODO: Update 個別の結果を先に確認するべき
			for (LONG index = 0; index < m_count; ++index)
			{
				com_ptr_t<IUpdateDownloadResult> result;

				if (auto hr = results->GetUpdateResult(index, &result); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
				}

				{
					LONG code = 0;

					if (auto hr = result->get_HResult(&code); FAILED(hr))
					{
						throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
					}

					if (FAILED(code)) // WU_E_PER_MACHINE_UPDATE_ACCESS_DENIED(0x80240044L)
					{
						throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
					}
				}

				{
					OperationResultCode code{};

					if (auto hr = result->get_ResultCode(&code); FAILED(hr))
					{
						throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
					}

					if (code != orcSucceeded)
					{
						throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
					}
				}
			}


			{
				LONG code = 0;

				if (auto hr = results->get_HResult(&code); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
				}

				if (FAILED(code)) // WU_E_ALL_UPDATES_FAILED(0x80240022L)
				{
					throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
				}
			}

			{
				OperationResultCode code{};

				if (auto hr = results->get_ResultCode(&code); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
				}

				if (code != orcSucceeded)
				{
					throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
				}
			}
		}
#endif
	}

	void Session::Install(Updates & updates, InstallCallback callback)
	{
		// TODO: Wait を上手く書きたいとこ
#if 0
		bool Install()
		{
			com_ptr_t<IInstallationResult> results;

			if (auto hr = m_installer->Install(&results); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
			}

			// TODO: Update 個別の結果を先に確認するべき
			for (LONG index = 0; index < m_count; ++index)
			{
				com_ptr_t<IUpdateInstallationResult> result;

				if (auto hr = results->GetUpdateResult(index, &result); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
				}

				{
					LONG code = 0;

					if (auto hr = result->get_HResult(&code); FAILED(hr))
					{
						throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
					}

					if (FAILED(code)) // WU_E_UH_NEEDANOTHERDOWNLOAD(0x8024200DL)
					{
						throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
					}
				}

				{
					OperationResultCode code{};

					if (auto hr = result->get_ResultCode(&code); FAILED(hr))
					{
						throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
					}

					if (code != orcSucceeded)
					{
						throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
					}
				}
			}


			{
				LONG code = 0;

				if (auto hr = results->get_HResult(&code); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
				}

				if (FAILED(code)) // WU_E_UH_NEEDANOTHERDOWNLOAD(0x8024200DL)
				{
					throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
				}
			}

			{
				OperationResultCode code{};

				if (auto hr = results->get_ResultCode(&code); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
				}

				if (code != orcSucceeded)
				{
					throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
				}
			}

			VARIANT_BOOL rebootRequired(VARIANT_FALSE);

			if (auto hr = results->get_RebootRequired(&rebootRequired); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
			}

			return (rebootRequired == VARIANT_FALSE);
		}
#endif
	}

	bool Session::RebootRequired()
	{
		// TODO: 自分でダウンロードしていないくても、再起動が必要な状態か調べておく必要がある
		return m_rebootRequired;
	}

	CompleteEvent::CompleteEvent(unsigned long timeout) : m_timeout(timeout)
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

	void CompleteEvent::Wait()
	{
		auto wait = ::WaitForSingleObject(m_event, m_timeout);

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

	HRESULT CompletedCallback::Notify()
	{
		try
		{
			m_event.Notify();
			return S_OK;
		}
		catch (const std::system_error & e)
		{
			return HRESULT_FROM_WIN32(e.code().value());
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
#if 0
	std::wostream & operator<<(std::wostream & out, com_ptr_t<IUpdate> & update)
	{
		_bstr_t title;

		if (auto hr = update->get_Title(title.GetAddress()); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		com_ptr_t<IUpdateIdentity> identity;

		if (auto hr = update->get_Identity(&identity); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		_bstr_t id;

		if (auto hr = identity->get_UpdateID(id.GetAddress()); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		LONG num = 0;

		if (auto hr = identity->get_RevisionNumber(&num); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		out << (const wchar_t *) title << L" {" << (const wchar_t *) id << L'.' << num << L'}';

		VARIANT_BOOL hidden = VARIANT_FALSE;

		if (auto hr = update->get_IsHidden(&hidden); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (hidden == VARIANT_TRUE)
		{
			out << L" (hidden)";
		}

		com_ptr_t<IStringCollection> articles;

		if (auto hr = update->get_KBArticleIDs(&articles); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		LONG count = 0;

		if (auto hr = articles->get_Count(&count); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (count > 0)
		{
			out << L" (";

			for (LONG index = 0; index < count; ++index)
			{
				_bstr_t article;

				if (auto hr = articles->get_Item(index, article.GetAddress()); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
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
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		out << L" Deployment action: " << action;

		com_ptr_t<IInstallationBehavior> behavior;

		if (auto hr = update->get_InstallationBehavior(&behavior); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		InstallationRebootBehavior reboot{};

		if (auto hr = behavior->get_RebootBehavior(&reboot); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		out << L" Reboot behavior: " << reboot;

		return out;
	}
#endif
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

std::wostream & operator<<(std::wostream & out, DeploymentAction action)
{
	switch (action)
	{
	case daNone:
		return out << L"daNone";
	case daInstallation:
		return out << L"daInstallation";
	case daUninstallation:
		return out << L"daUninstallation";
	case daDetection:
		return out << L"daDetection";
	case daOptionalInstallation:
		return out << L"daOptionalInstallation";
	default:
		return out << L"DeploymentAction(" << (int) action << L')';
	}
}

std::wostream & operator<<(std::wostream & out, OperationResultCode result)
{
	switch (result)
	{
	case orcNotStarted:
		return out << L"orcNotStarted";
	case orcInProgress:
		return out << L"orcInProgress";
	case orcSucceeded:
		return out << L"orcSucceeded";
	case orcSucceededWithErrors:
		return out << L"orcSucceededWithErrors";
	case orcFailed:
		return out << L"orcFailed";
	case orcAborted:
		return out << L"orcAborted";
	default:
		return out << L"OperationResultCode(" << (int) result << L')';
	}
}

std::wostream & operator<<(std::wostream & out, InstallationRebootBehavior behavior)
{
	switch (behavior)
	{
	case irbNeverReboots:
		return out << L"irbNeverReboots";
	case irbAlwaysRequiresReboot:
		return out << L"irbAlwaysRequiresReboot";
	case irbCanRequestReboot:
		return out << L"irbCanRequestReboot";
	default:
		return out << L"InstallationRebootBehavior(" << (int) behavior << L')';
	}
}
