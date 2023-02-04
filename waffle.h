#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <comdef.h>
#include <wuapi.h>
#pragma comment(lib, "wuguid")

#include <ostream>
#include <string_view>
#include <system_error>

namespace waffle
{
	template<typename T>
	using com_ptr_t = _com_ptr_t<_com_IIID<T, &__uuidof(T)>>;

	struct ComInitialize
	{
		ComInitialize()
		{
			if (auto hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}
		}

		~ComInitialize()
		{
			::CoUninitialize();
		}
	};

	class Session
	{
		com_ptr_t<IUpdateSession> m_session;

	public:
		Session()
		{
			if (auto hr = m_session.CreateInstance("Microsoft.Update.Session"); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}
		}

		void SetClientApplicationID(std::wstring_view appName)
		{
			if (auto hr = m_session->put_ClientApplicationID(_bstr_t(appName.data())); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}
		}

		IUpdateSession * operator->()
		{
			return m_session;
		}
	};

	class Updates
	{
		LONG m_count;

		com_ptr_t<IUpdateCollection> m_updates;

	public:
		Updates(com_ptr_t<ISearchResult> & result)
		{
			if (auto hr = result->get_Updates(&m_updates); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}

			if (auto hr = m_updates->get_Count(&m_count); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}
		}

		bool empty() const noexcept
		{
			return m_count == 0;
		}

		struct UpdateIterator
		{
			LONG m_index;

			com_ptr_t<IUpdateCollection> m_updates;

			bool operator==(LONG index) const
			{
				return m_index == index;
			}

			UpdateIterator & operator++()
			{
				++m_index;

				return *this;
			}

			com_ptr_t<IUpdate> operator*() const
			{
				com_ptr_t<IUpdate> update;

				if (auto hr = m_updates->get_Item(m_index, &update); FAILED(hr))
				{
					throw std::system_error(hr, std::system_category(), __func__);
				}

				return update;
			}
		};

		UpdateIterator begin() const
		{
			return { 0, m_updates };
		}

		LONG end() const
		{
			return m_count;
		}
	};

	class Searcher
	{
		com_ptr_t<IUpdateSearcher> m_searcher;

	public:
		Searcher(Session & session)
		{
			if (auto hr = session->CreateUpdateSearcher(&m_searcher); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}
		}

		Updates Search(const wchar_t * criteria)
		{
			com_ptr_t<ISearchResult> result;

			if (auto hr = m_searcher->Search(_bstr_t(criteria), &result); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), __func__);
			}

			return result;
		}
	};

	std::wostream & operator<<(std::wostream & out, std::string_view mbs)
	{
		auto bytes = static_cast<int>(mbs.size());

		std::wstring wcs;

		if (int cch = ::MultiByteToWideChar(CP_THREAD_ACP, 0, mbs.data(), bytes, nullptr, 0); cch > 0)
		{
			wcs.resize(cch);

			::MultiByteToWideChar(CP_THREAD_ACP, 0, mbs.data(), bytes, wcs.data(), cch);
		}

		return out << wcs;
	}

	std::wostream & operator<<(std::wostream & out, const std::exception & e)
	{
		return out << std::string_view(e.what());
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

	class CommandLineParameters
	{
		int m_count;
		const wchar_t ** m_first;

	public:
		CommandLineParameters() : CommandLineParameters(::GetCommandLineW())
		{}

		CommandLineParameters(const wchar_t * commandLine) :
			m_count(0),
			m_first(const_cast<const wchar_t **>(::CommandLineToArgvW(commandLine, &m_count)))
		{}

		~CommandLineParameters()
		{
			::LocalFree(m_first);
		}

		CommandLineParameters(const CommandLineParameters &) = delete;
		CommandLineParameters & operator=(const CommandLineParameters &) = delete;

		const wchar_t ** begin() const
		{
			return m_first;
		}

		const wchar_t ** end() const
		{
			return m_first + m_count;
		}

		std::size_t size() const noexcept
		{
			return m_count;
		}
	};

	inline const wchar_t * NamedParameter(const wchar_t * key)
	{
		static CommandLineParameters g_parameters;

		auto it = g_parameters.begin(), end = g_parameters.end();

		while (it != end)
		{
			auto param = *it;

			++it;

			if (param[0] == L'/' && _wcsicmp(param + 1, key) == 0)
			{
				return *it;
			}
		}

		return nullptr;
	}
}
