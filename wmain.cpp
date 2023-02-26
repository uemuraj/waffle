//
// Windows Update Agent API �� Windows Update �̌����A�_�E�����[�h�A�C���X�g�[��
// 
// * https://learn.microsoft.com/ja-jp/windows/win32/wua_sdk/searching--downloading--and-installing-updates
// 

#include <locale>
#include <iostream>
#include "waffle.h"

template<class Result, class Progress>
struct Callback
{
	long m_total;
	long m_index{ -1 };

	void operator()(long index, OperationResultCode code, IUpdate * update, Result * result, Progress * progress)
	{
		if (m_index < index)
		{
			m_index = index;
		}
		else
		{
			// TODO: ���z�[���̃G�X�P�[�v�V�[�P���X�𗘗p���Ĉ����߂�
		}

		// TODO: 100% ���R�[���o�b�N����Ȃ����Ƃ�����
		// TODO: �i���󋵂����ɂ��ď��������̗ʂ����炷

		switch (code)
		{
		case orcNotStarted:
		case orcSucceeded:
			break;
		case orcInProgress:
			std::wcout << progress << L' ' << update << std::endl;
			break;
		default:
			std::wcout << result << L' ' << update << std::endl;
			break;
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

		std::wcout << L"Searching for updates..." << std::endl;

		auto session = waffle::CreateSession();
		auto updates = session.Search(_bstr_t(szCriteria), msTimeout);

		if (!updates.empty())
		{
			session.Download(updates, Callback<IUpdateDownloadResult, IDownloadProgress>{ updates.size() });
			session.Install(updates, Callback<IUpdateInstallationResult, IInstallationProgress>{ updates.size() });
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
		std::wcerr << e.what() << std::endl;
		return -1;
	}
}
