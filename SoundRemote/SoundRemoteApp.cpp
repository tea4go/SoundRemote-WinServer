#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "SoundRemoteApp.h"

#include <CommCtrl.h>
#include <Windows.h>
#include <Windowsx.h>
#include <shellapi.h>

#include <boost/asio/post.hpp>

#include "CapturePipe.h"
#include "Clients.h"
#include "Controls.h"
#include "NetUtil.h"
#include "Server.h"
#include "Settings.h"
#include "Util.h"
#include "UpdateChecker.h"

using namespace std::placeholders;

namespace {
    constexpr int windowWidth = 600;            // main window width
    constexpr int windowHeight = 450;			// main window height
    constexpr int timerIdPeakMeter = 1;
    constexpr int timerPeriodPeakMeter = 33;    // in milliseconds

    constexpr auto defaultRenderDeviceKey = -1;
    constexpr auto defaultCaptureDeviceKey = -2;
    constexpr auto invalidDeviceKey = -3;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    std::unique_ptr<SoundRemoteApp> app = SoundRemoteApp::create(_In_ hInstance);
    return app ? app->exec(nCmdShow) : 1;
}

SoundRemoteApp::SoundRemoteApp(_In_ HINSTANCE hInstance): hInst_(hInstance), ioContext_() {}

SoundRemoteApp::~SoundRemoteApp() {
    boost::asio::post(ioContext_, std::bind(&SoundRemoteApp::shutdown, this));

    if (ioContextThread_ && ioContextThread_->joinable()) {
        ioContextThread_->join();
    }
    if (uiFont_) {
        DeleteObject(uiFont_);
    }
}

std::unique_ptr<SoundRemoteApp> SoundRemoteApp::create(_In_ HINSTANCE hInstance){
    return std::make_unique<SoundRemoteApp>(hInstance);
}

