#include "waffle.h"

#define MACRO_SOURCE_LOCATION() __FILE__ "(" _CRT_STRINGIZE(__LINE__) ")"

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
	_bstr_t title;

	if (auto hr = update->get_Title(title.GetAddress()); FAILED(hr))
	{
		throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
	}

	return out << (const wchar_t *) title;
}

std::wostream & operator<<(std::wostream & out, IUpdateDownloadResult * result)
{
	auto code = waffle::GetWUAErrorCode(result);

	if (auto msg = waffle::GetWUAErrorMessage(code); msg != nullptr)
	{
		return out << msg;
	}

	return out << std::format(L"Download Result 0x{0:08X}.", code);
}

std::wostream & operator<<(std::wostream & out, IUpdateInstallationResult * result)
{
	auto code = waffle::GetWUAErrorCode(result);

	if (auto msg = waffle::GetWUAErrorMessage(code); msg != nullptr)
	{
		return out << msg;
	}

	return out << std::format(L"Installation Result 0x{0:08X}.", code);
}

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

	Updates::Updates() : m_count(0)
	{
		if (auto hr = m_updates.CreateInstance(L"Microsoft.Update.UpdateColl"); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}
	}

	LONG Updates::Add(IUpdate * update)
	{
		LONG index{};

		if (auto hr = m_updates->Add(update, &index); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		++m_count;

		return index;
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

		com_ptr_t<ISystemInformation> sysinfo;

		if (auto hr = sysinfo.CreateInstance(L"Microsoft.Update.SystemInfo"); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		m_rebootRequired = GetRebootRequired(sysinfo);
	}

	Updates Session::Search(BSTR criteria, unsigned long timeout)
	{
		com_ptr_t<IUpdateSearcher> searcher;

		if (auto hr = m_session->CreateUpdateSearcher(&searcher); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		Asynchronous asynchronous(&IUpdateSearcher::BeginSearch, &IUpdateSearcher::EndSearch, &ISearchCompletedCallback::Invoke);

		auto result = asynchronous.Wait(timeout, searcher, criteria);

		if (auto code = GetOperationCode(result); code != orcSucceeded)
		{
			ValidateOperationCode(code, MACRO_SOURCE_LOCATION());
		}

		com_ptr_t<IUpdateCollection> items;

		if (auto hr = result->get_Updates(&items); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		LONG count = 0;

		if (auto hr = items->get_Count(&count); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		Updates updates;

		for (LONG index = 0; index < count; ++index)
		{
			com_ptr_t<IUpdate> item;

			if (auto hr = items->get_Item(index, &item); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
			}

			com_ptr_t<IUpdate2> update2;

			if (auto hr = item->QueryInterface(&update2); FAILED(hr))
			{
				throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
			}

			if (GetRebootRequired(update2))
			{
				m_rebootRequired = true;
				continue;
			}

			updates.Add(item);
		}

		return updates;
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

		Asynchronous asynchronous(&IUpdateDownloader::BeginDownload, &IUpdateDownloader::EndDownload, &IDownloadCompletedCallback::Invoke);

		auto result = asynchronous.Wait(INFINITE, donwloader, ProgressChangedCallback(updates, callback, &IDownloadProgressChangedCallback::Invoke));

		if (auto code = GetWUAErrorCode(result); FAILED(code))
		{
			throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (auto code = GetOperationCode(result); code != orcSucceeded)
		{
			ValidateOperationCode(code, MACRO_SOURCE_LOCATION());
		}
	}

	void Session::Install(Updates & updates, InstallationCallback callback)
	{
		com_ptr_t<IUpdateInstaller> installer;

		if (auto hr = m_session->CreateUpdateInstaller(&installer); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (auto hr = installer->put_Updates(updates); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		Asynchronous asynchronous(&IUpdateInstaller::BeginInstall, &IUpdateInstaller::EndInstall, &IInstallationCompletedCallback::Invoke);

		auto result = asynchronous.Wait(INFINITE, installer, ProgressChangedCallback(updates, callback, &IInstallationProgressChangedCallback::Invoke));

		if (auto code = GetWUAErrorCode(result); FAILED(code))
		{
			throw std::system_error(code, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (auto code = GetOperationCode(result); code != orcSucceeded)
		{
			ValidateOperationCode(code, MACRO_SOURCE_LOCATION());
		}

		m_rebootRequired = GetRebootRequired(result);
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

	auto GetTotalBytesDownloaded(IDownloadProgress * progress)
	{
		DECIMAL bytes{};

		if (auto hr = progress->get_TotalBytesDownloaded(&bytes); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (bytes.Hi32 != 0)
		{
			throw std::overflow_error(MACRO_SOURCE_LOCATION());
		}

		return bytes.Lo64;
	}

	auto GetTotalBytesToDownload(IDownloadProgress * progress)
	{
		DECIMAL bytes{};

		if (auto hr = progress->get_TotalBytesToDownload(&bytes); FAILED(hr))
		{
			throw std::system_error(hr, std::system_category(), MACRO_SOURCE_LOCATION());
		}

		if (bytes.Hi32 != 0)
		{
			throw std::overflow_error(MACRO_SOURCE_LOCATION());
		}

		return bytes.Lo64;
	}

	std::pair<ULONGLONG, ULONGLONG> GetTotalBytes(IDownloadProgress * progress)
	{
		return { GetTotalBytesToDownload(progress), GetTotalBytesDownloaded(progress) };
	}

	void ValidateOperationCode(OperationResultCode code, const char * what)
	{
		switch (code)
		{
		case orcNotStarted:
			throw std::runtime_error(std::string(what) + ": Not Started.");
		case orcInProgress:
			throw std::runtime_error(std::string(what) + ": In Pogress.");
		case orcSucceeded:
		case orcSucceededWithErrors:
			return;
		case orcFailed:
			throw std::runtime_error(std::string(what) + ": Failed.");
		case orcAborted:
			throw std::runtime_error(std::string(what) + ": Aborted.");
		default:
			throw std::runtime_error(std::format("{}: code({}).", what, (int) code));
		}
	}
}

#define MACRO_ERROR_MESSAGE(code, message) case code: return message

const char * waffle::GetWUAErrorMessage(LONG code)
{
	// https://learn.microsoft.com/en-us/windows/win32/wua_sdk/wua-success-and-error-codes-
	// https://learn.microsoft.com/en-us/troubleshoot/windows-client/deployment/common-windows-update-errors

	switch (code)
	{
		MACRO_ERROR_MESSAGE(WU_S_SERVICE_STOP, "WUA was stopped successfully.");
		MACRO_ERROR_MESSAGE(WU_S_SELFUPDATE, "WUA updated itself.");
		MACRO_ERROR_MESSAGE(WU_S_UPDATE_ERROR, "The operation completed successfully but errors occurred applying the updates.");
		MACRO_ERROR_MESSAGE(WU_S_MARKED_FOR_DISCONNECT, "A callback was marked to be disconnected later because the request to disconnect the operation came while a callback was executing.");
		MACRO_ERROR_MESSAGE(WU_S_REBOOT_REQUIRED, "The system must be restarted to complete installation of the update.");
		MACRO_ERROR_MESSAGE(WU_S_ALREADY_INSTALLED, "The update to be installed is already installed on the system.");
		MACRO_ERROR_MESSAGE(WU_S_ALREADY_UNINSTALLED, "The update to be removed is not installed on the system.");
		MACRO_ERROR_MESSAGE(WU_S_ALREADY_DOWNLOADED, "The update to be downloaded has already been downloaded.");
		MACRO_ERROR_MESSAGE(WU_S_UH_INSTALLSTILLPENDING, "The installation operation for the update is still in progress.");
		MACRO_ERROR_MESSAGE(WU_E_NO_SERVICE, "WUA was unable to provide the service.");
		MACRO_ERROR_MESSAGE(WU_E_MAX_CAPACITY_REACHED, "The maximum capacity of the service was exceeded.");
		MACRO_ERROR_MESSAGE(WU_E_UNKNOWN_ID, "WUA cannot find an ID.");
		MACRO_ERROR_MESSAGE(WU_E_NOT_INITIALIZED, "The object could not be initialized.");
		MACRO_ERROR_MESSAGE(WU_E_RANGEOVERLAP, "The update handler requested a byte range overlapping a previously requested range.");
		MACRO_ERROR_MESSAGE(WU_E_TOOMANYRANGES, "The requested number of byte ranges exceeds the maximum number.");
		MACRO_ERROR_MESSAGE(WU_E_INVALIDINDEX, "The index to a collection was invalid.");
		MACRO_ERROR_MESSAGE(WU_E_ITEMNOTFOUND, "The key for the item queried could not be found.");
		MACRO_ERROR_MESSAGE(WU_E_OPERATIONINPROGRESS, "Another conflicting operation was in progress. Some operations such as installation cannot be performed twice simultaneously.");
		MACRO_ERROR_MESSAGE(WU_E_COULDNOTCANCEL, "Cancellation of the operation was not allowed.");
		MACRO_ERROR_MESSAGE(WU_E_NOOP, "No operation was required.");
		MACRO_ERROR_MESSAGE(WU_E_XML_MISSINGDATA, "WUA could not find required information in the update's XML data.");
		MACRO_ERROR_MESSAGE(WU_E_CYCLE_DETECTED, "Circular update relationships were detected in the metadata.");
		MACRO_ERROR_MESSAGE(WU_E_TOO_DEEP_RELATION, "Update relationships too deep to evaluate were evaluated.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_RELATIONSHIP, "An invalid update relationship was detected.");
		MACRO_ERROR_MESSAGE(WU_E_REG_VALUE_INVALID, "An invalid registry value was read.");
		MACRO_ERROR_MESSAGE(WU_E_DUPLICATE_ITEM, "Operation tried to add a duplicate item to a list.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_INSTALL_REQUESTED, "Updates that are requested for install are not installable by the caller.");
		MACRO_ERROR_MESSAGE(WU_E_INSTALL_NOT_ALLOWED, "Operation tried to install while another installation was in progress or the system was pending a mandatory restart.");
		MACRO_ERROR_MESSAGE(WU_E_NOT_APPLICABLE, "Operation was not performed because there are no applicable updates.");
		MACRO_ERROR_MESSAGE(WU_E_NO_USERTOKEN, "Operation failed because a required user token is missing.");
		MACRO_ERROR_MESSAGE(WU_E_EXCLUSIVE_INSTALL_CONFLICT, "An exclusive update can't be installed with other updates at the same time.");
		MACRO_ERROR_MESSAGE(WU_E_POLICY_NOT_SET, "A policy value was not set.");
		MACRO_ERROR_MESSAGE(WU_E_SELFUPDATE_IN_PROGRESS, "The operation could not be performed because the Windows Update Agent is self-updating.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_UPDATE, "An update contains invalid metadata.");
		MACRO_ERROR_MESSAGE(WU_E_SERVICE_STOP, "Operation did not complete because the service or system was being shut down.");
		MACRO_ERROR_MESSAGE(WU_E_NO_CONNECTION, "Operation did not complete because the network connection was unavailable.");
		MACRO_ERROR_MESSAGE(WU_E_TIME_OUT, "Operation did not complete because it timed out.");
		MACRO_ERROR_MESSAGE(WU_E_EULAS_DECLINED, "The license terms for all updates were declined.");
		MACRO_ERROR_MESSAGE(WU_E_NO_UPDATE, "There are no updates.");
		MACRO_ERROR_MESSAGE(WU_E_USER_ACCESS_DISABLED, "Group Policy settings prevented access to Windows Update.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_UPDATE_TYPE, "The type of update is invalid.");
		MACRO_ERROR_MESSAGE(WU_E_URL_TOO_LONG, "The URL exceeded the maximum length.");
		MACRO_ERROR_MESSAGE(WU_E_UNINSTALL_NOT_ALLOWED, "The update could not be uninstalled because the request did not originate from a Windows Server Update Services (WSUS) server.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_PRODUCT_LICENSE, "Search may have missed some updates before there is an unlicensed application on the system.");
		MACRO_ERROR_MESSAGE(WU_E_MISSING_HANDLER, "A component required to detect applicable updates was missing.");
		MACRO_ERROR_MESSAGE(WU_E_LEGACYSERVER, "An operation did not complete because it requires a newer version of server.");
		MACRO_ERROR_MESSAGE(WU_E_BIN_SOURCE_ABSENT, "A delta-compressed update could not be installed because it required the source.");
		MACRO_ERROR_MESSAGE(WU_E_SOURCE_ABSENT, "A full-file update could not be installed because it required the source.");
		MACRO_ERROR_MESSAGE(WU_E_WU_DISABLED, "Access to an unmanaged server is not allowed.");
		MACRO_ERROR_MESSAGE(WU_E_CALL_CANCELLED_BY_POLICY, "Operation did not complete because the **DisableWindowsUpdateAccess** policy was set in the registry.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_PROXY_SERVER, "The format of the proxy list was invalid.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_FILE, "The file is in the wrong format.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_CRITERIA, "The search criteria string was invalid.");
		MACRO_ERROR_MESSAGE(WU_E_EULA_UNAVAILABLE, "License terms could not be downloaded.");
		MACRO_ERROR_MESSAGE(WU_E_DOWNLOAD_FAILED, "Update failed to download.");
		MACRO_ERROR_MESSAGE(WU_E_UPDATE_NOT_PROCESSED, "The update was not processed.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_OPERATION, "The object's current state did not allow the operation.");
		MACRO_ERROR_MESSAGE(WU_E_NOT_SUPPORTED, "The functionality for the operation is not supported.");
		MACRO_ERROR_MESSAGE(WU_E_TOO_MANY_RESYNC, "Agent is asked by server to resync too many times.");
		MACRO_ERROR_MESSAGE(WU_E_NO_SERVER_CORE_SUPPORT, "The WUA API method does not run on the server core installation.");
		MACRO_ERROR_MESSAGE(WU_E_SYSPREP_IN_PROGRESS, "Service is not available while sysprep is running.");
		MACRO_ERROR_MESSAGE(WU_E_UNKNOWN_SERVICE, "The update service is no longer registered with automatic updates.");
		MACRO_ERROR_MESSAGE(WU_E_NO_UI_SUPPORT, "No support for the WUA user interface.");
		MACRO_ERROR_MESSAGE(WU_E_PER_MACHINE_UPDATE_ACCESS_DENIED, "Only administrators can perform this operation on per-computer updates.");
		MACRO_ERROR_MESSAGE(WU_E_UNSUPPORTED_SEARCHSCOPE, "A search was attempted with a scope that is not currently supported for this type of search.");
		MACRO_ERROR_MESSAGE(WU_E_BAD_FILE_URL, "The URL does not point to a file.");
		MACRO_ERROR_MESSAGE(WU_E_INVALID_NOTIFICATION_INFO, "The featured update notification info returned by the server is invalid.");
		MACRO_ERROR_MESSAGE(WU_E_OUTOFRANGE, "The data is out of range.");
		MACRO_ERROR_MESSAGE(WU_E_SETUP_IN_PROGRESS, "WUA operations are not available while operating system setup is running.");
		MACRO_ERROR_MESSAGE(WU_E_UNEXPECTED, "An operation failed due to reasons not covered by another error code.");
		MACRO_ERROR_MESSAGE(WU_E_WINHTTP_INVALID_FILE, "The downloaded file has an unexpected content type.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_BAD_REQUEST, "Same as HTTP status 400 - The server could not process the request due to invalid syntax.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_DENIED, "Same as HTTP status 401 - The requested resource requires user authentication.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_FORBIDDEN, "Same as HTTP status 403 - Server understood the request, but declines to fulfill it.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_NOT_FOUND, "Same as HTTP status 404 - The server cannot find the requested URI (Uniform Resource Identifier).");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_BAD_METHOD, "Same as HTTP status 405 - The HTTP method is not allowed.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_REQUEST_TIMEOUT, "Same as HTTP status 408 - The server timed out waiting for the request.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_CONFLICT, "Same as HTTP status 409 - The request was not completed due to a conflict with the current state of the resource.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_GONE, "Same as HTTP status 410 - Requested resource is no longer available at the server.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_SERVER_ERROR, "Same as HTTP status 500 - An error internal to the server prevented fulfilling the request.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_NOT_SUPPORTED, "Same as HTTP status 501 - Server does not support the functionality required to fulfill the request. ");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_BAD_GATEWAY, "Same as HTTP status 502 - The server, while acting as a gateway or proxy, received an invalid response from the upstream server it accessed in attempting to fulfill the request.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_GATEWAY_TIMEOUT, "Same as HTTP status 504 - The request was timed out waiting for a gateway.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_VERSION_NOT_SUP, "Same as HTTP status 505 - The server does not support the HTTP protocol version used for the request.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_NOT_MAPPED, "The request could not be completed and the reason did not correspond to any of the WU_E_PT_HTTP_* error codes.");
		MACRO_ERROR_MESSAGE(WU_E_PT_WINHTTP_NAME_NOT_RESOLVED, "Same as ERROR_WINHTTP_NAME_NOT_RESOLVED - The proxy server or target server name cannot be resolved.");
		MACRO_ERROR_MESSAGE(WU_E_PT_ECP_SUCCEEDED_WITH_ERRORS, "External .cab file processing completed with some errors.\nThis error can be caused by the Lightspeed Rocket for web filtering software. \r\nAdd the IP addresses of devices you want to get updates to the exceptions list of Lightspeed Rocket.");
		MACRO_ERROR_MESSAGE(WU_E_UH_INVALIDMETADATA, "A handler operation couldn't be completed because the update contains invalid metadata.\nRename the software redistribution folder and try to download the updates again: \r\nRename the following folders to *.BAK: \r\n-*%systemroot%\\system32\\catroot2* \r\nType the following commands at a command prompt. Press ENTER after you type each command.\r\n- `Ren %systemroot%\\SoftwareDistribution\\DataStore *.bak`\r\n- `Ren %systemroot%\\SoftwareDistribution\\Download *.bak`\r\n- `Ren %systemroot%\\system32\\catroot2 *.bak`");
		MACRO_ERROR_MESSAGE(WU_E_ALL_UPDATES_FAILED, "failed for all the updates. Multiple root causes for this error.\ncommon issue is that antivirus software is blocking access to certain folders (like SoftwareDistribution). CBS.log analysis needed to determine the file or folder being protected.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_PROXY_AUTH_REQ, "as HTTP status 407 - proxy authentication is required.\nUnable to authenticate through a proxy server. | Either the Winhttp proxy or WinInet proxy settings aren't configured correctly. This error generally means that the Windows Update Agent was unable to connect to the update servers or your own update source, such as WSUS, Configuration Manager, or Microsoft Endpoint Manager, due to a proxy error. \r\n Verify the proxy settings on the client. The Windows Update Agent uses WinHTTP to scan for available updates. When there's a proxy server between the client and the update source, the proxy settings must be configured correctly on the clients to enable them to communicate by using the source's FQDN. \r\n Check with your network and proxy teams to confirm that the device can the update source without the proxy requiring user authentication.");
		MACRO_ERROR_MESSAGE(WU_E_PT_HTTP_STATUS_SERVICE_UNAVAIL, "as HTTP status 503 - the service is temporarily overloaded.\nUnable to connect to the configured update source. | Network troubleshooting needed to resolve the connectivity issue. Check with your network and proxy teams to confirm that the device can the update source without the proxy requiring user authentication.");
		MACRO_ERROR_MESSAGE(WU_E_NO_INTERACTIVE_USER, "Operation didn't complete because no interactive user is signed in.\nSign in to the device to start the installation and allow the device to restart.");
		MACRO_ERROR_MESSAGE(WU_E_UH_POSTREBOOTSTILLPENDING, "The post-restart operation for the update is still in progress.\nSome Windows updates require the device to be restarted. Restart the device to complete update installation.");
		MACRO_ERROR_MESSAGE(WU_E_DM_UNAUTHORIZED_LOCAL_USER, "The download failed because the local user was denied authorization to download the content.\nEnsure that the user attempting to download and install updates has been provided with sufficient privileges to install updates (Local Administrator).");
		MACRO_ERROR_MESSAGE(WU_E_CALL_CANCELLED, "Operation was canceled. | The operation was canceled by the user or service.\nmight also receive this error when we're unable to filter the results.");
		MACRO_ERROR_MESSAGE(WU_E_XML_INVALID, "Windows Update Agent found information in the update's XML data that isn't valid.\nCertain drivers contain more metadata information in Update.xml, which Orchestrator can interpret as data that isn't valid. Ensure that you have the latest Windows Update Agent installed on the device.");
		MACRO_ERROR_MESSAGE(WU_E_SETUP_SKIP_UPDATE, "An update to the Windows Update Agent was skipped due to a directive in the Wuident.cab file.\nYou might encounter this error when WSUS isn't sending the self-update to the clients.\r\nFor more information to resolve the issue, review [KB920659](/troubleshoot/windows-server/deployment/wsus-selfupdate-not-send-automatic-updates).");
		MACRO_ERROR_MESSAGE(WU_E_PT_SOAPCLIENT_SOAPFAULT, "SOAP client failed because there was a SOAP fault for reasons of `WU_E_PT_SOAP_*` error codes.\nThis issue occurs because Windows can't renew the cookies for Windows Update.  \r\nFor more information to resolve the issue, see [0x80244007 error when Windows tries to scan for updates on a WSUS server](https://support.microsoft.com/topic/0x80244007-error-when-windows-tries-to-scan-for-updates-on-a-wsus-server-6af342d9-9af6-f3bb-b6ad-2be56bf7826e).");
		MACRO_ERROR_MESSAGE(WININET_E_CONNECTION_ABORTED, "The connection with the server was closed abnormally, BITS is unable to transfer the file successfully.\nEncountered if BITS is broken or if the file being transferred can't be written to the destination folder on the client. This error is caused by connection errors while checking or downloading updates.\r\n From a cmd prompt run: `BITSADMIN /LIST /ALLUSERS /VERBOSE` \r\n Search for the 0x80072EFE error code. You should see a reference to an HTTP code with a specific file. Using a browser, try to download it manually, making sure you're using your organization's proxy settings. If the download fails, check with your proxy manager to allow for the communication to be sucesfull. Also check with your network team for this specific URL access.");
		MACRO_ERROR_MESSAGE(WININET_E_DECODING_FAILED, "Content decoding has failed, TLS 1.2 isn't configured correctly on the client.\nThis error generally means that the Windows Update Agent was unable to decode the received content. Install and configure TLS 1.2 by installing the update in [KB3140245](https://support.microsoft.com/topic/update-to-enable-tls-1-1-and-tls-1-2-as-default-secure-protocols-in-winhttp-in-windows-c4bd73d2-31d7-761e-0178-11268bb10392).");
		MACRO_ERROR_MESSAGE(WININET_E_TIMEOUT, "The operation timed out, Unable to scan for updates due to a connectivity issue to Windows Update, Configuration Manager, or WSUS.\nThis error generally means that the Windows Update Agent was unable to connect to the update servers or your own source, such as WSUS, Configuration Manager, or Microsoft Endpoint Manager. \r\n Check with your network team to ensure that the device can reach the update sources. For more info, see [Troubleshoot software update scan failures in Configuration Manager](/troubleshoot/mem/configmgr/troubleshoot-software-update-scan-failures). \r\n If you're using the public Microsoft update servers, check that your device can access the following Windows Update endpoints: \r\n `http://windowsupdate.microsoft.com` \r\n `https://*.windowsupdate.microsoft.com` \r\n `https://update.microsoft.com` \r\n `https://*.update.microsoft.com` \r\n `https://windowsupdate.com` \r\n `https://*.windowsupdate.com` \r\n `https://download.windowsupdate.com` \r\n `https://*.download.windowsupdate.com` \r\n `https://download.microsoft.com` \r\n `https://*.download.windowsupdate.com` \r\n `https://wustat.windows.com` \r\n `https://*.wustat.windows.com` \r\n `https://ntservicepack.microsoft.com`");
	default:
		return nullptr;
	}
}
