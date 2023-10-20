/*
 * MIT License
 *
 * Copyright (C) 2023 by wangwenx190 (Yuhang Zhao)
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

#include <windows.h>
#include <wuapi.h>
#include <netlistmgr.h>
#include <io.h>
#include <fcntl.h>
#include <versionhelpers.h>
#include <wrl/client.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.applicationmodel.store.preview.installcontrol.h>
#include <clocale>
#include <array>
#include <syscmdline/system.h>
#include <syscmdline/option.h>
#include <syscmdline/command.h>
#include <syscmdline/parser.h>

namespace WinUpdate
{

enum class ConsoleTextColor : uint8_t
{
    Default,
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White
};

static constexpr const wchar_t kAppName[] = L"Windows Updater";
static constexpr const auto kCodePage = UINT{ CP_UTF8 };

static constexpr const std::array<uint8_t, 9> kVirtualTerminalForegroundColor =
{
     0, // Default
    30, // Black
    31, // Red
    32, // Green
    33, // Yellow
    34, // Blue
    35, // Magenta
    36, // Cyan
    37  // White
};

static constexpr const std::array<WORD, 9> kClassicForegroundColor =
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

struct ScopedBSTR
{
    ScopedBSTR(const OLECHAR *psz)
    {
        str = ::SysAllocString(psz);
    }

    ~ScopedBSTR()
    {
        release();
    }

    [[nodiscard]] inline bool is_valid() const
    {
        return str != nullptr;
    }

    [[nodiscard]] inline BSTR data() const
    {
        return str;
    }

    inline void release()
    {
        if (str) {
            ::SysFreeString(str);
            str = nullptr;
        }
    }

    inline operator bool() const
    {
        return is_valid();
    }
    
    inline operator BSTR() const
    {
        return data();
    }

private:
    BSTR str = nullptr;
};

struct LocaleGuard
{
    LocaleGuard()
    {
        m_outMode = _setmode(_fileno(stdout), _O_U16TEXT);
        m_errMode = _setmode(_fileno(stderr), _O_U16TEXT);
    }

    ~LocaleGuard()
    {
        _setmode(_fileno(stdout), m_outMode);
        _setmode(_fileno(stderr), m_errMode);
    }

private:
    int m_outMode = 0;
    int m_errMode = 0;
};

[[nodiscard]] static inline bool IsVirtualTerminalSequencesSupported()
{
    static const bool support = ::IsWindows10OrGreater();
    return support;
}

static inline void PrintToConsole(const std::wstring_view text, const ConsoleTextColor color, const bool error)
{
    if (text.empty()) {
        return;
    }
    FILE *channel = (error ? stderr : stdout);
    if (IsVirtualTerminalSequencesSupported()) {
        const LocaleGuard localeGuard{};
        if (std::fwprintf(channel, L"\x1b[1;%dm%s\x1b[0m\n", kVirtualTerminalForegroundColor.at(static_cast<uint8_t>(color)), text.data()) < 1) {
            // ###
        }
    } else {
        const HANDLE hCon = ::GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (!hCon || (hCon == INVALID_HANDLE_VALUE)) {
            return;
        }
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        SecureZeroMemory(&csbi, sizeof(csbi));
        if (::GetConsoleScreenBufferInfo(hCon, &csbi) == FALSE) {
            return;
        }
        const WORD originalColor = csbi.wAttributes;
        const WORD newColor = ((color == ConsoleTextColor::Default) ? 0 : (kClassicForegroundColor.at(static_cast<uint8_t>(color)) | FOREGROUND_INTENSITY));
        if (::SetConsoleTextAttribute(hCon, (newColor | (originalColor & 0xF0))) == FALSE) {
            return;
        }
        {
            const LocaleGuard localeGuard{};
            if (std::fwprintf(channel, L"%s\n", text.data()) < 1) {
                // ###
            }
        }
        if (::SetConsoleTextAttribute(hCon, originalColor) == FALSE) {
        }
    }
}

[[nodiscard]] static inline std::wstring GetSystemErrorMessage(const DWORD dwError)
{
    if (dwError == ERROR_SUCCESS) {
        return {};
    }
    LPWSTR buffer = nullptr;
    if (::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr) == 0) {
        return {};
    }
    const std::wstring result(buffer);
    ::LocalFree(buffer);
    buffer = nullptr;
    return result;
}

static inline void PrintError(const std::wstring_view message)
{
    if (message.empty()) {
        return;
    }
    PrintToConsole(message, ConsoleTextColor::Red, true);
}

static inline void PrintError(const std::wstring_view name, const DWORD dwError)
{
    if (name.empty() || (dwError == ERROR_SUCCESS)) {
        return;
    }
    PrintError(L"Function \"" + std::wstring(name) + L"\" failed with error message: " + GetSystemErrorMessage(dwError));
}

static inline void PrintInfo(const std::wstring_view message)
{
    if (message.empty()) {
        return;
    }
    PrintToConsole(message, ConsoleTextColor::Default, false);
}

static inline void PrintSuccess(const std::wstring_view message)
{
    if (message.empty()) {
        return;
    }
    PrintToConsole(message, ConsoleTextColor::Green, false);
}

[[nodiscard]] static inline bool IsInternetAvailable()
{
    Microsoft::WRL::ComPtr<IUnknown> pUnknown = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(pUnknown.GetAddressOf()));
    if (FAILED(hr)) {
        PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
        return false;
    }
    Microsoft::WRL::ComPtr<INetworkListManager> pNetworkListManager = nullptr;
    hr = pUnknown->QueryInterface(IID_PPV_ARGS(pNetworkListManager.GetAddressOf()));
    if (FAILED(hr)) {
        PrintError(L"IUnknown::QueryInterface", HRESULT_CODE(hr));
        return false;
    }
    VARIANT_BOOL isConnected = VARIANT_FALSE;
    hr = pNetworkListManager->get_IsConnectedToInternet(&isConnected);
    if (FAILED(hr)) {
        PrintError(L"INetworkListManager::get_IsConnectedToInternet", HRESULT_CODE(hr));
        return false;
    }
    if (isConnected == VARIANT_FALSE) {
        return false;
    }
#if 0
    NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
    hr = pNetworkListManager->GetConnectivity(&connectivity);
    if (FAILED(hr)) {
        PrintError(L"INetworkListManager::GetConnectivity", HRESULT_CODE(hr));
        return false;
    }
    return ((connectivity == NLM_CONNECTIVITY_IPV4_INTERNET) || (connectivity == NLM_CONNECTIVITY_IPV6_INTERNET));
#else
    return true;
#endif
}

static inline void UpdateMicrosoftStoreApps()
{
    static const bool win10 = ::IsWindows10OrGreater();
    if (!win10) {
        return;
    }

    PrintToConsole(L"Start updating Microsoft Store applications ......", ConsoleTextColor::Cyan, false);

    while (true) {
        std::vector<HANDLE> completeSignals = {};

        winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallManager appInstallManager = {};
        const winrt::Windows::Foundation::Collections::IVectorView<winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallItem> updateList = appInstallManager.SearchForAllUpdatesAsync().get();

        const uint32_t count = updateList.Size();
        if (count < 1) {
            break;
        }

        for (auto &&update : std::as_const(updateList)) {
            const std::wstring message = std::wstring(L"Updating ") + update.PackageFamilyName().c_str() + std::wstring(L" ......");
            PrintInfo(message);

            const HANDLE completeSignal = ::CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            if (!completeSignal) {
                PrintError(L"CreateEventExW", ::GetLastError());
                return;
            }

            completeSignals.push_back(completeSignal);

            update.Completed([completeSignal](winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallItem const &sender, winrt::Windows::Foundation::IInspectable const &args){
                UNREFERENCED_PARAMETER(args);

                const std::wstring message = sender.PackageFamilyName().c_str() + std::wstring(L" has been successfully updated.");
                PrintSuccess(message);

                if (::SetEvent(completeSignal) == FALSE) {
                    PrintError(L"SetEvent", ::GetLastError());
                }
            });

            update.StatusChanged([](winrt::Windows::ApplicationModel::Store::Preview::InstallControl::AppInstallItem const &sender, winrt::Windows::Foundation::IInspectable const &args){
                UNREFERENCED_PARAMETER(args);

                const std::wstring title = std::wstring(L"Downloading ") + sender.PackageFamilyName().c_str() + std::wstring(L": ") + std::to_wstring(sender.GetCurrentStatus().PercentComplete()) + L'%';
                if (::SetConsoleTitleW(title.c_str()) == FALSE) {
                    PrintError(L"SetConsoleTitleW", ::GetLastError());
                }
            });
        }

        if (completeSignals.empty()) {
            break;
        } else {
            if (::WaitForMultipleObjectsEx(DWORD(completeSignals.size()), completeSignals.data(), TRUE, INFINITE, FALSE) == WAIT_FAILED) {
                PrintError(L"WaitForMultipleObjectsEx", ::GetLastError());
            }

            for (auto &&signal : std::as_const(completeSignals)) {
                if (::CloseHandle(signal) == FALSE) {
                    PrintError(L"CloseHandle", ::GetLastError());
                }
            }
        }
    }

    PrintSuccess(L"All your Microsoft Store applications are update to date!");
}

static inline void UpdateSystem()
{
    PrintToConsole(L"Start updating Windows ......", ConsoleTextColor::Cyan, false);

    [[maybe_unused]] static const bool win10 = ::IsWindows10OrGreater();

    while (true) {
#if 0
        Microsoft::WRL::ComPtr<IAutomaticUpdates2> pAutomaticUpdates = nullptr;
        HRESULT hr = ::CoCreateInstance(CLSID_AutomaticUpdates, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pAutomaticUpdates.GetAddressOf()));
        if (FAILED(hr)) {
            PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
            return;
        }
#if 0 // "EnableService()" always fail, don't know why.
        hr = pAutomaticUpdates->EnableService();
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdates2::EnableService", HRESULT_CODE(hr));
            return;
        }
#endif
        Microsoft::WRL::ComPtr<IAutomaticUpdatesSettings3> pAutomaticUpdatesSettings = nullptr;
        hr = pAutomaticUpdates->get_Settings(reinterpret_cast<IAutomaticUpdatesSettings **>(pAutomaticUpdatesSettings.GetAddressOf()));
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdates2::get_Settings", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->Refresh();
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdatesSettings3::Refresh", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_NotificationLevel(aunlScheduledInstallation);
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdatesSettings3::put_NotificationLevel", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_IncludeRecommendedUpdates(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdatesSettings3::put_IncludeRecommendedUpdates", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_FeaturedUpdatesEnabled(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdatesSettings3::put_FeaturedUpdatesEnabled", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->put_NonAdministratorsElevated(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdatesSettings3::put_NonAdministratorsElevated", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdatesSettings->Save();
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdatesSettings3::Save", HRESULT_CODE(hr));
            return;
        }
        hr = pAutomaticUpdates->DetectNow();
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdates2::DetectNow", HRESULT_CODE(hr));
            return;
        }
        Microsoft::WRL::ComPtr<IAutomaticUpdatesResults> pAutomaticUpdatesResults = nullptr;
        hr = pAutomaticUpdates->get_Results(pAutomaticUpdatesResults.GetAddressOf());
        if (FAILED(hr)) {
            PrintError(L"IAutomaticUpdates2::get_Results", HRESULT_CODE(hr));
            return;
        }
        break;
#else
        Microsoft::WRL::ComPtr<IUpdateSession3> pUpdateSession = nullptr;
        HRESULT hr = ::CoCreateInstance(CLSID_UpdateSession, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(pUpdateSession.GetAddressOf()));
        if (FAILED(hr)) {
            PrintError(L"CoCreateInstance", HRESULT_CODE(hr));
            return;
        }
        const BSTR appId = ScopedBSTR(kAppName);
        hr = pUpdateSession->put_ClientApplicationID(appId);
        if (FAILED(hr)) {
            PrintError(L"IUpdateSession3::put_ClientApplicationID", HRESULT_CODE(hr));
            return;
        }
        Microsoft::WRL::ComPtr<IUpdateSearcher3> pUpdateSearcher = nullptr;
        hr = pUpdateSession->CreateUpdateSearcher(reinterpret_cast<IUpdateSearcher **>(pUpdateSearcher.GetAddressOf()));
        if (FAILED(hr)) {
            PrintError(L"IUpdateSession3::CreateUpdateSearcher", HRESULT_CODE(hr));
            return;
        }
#if 0 // "put_CanAutomaticallyUpgradeService()" always fail, don't know why.
        hr = pUpdateSearcher->put_CanAutomaticallyUpgradeService(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"IUpdateSearcher3::put_CanAutomaticallyUpgradeService", HRESULT_CODE(hr));
            return;
        }
#endif
        hr = pUpdateSearcher->put_Online(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"IUpdateSearcher3::put_Online", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateSearcher->put_ServerSelection(ssWindowsUpdate);
        if (FAILED(hr)) {
            PrintError(L"IUpdateSearcher3::put_ServerSelection", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateSearcher->put_IncludePotentiallySupersededUpdates(VARIANT_FALSE);
        if (FAILED(hr)) {
            PrintError(L"IUpdateSearcher3::put_IncludePotentiallySupersededUpdates", HRESULT_CODE(hr));
            return;
        }
        const BSTR criteria = ScopedBSTR(L"( IsInstalled = 0 AND IsHidden = 0 )");
        Microsoft::WRL::ComPtr<ISearchResult> pSearchResult = nullptr;
        hr = pUpdateSearcher->Search(criteria, pSearchResult.GetAddressOf());
        if (FAILED(hr)) {
            PrintError(L"IUpdateSearcher3::Search", HRESULT_CODE(hr));
            return;
        }
        OperationResultCode searchResultCode = orcNotStarted;
        hr = pSearchResult->get_ResultCode(&searchResultCode);
        if (FAILED(hr)) {
            PrintError(L"ISearchResult::get_ResultCode", HRESULT_CODE(hr));
            return;
        }
        if (searchResultCode != orcSucceeded) {
            PrintError(L"Failed to search for Windows updates.");
            return;
        }
        Microsoft::WRL::ComPtr<IUpdateCollection> pUpdateCollection = nullptr;
        hr = pSearchResult->get_Updates(pUpdateCollection.GetAddressOf());
        if (FAILED(hr)) {
            PrintError(L"ISearchResult::get_Updates", HRESULT_CODE(hr));
            return;
        }
        LONG updateCount = 0;
        hr = pUpdateCollection->get_Count(&updateCount);
        if (FAILED(hr)) {
            PrintError(L"IUpdateCollection::get_Count", HRESULT_CODE(hr));
            return;
        }
        if (updateCount < 1) {
            break;
        }
        Microsoft::WRL::ComPtr<IUpdateDownloader> pUpdateDownloader = nullptr;
        hr = pUpdateSession->CreateUpdateDownloader(pUpdateDownloader.GetAddressOf());
        if (FAILED(hr)) {
            PrintError(L"IUpdateSession3::CreateUpdateDownloader", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateDownloader->put_Updates(pUpdateCollection.Get());
        if (FAILED(hr)) {
            PrintError(L"IUpdateDownloader::put_Updates", HRESULT_CODE(hr));
            return;
        }
        Microsoft::WRL::ComPtr<IDownloadResult> pDownloadResult = nullptr;
        hr = pUpdateDownloader->Download(pDownloadResult.GetAddressOf());
        if (FAILED(hr)) {
            PrintError(L"IUpdateDownloader::Download", HRESULT_CODE(hr));
            return;
        }
        OperationResultCode downloadResultCode = orcNotStarted;
        hr = pDownloadResult->get_ResultCode(&downloadResultCode);
        if (FAILED(hr)) {
            PrintError(L"IDownloadResult::get_ResultCode", HRESULT_CODE(hr));
            return;
        }
        if (downloadResultCode != orcSucceeded) {
            PrintError(L"Failed to download Windows updates.");
            return;
        }
        Microsoft::WRL::ComPtr<IUpdateInstaller4> pUpdateInstaller = nullptr;
        hr = pUpdateSession->CreateUpdateInstaller(reinterpret_cast<IUpdateInstaller **>(pUpdateInstaller.GetAddressOf()));
        if (FAILED(hr)) {
            PrintError(L"IUpdateSession3::CreateUpdateInstaller", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateInstaller->put_Updates(pUpdateCollection.Get());
        if (FAILED(hr)) {
            PrintError(L"IUpdateInstaller4::put_Updates", HRESULT_CODE(hr));
            return;
        }
        hr = pUpdateInstaller->put_ForceQuiet(VARIANT_TRUE);
        if (FAILED(hr)) {
            PrintError(L"IUpdateInstaller4::put_ForceQuiet", HRESULT_CODE(hr));
            return;
        }
        if (win10) {
            hr = pUpdateInstaller->put_AttemptCloseAppsIfNecessary(VARIANT_TRUE);
            if (FAILED(hr)) {
                PrintError(L"IUpdateInstaller4::put_AttemptCloseAppsIfNecessary", HRESULT_CODE(hr));
                return;
            }
        }
        Microsoft::WRL::ComPtr<IInstallationResult> pInstallationResult = nullptr;
        hr = pUpdateInstaller->Install(pInstallationResult.GetAddressOf());
        if (FAILED(hr)) {
            PrintError(L"IUpdateInstaller4::Install", HRESULT_CODE(hr));
            return;
        }
        OperationResultCode installationResultCode = orcNotStarted;
        hr = pInstallationResult->get_ResultCode(&installationResultCode);
        if (FAILED(hr)) {
            PrintError(L"IInstallationResult::get_ResultCode", HRESULT_CODE(hr));
            return;
        }
        if (installationResultCode != orcSucceeded) {
            PrintError(L"Failed to install Windows updates.");
            return;
        }
        if (win10) {
            hr = pUpdateInstaller->Commit(0);
            if (FAILED(hr)) {
                PrintError(L"IUpdateInstaller4::Commit", HRESULT_CODE(hr));
                return;
            }
        }
#endif
    }

    PrintSuccess(L"Your Windows is update to date!");
}

static inline void InitializeConsole()
{
    static const bool win10 = ::IsWindows10OrGreater();
    if (win10) {
        const auto EnableVTSequencesForConsole = [](const DWORD handleId) -> bool {
            const HANDLE handle = ::GetStdHandle(handleId);
            if (!handle || (handle == INVALID_HANDLE_VALUE)) {
                PrintError(L"GetStdHandle", ::GetLastError());
                return false;
            }
            DWORD mode = 0;
            if (::GetConsoleMode(handle, &mode) == FALSE) {
                PrintError(L"GetConsoleMode", ::GetLastError());
                return false;
            }
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (::SetConsoleMode(handle, mode) == FALSE) {
                PrintError(L"SetConsoleMode", ::GetLastError());
                return false;
            }
            return true;
        };
        EnableVTSequencesForConsole(STD_OUTPUT_HANDLE);
        EnableVTSequencesForConsole(STD_ERROR_HANDLE);
    }

    if (::SetConsoleCP(kCodePage) == FALSE) {
        PrintError(L"SetConsoleCP", ::GetLastError());
    }
    if (::SetConsoleOutputCP(kCodePage) == FALSE) {
        PrintError(L"SetConsoleOutputCP", ::GetLastError());
    }

    if (::SetConsoleTitleW(kAppName) == FALSE) {
        PrintError(L"SetConsoleTitleW", ::GetLastError());
    }
}

} // namespace WinUpdate

EXTERN_C int WINAPI wmain(int argc, wchar_t *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    std::setlocale(LC_ALL, "en_US.UTF-8");
    WinUpdate::InitializeConsole();

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    if (WinUpdate::IsInternetAvailable()) {
        const SysCmdLine::Option updateStoreAppsOption("update-store-apps", "Update Microsoft Store applications");
        const SysCmdLine::Option updateSystemOption("update-system", "Update Windows");
        SysCmdLine::Command rootCommand(SysCmdLine::appName(), "A convenient tool to update Microsoft Store applications and Windows.");
        rootCommand.addVersionOption("1.0.0.0");
        rootCommand.addHelpOption(true, true);
        rootCommand.addOption(updateStoreAppsOption);
        rootCommand.addOption(updateSystemOption);
        rootCommand.setHandler([&updateStoreAppsOption, &updateSystemOption](const SysCmdLine::Parser &parser) -> int {
            if (parser.optionIsSet(updateStoreAppsOption)) {
                WinUpdate::UpdateMicrosoftStoreApps();
            }
            if (parser.optionIsSet(updateSystemOption)) {
                WinUpdate::UpdateSystem();
            }
            return EXIT_SUCCESS;
        });
        SysCmdLine::Parser parser(rootCommand);
        parser.setDisplayOptions(SysCmdLine::Parser::ShowOptionalOptionsOnUsage);
        parser.setText(SysCmdLine::Parser::Top, "Thanks a lot for using Windows Updater, a small tool from wangwenx190's utility tools collection.");
        parser.setText(SysCmdLine::Parser::Bottom, "Please checkout https://github.com/wangwenx190/winupdate/ for more information.");
        parser.invoke(SysCmdLine::commandLineArguments(), EXIT_FAILURE, SysCmdLine::Parser::IgnoreCommandCase | SysCmdLine::Parser::IgnoreOptionCase | SysCmdLine::Parser::AllowDosStyleOptions);
    } else {
        WinUpdate::PrintError(L"You need to connect to the Internet first!");
    }

    winrt::uninit_apartment();

    WinUpdate::PrintToConsole(L"\n\n\n--- PRESS THE <ENTER> KEY TO EXIT ---", WinUpdate::ConsoleTextColor::Magenta, false);
    std::getchar();

    return EXIT_SUCCESS;
}
