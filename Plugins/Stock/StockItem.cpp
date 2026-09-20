#include "pch.h"
#include "StockItem.h"
#include "DataManager.h"
#include "Stock.h"
#include "Common.h"
#include <algorithm>
#include "FloatingWnd.h"
#undef min
#undef max

const wchar_t *StockItem::GetItemName() const
{
    std::wstring &item_name{ m_item_name_cache };
    //在锁内读取股票数据，避免与后台刷新线程产生数据竞争
    std::lock_guard<std::recursive_mutex> lock(Stock::Instance().m_stockDataMutex);
    auto data = g_data.GetStockData(stock_id);
    if (data->info.is_ok)
    {
        //GetStockData不会返回空指针(miss时插入新项)，无需判空
        item_name = data->info.displayName;
    }
    else
    {
        item_name = stock_id + L" " + g_data.StringRes(IDS_LOAD_FAIL).GetString();
    }
    return item_name.c_str();
}

const wchar_t *StockItem::GetItemId() const
{
    std::wstring &item_id{ m_item_id_cache };
    item_id = L"qL0KmmYi";
    item_id += std::to_wstring(index);
    return item_id.c_str();
}

const wchar_t *StockItem::GetItemLableText() const
{
    return L"";
}

const wchar_t *StockItem::GetItemValueText() const
{
    return L"";
}

bool StockItem::IsCustomDraw() const
{
    return true;
}
int StockItem::GetItemWidthEx(void *hDC) const
{
    CDC *pDC = CDC::FromHandle((HDC)hDC);
    //股票数据与设置字段分别由两把锁保护，两把锁从不嵌套(与后台线程锁序一致)，
    //各自在锁内拷贝副本、锁外使用
    bool show_stock_name;
    {
        std::lock_guard<std::mutex> settings_lock(g_data.m_settings_mutex);
        show_stock_name = g_data.m_setting_data.m_show_stock_name;
    }
    std::wstring display_text;
    {
        std::lock_guard<std::recursive_mutex> lock(Stock::Instance().m_stockDataMutex);
        display_text = g_data.GetStockData(stock_id)->GetCurrentDisplay(show_stock_name);
    }

    int width = pDC->GetTextExtent(display_text.c_str()).cx;

    LogX(L"GetItemWidthEx: %d\n", width);
    return width;
}

void StockItem::DrawItem(void *hDC, int x, int y, int w, int h, bool dark_mode)
{
    // 绘图句柄
    CDC *pDC = CDC::FromHandle((HDC)hDC);

    // 矩形区域
    CRect rect(CPoint(x, y), CSize(w, h));

    //在锁内拷贝绘图所需的数据副本，避免与后台刷新线程产生数据竞争；
    //股票数据与设置字段分别由两把锁保护，锁不嵌套，各自拷贝后锁外使用
    STOCK::StockInfo info_copy;
    std::wstring display_text;
    bool show_stock_name;
    bool color_with_price;
    {
        std::lock_guard<std::recursive_mutex> lock(Stock::Instance().m_stockDataMutex);
        auto data = g_data.GetStockData(stock_id);
        info_copy = data->info;
        display_text = data->GetCurrentDisplay(false);
    }
    {
        std::lock_guard<std::mutex> settings_lock(g_data.m_settings_mutex);
        show_stock_name = g_data.m_setting_data.m_show_stock_name;
        color_with_price = g_data.m_setting_data.m_color_with_price;
    }

    // 文本颜色
    COLORREF color_default;
    COLORREF color_red;
    COLORREF color_green;
    if (dark_mode)
    {
        color_default = RGB(255, 255, 255);
        color_red = RGB(255, 121, 120);
        color_green = RGB(111, 215, 149);
    }
    else
    {
        color_default = RGB(0, 0, 0);
        color_red = RGB(195, 0, 0);
        color_green = RGB(46, 139, 87);
    }

    CRect rect_value{rect};
    if (info_copy.is_ok && show_stock_name)
    {
        // 绘制名称
        pDC->SetTextColor(color_default);
        CString stock_name{info_copy.displayName.c_str()};
        stock_name += _T(": ");
        CRect rect_name{rect};
        rect_name.right = rect_name.left + pDC->GetTextExtent(stock_name).cx;
        pDC->DrawText(stock_name, rect_name, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        rect_value.left = rect_name.right;
    }

    if (color_with_price)
    {
        // 绘制数值
        if (info_copy.displayFluctuation.find('-') != std::wstring::npos)
            pDC->SetTextColor(color_green);
        else
            pDC->SetTextColor(color_red);
    }
    else
    {
        pDC->SetTextColor(color_default);
    }

    UINT flags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    if (g_data.m_right_align)
        flags |= DT_RIGHT;
    pDC->DrawText(display_text.c_str(), rect_value, flags);
}

const wchar_t *StockItem::GetItemValueSampleText() const
{
    //    if (g_data.m_setting_data.m_show_stock_name)
    //    {
    //        return L"--------: 0000000.00 +00.00%";
    //    }
    //    else
    //    {
    //        return L"0000000.00 +00.00%";
    //    }
    return L"";
}

int StockItem::OnMouseEvent(MouseEventType type, int x, int y, void *hWnd, int flag)
{
    CWnd *pWnd = CWnd::FromHandle((HWND)hWnd);
    LogX(L"OnMouseEvent: %d\n", type);
    switch (type)
    {
    case IPluginItem::MT_RCLICKED:
        Stock::Instance().ShowContextMenu(pWnd);
        return 1;

    case IPluginItem::MT_LCLICKED:
    {
        if (stock_id.find(kSZ) == 0 || stock_id.find(kBJ) == 0 || stock_id.find(kSH) == 0)
        {
            CPoint ptScreen = CPoint(x, y);
            Stock::Instance().ShowFloatingWnd(hWnd, ptScreen, stock_id);
            return 1;
        }
        else
        {
            //弹出了提示框即已处理本次点击，必须返回1，否则穿透到default返回0，
            //宿主会继续按未处理处理该点击
            MessageBox((HWND)hWnd, g_data.StringRes(IDS_UNSUPPORT_SHOW_KLINE_STOCK_TIP), g_data.StringRes(IDS_PLUGIN_NAME), MB_ICONINFORMATION | MB_OK);
            return 1;
        }
    }
    default:
        break;
    }
    return 0;
}
