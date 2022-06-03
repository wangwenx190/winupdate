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
#include <netlistmgr.h>
#include <atlbase.h>
#include <io.h>
#include <fcntl.h>
#include <VersionHelpers.h>
#include <winrt\Windows.Foundation.Collections.h>
#include <winrt\Windows.ApplicationModel.Store.Preview.InstallControl.h>

enum class ConsoleTextColor
{
    Default = 0,
    Black   = 1,
    Red     = 2,
    Green   = 3,
    Yellow  = 4,
    Blue    = 5,
    Magenta = 6,
    Cyan    = 7,
    White   = 8
};

static constexpr const int kVirtualTerminalForegroundColor[] =
{
    0, // Default
    30, // Black
    31, // Red
    32, // Green
    33, // Yellow
    34, // Blue
    35, // Magenta
    36, // Cyan
    37 // White
};

static constexpr const WORD kClassicForegroundColor[] =
{
    0, // Default
    0, // Black
    FOREGROUND_RED, // Red
    FOREGROUND_GREEN, // Green
    FOREGROUND_RED | FOREGROUND_GREEN, // Yellow
    FOREGROUND_BLUE, // Blue
    FOREGROUND_RED | FOREGROUND_BLUE, // Magenta
    FOREGROUND_GREEN | FOREGROUND_BLUE, // Cyan
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE // White
};

[[nodiscard]] static inline bool IsVirtualTerminalSequencesSupported()
{
    static const bool support = IsWindows10OrGreater();
    return support;
}

static inline void PrintToConsole(const std::wstring &text, const ConsoleTextColor color, const bool error)
{
    if (text.empty()) {
        return;
    }
    FILE * const channel = (error ? stderr : stdout);
    if (IsVirtualTerminalSequencesSupported()) {
        if (std::fwprintf(channel, L"\x1b[1;%dm%s\x1b[0m\r\n", kVirtualTerminalForegroundColor[static_cast<int>(color)], text.c_str()) < 1) {
            // ###
        }
    } else {
        const HANDLE hCon = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (!hCon || (hCon == INVALID_HANDLE_VALUE)) {
            return;
        }
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        SecureZeroMemory(&csbi, sizeof(csbi));
        if (GetConsoleScreenBufferInfo(hCon, &csbi) == FALSE) {
            return;
        }
        const WORD originalColor = csbi.wAttributes;
        const WORD newColor = ((color == ConsoleTextColor::Default) ? 0 : (kClassicForegroundColor[static_cast<int>(color)] | FOREGROUND_INTENSITY));
        if (SetConsoleTextAttribute(hCon, (newColor | (originalColor & 0xF0))) == FALSE) {
            return;
        }
        if (std::fwprintf(channel, L"%s\r\n", text.c_str()) < 1) {
            // ###
        }
        if (SetConsoleTextAttribute(hCon, originalColor) == FALSE) {
        }
    }
}

[[nodiscard]] static inline std::wstring GetSystemErrorMessage(const DWORD dwError)
{
    LPWSTR buffer = nullptr;
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr) == 0) {
        return L"FormatMessageW() returned empty string.";
    }
    const std::wstring result = buffer;
    LocalFree(buffer);
    buffer = nullptr;
    return result;
}

static inline void PrintError(const std::wstring &message)
{
    if (message.empty()) {
        return;
    }
    PrintToConsole(message, ConsoleTextColor::Red, true);
}

static inline void PrintError(const std::wstring &name, const DWORD dwError)
{
    if (name.empty() || (dwError == ERROR_SUCCESS)) {
        return;
    }
    const std::wstring errorMessage = GetSystemErrorMessage(dwError);
    const std::wstring text = name + L"() failed with error " + errorMessage;
    PrintError(text);
}

static inline void PrintWarning(const std::wstring &message)
{
    if (message.empty()) {
        return;
    }
    PrintToConsole(message, ConsoleTextColor::Yellow, true);
}

static inline void PrintInfo(const std::wstring &message)
{
    if (message.empty()) {
        return;
    }
    PrintToConsole(message, ConsoleTextColor::Cyan, false);
}

