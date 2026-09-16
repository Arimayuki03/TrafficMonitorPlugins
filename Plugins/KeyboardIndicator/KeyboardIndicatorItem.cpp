#include "pch.h"
#include "KeyboardIndicatorItem.h"
#include "DataManager.h"
#include "KeyboardIndicator.h"
#include "KeyboardHook.h"
#include <gdiplus.h>

const wchar_t* INDICATOR_CAPS_LOCK = L"Caps";
const wchar_t* INDICATOR_NUM_LOCK = L"Num";
const wchar_t* INDICATOR_SCROLL_LOCK = L"ScrLk";

//获取当前要显示的按键文本，如"Ctrl+Shift+A"。没有需要显示的内容时返回false
static bool GetPressedKeyText(std::wstring& text)
{
    if (!g_data.m_setting_data.show_pressed_key)
        return false;
    return CKeyboardHook::Instance().GetDisplayText(text, g_data.m_setting_data.pressed_key_show_time);
}


const wchar_t* CKeyboardIndicatorItem::GetItemName() const
{
    return g_data.StringRes(IDS_PLUGIN_ITEM_NAME);
}

const wchar_t* CKeyboardIndicatorItem::GetItemId() const
{
    return L"3db99u17";
}

const wchar_t* CKeyboardIndicatorItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CKeyboardIndicatorItem::GetItemValueText() const
{
    return L"";
}

const wchar_t* CKeyboardIndicatorItem::GetItemValueSampleText() const
{
    return L"";
}

bool CKeyboardIndicatorItem::IsCustomDraw() const
{
    return true;
}

//根据绘图DC准备绘制上下文。多显示器下每个窗口的DC的DPI不同，
//主程序会交替用不同窗口的DC调用GetItemWidthEx和DrawItem，
//因此这里只读取本次DC的DPI，不修改任何跨调用共享的状态（否则两个显示器交替绘制时尺寸会反复变化）
CKeyboardIndicatorItem::DrawContext CKeyboardIndicatorItem::PrepareDrawContext(HDC hDC) const
{
    DrawContext ctx;
    ctx.dpi = GetDeviceCaps(hDC, LOGPIXELSY);
    if (ctx.dpi <= 0)
        ctx.dpi = 96;
    auto dpi = [ctx](int pixel) { return ctx.dpi * pixel / 96; };
    //字体高度按96DPI下的9像素计算，测量与绘制使用同一种高度，保证宽度计算与实际绘制一致
    ctx.font_height = dpi(9);
    //指示器高度固定为96DPI下的14像素按DPI缩放，不依赖绘图区高度，
    //避免主程序布局变化时绘制尺寸随之波动
    ctx.item_height = dpi(14);
    ctx.text_gap = dpi(4);
    ctx.box_space = dpi(2);
    //每个指示器占用的宽度 = 文字 + text_gap + box_space，与DrawIndicator的实际横向推进一致
    //(框宽 文字+text_gap，推进后再加box_space)，保证预留宽度不小于绘制消耗，避免末尾指示器被裁剪
    ctx.item_space = ctx.text_gap + ctx.box_space;
    ctx.font = GetFontFor(ctx.font_height);
    return ctx;
}

