#include "pch.h"
#include "WeatherItem.h"
#include "DataManager.h"
#include "Weather.h"
#include <algorithm>
#undef min
#undef max

const wchar_t* CWeatherItem::GetItemName() const
{
    return g_data.StringRes(IDS_WEATHER);
}

const wchar_t* CWeatherItem::GetItemId() const
{
    return L"NdKZEf39";
}

const wchar_t* CWeatherItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CWeatherItem::GetItemValueText() const
{
    return L"";
}

const wchar_t* CWeatherItem::GetItemValueSampleText() const
{
    const WeatherInfo& weather_info{ g_data.GetWeather() };
    if (g_data.m_setting_data.m_use_weather_icon)
    {
        if (weather_info.is_cur_weather)
            return L"20℃";
        else
            return L"20~20℃";
    }
    else
    {
        if (weather_info.is_cur_weather)
            return L"多云 20℃";
        else
            return L"多云 20~20℃";
    }
}

bool CWeatherItem::IsCustomDraw() const
{
    return true;
}

int CWeatherItem::GetItemWidthEx(void* hDC) const
{
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    //在锁内复制天气文本，避免与后台刷新线程产生数据竞争
    std::wstring weather_text;
    {
        std::lock_guard<std::recursive_mutex> lock(CWeather::Instance().m_data_mutex);
        weather_text = (g_data.m_setting_data.m_use_weather_icon ? g_data.GetWeather().ToStringTemperature() : g_data.GetWeather().ToString());
    }
    if (g_data.m_setting_data.m_use_weather_icon)
    {
        int icon_width = m_double_line ? g_data.DPI(36) : g_data.DPI(20);
        return icon_width + pDC->GetTextExtent(weather_text.c_str()).cx;
    }
    else
    {
        return pDC->GetTextExtent(weather_text.c_str()).cx;
    }
}

void CWeatherItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    //判断是否为双行显示
    m_double_line = h >= g_data.DPI(32);
    //绘图句柄
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    //矩形区域
    CRect rect(CPoint(x, y), CSize(w, h));
    //在锁内复制需要显示的天气数据，避免与后台刷新线程产生数据竞争
    std::wstring weather_type;
    std::wstring weather_text;
    {
        std::lock_guard<std::recursive_mutex> lock(CWeather::Instance().m_data_mutex);
        weather_type = g_data.GetWeather().m_type;
        weather_text = (g_data.m_setting_data.m_use_weather_icon ? g_data.GetWeather().ToStringTemperature() : g_data.GetWeather().ToString());
    }
    if (g_data.m_setting_data.m_use_weather_icon)
    {
        //绘制天气图标
        const int icon_size{ m_double_line ? g_data.DPI(24) : g_data.DPI(16) };
        HICON hIcon = g_data.GetWeatherIcon(weather_type, m_double_line);
        CPoint icon_point{ rect.TopLeft() };
        icon_point.x = rect.left + g_data.DPI(2);
        icon_point.y = rect.top + (rect.Height() - icon_size) / 2;
        ::DrawIconEx(pDC->GetSafeHdc(), icon_point.x, icon_point.y, hIcon, icon_size, icon_size, 0, NULL, DI_NORMAL);
        //绘制天气文本
        CRect rc_text{ rect };
        rc_text.left += (icon_size + g_data.DPI(4));
        pDC->DrawText(weather_text.c_str(), rc_text, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        pDC->DrawText(weather_text.c_str(), rect, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

int CWeatherItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
    //CWnd* pWnd = CWnd::FromHandle((HWND)hWnd);
    //if (type == IPluginItem::MT_RCLICKED)
    //{
    //    CWeather::Instance().ShowContextMenu(pWnd);
    //    return 1;
    //}
    return 0;
}