int SoundRemoteApp::exec(int nCmdShow) {
    if (!initInstance(nCmdShow)) {
        return 1;
    }

    run();

    HACCEL hAccelTable = LoadAccelerators(hInst_, MAKEINTRESOURCE(IDC_SOUNDREMOTE));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(mainWindow_, &msg) && !TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

bool SoundRemoteApp::toggleMenuItem(UINT itemId) const {
    HMENU menu = GetMenu(mainWindow_);
    MENUITEMINFO mii{ sizeof(MENUITEMINFO) };
    mii.fMask = MIIM_STATE;
    GetMenuItemInfo(menu, itemId, FALSE, &mii);
    mii.fState ^= MFS_CHECKED;
    SetMenuItemInfo(menu, itemId, FALSE, &mii);
    return (mii.fState & MFS_CHECKED) != 0;
}

void SoundRemoteApp::run() {
    Util::setMainWindow(mainWindow_);
    initMenu();
    if (settings_->getCheckUpdates()) {
        checkUpdates(true);
    }
    // Register for system suspend events
    RegisterSuspendResumeNotification(mainWindow_, DEVICE_NOTIFY_WINDOW_HANDLE);
    try {
        const auto clientPort = settings_->getClientPort();
        const auto serverPort = settings_->getServerPort();

        clients_ = std::make_shared<Clients>();
        clients_->addClientsListener(std::bind(&SoundRemoteApp::onClientsUpdate, this, _1));
        server_ = std::make_shared<Server>(clientPort, serverPort, ioContext_, clients_);
        clients_->addClientsListener(std::bind(&Server::onClientsUpdate, server_.get(), _1));
        server_->setKeystrokeCallback(std::bind(&SoundRemoteApp::onReceiveKeystroke, this, _1));
        // io_context will run as long as the server works and waiting for incoming packets.
        ioContextThread_ = std::make_unique<std::thread>(std::bind(&SoundRemoteApp::asioEventLoop, this, _1), std::ref(ioContext_));

        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    }
    catch (const std::exception& e) {
        Util::showError(e.what());
        std::exit(EXIT_FAILURE);
    }
    catch (...) {
        Util::showError("Start server: unknown error");
        std::exit(EXIT_FAILURE);
    }
    restoreCaptureDevice();
    // Create audio source by selecting a device.
    onDeviceSelect();
}

void SoundRemoteApp::shutdown() {
    server_->sendDisconnectBlocking();
    stopCapture();
    ioContext_.stop();
}

void SoundRemoteApp::addDevices(HWND comboBox, EDataFlow flow) {
    const auto devices = Audio::getEndpointDevices(flow);
    if (devices.empty()) {
        return;
    }
    if (flow == eRender || flow == eAll) {                      // Add default playback
        addDefaultDevice(comboBox, eRender);
    }
    if (flow == eCapture || flow == eAll) {                     // Add default recording
        addDefaultDevice(comboBox, eCapture);
    }
    for (auto&& iter = devices.cbegin(); iter != devices.end(); ++iter) {
        const auto index = ComboBox_AddString(comboBox, iter->first.c_str());
        ComboBox_SetItemData(comboBox, index, index);
        deviceIds_[index] = iter->second;
    }
}

void SoundRemoteApp::addDefaultDevice(HWND comboBox, EDataFlow flow) {
    assert(flow == eRender || flow == eCapture);

    int newItemIndex, deviceKey;
    if (flow == eRender) {
        newItemIndex = ComboBox_AddString(comboBox, defaultRenderDeviceLabel_.data());
        deviceKey = defaultRenderDeviceKey;
    } else {
        newItemIndex = ComboBox_AddString(comboBox, defaultCaptureDeviceLabel_.data());
        deviceKey = defaultCaptureDeviceKey;
    }
    ComboBox_SetItemData(comboBox, newItemIndex, (LPARAM)deviceKey);
}

std::wstring SoundRemoteApp::getDeviceId(const int deviceKey) const {
    if (!deviceIds_.contains(deviceKey)) {
        assert(deviceKey == defaultCaptureDeviceKey || deviceKey == defaultRenderDeviceKey);
        EDataFlow flow = (deviceKey == defaultCaptureDeviceKey) ? eCapture : eRender;
        return Audio::getDefaultDevice(flow);
    }
    return deviceIds_.at(deviceKey);
}

int SoundRemoteApp::getDeviceKey(const std::wstring& deviceId) const {
    if (deviceId == defaultCaptureDeviceId) {
        return defaultCaptureDeviceKey;
    } else if (deviceId == defaultRenderDeviceId) {
        return defaultRenderDeviceKey;
    } else {
        for (auto&& iter = deviceIds_.cbegin(); iter != deviceIds_.end(); ++iter) {
            if (iter->second == deviceId) {
                return iter->first;
            }
        }
    }
    return invalidDeviceKey;
}

void SoundRemoteApp::restoreCaptureDevice() {
    const int key = getDeviceKey(settings_->getCaptureDevice());
    if (invalidDeviceKey == key) {
        return;
    }
    int index = -1;
    int itemCount = ComboBox_GetCount(deviceComboBox_);
    for (int i = 0; i < itemCount; i++) {
        if (ComboBox_GetItemData(deviceComboBox_, i) == key) {
            index = i;
        }
    }
    if (index != -1) {
        ComboBox_SetCurSel(deviceComboBox_, index);
    }
}

void SoundRemoteApp::rememberCaptureDevice(int deviceKey, const std::wstring& deviceId) {
    switch (deviceKey) {
    case defaultRenderDeviceKey:
        settings_->setCaptureDevice(defaultRenderDeviceId);
        break;

    case defaultCaptureDeviceKey:
        settings_->setCaptureDevice(defaultCaptureDeviceId);
        break;

    default:
        settings_->setCaptureDevice(deviceId);
        break;
    };
}

long SoundRemoteApp::getCharHeight(HWND hWnd) const {
    HDC hdc = GetDC(hWnd);
    HFONT oldFont = nullptr;
    if (uiFont_) {
        oldFont = (HFONT)SelectObject(hdc, uiFont_);
    }
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    if (oldFont) SelectObject(hdc, oldFont);
    ReleaseDC(hWnd, hdc);
    return tm.tmHeight + tm.tmExternalLeading;
}

HWND SoundRemoteApp::setTooltip(HWND toolWindow, PTSTR text, HWND parentWindow) const {
    HWND tooltip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parentWindow, NULL, hInst_, NULL);

    // Must explicitly define a tooltip control as topmost
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Associate the tooltip with the tool.
    TOOLINFO toolInfo = { 0 };
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.hwnd = parentWindow;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = reinterpret_cast<UINT_PTR>(toolWindow);
    toolInfo.lpszText = text;
    SendMessage(tooltip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&toolInfo));

    return tooltip;
}

