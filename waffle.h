#pragma once
#pragma comment(lib, "wuguid")

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <comdef.h>
#include <wuapi.h>
#include <wuerror.h>

template<typename T>
using com_ptr_t = _com_ptr_t<_com_IIID<T, &__uuidof(T)>>;

#include <cstdlib>
#include <format>
#include <utility>
#include <iostream>
#include <functional>
#include <system_error>

std::wostream & operator<<(std::wostream & out, const char * wcs);
std::wostream & operator<<(std::wostream & out, IUpdate * update);
std::wostream & operator<<(std::wostream & out, IUpdateDownloadResult * result);
std::wostream & operator<<(std::wostream & out, IUpdateInstallationResult * result);

namespace waffle
{
	class Session;

	Session CreateSession();

	using DownloadCallback = std::function<void(long, OperationResultCode, IUpdate *, IUpdateDownloadResult *, IDownloadProgress *)>;
	using InstallationCallback = std::function<void(long, OperationResultCode, IUpdate *, IUpdateInstallationResult *, IInstallationProgress *)>;

	class Updates
	{
		LONG m_count;

		com_ptr_t<IUpdateCollection> m_updates;

	public:
		Updates();
		~Updates() = default;

		LONG Add(IUpdate * update);

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

	public:
		Session();
		~Session() = default;

		Updates Search(BSTR criteria, unsigned long timeout);

		void Download(Updates & updates, DownloadCallback callback);
		void Install(Updates & updates, InstallationCallback callback);

		bool RebootRequired()
		{
			return m_rebootRequired;
		}
	};

	class CompleteEvent
	{
		HANDLE m_event;

	public:
		CompleteEvent();
		~CompleteEvent();

		CompleteEvent(const CompleteEvent &) = delete;
		CompleteEvent & operator=(const CompleteEvent &) = delete;

		void Wait(unsigned long timeout);
		void Notify();
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

	template<class Worker, class Job, class Result, class BeginArg, class Callback, class CallbackArgs>
	class Asynchronous : public Unknown<Callback>
	{
		typedef HRESULT(STDMETHODCALLTYPE Worker:: * BeginMethod)(BeginArg, IUnknown *, VARIANT, Job **);
		typedef HRESULT(STDMETHODCALLTYPE Worker:: * EndMethod)(Job *, Result **);
		typedef HRESULT(STDMETHODCALLTYPE Callback:: * CompletedMethod)(Job *, CallbackArgs *);

		CompleteEvent m_completeEvent;
		BeginMethod m_beginMethod;
		EndMethod m_endMethod;

	public:
		Asynchronous(BeginMethod begin, EndMethod end, CompletedMethod comp) : m_beginMethod(begin), m_endMethod(end)
		{}

		auto Begin(Worker * worker, BeginArg arg, VARIANT state)
		{
			com_ptr_t<Job> job;

			if (auto hr = (worker->*m_beginMethod)(arg, this, state, &job); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __FUNCTION__);
			}

			return job;
		}

		auto End(Worker * worker, Job * job)
		{
			com_ptr_t<Result> result;

			if (auto hr = (worker->*m_endMethod)(job, &result); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __FUNCTION__);
			}

			return result;
		}

		auto Wait(unsigned long msTimeout, Worker * worker, BeginArg arg)
		{
			_variant_t state;

			auto job = Begin(worker, arg, state);

			try
			{
				m_completeEvent.Wait(msTimeout);

				auto result = End(worker, job);

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

		HRESULT STDMETHODCALLTYPE Invoke(Job *, CallbackArgs *) override
		{
			try
			{
				m_completeEvent.Notify();

				return S_OK;
			}
			catch (const std::system_error & e)
			{
				return HRESULT_FROM_WIN32(e.code().value());
			}
		}
	};

	template<class Job, class Callback, class CallbackArgs, class Progress, class Result>
	class ProgressChangedCallback : public Unknown<Callback>
	{
		typedef HRESULT(STDMETHODCALLTYPE Callback:: * ChangedMethod)(Job *, CallbackArgs *);
		typedef std::function<void(long, OperationResultCode, IUpdate *, Result *, Progress *)> WaffleCallback;

		Updates & m_updates;
		WaffleCallback m_callback;

	public:
		ProgressChangedCallback(Updates & updates, WaffleCallback callback, ChangedMethod changed) : m_updates(updates), m_callback(callback)
		{}

		HRESULT STDMETHODCALLTYPE Invoke(Job * job, CallbackArgs * args) override
		{
			com_ptr_t<Progress> progress;

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

			com_ptr_t<Result> result;

			if (auto hr = progress->GetUpdateResult(index, &result); FAILED(hr))
			{
				return hr;
			}

			OperationResultCode code{};

			if (auto hr = result->get_ResultCode(&code); FAILED(hr))
			{
				return hr;
			}

			try
			{
				m_callback(index, code, update, result, progress);
			}
			catch (const std::system_error & e)
			{
				return HRESULT_FROM_WIN32(e.code().value());
			}
			catch (...)
			{
				return E_FAIL;
			}

			return S_OK;
		}
	};

	std::pair<ULONGLONG, ULONGLONG> GetTotalBytes(IDownloadProgress * progress);

	template<class Progress>
	LONG GetPercentComplete(Progress * progress)
	{
		LONG percent = 0;

		if (auto hr = progress->get_PercentComplete(&percent); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __FUNCTION__);
		}

		return percent;
	}

	template<class Result>
	LONG GetWUAErrorCode(Result result)
	{
		// https://learn.microsoft.com/en-us/windows/win32/wua_sdk/wua-success-and-error-codes-

		LONG code = 0;

		if (auto hr = result->get_HResult(&code); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __FUNCTION__);
		}

		return code;
	}

	template<class Result>
	OperationResultCode GetOperationCode(Result result)
	{
		OperationResultCode code{};

		if (auto hr = result->get_ResultCode(&code); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __FUNCTION__);
		}

		return code;
	}

	template<class Result>
	bool GetRebootRequired(Result result)
	{
		VARIANT_BOOL rebootRequired{};

		if (auto hr = result->get_RebootRequired(&rebootRequired); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), __FUNCTION__);
		}

		return (rebootRequired == VARIANT_TRUE);
	}

	const char * GetWUAErrorMessage(LONG code);
}