static inline void PrintSuccess(const std::wstring &message)
{
    if (message.empty()) {
        return;
    }
    const std::wstring text = L"Congratulations! " + message;
    PrintToConsole(text, ConsoleTextColor::Green, false);
}

[[nodiscard]] static inline bool IsInternetAvailable()
{
    CComPtr<IUnknown> pUnknown = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pUnknown));
    if (FAILED(hr)) {
        PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
        return false;
    }
    CComPtr<INetworkListManager> pNetworkListManager = nullptr;
    hr = pUnknown->QueryInterface(IID_PPV_ARGS(&pNetworkListManager));
    if (FAILED(hr)) {
        PrintError(L"QueryInterface", HRESULT_CODE(hr));
        return false;
    }
    VARIANT_BOOL isConnected = VARIANT_FALSE;
    hr = pNetworkListManager->get_IsConnectedToInternet(&isConnected);
    if (FAILED(hr)) {
        PrintError(L"get_IsConnectedToInternet", HRESULT_CODE(hr));
        return false;
    }
    if (isConnected == VARIANT_FALSE) {
        return false;
    }
#if 0
    NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
    hr = pNetworkListManager->GetConnectivity(&connectivity);
    if (FAILED(hr)) {
        PrintError(L"GetConnectivity", HRESULT_CODE(hr));
        return false;
    }
    return ((connectivity == NLM_CONNECTIVITY_IPV4_INTERNET)
            || (connectivity == NLM_CONNECTIVITY_IPV6_INTERNET));
#else
    return true;
#endif
}

[[nodiscard]] static inline bool IsCurrentProcessElevated()
{
    static const bool admin = []() -> bool {
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID administratorsGroup = nullptr;
        BOOL result = AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administratorsGroup);
        if (result == FALSE) {
            PrintError(L"AllocateAndInitializeSid", GetLastError());
        } else {
            if (CheckTokenMembership(nullptr, administratorsGroup, &result) == FALSE) {
                result = FALSE;
                PrintError(L"CheckTokenMembership", GetLastError());
            }
            if (FreeSid(administratorsGroup) != nullptr) {
                PrintError(L"FreeSid", GetLastError());
            }
        }
        return (result != FALSE);
    }();
    return admin;
}

[[nodiscard]] static inline std::wstring GetApplicationFilePath()
{
    static const std::wstring result = []() -> std::wstring {
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
            PrintError(L"GetModuleFileNameW", GetLastError());
            return L"";
        }
        return path;
    }();
    return result;
}

static inline void RestartAsElevatedProcess()
{
    static const std::wstring path = GetApplicationFilePath();

    SHELLEXECUTEINFOW sei;
    SecureZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);

    sei.lpVerb = L"runas";
    sei.nShow = SW_SHOW;
    sei.lpFile = path.c_str();
    sei.fMask = SEE_MASK_NOASYNC;

    if (ShellExecuteExW(&sei) == FALSE) {
        PrintError(L"ShellExecuteExW", GetLastError());
    }
}

static inline void EnableMicrosoftUpdate()
{
    static const bool win10 = IsWindows10OrGreater();
    if (!win10) {
        return;
    }
    CComPtr<IUpdateServiceManager2> pUpdateServiceManager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_UpdateServiceManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pUpdateServiceManager));
    if (FAILED(hr)) {
        PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
        return;
    }
    const BSTR appId = SysAllocString(L"My App");
    hr = pUpdateServiceManager->put_ClientApplicationID(appId);
    if (FAILED(hr)) {
        SysFreeString(appId);
        PrintError(L"put_ClientApplicationID", HRESULT_CODE(hr));
        return;
    }
    const BSTR serviceId = SysAllocString(L"7971f918-a847-4430-9279-4a52d1efe18d");
    const BSTR authorizationCabPath = SysAllocString(L"");
    const DWORD flags = (AddServiceFlag::asfAllowPendingRegistration | AddServiceFlag::asfAllowOnlineRegistration | AddServiceFlag::asfRegisterServiceWithAU);
    CComPtr<IUpdateServiceRegistration> pUpdateServiceRegistration = nullptr;
    hr = pUpdateServiceManager->AddService2(serviceId, flags, authorizationCabPath, &pUpdateServiceRegistration);
    if (FAILED(hr)) {
        PrintError(L"AddService2", HRESULT_CODE(hr));
    }
    SysFreeString(authorizationCabPath);
    SysFreeString(serviceId);
    SysFreeString(appId);
}