std::wstring SoundRemoteApp::loadStringResource(UINT resourceId) const {
    const WCHAR* unterminatedString = nullptr;
    const auto stringLength = LoadStringW(hInst_, resourceId, (LPWSTR)&unterminatedString, 0);
    return { unterminatedString, static_cast<size_t>(stringLength) };
}

void SoundRemoteApp::onDeviceSelect() {
    const auto itemIndex = ComboBox_GetCurSel(deviceComboBox_);
    if (CB_ERR == itemIndex) {
        return;
    }
    const auto itemData = ComboBox_GetItemData(deviceComboBox_, itemIndex);
    const int deviceKey = static_cast<int>(itemData);
    const std::wstring deviceId = getDeviceId(deviceKey);

    boost::asio::post(ioContext_, std::bind(&SoundRemoteApp::changeCaptureDevice, this, deviceId));
    rememberCaptureDevice(deviceKey, deviceId);
    startPeakMeter();
}

void SoundRemoteApp::changeCaptureDevice(const std::wstring& deviceId) {
    if (deviceId == currentDeviceId_) {
        return;
    }
    currentDeviceId_.clear();
    stopCapture();
    capturePipe_ = std::make_unique<CapturePipe>(deviceId, server_, ioContext_);
    clients_->addClientsListener(std::bind(&CapturePipe::onClientsUpdate, capturePipe_.get(), _1));
    currentDeviceId_ = deviceId;
    capturePipe_->start();
}

void SoundRemoteApp::stopCapture() {
    if (capturePipe_) {
        auto removed = clients_->removeClientsListener(
            std::bind(&CapturePipe::onClientsUpdate, capturePipe_.get(), _1)
        );
        if (removed != 1) {
            throw std::runtime_error(
                Util::makeFatalErrorText(ErrorCode::REMOVE_CAPTURE_PIPE_CLIENTS_LISTENER)
            );
        }
        capturePipe_.reset();
    }
}

void SoundRemoteApp::onClientListUpdate(std::forward_list<std::string> clients) const {
    std::ostringstream addresses;
    for (const auto& client : clients) {
        addresses << client << "\r\n";
    }
    SetWindowTextA(clientsList_, addresses.str().c_str());
}

void SoundRemoteApp::onClientsUpdate(std::forward_list<ClientInfo> clients) const {
    std::ostringstream addresses;
    for (auto&& client : clients) {
        addresses << client.address.to_string() << "\r\n";
    }
    SetWindowTextA(clientsList_, addresses.str().c_str());
}

void SoundRemoteApp::onAddressButtonClick() const {
    auto addresses = Net::getLocalAddresses();
    std::wstring addressesStr;
    for (auto& adr : addresses) {
        addressesStr += adr + L"\n";
    }
    Util::showInfo(addressesStr, serverAddressesLabel_);
}

void SoundRemoteApp::updatePeakMeter() {
    if (capturePipe_) {
        const auto peakValue = capturePipe_->getPeakValue();
        const int peak = static_cast<int>(peakValue * 100);
        SendMessage(peakMeterProgress_, PBM_SETPOS, peak, 0);
    } else {
        stopPeakMeter();
    }
}

void SoundRemoteApp::onReceiveKeystroke(const Keystroke& keystroke) const {
    auto currentTextLength = Edit_GetTextLength(keystrokes_);
    Edit_SetSel(keystrokes_, currentTextLength, currentTextLength);
    tm now;
    const auto t = time(nullptr);
    localtime_s(&now, &t);
    wchar_t timeStr[20];
    wcsftime(timeStr, sizeof(timeStr), L"%T%t", &now);
    std::wstring keystrokeDesc = timeStr + keystroke.toString() + L"\r\n";
    Edit_ReplaceSel(keystrokes_, keystrokeDesc.c_str());
}