CFont* CKeyboardIndicatorItem::GetFontFor(int font_height) const
{
    std::lock_guard<std::mutex> lock(m_font_mutex);
    auto iter = m_fonts.find(font_height);
    if (iter != m_fonts.end())
        return iter->second.get();
    auto font = std::make_unique<CFont>();
    if (!font->CreateFont(-font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Segoe UI")))
        return nullptr;
    auto result = m_fonts.emplace(font_height, std::move(font));
    return result.first->second.get();
}

int CKeyboardIndicatorItem::GetItemWidthEx(void * hDC) const
{
    //绘图句柄
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    DrawContext ctx = PrepareDrawContext(pDC->GetSafeHdc());
    if (ctx.font == nullptr)
        return 0;
    CFont* pOldFont = pDC->SelectObject(ctx.font);
    int width = 0;
    if (g_data.m_setting_data.show_caps_lock)
        width += (pDC->GetTextExtent(INDICATOR_CAPS_LOCK).cx + ctx.item_space);
    if (g_data.m_setting_data.show_num_lock)
        width += (pDC->GetTextExtent(INDICATOR_NUM_LOCK).cx + ctx.item_space);
    if (g_data.m_setting_data.show_scroll_lock)
        width += (pDC->GetTextExtent(INDICATOR_SCROLL_LOCK).cx + ctx.item_space);
    //主程序每隔1秒才重新计算一次显示区域的宽度，如果根据当前显示的按键文本计算宽度，
    //按下的组合键变长时会因为宽度不足导致按键文本被旁边的项目遮挡约1秒，
    //因此默认使用固定的预留宽度，保证任意按键按下时都能立即完整显示
    if (g_data.m_setting_data.show_pressed_key)
    {
        if (g_data.m_setting_data.pressed_key_reserved_width > 0)
        {
            width += (g_data.m_setting_data.pressed_key_reserved_width * ctx.dpi / 96 + ctx.item_space);
        }
        else
        {
            //预留宽度为0时按当前显示的按键文本自适应
            std::wstring pressed_key_text;
            if (CKeyboardHook::Instance().GetDisplayText(pressed_key_text, g_data.m_setting_data.pressed_key_show_time))
                width += (pDC->GetTextExtent(pressed_key_text.c_str()).cx + ctx.item_space);
        }
    }
    //恢复字体
    pDC->SelectObject(pOldFont);
    return width;
}

static void DrawRectOutLine(CDC* pDC, CRect rect, COLORREF color, int pen_width)	//绘制矩形边框
{
    CPen aPen, * pOldPen;
    aPen.CreatePen(PS_SOLID, pen_width, color);
    pOldPen = pDC->SelectObject(&aPen);
    CBrush* pOldBrush{ dynamic_cast<CBrush*>(pDC->SelectStockObject(NULL_BRUSH)) };

    pDC->Rectangle(rect);
    pDC->SelectObject(pOldPen);
    pDC->SelectObject(pOldBrush);       // Restore the old brush
    aPen.DeleteObject();
}

static void DrawRoundRectOutLine(CDC* pDC, CRect rect, COLORREF color, float pen_width, int corner_radius)  //绘制圆角矩形
{
    Gdiplus::Graphics graphics(pDC->GetSafeHdc()); // 创建GDI+ Graphics对象
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias); // 启用抗锯齿

    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color))); // 创建画笔
    pen.SetWidth(pen_width); // 设置画笔宽度

    //GDI+描边以路径为中心线，路径向内收缩半个笔宽，保证整个边框落在rect内，
    //高DPI下pen_width按DPI放大后不会外溢到相邻显示区
    float x = rect.left + pen_width / 2; // 路径左上角x坐标
    float y = rect.top + pen_width / 2; // 路径左上角y坐标
    float width = static_cast<float>(rect.Width()) - pen_width; // 路径宽度
    float height = static_cast<float>(rect.Height()) - pen_width; // 路径高度
    float cornerRadius = static_cast<float>(corner_radius); // 圆角半径
    //圆角直径不能超过路径的宽/高，否则四段圆弧自交，边框变形
    if (cornerRadius * 2 > width)
        cornerRadius = width / 2;
    if (cornerRadius * 2 > height)
        cornerRadius = height / 2;

    // 创建圆角矩形路径
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, cornerRadius * 2, cornerRadius * 2, 180, 90); // 左上角
    path.AddArc(x + width - cornerRadius * 2, y, cornerRadius * 2, cornerRadius * 2, 270, 90); // 右上角
    path.AddArc(x + width - cornerRadius * 2, y + height - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 0, 90); // 右下角
    path.AddArc(x, y + height - cornerRadius * 2, cornerRadius * 2, cornerRadius * 2, 90, 90); // 左下角
    path.CloseFigure();

    // 绘制无填充的圆角矩形
    graphics.DrawPath(&pen, &path);
}

