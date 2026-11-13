#include "OptionsDlg.h"
#include "HidBattery.h"
#include "XInputBattery.h"
#include "ReceiverBattery.h"

#include <commctrl.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <string>

namespace
{
    constexpr int IDC_DEVICE_COMBO = 1010;
    constexpr int IDC_TOOLTIP_CHECK = 1012;
    constexpr int IDC_GENERIC_CHECK = 1013;
    constexpr int IDC_INTERVAL_EDIT = 1014;
    constexpr int IDC_DETECT_BUTTON = 1015;
    constexpr int IDC_SHOW_2G4 = 1016;
    constexpr int IDC_SHOW_LOGITECH = 1017;
    constexpr int IDC_SHOW_GAMEPAD = 1018;

    constexpr int kLogicalWidth = 430;
    const wchar_t* const kNoteText =
        L"提示：任务栏只显示一个“外设电量”项目，请在 TrafficMonitor 的显示设置中勾选；\n"
        L"“同时显示”勾选多个设备时会忽略上方“显示设备”；刷新间隔可输入 3–300 秒。";
    const wchar_t* const kGenericHint =
        L"用于没有专用协议的 2.4G 键鼠；罗技和手柄不受影响。";

    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    using SystemParametersInfoForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);

    GetDpiForWindowFn GetDpiForWindowProc()
    {
        static GetDpiForWindowFn fn = []()
        {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return reinterpret_cast<GetDpiForWindowFn>(
                user32 != nullptr ? GetProcAddress(user32, "GetDpiForWindow") : nullptr);
        }();
        return fn;
    }

    SystemParametersInfoForDpiFn SystemParametersInfoForDpiProc()
    {
        static SystemParametersInfoForDpiFn fn = []()
        {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return reinterpret_cast<SystemParametersInfoForDpiFn>(
                user32 != nullptr ? GetProcAddress(user32, "SystemParametersInfoForDpi") : nullptr);
        }();
        return fn;
    }

    std::wstring FormatDeviceLine(const HidDeviceInfo& device)
    {
        return device.DisplayName() + L" [" + device.IdString() + L"]";
    }

    std::wstring JoinDeviceLines(const std::vector<std::wstring>& lines, const wchar_t* empty_text)
    {
        if (lines.empty())
            return empty_text;
        std::wstring text = lines.front();
        if (lines.size() > 1)
            text += L" 等 " + std::to_wstring(lines.size()) + L" 个";
        return text;
    }
}

COptionsDlg::COptionsDlg(HWND parent, const SettingData& settings)
    : m_parent(parent), m_settings(settings)
{
}

int COptionsDlg::Scale(int value) const
{
    return MulDiv(value, static_cast<int>(m_dpi), 96);
}

UINT COptionsDlg::QueryDpi() const
{
    if (m_hwnd != nullptr)
    {
        GetDpiForWindowFn get_dpi = GetDpiForWindowProc();
        if (get_dpi != nullptr)
        {
            const UINT dpi = get_dpi(m_hwnd);
            if (dpi != 0)
                return dpi;
        }
    }

    HWND reference = m_hwnd != nullptr ? m_hwnd : m_parent;
    HDC dc = GetDC(reference);
    if (dc != nullptr)
    {
        const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(reference, dc);
        if (dpi > 0)
            return static_cast<UINT>(dpi);
    }
    return 96;
}

void COptionsDlg::RecreateFont()
{
    if (m_font_owned && m_font != nullptr)
        DeleteObject(m_font);
    m_font = nullptr;
    m_font_owned = false;

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    bool got_metrics = false;

    SystemParametersInfoForDpiFn spi_for_dpi = SystemParametersInfoForDpiProc();
    if (spi_for_dpi != nullptr)
    {
        got_metrics = spi_for_dpi(SPI_GETNONCLIENTMETRICS,
            sizeof(metrics), &metrics, 0, m_dpi) != FALSE;
    }

    if (!got_metrics)
    {
        got_metrics = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
            sizeof(metrics), &metrics, 0) != FALSE;
        if (got_metrics)
        {
            HDC screen = GetDC(nullptr);
            const int system_dpi = screen != nullptr ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
            if (screen != nullptr)
                ReleaseDC(nullptr, screen);
            if (system_dpi > 0 && system_dpi != static_cast<int>(m_dpi))
            {
                metrics.lfMessageFont.lfHeight = MulDiv(
                    metrics.lfMessageFont.lfHeight, static_cast<int>(m_dpi), system_dpi);
            }
        }
    }

    if (got_metrics)
    {
        m_font = CreateFontIndirectW(&metrics.lfMessageFont);
        m_font_owned = m_font != nullptr;
    }

    HWND controls[] = {
        m_detect_label, m_detect_lines[0], m_detect_lines[1], m_detect_lines[2],
        m_detect_button, m_device_label, m_device_combo, m_multi_label,
        m_show_2g4_check, m_show_logitech_check, m_show_gamepad_check,
        m_generic_check, m_generic_hint, m_tooltip_check, m_interval_label,
        m_interval_edit, m_interval_unit, m_note, m_ok_button, m_cancel_button
    };
    for (HWND control : controls)
    {
        if (control != nullptr)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    }
}

