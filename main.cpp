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
#include <wuapi.h>
#include <atlbase.h>
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
    LPWSTR buf = nullptr;
    if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buf), 0, nullptr) == 0) {
        return L"UNKNOWN ERROR";
    }
    const std::wstring result = buf;
    LocalFree(buf);
    buf = nullptr;
    return result;
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

            HANDLE completeSignal = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            if (!completeSignal) {
                std::wcerr << L"CreateEventExW() failed with error " << GetSystemErrorMessage(GetLastError());
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
            WaitForMultipleObjectsEx(static_cast<DWORD>(completeSignals.size()), &completeSignals[0], TRUE, INFINITE, FALSE);

            for (auto &&signal : std::as_const(completeSignals)) {
                CloseHandle(signal);
            }
        }
    }
    std::wcout << L"Congratulations! All your Microsoft Store applications are update to date!" << std::endl;
    std::wcout << std::endl << std::endl;
}

static inline void UpdateSystem()
{
    std::wcout << L"Start updating system ..." << std::endl;
    EnableMicrosoftUpdate();
    std::wcout << L"### TO BE IMPLEMENTED" << std::endl;
    std::wcout << L"Congratulations! Your system is update to date!" << std::endl;
    std::wcout << std::endl << std::endl;
}

EXTERN_C int APIENTRY wmain(int argc, wchar_t *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    winrt::init_apartment();

    UpdateStoreApps();
    UpdateSystem();

    winrt::uninit_apartment();

    std::wcout << L"-- PRESS THE <ENTER> KEY TO EXIT --" << std::endl;
    getchar();

    return 0;
}
