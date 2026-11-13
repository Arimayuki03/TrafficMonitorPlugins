#pragma once

#include <windows.h>

#include "Settings.h"

class COptionsDlg
{
public:
    COptionsDlg(HWND parent, const SettingData& settings);
    bool DoModal();
    const SettingData& GetSettings() const { return m_settings; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void OnCreate();
    void OnDpiChanged(WPARAM wParam, LPARAM lParam);
    void Layout();
    void PositionWindow();
    void RecreateFont();
    void OnOk();
    void UpdateDeviceControls();
    void RefreshDetection(bool probe_logitech);
    HWND CreateControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id);
    int Scale(int value) const;
    UINT QueryDpi() const;
    int MeasureTextWidth(const wchar_t* text) const;
    int MeasureWrappedHeight(const wchar_t* text, int width) const;

    HWND m_hwnd{};
    HWND m_parent{};
    HINSTANCE m_instance{};
    HFONT m_font{};
    bool m_font_owned{};
    UINT m_dpi{ 96 };
    bool m_result{};
    bool m_done{};
    SettingData m_settings;

    HWND m_device_label{};
    HWND m_device_combo{};
    HWND m_generic_check{};
    HWND m_generic_hint{};
    HWND m_multi_label{};
    HWND m_show_2g4_check{};
    HWND m_show_logitech_check{};
    HWND m_show_gamepad_check{};
    HWND m_tooltip_check{};
    HWND m_interval_label{};
    HWND m_interval_edit{};
    HWND m_interval_unit{};
    HWND m_note{};
    HWND m_detect_label{};
    HWND m_detect_lines[3]{};
    HWND m_detect_button{};
    HWND m_ok_button{};
    HWND m_cancel_button{};
};