void SoundRemoteApp::checkUpdates(bool quiet) {
    if (!updateChecker_) {
        updateChecker_ = std::make_unique<UpdateChecker>(mainWindow_);
    }
    updateChecker_->checkUpdates(quiet);
}

void SoundRemoteApp::onUpdateCheckFinish(WPARAM wParam, LPARAM lParam) {
    switch (wParam) {
    case UPDATE_FOUND:
        if (IDYES == MessageBox(
            mainWindow_,
            updateCheckFound_.c_str(),
            updateCheckTitle_.c_str(),
            MB_ICONINFORMATION | MB_YESNO
        )) {
            ShellExecute(
                nullptr,
                TEXT("open"),
                TEXT("https://github.com/SoundRemote/server-windows/releases"),
                nullptr,
                nullptr,
                SW_NORMAL
            );
        }
        return;
    case UPDATE_NOT_FOUND:
        Util::showInfo(updateCheckNotFound_, updateCheckTitle_);
        return;
    case UPDATE_CHECK_ERROR:
        Util::showInfo(updateCheckError_, updateCheckTitle_);
        return;
    }
}

void SoundRemoteApp::visitHomepage() const {
    ShellExecute(
        nullptr,
        TEXT("open"),
        TEXT("https://soundremote.github.io"),
        nullptr,
        nullptr,
        SW_NORMAL
    );
}

void SoundRemoteApp::asioEventLoop(boost::asio::io_context& ctx) {
    for (;;) {
        try {
            ctx.run();
            break;
        }
        catch (const Audio::Error& e) {
            Util::showError(e.what());
            stopCapture();
        }
        catch (const std::exception& e) {
            //logger.log(LOG_ERR) << "[eventloop] An unexpected error occurred running " << name << " task: " << e.what();
            Util::showError(e.what());
            std::exit(EXIT_FAILURE);
        }
        catch (...) {
            Util::showError("Event loop: unknown error");
            std::exit(EXIT_FAILURE);
        }
    }
}

