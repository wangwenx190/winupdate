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
        std::wcerr << L"AllocateAndInitializeSid() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
    } else {
        if (CheckTokenMembership(nullptr, administratorsGroup, &result) == FALSE) {
            result = FALSE;
            std::wcerr << L"CheckTokenMembership() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
        }
        FreeSid(administratorsGroup);
    }
    return (result != FALSE);
}

[[nodiscard]] static inline std::wstring GetApplicationFilePath()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        std::wcerr << L"GetModuleFileNameW() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
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
        std::wcerr << L"ShellExecuteExW() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
    }
}

static inline void EnableMicrosoftUpdate()
{
    CComPtr<IUpdateServiceManager2> pUsm = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_UpdateServiceManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pUsm));
    if (FAILED(hr)) {
        std::wcerr << L"CoCreateInstance() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << std::endl;
        return;
    }
    const BSTR appId = SysAllocString(L"My App");
    hr = pUsm->put_ClientApplicationID(appId);
    if (FAILED(hr)) {
        SysFreeString(appId);
        std::wcerr << L"put_ClientApplicationID() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << std::endl;
        return;
    }
    const BSTR serviceId = SysAllocString(L"7971f918-a847-4430-9279-4a52d1efe18d");
    const BSTR authorizationCabPath = SysAllocString(L"");
    const DWORD flags = (AddServiceFlag::asfAllowPendingRegistration | AddServiceFlag::asfAllowOnlineRegistration | AddServiceFlag::asfRegisterServiceWithAU);
    CComPtr<IUpdateServiceRegistration> pUsr = nullptr;
    hr = pUsm->AddService2(serviceId, flags, authorizationCabPath, &pUsr);
    if (FAILED(hr)) {
        std::wcerr << L"AddService2() failed with error " << GetSystemErrorMessage(HRESULT_CODE(hr)) << std::endl;
    }
    SysFreeString(authorizationCabPath);
    SysFreeString(serviceId);
    SysFreeString(appId);
}

static inline void UpdateStoreApps()
{
    std::wcout << L"Start updating Microsoft Store applications ..." << std::endl;

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
            std::wcout << L"Updating " << update.PackageFamilyName().c_str() << L"..." << std::endl;

            const HANDLE completeSignal = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            if (!completeSignal) {
                std::wcerr << L"CreateEventExW() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
                return;
            }

            completeSignals.push_back(completeSignal);

            update.Completed([completeSignal](winrt::AppInstallItem const &sender, winrt::IInspectable const &args){
                UNREFERENCED_PARAMETER(args);

                std::wcout << sender.PackageFamilyName().c_str() << L" has been updated to the latest version." << std::endl;

                SetEvent(completeSignal);
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
                std::wcerr << L"WaitForMultipleObjectsEx() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
            }

            for (auto &&signal : std::as_const(completeSignals)) {
                if (CloseHandle(signal) == FALSE) {
                    std::wcerr << L"CloseHandle() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
                }
            }
        }
    }
    std::wcout << L"Congratulations! All your Microsoft Store applications are update to date!" << std::endl;
    std::wcout << std::endl << std::endl;
}

static inline void UpdateSystem()
{
    std::wcout << L"Start updating system ..." << std::endl;
    std::wcout << L"### TO BE IMPLEMENTED" << std::endl;
    std::wcout << L"Congratulations! Your system is update to date!" << std::endl;
    std::wcout << std::endl << std::endl;
}

static inline void InitializeConsole()
{
    const auto EnableVTSequencesForConsole = [](const DWORD handleId) -> bool {
        const HANDLE handle = GetStdHandle(handleId);
        if (!handle || (handle == INVALID_HANDLE_VALUE)) {
            std::wcerr << L"GetStdHandle() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
            return false;
        }
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == FALSE) {
            std::wcerr << L"GetConsoleMode() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
            return false;
        }
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(handle, mode) == FALSE) {
            std::wcerr << L"SetConsoleMode() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
            return false;
        }
        return true;
    };
    EnableVTSequencesForConsole(STD_OUTPUT_HANDLE);
    EnableVTSequencesForConsole(STD_ERROR_HANDLE);

    if (SetConsoleOutputCP(CP_UTF8) == FALSE) {
        std::wcerr << L"SetConsoleOutputCP() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
    }

    if (SetConsoleTitleW(L"WinUpdate") == FALSE) {
        std::wcerr << L"SetConsoleTitleW() failed with error " << GetSystemErrorMessage(GetLastError()) << std::endl;
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
        std::wcout << L"This application requires the administrator privilege to run." << std::endl;
        std::wcout << L"Trying to restart this application as an elevated process ..." << std::endl;
        RestartAsElevatedProcess();
        return 0;
    }

    UpdateStoreApps();
    EnableMicrosoftUpdate();
    UpdateSystem();

    winrt::uninit_apartment();

    std::wcout << L"-- PRESS THE <ENTER> KEY TO EXIT --" << std::endl;
    getchar();

    return 0;
}