int COptionsDlg::MeasureTextWidth(const wchar_t* text) const
{
    if (text == nullptr)
        return 0;
    HDC dc = GetDC(m_hwnd);
    if (dc == nullptr)
        return 0;

    HGDIOBJ old_font = m_font != nullptr ? SelectObject(dc, m_font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    if (old_font != nullptr)
        SelectObject(dc, old_font);
    ReleaseDC(m_hwnd, dc);
    return size.cx;
}

int COptionsDlg::MeasureWrappedHeight(const wchar_t* text, int width) const
{
    if (text == nullptr || width <= 0)
        return 0;
    HDC dc = GetDC(m_hwnd);
    if (dc == nullptr)
        return 0;

    HGDIOBJ old_font = m_font != nullptr ? SelectObject(dc, m_font) : nullptr;
    RECT rect{ 0, 0, width, 0 };
    DrawTextW(dc, text, -1, &rect, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
    if (old_font != nullptr)
        SelectObject(dc, old_font);
    ReleaseDC(m_hwnd, dc);
    return rect.bottom - rect.top;
}

HWND COptionsDlg::CreateControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id)
{
    HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 10, 10, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_instance, nullptr);
    if (control != nullptr && m_font != nullptr)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    return control;
}

void COptionsDlg::OnCreate()
{
    m_dpi = QueryDpi();
    RecreateFont();

    m_detect_label = CreateControl(WC_STATICW, L"设备检测",
        SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
    for (int i = 0; i < 3; ++i)
    {
        m_detect_lines[i] = CreateControl(WC_STATICW, L"",
            SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | SS_ENDELLIPSIS, 0);
    }
    m_detect_button = CreateControl(WC_BUTTONW, L"重新检测",
        BS_PUSHBUTTON | WS_TABSTOP, IDC_DETECT_BUTTON);

    m_device_label = CreateControl(WC_STATICW, L"显示设备：",
        SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
    m_device_combo = CreateControl(WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_DEVICE_COMBO);
    SendMessageW(m_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（优先 2.4G）"));
    SendMessageW(m_device_combo, CB_SETITEMDATA, 0, 0);
    SendMessageW(m_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2.4G 接收器"));
    SendMessageW(m_device_combo, CB_SETITEMDATA, 1, 1);
    SendMessageW(m_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"罗技设备"));
    SendMessageW(m_device_combo, CB_SETITEMDATA, 2, 2);
    SendMessageW(m_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"手柄"));
    SendMessageW(m_device_combo, CB_SETITEMDATA, 3, 3);
    SendMessageW(m_device_combo, CB_SETCURSEL,
        std::clamp(m_settings.selected_device, 0, 3), 0);

    m_multi_label = CreateControl(WC_STATICW, L"同时显示：",
        SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
    m_show_2g4_check = CreateControl(WC_BUTTONW, L"2.4G",
        BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SHOW_2G4);
    m_show_logitech_check = CreateControl(WC_BUTTONW, L"罗技",
        BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SHOW_LOGITECH);
    m_show_gamepad_check = CreateControl(WC_BUTTONW, L"手柄",
        BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SHOW_GAMEPAD);
    m_generic_check = CreateControl(WC_BUTTONW, L"读取标准 HID 电量设备",
        BS_AUTOCHECKBOX | WS_TABSTOP, IDC_GENERIC_CHECK);
    m_generic_hint = CreateControl(WC_STATICW, kGenericHint,
        SS_LEFT | SS_NOPREFIX, 0);
    m_tooltip_check = CreateControl(WC_BUTTONW, L"在鼠标提示中显示详细信息",
        BS_AUTOCHECKBOX | WS_TABSTOP, IDC_TOOLTIP_CHECK);
    m_interval_label = CreateControl(WC_STATICW, L"刷新间隔：",
        SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);
    m_interval_edit = CreateControl(WC_EDITW, L"",
        ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, IDC_INTERVAL_EDIT);
    m_interval_unit = CreateControl(WC_STATICW, L"秒",
        SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, 0);

    const int interval = std::clamp(m_settings.poll_interval, 3, 300);
    wchar_t interval_text[32]{};
    swprintf_s(interval_text, L"%d", interval);
    SetWindowTextW(m_interval_edit, interval_text);

    const int mask = m_settings.display_mask & 0x07;
    SendMessageW(m_show_2g4_check, BM_SETCHECK,
        (mask & 0x01) != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_show_logitech_check, BM_SETCHECK,
        (mask & 0x02) != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_show_gamepad_check, BM_SETCHECK,
        (mask & 0x04) != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
    UpdateDeviceControls();
    SendMessageW(m_generic_check, BM_SETCHECK,
        m_settings.enable_generic_hid ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_tooltip_check, BM_SETCHECK,
        m_settings.show_tooltip ? BST_CHECKED : BST_UNCHECKED, 0);

    m_note = CreateControl(WC_STATICW, kNoteText, SS_LEFT | SS_NOPREFIX, 0);

    m_ok_button = CreateControl(WC_BUTTONW, L"确定",
        BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK);
    m_cancel_button = CreateControl(WC_BUTTONW, L"取消",
        BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL);

    RefreshDetection(false);
    SetWindowTextW(m_hwnd, L"外设电量设置");
    Layout();
    SetFocus(mask != 0 ? m_show_2g4_check : m_device_combo);
}

void COptionsDlg::Layout()
{
    if (m_hwnd == nullptr)
        return;

    const int margin = Scale(16);
    const int gap = Scale(8);

    int font_height = Scale(20);
    HDC dc = GetDC(m_hwnd);
    if (dc != nullptr)
    {
        HGDIOBJ old_font = m_font != nullptr ? SelectObject(dc, m_font) : nullptr;
        TEXTMETRICW text_metrics{};
        if (GetTextMetricsW(dc, &text_metrics))
            font_height = text_metrics.tmHeight;
        if (old_font != nullptr)
            SelectObject(dc, old_font);
        ReleaseDC(m_hwnd, dc);
    }

    const int row_height = std::max(Scale(24), font_height + Scale(6));
    const int client_width = Scale(kLogicalWidth);

    const int device_label_width = std::max(Scale(70), MeasureTextWidth(L"显示设备：") + Scale(8));
    const int interval_label_width = std::max(Scale(70), MeasureTextWidth(L"刷新间隔：") + Scale(8));
    const int combo_width = std::max(Scale(200),
        client_width - margin * 2 - device_label_width - gap);

    int y = margin;

    const int detect_button_width = std::max(Scale(90),
        MeasureTextWidth(L"重新检测") + Scale(24));
    MoveWindow(m_detect_label, margin, y,
        client_width - margin * 2 - detect_button_width - gap, row_height, TRUE);
    MoveWindow(m_detect_button, client_width - margin - detect_button_width, y,
        detect_button_width, row_height, TRUE);
    y += row_height + Scale(4);
    for (HWND line : m_detect_lines)
    {
        MoveWindow(line, margin + Scale(4), y,
            client_width - margin * 2 - Scale(4), row_height, TRUE);
        y += row_height;
    }
    y += gap;

    MoveWindow(m_device_label, margin, y, device_label_width, row_height, TRUE);
    MoveWindow(m_device_combo, margin + device_label_width + gap, y,
        combo_width, row_height * 8, TRUE);
    SendMessageW(m_device_combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
        row_height - Scale(4));
    y += row_height + gap;

    const int multi_label_width = std::max(Scale(70),
        MeasureTextWidth(L"同时显示：") + Scale(8));
    MoveWindow(m_multi_label, margin, y, multi_label_width, row_height, TRUE);
    int multi_x = margin + multi_label_width + gap;
    auto place_check = [&](HWND check, const wchar_t* text)
    {
        const int check_width = MeasureTextWidth(text) + Scale(30);
        MoveWindow(check, multi_x, y, check_width, row_height, TRUE);
        multi_x += check_width + Scale(12);
    };
    place_check(m_show_2g4_check, L"2.4G");
    place_check(m_show_logitech_check, L"罗技");
    place_check(m_show_gamepad_check, L"手柄");
    y += row_height + gap;

    MoveWindow(m_generic_check, margin, y, client_width - margin * 2, row_height, TRUE);
    y += row_height + Scale(2);
    const int hint_width = client_width - margin * 2 - Scale(20);
    const int hint_height = MeasureWrappedHeight(kGenericHint, hint_width) + Scale(2);
    MoveWindow(m_generic_hint, margin + Scale(20), y, hint_width, hint_height, TRUE);
    y += hint_height + gap;

    MoveWindow(m_tooltip_check, margin, y, client_width - margin * 2, row_height, TRUE);
    y += row_height + gap;

    MoveWindow(m_interval_label, margin, y, interval_label_width, row_height, TRUE);
    const int edit_width = std::max(Scale(80), MeasureTextWidth(L"300") + Scale(32));
    const int edit_height = std::max(Scale(20), row_height - Scale(4));
    MoveWindow(m_interval_edit, margin + interval_label_width + gap,
        y + (row_height - edit_height) / 2, edit_width, edit_height, TRUE);
    const int unit_width = MeasureTextWidth(L"秒") + Scale(4);
    MoveWindow(m_interval_unit,
        margin + interval_label_width + gap + edit_width + Scale(4),
        y, unit_width, row_height, TRUE);
    y += row_height + gap * 2;

    const int note_width = client_width - margin * 2;
    const int note_text_height = MeasureWrappedHeight(kNoteText, note_width);
    const int note_height = note_text_height + Scale(4);
    MoveWindow(m_note, margin, y, note_width, note_height, TRUE);
    y += note_height + gap * 2;

    const int button_height = std::max(Scale(28), row_height);
    const int button_width = Scale(90);
    const int cancel_x = client_width - margin - button_width;
    const int ok_x = cancel_x - gap - button_width;
    MoveWindow(m_ok_button, ok_x, y, button_width, button_height, TRUE);
    MoveWindow(m_cancel_button, cancel_x, y, button_width, button_height, TRUE);
    y += button_height + margin;

    RECT window_rect{ 0, 0, client_width, y };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectEx(&window_rect, style, FALSE, ex_style);
    SetWindowPos(m_hwnd, nullptr, 0, 0,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    PositionWindow();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void COptionsDlg::PositionWindow()
{
    if (m_hwnd == nullptr)
        return;

    RECT window_rect{};
    if (!GetWindowRect(m_hwnd, &window_rect))
        return;
    const int width = window_rect.right - window_rect.left;
    const int height = window_rect.bottom - window_rect.top;

    RECT target{};
    bool has_target = false;
    if (m_parent != nullptr && IsWindow(m_parent) && IsWindowVisible(m_parent))
    {
        GetWindowRect(m_parent, &target);
        has_target = true;
    }
    else
    {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr && GetMonitorInfoW(monitor, &info))
        {
            target = info.rcWork;
            has_target = true;
        }
    }

    int x = window_rect.left;
    int y = window_rect.top;
    if (has_target)
    {
        x = target.left + ((target.right - target.left) - width) / 2;
        y = target.top + ((target.bottom - target.top) - height) / 2;
    }

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info))
    {
        const RECT& work = monitor_info.rcWork;
        if (x + width > work.right)
            x = work.right - width;
        if (y + height > work.bottom)
            y = work.bottom - height;
        if (x < work.left)
            x = work.left;
        if (y < work.top)
            y = work.top;
    }

    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0,
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void COptionsDlg::OnDpiChanged(WPARAM wParam, LPARAM lParam)
{
    m_dpi = LOWORD(wParam);
    if (m_dpi == 0)
        m_dpi = 96;

    RECT* suggested = reinterpret_cast<RECT*>(lParam);
    if (suggested != nullptr)
    {
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RecreateFont();
    Layout();
}

void COptionsDlg::UpdateDeviceControls()
{
    const bool custom =
        SendMessageW(m_show_2g4_check, BM_GETCHECK, 0, 0) == BST_CHECKED ||
        SendMessageW(m_show_logitech_check, BM_GETCHECK, 0, 0) == BST_CHECKED ||
        SendMessageW(m_show_gamepad_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(m_device_combo, custom ? FALSE : TRUE);
    EnableWindow(m_device_label, custom ? FALSE : TRUE);
}

void COptionsDlg::OnOk()
{
    int device = 0;
    const int device_selection = static_cast<int>(SendMessageW(m_device_combo, CB_GETCURSEL, 0, 0));
    if (device_selection >= 0)
    {
        const LRESULT data = SendMessageW(m_device_combo, CB_GETITEMDATA, device_selection, 0);
        if (data != CB_ERR)
            device = static_cast<int>(data);
    }

    wchar_t interval_text[64]{};
    GetWindowTextW(m_interval_edit, interval_text, static_cast<int>(std::size(interval_text)));
    wchar_t* end = nullptr;
    const long interval = wcstol(interval_text, &end, 10);
    while (end != nullptr && iswspace(*end))
        ++end;
    if (end == interval_text || (end != nullptr && *end != L'\0') ||
        interval < 3 || interval > 300)
    {
        MessageBoxW(m_hwnd, L"刷新间隔请输入 3 到 300 秒之间的整数。",
            L"外设电量设置", MB_OK | MB_ICONWARNING);
        SetFocus(m_interval_edit);
        return;
    }

    int display_mask = 0;
    if (SendMessageW(m_show_2g4_check, BM_GETCHECK, 0, 0) == BST_CHECKED)
        display_mask |= 0x01;
    if (SendMessageW(m_show_logitech_check, BM_GETCHECK, 0, 0) == BST_CHECKED)
        display_mask |= 0x02;
    if (SendMessageW(m_show_gamepad_check, BM_GETCHECK, 0, 0) == BST_CHECKED)
        display_mask |= 0x04;

    m_settings.selected_device = std::clamp(device, 0, 3);
    m_settings.show_tooltip = SendMessageW(m_tooltip_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    m_settings.enable_generic_hid = SendMessageW(m_generic_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    m_settings.display_mask = display_mask;
    m_settings.poll_interval = static_cast<int>(interval);

    m_result = true;
    DestroyWindow(m_hwnd);
}

void COptionsDlg::RefreshDetection(bool probe_logitech)
{
    const std::vector<HidDeviceInfo> devices = EnumerateHidDevices();
    std::vector<std::wstring> receiver;
    std::vector<const HidDeviceInfo*> logitech;
    for (const HidDeviceInfo& device : devices)
    {
        if (device.kind == DeviceKind::LogitechHidpp)
            logitech.push_back(&device);
        else if (device.kind == DeviceKind::Aula2G4 || device.kind == DeviceKind::AtkVxe2G4 ||
                 device.kind == DeviceKind::GenericHid)
            receiver.push_back(FormatDeviceLine(device));
    }

    std::wstring logitech_text;
    if (!probe_logitech)
    {
        // 打开选项时先用后台线程已经读到的结果，避免同步查询 HID 造成窗口卡顿。
        const BatterySnapshot snapshot = g_data.GetSnapshot(SnapshotKind::Logitech);
        if (snapshot.has_data && !snapshot.device_name.empty())
        {
            logitech_text = snapshot.device_name;
            if (snapshot.percent >= 0)
                logitech_text += L" " + std::to_wstring(snapshot.percent) + L"%";
            else if (snapshot.wired)
                logitech_text += L" 有线";
            if (!snapshot.device_id.empty())
                logitech_text += L" [" + snapshot.device_id + L"]";
            if (!snapshot.connected)
                logitech_text += L"（无响应）";
        }
    }

    if (logitech_text.empty())
    {
        // 和后台查询保持相同的优先级：长报文优先，其次 USB 接收器，最后蓝牙。
        auto candidate_score = [](const HidDeviceInfo* device)
        {
            int score = device->output_len >= 20 ? 10 : 0;
            if (device->path.find(L"vid_046d&pid_") != std::wstring::npos)
                score += 5;
            return score;
        };
        std::stable_sort(logitech.begin(), logitech.end(),
            [&](const HidDeviceInfo* a, const HidDeviceInfo* b)
            {
                return candidate_score(a) > candidate_score(b);
            });

        if (probe_logitech)
        {
            logitech_text = L"未检测到";
            for (const HidDeviceInfo* device : logitech)
            {
                const BatteryReading reading = ReadBatteryFromDevice(*device);
                if (!reading.ok)
                    continue;
                logitech_text = reading.device_name.empty()
                    ? device->DisplayName() : reading.device_name;
                if (reading.percent >= 0)
                    logitech_text += L" " + std::to_wstring(reading.percent) + L"%";
                else if (reading.wired)
                    logitech_text += L" 有线";
                logitech_text += L" [" + device->IdString() + L"]";
                break;
            }
            if (logitech_text == L"未检测到" && !logitech.empty())
                logitech_text = L"设备无响应（可能已休眠或被其他程序占用）";
        }
        else
        {
            std::vector<std::wstring> names;
            for (const HidDeviceInfo* device : logitech)
                names.push_back(FormatDeviceLine(*device));
            logitech_text = JoinDeviceLines(names, L"未检测到");
        }
    }

    const std::vector<XInputDeviceInfo> pads = EnumerateXInputDevices();
    std::wstring pad_text = L"未检测到";
    if (!pads.empty())
    {
        pad_text = pads.front().name;
        if (pads.size() > 1)
            pad_text += L" 等 " + std::to_wstring(pads.size()) + L" 个";
    }

    SetWindowTextW(m_detect_lines[0],
        (L"2.4G：" + JoinDeviceLines(receiver, L"未检测到")).c_str());
    SetWindowTextW(m_detect_lines[1], (L"罗技：" + logitech_text).c_str());
    SetWindowTextW(m_detect_lines[2], (L"手柄：" + pad_text).c_str());
}

LRESULT CALLBACK COptionsDlg::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<COptionsDlg*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }

    auto* self = reinterpret_cast<COptionsDlg*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr)
        return self->WndProc(hwnd, message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT COptionsDlg::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(wParam, lParam);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_DETECT_BUTTON:
            RefreshDetection(true);
            return 0;
        case IDC_SHOW_2G4:
        case IDC_SHOW_LOGITECH:
        case IDC_SHOW_GAMEPAD:
            UpdateDeviceControls();
            return 0;
        case IDOK:
            OnOk();
            return 0;
        case IDCANCEL:
            m_result = false;
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        m_result = false;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        m_done = true;
        m_hwnd = nullptr;
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            m_result = false;
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            OnOk();
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool COptionsDlg::DoModal()
{
    m_done = false;
    m_result = false;
    m_hwnd = nullptr;

    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&COptionsDlg::StaticWndProc), &module);
    m_instance = module;

    HDC parent_dc = GetDC(m_parent);
    if (parent_dc != nullptr)
    {
        const int dpi = GetDeviceCaps(parent_dc, LOGPIXELSY);
        if (dpi > 0)
            m_dpi = static_cast<UINT>(dpi);
        ReleaseDC(m_parent, parent_dc);
    }
    if (m_dpi == 0)
        m_dpi = 96;

    const wchar_t* class_name = L"ReceiverBatteryOptionsWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = m_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = class_name;
    RegisterClassExW(&wc);

    RECT rough_rect{ 0, 0, Scale(kLogicalWidth), Scale(360) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectEx(&rough_rect, style, FALSE, ex_style);

    const int width = rough_rect.right - rough_rect.left;
    const int height = rough_rect.bottom - rough_rect.top;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (m_parent != nullptr && IsWindow(m_parent))
    {
        RECT parent_rect{};
        GetWindowRect(m_parent, &parent_rect);
        x = parent_rect.left + ((parent_rect.right - parent_rect.left) - width) / 2;
        y = parent_rect.top + ((parent_rect.bottom - parent_rect.top) - height) / 2;
    }

    m_hwnd = CreateWindowExW(ex_style, class_name, L"外设电量设置", style,
        x, y, width, height, m_parent, nullptr, m_instance, this);
    if (m_hwnd == nullptr)
        return false;

    if (m_parent != nullptr)
        EnableWindow(m_parent, FALSE);

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    SetForegroundWindow(m_hwnd);

    MSG msg{};
    while (!m_done && IsWindow(m_hwnd))
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_done = true;
                break;
            }
            if (!IsDialogMessageW(m_hwnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (m_done || !IsWindow(m_hwnd))
                break;
        }
        if (!m_done && IsWindow(m_hwnd))
            MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_ALLINPUT);
    }

    if (m_parent != nullptr)
    {
        EnableWindow(m_parent, TRUE);
        SetActiveWindow(m_parent);
    }

    if (m_font_owned && m_font != nullptr)
        DeleteObject(m_font);
    m_font = nullptr;
    m_font_owned = false;
    return m_result;
}