void SoundRemoteApp::initInterface(HWND hWndParent) {
    RECT wndRect;
    GetClientRect(hWndParent, &wndRect);
    const int windowW = wndRect.right;
    const int windowH = wndRect.bottom;

    auto const charH = getCharHeight(hWndParent);
    constexpr int padding = 5;
    constexpr int rightBlockW = 30;
    const int leftBlockW = windowW - rightBlockW - padding * 3;

// Device combobox
    const int deviceComboX = padding;
    const int deviceComboY = padding;
    const int deviceComboW = windowW - padding * 2;
    const int deviceComboH = 100;
    deviceComboBox_ = CreateWindowW(WC_COMBOBOX, (LPCWSTR)NULL, CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
        deviceComboX, deviceComboY, deviceComboW, deviceComboH, hWndParent, NULL, hInst_, NULL);

    RECT deviceComboRect;
    GetClientRect(deviceComboBox_, &deviceComboRect);
    MapWindowPoints(deviceComboBox_, hWndParent, (LPPOINT)&deviceComboRect, 2);
    
// Tab control (clients / keystrokes)
    const int tabX = padding;
    const int tabY = deviceComboRect.bottom + padding + 20;
    const int tabW = leftBlockW;
    const int tabH = windowH - tabY - padding;
    tabControl_ = CreateWindowW(WC_TABCONTROL, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        tabX, tabY, tabW, tabH, hWndParent, nullptr, hInst_, nullptr);

    TCITEMW tie{};
    tie.mask = TCIF_TEXT;
    tie.pszText = const_cast<LPWSTR>(clientListLabel_.c_str());
    TabCtrl_InsertItem(tabControl_, 0, &tie);
    tie.pszText = const_cast<LPWSTR>(keystrokeListLabel_.c_str());
    TabCtrl_InsertItem(tabControl_, 1, &tie);
    // Ensure tab labels are wide enough for the current font (Chinese chars: width ≈ height)
    TabCtrl_SetMinTabWidth(tabControl_, charH * 3 + 20);
    TabCtrl_SetPadding(tabControl_, 6, 10);

    // Get display area inside the tab control
    RECT tabDisplay{ 0, 0, tabW, tabH };
    TabCtrl_AdjustRect(tabControl_, FALSE, &tabDisplay);
    const int editX = tabDisplay.left;
    const int editY = tabDisplay.top;
    const int editW = tabDisplay.right - tabDisplay.left;
    const int editH = tabDisplay.bottom - tabDisplay.top;

    clientsList_ = CreateWindowW(WC_EDIT, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_READONLY,
        editX, editY, editW, editH, tabControl_, nullptr, hInst_, nullptr);

    keystrokes_ = CreateWindowW(WC_EDIT, nullptr,
        WS_CHILD | WS_BORDER | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_READONLY,
        editX, editY, editW, editH, tabControl_, nullptr, hInst_, nullptr);

// Address button
    const int addressButtonX = windowW - rightBlockW - padding;
    const int addressButtonY = deviceComboRect.bottom + padding + 20;
    const int addressButtonW = rightBlockW;
    const int addressButtonH = rightBlockW;
    addressButton_ = CreateWindowW(WC_BUTTON, L"IP", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        addressButtonX, addressButtonY, addressButtonW, addressButtonH, hWndParent, NULL, hInst_, NULL);
    setTooltip(addressButton_, serverAddressesLabel_.data(), hWndParent);

// Mute button
    Rect muteButtonRect = Rect(addressButtonX, windowH - rightBlockW - padding, rightBlockW, rightBlockW);
    muteButton_ = std::make_unique<MuteButton>(hWndParent, muteButtonRect, muteButtonText_);
    muteButton_->setStateCallback([&](bool v) { capturePipe_->setMuted(v); });

// Peak meter
    const int peakMeterX = addressButtonX;
    const int peakMeterY = addressButtonY + addressButtonH + padding;
    const int peakMeterW = rightBlockW;
    const int peakMeterH = muteButtonRect.y - peakMeterY - padding;
    peakMeterProgress_ = CreateWindowW(PROGRESS_CLASS, (LPCWSTR)NULL, WS_CHILD | WS_VISIBLE | PBS_VERTICAL | PBS_SMOOTH,
        peakMeterX, peakMeterY, peakMeterW, peakMeterH, hWndParent, NULL, hInst_, NULL);
}

void SoundRemoteApp::initControls() {
    //Device ComboBox
    ComboBox_ResetContent(deviceComboBox_);
    deviceIds_.clear();
    addDevices(deviceComboBox_, eRender);
    addDevices(deviceComboBox_, eCapture);
    ComboBox_SetCurSel(deviceComboBox_, 0);
}

void SoundRemoteApp::startPeakMeter() const {
    SetTimer(mainWindow_, timerIdPeakMeter, timerPeriodPeakMeter, nullptr);
}

void SoundRemoteApp::stopPeakMeter() const {
    KillTimer(mainWindow_, timerIdPeakMeter);
    SendMessage(peakMeterProgress_, PBM_SETPOS, 0, 0);
}

void SoundRemoteApp::initSettings() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring settingsPath(exePath);
    settingsPath = settingsPath.substr(0, settingsPath.find_last_of(L"\\/") + 1) + L"settings.ini";
    settings_ = std::make_unique<Settings>(settingsPath);
}