static inline void UpdateStoreApps()
{
    static const bool win10 = IsWindows10OrGreater();
    if (!win10) {
        return;
    }

    PrintInfo(L"Start updating Microsoft Store applications ...");

    while (true) {
        std::vector<HANDLE> completeSignals = {};

        winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallManager appInstallManager = {};
        const winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallItem> updateList = appInstallManager.SearchForAllUpdatesAsync().get();

        const int count = updateList.Size();
        if (count < 1) {
            break;
        }

        for (auto &&update : std::as_const(updateList)) {
            const std::wstring message = std::wstring(L"Updating ") + update.PackageFamilyName().c_str() + std::wstring(L" ...");
            PrintToConsole(message, ConsoleTextColor::Default, false);

            const HANDLE completeSignal = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            if (!completeSignal) {
                PrintError(L"CreateEventExW", GetLastError());
                return;
            }

            completeSignals.push_back(completeSignal);

            update.Completed([completeSignal](winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallItem const &sender, winrt::Windows::Foundation::IInspectable const &args){
                UNREFERENCED_PARAMETER(sender);
                UNREFERENCED_PARAMETER(args);

                if (SetEvent(completeSignal) == FALSE) {
                    PrintError(L"SetEvent", GetLastError());
                }
            });

            update.StatusChanged([](winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallItem const &sender, winrt::Windows::Foundation::IInspectable const &args){
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
                PrintError(L"WaitForMultipleObjectsEx", GetLastError());
            }

            for (auto &&signal : std::as_const(completeSignals)) {
                if (CloseHandle(signal) == FALSE) {
                    PrintError(L"CloseHandle", GetLastError());
                }
            }
        }
    }

    PrintSuccess(L"All your Microsoft Store applications are update to date!\r\n\r\n");
}

static inline void UpdateSystem()
{
    PrintInfo(L"Start updating system ...");

    static const bool win10 = IsWindows10OrGreater();

    while (true) {
#if 0
        CComPtr<IAutomaticUpdates2> pAutomaticUpdates = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_AutomaticUpdates, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pAutomaticUpdates));
        if (FAILED(hr)) {
            PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
            return;
        }
#if 0 // "EnableService()" always fail, don't know why.
        hr = pAutomaticUpdates->EnableService();
        if (FAILED(hr)) {
            PrintError(L"EnableService", HRESULT_CODE(hr));
            return;
        }
#endif
        CComPtr<IAutomaticUpdatesSettings3> pAutomaticUpdatesSettings = nullptr;
        hr = pAutomaticUpdates->get_Settings(reinterpret_cast<IAutomaticUpdatesSettings **>(&pAutomaticUpdatesSettings));
        if (FAILED(hr)) {
            PrintError(L"get_Settings", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->Refresh();
        if (FAILED(hr)) {
            PrintError(L"Refresh", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_NotificationLevel(aunlScheduledInstallation);
        if (FAILED(hr)) {
            PrintError(L"put_NotificationLevel", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_IncludeRecommendedUpdates(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"put_IncludeRecommendedUpdates", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_FeaturedUpdatesEnabled(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"put_FeaturedUpdatesEnabled", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_NonAdministratorsElevated(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"put_NonAdministratorsElevated", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->Save();
        if (FAILED(hr)) {
            PrintError(L"Save", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdates->DetectNow();
        if (FAILED(hr)) {
            PrintError(L"DetectNow", HRESULT_CODE(hr));
            return;
        }
        CComPtr<IAutomaticUpdatesResults> pAutomaticUpdatesResults = nullptr;
        hr = pAutomaticUpdates->get_Results(&pAutomaticUpdatesResults);
        if (FAILED(hr)) {
            PrintError(L"get_Results", HRESULT_CODE(hr));
            return;
        }
        break;
#else
        CComPtr<IUpdateSession3> pUpdateSession = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_UpdateSession, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pUpdateSession));
        if (FAILED(hr)) {
            PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
            return;
        }
        const BSTR appId = SysAllocString(L"My App");
        hr = pUpdateSession->put_ClientApplicationID(appId);
        if (FAILED(hr)) {
            SysFreeString(appId);
            PrintError(L"put_ClientApplicationID", HRESULT_CODE(hr));
            return;
        }
        CComPtr<IUpdateSearcher3> pUpdateSearcher = nullptr;
        hr = pUpdateSession->CreateUpdateSearcher(reinterpret_cast<IUpdateSearcher **>(&pUpdateSearcher));
        if (FAILED(hr)) {
            SysFreeString(appId);
            PrintError(L"CreateUpdateSearcher", HRESULT_CODE(hr));
            return;
        }
#if 0 // "put_CanAutomaticallyUpgradeService()" always fail, don't know why.
        hr = pUpdateSearcher->put_CanAutomaticallyUpgradeService(VARIANT_TRUE);
        if (FAILED(hr)) {
            SysFreeString(appId);
            PrintError(L"put_CanAutomaticallyUpgradeService", HRESULT_CODE(hr));
            return;
        }
#endif
        hr = pUpdateSearcher->put_Online(VARIANT_TRUE);
        if (FAILED(hr)) {
            SysFreeString(appId);
            PrintError(L"put_Online", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateSearcher->put_ServerSelection(ssWindowsUpdate);
        if (FAILED(hr)) {
            SysFreeString(appId);
            PrintError(L"put_ServerSelection", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateSearcher->put_IncludePotentiallySupersededUpdates(VARIANT_FALSE);
        if (FAILED(hr)) {
            SysFreeString(appId);
            PrintError(L"put_IncludePotentiallySupersededUpdates", HRESULT_CODE(hr));
            return;
        }
        const BSTR criteria = SysAllocString(L"( IsInstalled = 0 AND IsHidden = 0 )");
        CComPtr<ISearchResult> pSearchResult = nullptr;
        hr = pUpdateSearcher->Search(criteria, &pSearchResult);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"Search", HRESULT_CODE(hr));
            return;
        }
        OperationResultCode searchResultCode = orcNotStarted;
        hr = pSearchResult->get_ResultCode(&searchResultCode);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"get_ResultCode", HRESULT_CODE(hr));
            return;
        }
        if (searchResultCode != orcSucceeded) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"Failed to search for Windows updates.");
            return;
        }
        CComPtr<IUpdateCollection> pUpdateCollection = nullptr;
        hr = pSearchResult->get_Updates(&pUpdateCollection);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"get_Updates", HRESULT_CODE(hr));
            return;
        }
        LONG updateCount = 0;
        hr = pUpdateCollection->get_Count(&updateCount);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"get_Count", HRESULT_CODE(hr));
            return;
        }
        if (updateCount < 1) {
            SysFreeString(criteria);
            SysFreeString(appId);
            break;
        }
        CComPtr<IUpdateDownloader> pUpdateDownloader = nullptr;
        hr = pUpdateSession->CreateUpdateDownloader(&pUpdateDownloader);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"CreateUpdateDownloader", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateDownloader->put_Updates(pUpdateCollection);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"put_Updates", HRESULT_CODE(hr));
            return;
        }
        CComPtr<IDownloadResult> pDownloadResult = nullptr;
        hr = pUpdateDownloader->Download(&pDownloadResult);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"Download", HRESULT_CODE(hr));
            return;
        }
        OperationResultCode downloadResultCode = orcNotStarted;
        hr = pDownloadResult->get_ResultCode(&downloadResultCode);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"get_ResultCode", HRESULT_CODE(hr));
            return;
        }
        if (downloadResultCode != orcSucceeded) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"Failed to download Windows updates.");
            return;
        }
        CComPtr<IUpdateInstaller4> pUpdateInstaller = nullptr;
        hr = pUpdateSession->CreateUpdateInstaller(reinterpret_cast<IUpdateInstaller **>(&pUpdateInstaller));
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"CreateUpdateInstaller", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateInstaller->put_Updates(pUpdateCollection);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"put_Updates", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateInstaller->put_ForceQuiet(VARIANT_TRUE);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"put_ForceQuiet", HRESULT_CODE(hr));
            return;
        }
        if (win10) {
            hr = pUpdateInstaller->put_AttemptCloseAppsIfNecessary(VARIANT_TRUE);
            if (FAILED(hr)) {
                SysFreeString(criteria);
                SysFreeString(appId);
                PrintError(L"put_AttemptCloseAppsIfNecessary", HRESULT_CODE(hr));
                return;
            }
        }
        CComPtr<IInstallationResult> pInstallationResult = nullptr;
        hr = pUpdateInstaller->Install(&pInstallationResult);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"Install", HRESULT_CODE(hr));
            return;
        }
        OperationResultCode installationResultCode = orcNotStarted;
        hr = pInstallationResult->get_ResultCode(&installationResultCode);
        if (FAILED(hr)) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"get_ResultCode", HRESULT_CODE(hr));
            return;
        }
        if (installationResultCode != orcSucceeded) {
            SysFreeString(criteria);
            SysFreeString(appId);
            PrintError(L"Failed to install Windows updates.");
            return;
        }
        if (win10) {
            hr = pUpdateInstaller->Commit(0);
            if (FAILED(hr)) {
                SysFreeString(criteria);
                SysFreeString(appId);
                PrintError(L"Commit", HRESULT_CODE(hr));
                return;
            }
        }
        SysFreeString(criteria);
        SysFreeString(appId);
