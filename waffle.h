#pragma once
#pragma comment(lib, "wuguid")

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <comdef.h>
#include <wuapi.h>

template<typename T> using com_ptr_t = _com_ptr_t<_com_IIID<T, &__uuidof(T)>>;

#include <cstdlib>
#include <functional>
#include <iostream>

std::wostream & operator<<(std::wostream & out, const char * mbs);

std::wostream & operator<<(std::wostream & out, IUpdate * update);
std::wostream & operator<<(std::wostream & out, IDownloadProgress * update);
std::wostream & operator<<(std::wostream & out, IInstallationProgress * update);

std::wostream & operator<<(std::wostream & out, DeploymentAction action);
std::wostream & operator<<(std::wostream & out, OperationResultCode result);
std::wostream & operator<<(std::wostream & out, InstallationRebootBehavior behavior);

namespace waffle
{
	using DownloadCallback = std::function<void(long, IUpdate *, IDownloadProgress *)>;
	using InstallCallback = std::function<void(long, IUpdate *, IInstallationProgress *)>;

	class Session;

	Session CreateSession();

	class Updates
	{
		LONG m_count;

		com_ptr_t<IUpdateCollection> m_updates;

	public:
		Updates(com_ptr_t<ISearchResult> result);
		~Updates() = default;

		bool empty() const noexcept
		{
			return m_count == 0;
		}

		LONG size() const noexcept
		{
			return m_count;
		}

		operator IUpdateCollection * ()
		{
			return m_updates;
		}

		IUpdateCollection * operator->()
		{
			return m_updates;
		}
	};

	class Session
	{
		bool m_rebootRequired;

		com_ptr_t<IUpdateSession> m_session;

		_variant_t m_state;

	public:
		Session();
		~Session() = default;

		Updates Search(const wchar_t * criteria, unsigned long timeout);

		void Download(Updates & updates, DownloadCallback callback);

		void Install(Updates & updates, InstallCallback callback);

		bool RebootRequired();
	};

	template<class T>
	struct Unknown : public T
	{
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void ** ppvObject) override
		{
			if (ppvObject == nullptr)
			{
				return E_POINTER;
			}

			if (riid == __uuidof(IUnknown) || riid == __uuidof(T))
			{
				*ppvObject = this;
				return S_OK;
			}

			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef(void) override
		{
			return 2;
		}

		ULONG STDMETHODCALLTYPE Release(void) override
		{
			return 1;
		}

		operator IUnknown * ()
		{
			return this;
		}
	};

	class CompleteEvent
	{
		HANDLE m_event;
		unsigned long m_timeout;

	public:
		CompleteEvent(unsigned long timeout);
		~CompleteEvent();

		CompleteEvent(const CompleteEvent &) = delete;
		CompleteEvent & operator=(const CompleteEvent &) = delete;

		void Wait();
		void Notify();
	};

	class CompletedCallback
	{
		CompleteEvent & m_event;

	public:
		CompletedCallback(CompleteEvent & e) : m_event(e)
		{}

		HRESULT Notify();
	};

	struct SearchCompletedCallback : public CompletedCallback, public Unknown<ISearchCompletedCallback>
	{
		using CompletedCallback::CompletedCallback;

		HRESULT STDMETHODCALLTYPE Invoke(ISearchJob *, ISearchCompletedCallbackArgs *) override
		{
			return Notify();
		}
	};

	struct DownloadCompletedCallback : public CompletedCallback, public Unknown<IDownloadCompletedCallback>
	{
		using CompletedCallback::CompletedCallback;

		HRESULT STDMETHODCALLTYPE Invoke(IDownloadJob *, IDownloadCompletedCallbackArgs *) override
		{
			return Notify();
		}
	};

	struct InstallationCompletedCallback : public CompletedCallback, public Unknown<IInstallationCompletedCallback>
	{
		using CompletedCallback::CompletedCallback;

		HRESULT STDMETHODCALLTYPE Invoke(IInstallationJob *, IInstallationCompletedCallbackArgs *) override
		{
			return Notify();
		}
	};

	// TODO: この２つはテンプレートで実装できないか？インタフェースは異なるが使うメソッドの名前が同じ

	struct DownloadProgressChangedCallback : public Unknown<IDownloadProgressChangedCallback>
	{
		Updates & m_updates;

		DownloadCallback m_callback;

		DownloadProgressChangedCallback(Updates & updates, DownloadCallback & callback) : m_updates(updates), m_callback(callback)
		{}

		HRESULT STDMETHODCALLTYPE Invoke(IDownloadJob * job, IDownloadProgressChangedCallbackArgs * args) override
		{
			com_ptr_t<IDownloadProgress> progress;

			if (auto hr = args->get_Progress(&progress); FAILED(hr))
			{
				return hr;
			}

			LONG index{};

			if (auto hr = progress->get_CurrentUpdateIndex(&index); FAILED(hr))
			{
				return hr;
			}

			com_ptr_t<IUpdate> update;

			if (auto hr = m_updates->get_Item(index, &update); FAILED(hr))
			{
				return hr;
			}

			m_callback(index, update, progress);

			return S_OK;
		}
	};

	struct InstallationProgressChangedCallback : public Unknown<IInstallationProgressChangedCallback>
	{
		HRESULT STDMETHODCALLTYPE Invoke(IInstallationJob * job, IInstallationProgressChangedCallbackArgs * args) override
		{
			return E_NOTIMPL;
		}
	};
}