void SoundRemoteApp::initMenu() {
    HMENU menu = GetMenu(mainWindow_);
    MENUITEMINFO mii{ sizeof(MENUITEMINFO) };
    mii.fMask = MIIM_STATE;
    if (settings_->getCheckUpdates()) {
        mii.fState = MFS_CHECKED;
    } else {
        mii.fState = MFS_UNCHECKED;
    }
    SetMenuItemInfo(menu, IDM_CHECK_UPDATES_ON_START, FALSE, &mii);

    if (settings_->getLanguage() == L"Chinese") {
        MENUITEMINFO miText{ sizeof(MENUITEMINFO) };
        miText.fMask = MIIM_STRING;
        auto setMenuText = [&](UINT id, LPCWSTR text) {
            miText.dwTypeData = const_cast<LPWSTR>(text);
            SetMenuItemInfo(menu, id, FALSE, &miText);
        };
        // File menu
        HMENU fileMenu = GetSubMenu(menu, 0);
        ModifyMenuW(menu, 0, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)fileMenu, L"文件(&F)");
        setMenuText(IDM_EXIT, L"退出(&X)");
        // Help menu
        HMENU helpMenu = GetSubMenu(menu, 1);
        ModifyMenuW(menu, 1, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)helpMenu, L"帮助(&H)");
        setMenuText(IDM_CHECK_UPDATES_ON_START, L"启动时检查更新");
        setMenuText(IDM_CHECK_UPDATES, L"检查更新...");
        setMenuText(IDM_HOMEPAGE, L"主页");
        setMenuText(IDM_ABOUT, L"关于(&A)...");
        DrawMenuBar(mainWindow_);
    }
}

void SoundRemoteApp::initStrings() {
    if (settings_->getLanguage() == L"Chinese") {
        mainWindowTitle_          = L"SoundRemote";
        serverAddressesLabel_     = L"服务器IP地址";
        defaultRenderDeviceLabel_ = L"默认播放设备";
        defaultCaptureDeviceLabel_= L"默认录音设备";
        clientListLabel_          = L"客户端";
        keystrokeListLabel_       = L"快捷键";
        muteButtonText_           = L"静音";
        updateCheckTitle_         = L"检查更新";
        updateCheckFound_         = L"有新版本可用。\n是否下载？";
        updateCheckNotFound_      = L"暂无更新";
        updateCheckError_         = L"检查更新时发生错误";
    } else {
        mainWindowTitle_          = loadStringResource(IDS_APP_TITLE);
        serverAddressesLabel_     = loadStringResource(IDS_SERVER_ADDRESSES);
        defaultRenderDeviceLabel_ = loadStringResource(IDS_DEFAULT_RENDER);
        defaultCaptureDeviceLabel_= loadStringResource(IDS_DEFAULT_CAPTURE);
        clientListLabel_          = loadStringResource(IDS_CLIENTS);
        keystrokeListLabel_       = loadStringResource(IDS_HOTKEYS);
        muteButtonText_           = loadStringResource(IDS_MUTE);
        updateCheckTitle_         = loadStringResource(IDS_UPDATE_CHECK);
        updateCheckFound_         = loadStringResource(IDS_UPDATE_FOUND);
        updateCheckNotFound_      = loadStringResource(IDS_UPDATE_NOT_FOUND);
        updateCheckError_         = loadStringResource(IDS_UPDATE_CHECK_ERROR);
    }
}