static void DrawIndicator(CDC* pDC, CRect& rect, const wchar_t* text, bool dark_mode, bool enable, COLORREF color_ori, int pen_width, int corner_radius, int spacing, int text_gap, int max_width = 0)
{
    COLORREF color_default;
    COLORREF color_disable;
    if (dark_mode)
    {
        color_default = RGB(255, 255, 255);
        color_disable = RGB(145, 145, 145);
    }
    else
    {
        color_default = RGB(0, 0, 0);
        color_disable = RGB(140, 140, 140);
    }

    COLORREF color_text = enable ? color_ori : color_disable;
    COLORREF color_frame = enable ? color_default : color_disable;
    //根据文本宽度设置矩形的宽度
    rect.right = rect.left + pDC->GetTextExtent(text).cx + text_gap;
    //可用宽度不足时限制矩形宽度，文本超出部分显示为省略号，避免绘制到旁边的显示区域
    if (max_width > 0 && rect.Width() > max_width)
        rect.right = rect.left + max_width;
    //绘制边框
    if (g_data.m_setting_data.draw_round_rect)
        DrawRoundRectOutLine(pDC, rect, color_frame, (float)pen_width, corner_radius);
    else
        DrawRectOutLine(pDC, rect, color_frame, pen_width);
    //绘制文本
    pDC->SetTextColor(color_text);
    pDC->DrawText(text, rect, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    //绘制完成后将矩形的左边框移动到右边框的位置
    rect.MoveToX(rect.right + spacing);
}

void CKeyboardIndicatorItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    //绘图句柄
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    DrawContext ctx = PrepareDrawContext(pDC->GetSafeHdc());
    if (ctx.font == nullptr)
        return;
    //矩形区域
    CRect rect(CPoint(x, y), CSize(w, h));
    //TrafficMonitor设置的文本颜色
    COLORREF color_ori = pDC->GetTextColor();
    CFont* old_font = pDC->SelectObject(ctx.font);
    //指示器高度固定为96DPI下的14像素按DPI缩放（不随绘图区高度波动），
    //超出绘图区时收缩到绘图区内，保证任何任务栏高度下都不越界
    int item_height = ctx.item_height;
    //先按绘图区高度收缩保证不越界(上下各留至少1px)，再兜底最小可见高度；
    //不能用固定下限覆盖收缩结果，否则绘图区很矮时会画到区域外
    if (item_height > h - 1)
        item_height = h - 1;
    if (item_height < 1)
        item_height = 1;
    int pen_width = ctx.dpi / 96;
    if (pen_width < 1)
        pen_width = 1;
    int corner_radius = ctx.dpi * 3 / 96;
    CRect rect_indicator{ rect };
    rect_indicator.top += (rect.Height() - item_height) / 2;
    rect_indicator.bottom = rect_indicator.top + item_height;
    //绘制Caps Lock
    if (g_data.m_setting_data.show_caps_lock)
        DrawIndicator(pDC, rect_indicator, INDICATOR_CAPS_LOCK, dark_mode, CKeyboardIndicator::IsCapsLockOn(), color_ori, pen_width, corner_radius, ctx.box_space, ctx.text_gap);
    //绘制num lock
    if (g_data.m_setting_data.show_num_lock)
        DrawIndicator(pDC, rect_indicator, INDICATOR_NUM_LOCK, dark_mode, CKeyboardIndicator::IsNumLockOn(), color_ori, pen_width, corner_radius, ctx.box_space, ctx.text_gap);
    //绘制scroll lock
    if (g_data.m_setting_data.show_scroll_lock)
        DrawIndicator(pDC, rect_indicator, INDICATOR_SCROLL_LOCK, dark_mode, CKeyboardIndicator::IsScrollLockOn(), color_ori, pen_width, corner_radius, ctx.box_space, ctx.text_gap);
    //绘制当前按下的按键，有按键按住时高亮显示，松开后显示时长内以灰色显示
    std::wstring pressed_key_text;
    if (GetPressedKeyText(pressed_key_text))
    {
        //使用固定预留宽度时，限制按键框不超出本显示项的剩余区域
        int max_key_width = 0;
        if (g_data.m_setting_data.pressed_key_reserved_width > 0)
            max_key_width = (x + w) - rect_indicator.left;
        DrawIndicator(pDC, rect_indicator, pressed_key_text.c_str(), dark_mode, CKeyboardHook::Instance().IsAnyKeyDown(), color_ori, pen_width, corner_radius, ctx.box_space, ctx.text_gap, max_key_width);
    }
    //恢复字体
    pDC->SelectObject(old_font);
}
