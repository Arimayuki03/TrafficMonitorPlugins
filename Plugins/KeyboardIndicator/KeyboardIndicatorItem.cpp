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

//获取当前要显示的累计按键计数文本。统计未启用或不显示计数时返回false
static bool GetKeyCountText(std::wstring& text)
{
    if (!g_data.m_setting_data.enable_key_stats || !g_data.m_setting_data.show_key_count)
        return false;
    unsigned long long total = g_data.GetStatsTotal();
    wchar_t buff[24];
    swprintf_s(buff, L"%llu", total);
    text = buff;
    return true;
}

//获取当前要显示的KPM文本(如"42/min")。统计未启用或未开启KPM时返回false。
//KPM是独立显示项：不依赖"显示累计计数"开关，关闭计数时KPM框独自显示在最左
static bool GetKpmText(std::wstring& text)
{
    if (!g_data.m_setting_data.enable_key_stats || !g_data.m_setting_data.show_kpm)
        return false;
    unsigned long current_kpm = 0, peak_kpm = 0, avg_kpm = 0;
    CKeyboardHook::Instance().GetTypingSpeed(current_kpm, peak_kpm, avg_kpm);
    wchar_t buff[24];
    swprintf_s(buff, L"%lu/min", current_kpm);
    text = buff;
    return true;
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

//按用户配置的顺序收集当前可见的显示区域，并计算各自的宽度。
//测量与绘制都调用本函数，是宽度计算的唯一来源：任何"某区域宽度设置不生效"的问题
//都只可能出在这里，不会再出现测量与绘制各算一套的情况
std::vector<CKeyboardIndicatorItem::FieldPlan> CKeyboardIndicatorItem::PlanFields(CDC* pDC, const DrawContext& ctx) const
{
    const SettingData& setting = g_data.m_setting_data;
    //区域宽度 = 文字宽度 + 框内左右留白；配置了最小宽度(>0)时至少保持该宽度(按本DC的DPI缩放)。
    //最小宽度保证任务栏宽度不随内容长短抖动，内容更长时自动扩展，绝不裁掉内容
    auto field_width = [&](const std::wstring& text, int min_width) {
        int width = pDC->GetTextExtent(text.c_str()).cx + ctx.text_gap;
        if (min_width > 0)
        {
            const int scaled = ctx.dpi * min_width / 96;
            if (width < scaled)
                width = scaled;
        }
        return width;
    };
    std::vector<FieldPlan> fields;
    fields.reserve(DISPLAY_ITEM_COUNT);
    for (int i = 0; i < DISPLAY_ITEM_COUNT; ++i)
    {
        //顺序表来自INI，LoadConfig已校验为合法排列，这里再兜底一次以防御越界
        const int item_index = setting.item_order[i];
        if (item_index < 0 || item_index >= DISPLAY_ITEM_COUNT)
            continue;
        const DisplayItem item = static_cast<DisplayItem>(item_index);
        FieldPlan field;
        field.id = item;
        field.color = setting.item_colors[item_index];
        switch (item)
        {
        case DisplayItem::CapsLock:
            if (!setting.show_caps_lock)
                continue;
            field.text = INDICATOR_CAPS_LOCK;
            field.active = CKeyboardIndicator::IsCapsLockOn();
            field.width = field_width(field.text, 0);
            break;
        case DisplayItem::NumLock:
            if (!setting.show_num_lock)
                continue;
            field.text = INDICATOR_NUM_LOCK;
            field.active = CKeyboardIndicator::IsNumLockOn();
            field.width = field_width(field.text, 0);
            break;
        case DisplayItem::ScrollLock:
            if (!setting.show_scroll_lock)
                continue;
            field.text = INDICATOR_SCROLL_LOCK;
            field.active = CKeyboardIndicator::IsScrollLockOn();
            field.width = field_width(field.text, 0);
            break;
        case DisplayItem::KeyCount:
            if (!setting.enable_key_stats || !setting.show_key_count)
                continue;
            if (!GetKeyCountText(field.text))
                continue;
            field.active = true;
            field.width = field_width(field.text, setting.key_count_reserved_width);
            break;
        case DisplayItem::Kpm:
            if (!setting.enable_key_stats || !setting.show_kpm)
                continue;
            if (!GetKpmText(field.text))
                continue;
            field.active = true;
            field.width = field_width(field.text, setting.kpm_reserved_width);
            break;
        case DisplayItem::PressedKey:
        {
            if (!setting.show_pressed_key)
                continue;
            GetPressedKeyText(field.text);
            field.active = CKeyboardHook::Instance().IsAnyKeyDown();
            const int min_width = setting.pressed_key_reserved_width;
            //配置了最小宽度时始终占位(没有按键按下时留空框位)：主程序每隔1秒才重算
            //显示区宽度，始终占位才不会在按下/松开按键时让整个显示项左右跳动
            if (field.text.empty() && min_width <= 0)
                continue;
            field.width = field_width(field.text, min_width);
            break;
        }
        default:
            continue;       //DisplayItem::Count不在任务栏上显示
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

int CKeyboardIndicatorItem::GetItemWidthEx(void * hDC) const
{
    //绘图句柄
    CDC* pDC = CDC::FromHandle((HDC)hDC);
    DrawContext ctx = PrepareDrawContext(pDC->GetSafeHdc());
    if (ctx.font == nullptr)
        return 0;
    CFont* pOldFont = pDC->SelectObject(ctx.font);
    //与DrawItem共用同一套区域计划：测量值即绘制实际消耗的宽度(每个区域宽度+区域间距)
    const std::vector<FieldPlan> fields = PlanFields(pDC, ctx);
    int width = 0;
    for (const FieldPlan& field : fields)
        width += field.width + ctx.box_space;
    //恢复字体
    pDC->SelectObject(pOldFont);
    return width;
}

static void DrawRectOutLine(CDC* pDC, CRect rect, COLORREF color, int pen_width)	//绘制矩形边框
{
    CPen aPen, * pOldPen;
    aPen.CreatePen(PS_SOLID, pen_width, color);
    pOldPen = pDC->SelectObject(&aPen);
    //SelectStockObject返回的是CGdiObject临时包装，dynamic_cast<CBrush*>恒为空，
    //恢复会变成no-op并把NULL_BRUSH状态泄漏进宿主DC；用基类指针保存并回写
    CGdiObject* pOldBrush = pDC->SelectStockObject(NULL_BRUSH);

    pDC->Rectangle(rect);
    pDC->SelectObject(pOldPen);
    if (pOldBrush != nullptr)
        pDC->SelectObject(pOldBrush);   // Restore the old brush
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

//绘制一个指示器框。区域宽度由调用方按"配置的最小宽度/内容宽度"算好后传入，
//这里不再自行改动宽度，保证绘制消耗与GetItemWidthEx的测量值一一对应
static void DrawIndicator(CDC* pDC, CRect& rect, const wchar_t* text, bool dark_mode, bool active,
    COLORREF color_ori, int pen_width, int corner_radius, int spacing, int box_width,
    COLORREF color_custom = ITEM_COLOR_DEFAULT, COLORREF color_text_override = 0)
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

    //未点亮(锁定键未开启、按键已松开)时用灰色，保持状态可辨；
    //点亮时若配置了自定义颜色则文字与边框都用它，否则文字用主程序设置的颜色、边框用主题色
    COLORREF color_text;
    COLORREF color_frame;
    if (!active)
    {
        color_text = color_disable;
        color_frame = color_disable;
    }
    else if (color_custom != ITEM_COLOR_DEFAULT)
    {
        color_text = color_custom;
        color_frame = color_custom;
    }
    else
    {
        color_text = color_ori;
        color_frame = color_default;
    }
    //外部指定的状态提示色优先(如计数达到每日目标时的绿色)
    if (color_text_override != 0)
        color_text = color_text_override;
    if (box_width < 1)
        box_width = 1;
    rect.right = rect.left + box_width;
    //绘制边框
    if (g_data.m_setting_data.draw_round_rect)
        DrawRoundRectOutLine(pDC, rect, color_frame, (float)pen_width, corner_radius);
    else
        DrawRectOutLine(pDC, rect, color_frame, pen_width);
    //绘制文本(文本宽度超过区域宽度时显示省略号)
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
    //按用户配置的顺序绘制各显示区域(顺序、最小宽度、自定义颜色都在计划里)
    const std::vector<FieldPlan> fields = PlanFields(pDC, ctx);
    const int goal = g_data.m_setting_data.daily_goal;
    const bool goal_reached = (goal > 0 && g_data.GetStatsToday() >= static_cast<unsigned long long>(goal));
    for (const FieldPlan& field : fields)
    {
        //主程序分配的宽度可能小于测量值(配置刚改动、主程序尚未重算)，此时收缩到剩余区域，
        //保证任何情况下都不会画到相邻显示项目上
        const int remaining = (x + w) - rect_indicator.left;
        if (remaining <= 0)
            break;
        int box_width = field.width;
        if (box_width > remaining)
            box_width = remaining;
        if (field.text.empty())
        {
            //配置了最小宽度但当前没有内容可显示(如没有按键按下)：只占位不绘制
            rect_indicator.MoveToX(rect_indicator.left + box_width + ctx.box_space);
            continue;
        }
        //计数达到每日目标时用绿色文字提示(自定义颜色不覆盖这一状态提示)
        COLORREF text_override = 0;
        if (field.id == DisplayItem::KeyCount && goal_reached)
            text_override = dark_mode ? RGB(80, 220, 100) : RGB(0, 150, 60);
        DrawIndicator(pDC, rect_indicator, field.text.c_str(), dark_mode, field.active, color_ori,
            pen_width, corner_radius, ctx.box_space, box_width, field.color, text_override);
    }
    //恢复字体
    pDC->SelectObject(old_font);
}