void SoundRemoteApp::initFont() {
    if (uiFont_) {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
    }
    const auto fontName = settings_->getFont();
    const int pointSize  = static_cast<int>(settings_->getFontSize());
    const bool bold      = settings_->getFontBold();
    HDC hdc = GetDC(mainWindow_);
    const int lfHeight = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(mainWindow_, hdc);
    uiFont_ = CreateFontW(
        lfHeight, 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        fontName.c_str()
    );
    if (!uiFont_) return;
    // Apply to all child controls
    EnumChildWindows(mainWindow_, [](HWND child, LPARAM lParam) -> BOOL {
        SendMessage(child, WM_SETFONT, lParam, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(uiFont_));
}

bool SoundRemoteApp::initInstance(int nCmdShow) {
    constexpr wchar_t CLASS_NAME[] = L"SOUNDREMOTE";

    initSettings();

    WNDCLASSEXW wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = staticWndProc;
    wcex.hInstance = hInst_;
    wcex.hIcon = LoadIcon(hInst_, MAKEINTRESOURCE(IDI_SOUNDREMOTE));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_SOUNDREMOTE);
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIconSm = nullptr;
    if (RegisterClassExW(&wcex) == 0) {
        return false;
    }

    initStrings();

    mainWindow_ = CreateWindowW(CLASS_NAME, mainWindowTitle_.data(), WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, 0, windowWidth, windowHeight, nullptr, nullptr, hInst_, this);
    if (mainWindow_ == NULL) {
        return false;
    }

    initFont();
    initInterface(mainWindow_);
    initControls();
    // Apply font to controls created by initInterface/initControls
    if (uiFont_) {
        EnumChildWindows(mainWindow_, [](HWND child, LPARAM lParam) -> BOOL {
            SendMessage(child, WM_SETFONT, lParam, TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(uiFont_));
    }

    ShowWindow(mainWindow_, nCmdShow);
    return true;
}

INT_PTR SoundRemoteApp::about(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

LRESULT SoundRemoteApp::staticWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SoundRemoteApp* app = nullptr;
    if (message == WM_CREATE) {
        LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        app = static_cast<SoundRemoteApp*>(lpcs->lpCreateParams);
        app->mainWindow_ = hWnd;
        ::SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<SoundRemoteApp*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (app) {
        return app->wndProc(message, wParam, lParam);
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT SoundRemoteApp::wndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message)
    {
    case WM_COMMAND:
    {
        const int wmType = HIWORD(wParam);
        const int wmId = LOWORD(wParam);
        if (lParam == 0) {  // If Menu or Accelerator
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_ABOUTBOX), mainWindow_, about);
                return 0;

            case IDM_EXIT:
                DestroyWindow(mainWindow_);
                return 0;

            case IDM_CHECK_UPDATES:
                checkUpdates();
                return 0;

            case IDM_HOMEPAGE:
                visitHomepage();
                return 0;

            case IDM_CHECK_UPDATES_ON_START: {
                bool checked = toggleMenuItem(IDM_CHECK_UPDATES_ON_START);
                settings_->setCheckUpdates(checked);
                return 0;
            }

            default:
                break;
            }
        } else {    // If Control
            const HWND controlHwnd = reinterpret_cast<HWND>(lParam);
            switch (wmType)
            {
            case CBN_SELCHANGE:
                // The only combobox is device select.
                onDeviceSelect();
                return 0;

            case BN_CLICKED: {
                if (controlHwnd == addressButton_) {
                    onAddressButtonClick();
                    return 0;
                }
                if (controlHwnd == muteButton_->handle()) {
                    muteButton_->onClick();
                    return 0;
                }
            }
            break;

            default:    // Other controls
                break;
            }
        }
    }
    break;

    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == tabControl_) {
            if (reinterpret_cast<LPNMHDR>(lParam)->code == TCN_SELCHANGE) {
                int sel = TabCtrl_GetCurSel(tabControl_);
                ShowWindow(clientsList_, sel == 0 ? SW_SHOW : SW_HIDE);
                ShowWindow(keystrokes_,  sel == 1 ? SW_SHOW : SW_HIDE);
                return 0;
            }
        }
    break;

    case WM_SYSCOMMAND:
        if (wParam == SC_CLOSE) {
            DestroyWindow(mainWindow_);
            return 0;
        }
    break;

    // Ignore WM_CLOSE because multiline edit sends it on Esc.
    case WM_CLOSE:
        return 0;

    case WM_TIMER:
    {
        switch ((int)wParam) {
        case timerIdPeakMeter:
            updatePeakMeter();
            return 0;

        default:
            break;
        }
    }
    break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(mainWindow_, &ps);
        // TODO: Add any drawing code that uses hdc here...
        EndPaint(mainWindow_, &ps);
    }
    return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_UPDATE_CHECK:
        onUpdateCheckFinish(wParam, lParam);
        return 0;

    case WM_POWERBROADCAST:
    {
        if (PBT_APMSUSPEND == wParam) {
            clients_->removeAll();
        }
    }
    break;

    default:
        break;
    }
    return DefWindowProc(mainWindow_, message, wParam, lParam);
}