#endif
    }

    PrintSuccess(L"Your system is update to date!\r\n\r\n");
}

static inline void InitializeConsole()
{
    static const bool win10 = IsWindows10OrGreater();
    if (win10) {
        const auto EnableVTSequencesForConsole = [](const DWORD handleId) -> bool {
            const HANDLE handle = GetStdHandle(handleId);
            if (!handle || (handle == INVALID_HANDLE_VALUE)) {
                PrintError(L"GetStdHandle", GetLastError());
                return false;
            }
            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode) == FALSE) {
                PrintError(L"GetConsoleMode", GetLastError());
                return false;
            }
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(handle, mode) == FALSE) {
                PrintError(L"SetConsoleMode", GetLastError());
                return false;
            }
            return true;
        };
        EnableVTSequencesForConsole(STD_OUTPUT_HANDLE);
        EnableVTSequencesForConsole(STD_ERROR_HANDLE);
    }

    if (SetConsoleOutputCP(CP_UTF8) == FALSE) {
        PrintError(L"SetConsoleOutputCP", GetLastError());
    }

    if (SetConsoleTitleW(L"WinUpdate") == FALSE) {
        PrintError(L"SetConsoleTitleW", GetLastError());
    }
}

extern "C" int APIENTRY wmain(int argc, wchar_t *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);

    InitializeConsole();

    if (IsInternetAvailable()) {
        if (!IsCurrentProcessElevated()) {
            PrintWarning(L"This application requires the administrator privilege to run.");
            PrintToConsole(L"Trying to restart this application as an elevated process ...", ConsoleTextColor::Default, false);
            RestartAsElevatedProcess();
            winrt::uninit_apartment();
            return 0;
        }

        UpdateStoreApps();
        EnableMicrosoftUpdate();
        UpdateSystem();
    } else {
        PrintError(L"You need to connect to the Internet first!\r\n");
    }

    winrt::uninit_apartment();

    PrintToConsole(L"\r\n-- PRESS THE <ENTER> KEY TO EXIT --", ConsoleTextColor::Magenta, false);
    getchar();

    return 0;
}
