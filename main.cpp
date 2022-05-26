/*
 * MIT License
 *
 * Copyright (C) 2022 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * The MIT License (MIT)
 *
 * Copyright (c) M2-Team and Contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Windows.h>
#include <shellapi.h>
#include <wuapi.h>
#include <atlbase.h>
#include <io.h>
#include <fcntl.h>
#include <winrt\Windows.Foundation.h>
#include <winrt\Windows.Foundation.Collections.h>
#include <winrt\Windows.ApplicationModel.Store.Preview.InstallControl.h>
#include <iostream>

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Collections;
    using namespace Windows::ApplicationModel::Store::Preview::InstallControl;
}

[[maybe_unused]] static constexpr const wchar_t VT_DEFAULT[] = L"\x1b[0m";
[[maybe_unused]] static constexpr const wchar_t VT_BLACK[] = L"\x1b[1;90m";
[[maybe_unused]] static constexpr const wchar_t VT_RED[] = L"\x1b[1;91m";
[[maybe_unused]] static constexpr const wchar_t VT_GREEN[] = L"\x1b[1;92m";
[[maybe_unused]] static constexpr const wchar_t VT_YELLOW[] = L"\x1b[1;93m";
[[maybe_unused]] static constexpr const wchar_t VT_BLUE[] = L"\x1b[1;94m";
[[maybe_unused]] static constexpr const wchar_t VT_MAGENTA[] = L"\x1b[1;95m";
[[maybe_unused]] static constexpr const wchar_t VT_CYAN[] = L"\x1b[1;96m";
[[maybe_unused]] static constexpr const wchar_t VT_WHITE[] = L"\x1b[1;97m";

[[nodiscard]] static inline std::wstring GetSystemErrorMessage(const DWORD dwError)
{
    LPWSTR buffer = nullptr;
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr) == 0) {
        return L"FormatMessageW() returns empty string.";
    }
    const std::wstring result = buffer;
    LocalFree(buffer);
    buffer = nullptr;
    return result;
}

[[nodiscard]] static inline bool IsCurrentProcessElevated()
{
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID administratorsGroup = nullptr;
    BOOL result = AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administratorsGroup);
    if (result == FALSE) {
        std::wcerr << VT_RED << L"AllocateAndInitializeSid() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
    } else {
        if (CheckTokenMembership(nullptr, administratorsGroup, &result) == FALSE) {
            result = FALSE;
            std::wcerr << VT_RED << L"CheckTokenMembership() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
        }
        FreeSid(administratorsGroup);
    }
    return (result != FALSE);
}

[[nodiscard]] static inline std::wstring GetApplicationFilePath()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        std::wcerr << VT_RED << L"GetModuleFileNameW() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
        return L"";
    }
    return path;
}

static inline void RestartAsElevatedProcess()
{
    const std::wstring path = GetApplicationFilePath();

    SHELLEXECUTEINFOW sei;
    SecureZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);

    sei.lpVerb = L"runas";
    sei.nShow = SW_SHOW;
    sei.lpFile = path.c_str();
    sei.fMask = SEE_MASK_NOASYNC;

    if (ShellExecuteExW(&sei) == FALSE) {
        std::wcerr << VT_RED << L"ShellExecuteExW() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
    }
}

static inline void EnableMicrosoftUpdate()
{
    CComPtr<IUpdateServiceManager2> pUsm = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_UpdateServiceManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pUsm));
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"CoCreateInstance() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    const BSTR appId = SysAllocString(L"My App");
    hr = pUsm->put_ClientApplicationID(appId);
    if (FAILED(hr)) {
        SysFreeString(appId);
        std::wcerr << VT_RED << L"put_ClientApplicationID() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    const BSTR serviceId = SysAllocString(L"7971f918-a847-4430-9279-4a52d1efe18d");
    const BSTR authorizationCabPath = SysAllocString(L"");
    const DWORD flags = (AddServiceFlag::asfAllowPendingRegistration | AddServiceFlag::asfAllowOnlineRegistration | AddServiceFlag::asfRegisterServiceWithAU);
    CComPtr<IUpdateServiceRegistration> pUsr = nullptr;
    hr = pUsm->AddService2(serviceId, flags, authorizationCabPath, &pUsr);
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"AddService2() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
    }
    SysFreeString(authorizationCabPath);
    SysFreeString(serviceId);
    SysFreeString(appId);
}

static inline void UpdateStoreApps()
{
    std::wcout << VT_CYAN << L"Start updating Microsoft Store applications ..." << VT_DEFAULT << std::endl;

    while (true) {
        std::vector<HANDLE> completeSignals = {};

        winrt::AppInstallManager appInstallManager = {};
        const winrt::IVectorView<winrt::AppInstallItem> updateList = appInstallManager.SearchForAllUpdatesAsync().get();

        const int count = updateList.Size();
        if (count < 1) {
            break;
        }
        std::wcout << L"Found " << count << L" updates in total." << std::endl;

        for (auto &&update : std::as_const(updateList)) {
            std::wcout << L"Updating " << update.PackageFamilyName().c_str() << L" ..." << std::endl;

            const HANDLE completeSignal = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            if (!completeSignal) {
                std::wcerr << VT_RED << L"CreateEventExW() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
                return;
            }

            completeSignals.push_back(completeSignal);

            update.Completed([completeSignal](winrt::AppInstallItem const &sender, winrt::IInspectable const &args){
                UNREFERENCED_PARAMETER(args);

                std::wcout << sender.PackageFamilyName().c_str() << L" has been updated to the latest version." << std::endl;

                if (SetEvent(completeSignal) == FALSE) {
                    std::wcerr << VT_RED << L"SetEvent() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
                }
            });

            update.StatusChanged([](winrt::AppInstallItem const &sender, winrt::IInspectable const &args){
                UNREFERENCED_PARAMETER(sender);
                UNREFERENCED_PARAMETER(args);
                //sender.PackageFamilyName().c_str()
                //sender.GetCurrentStatus().PercentComplete()
            });
        }

        if (completeSignals.empty()) {
            break;
        } else {
            if (WaitForMultipleObjectsEx(static_cast<DWORD>(completeSignals.size()),
                         &completeSignals[0], TRUE, INFINITE, FALSE) == WAIT_FAILED) {
                std::wcerr << VT_RED << L"WaitForMultipleObjectsEx() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
            }

            for (auto &&signal : std::as_const(completeSignals)) {
                if (CloseHandle(signal) == FALSE) {
                    std::wcerr << VT_RED << L"CloseHandle() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
                }
            }
        }
    }

    std::wcout << VT_GREEN << L"Congratulations! All your Microsoft Store applications are update to date!" << VT_DEFAULT << std::endl;
    std::wcout << std::endl << std::endl;
}

static inline void UpdateSystem()
{
    std::wcout << VT_CYAN << L"Start updating system ..." << VT_DEFAULT << std::endl;

    CComPtr<IAutomaticUpdates2> pAu = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_AutomaticUpdates, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pAu));
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"CoCreateInstance() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAu->EnableService();
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"EnableService() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    CComPtr<IAutomaticUpdatesSettings3> pAus = nullptr;
    hr = pAu->get_Settings(reinterpret_cast<IAutomaticUpdatesSettings **>(&pAus));
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"get_Settings() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAus->Refresh();
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"Refresh() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAus->put_NotificationLevel(aunlScheduledInstallation);
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"put_NotificationLevel() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAus->put_IncludeRecommendedUpdates(VARIANT_TRUE);
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"put_IncludeRecommendedUpdates() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAus->put_FeaturedUpdatesEnabled(VARIANT_TRUE);
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"put_FeaturedUpdatesEnabled() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAus->put_NonAdministratorsElevated(VARIANT_TRUE);
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"put_NonAdministratorsElevated() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAus->Save();
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"Save() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    hr = pAu->DetectNow();
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"DetectNow() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }
    CComPtr<IAutomaticUpdatesResults> pAur = nullptr;
    hr = pAu->get_Results(&pAur);
    if (FAILED(hr)) {
        std::wcerr << VT_RED << L"get_Results() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << VT_DEFAULT << std::endl;
        return;
    }

    std::wcout << VT_GREEN << L"Congratulations! Your system is update to date!" << VT_DEFAULT << std::endl;
    std::wcout << std::endl << std::endl;
}

static inline void InitializeConsole()
{
    const auto EnableVTSequencesForConsole = [](const DWORD handleId) -> bool {
        const HANDLE handle = GetStdHandle(handleId);
        if (!handle || (handle == INVALID_HANDLE_VALUE)) {
            std::wcerr << VT_RED << L"GetStdHandle() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
            return false;
        }
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == FALSE) {
            std::wcerr << VT_RED << L"GetConsoleMode() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
            return false;
        }
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(handle, mode) == FALSE) {
            std::wcerr << VT_RED << L"SetConsoleMode() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
            return false;
        }
        return true;
    };
    EnableVTSequencesForConsole(STD_OUTPUT_HANDLE);
    EnableVTSequencesForConsole(STD_ERROR_HANDLE);

    if (SetConsoleOutputCP(CP_UTF8) == FALSE) {
        std::wcerr << VT_RED << L"SetConsoleOutputCP() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
    }

    if (SetConsoleTitleW(L"WinUpdate") == FALSE) {
        std::wcerr << VT_RED << L"SetConsoleTitleW() failed with error " << GetSystemErrorMessage(GetLastError()) << VT_DEFAULT << std::endl;
    }
}

extern "C" int APIENTRY wmain(int argc, wchar_t *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    InitializeConsole();

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    if (!IsCurrentProcessElevated()) {
        std::wcout << VT_YELLOW << L"This application requires the administrator privilege to run." << VT_DEFAULT << std::endl;
        std::wcout << L"Trying to restart this application as an elevated process ..." << std::endl;
        RestartAsElevatedProcess();
        return 0;
    }

    UpdateStoreApps();
    EnableMicrosoftUpdate();
    UpdateSystem();

    winrt::uninit_apartment();

    std::wcout << VT_MAGENTA << L"-- PRESS THE <ENTER> KEY TO EXIT --" << VT_DEFAULT << std::endl;
    getchar();

    return 0;
}