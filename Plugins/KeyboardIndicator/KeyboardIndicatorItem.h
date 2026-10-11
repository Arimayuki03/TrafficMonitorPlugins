#pragma once
#include "PluginInterface.h"
#include "DataManager.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CKeyboardIndicatorItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

private:
    //多显示器下每个窗口的DC的DPI不同，字体必须按DPI缓存。
    //绘制/测量函数内不允许修改任何跨调用共享的可变状态，
    //否则两个显示器交替绘制时状态互相污染，导致显示尺寸反复变化
    struct DrawContext
    {
        int dpi{ 96 };
        CFont* font{ nullptr };     //指向m_fonts中的字体，生命周期由m_fonts保证
        int item_height{ 0 };
        int font_height{ 0 };
        int text_gap{ 0 };          //指示器框内，文字左右多留的宽度
        int box_space{ 0 };         //相邻指示器框之间的间距
    };
    DrawContext PrepareDrawContext(HDC hDC) const;
    CFont* GetFontFor(int font_height) const;

    //一个显示区域的绘制计划。测量(GetItemWidthEx)与绘制(DrawItem)都先生成它，
    //两者用的是同一套宽度，不会出现"测量值与实际绘制的消耗不一致"的问题
    struct FieldPlan
    {
        DisplayItem id{};                       //显示项目
        std::wstring text;                      //要显示的文字(不含框内左右留白)，为空表示该区域只占位不绘制
        bool active{ false };                   //点亮状态(锁定键开启、按键按住；计数与KPM恒为点亮)
        COLORREF color{ ITEM_COLOR_DEFAULT };   //自定义颜色，ITEM_COLOR_DEFAULT为跟随主程序默认色
        int width{ 0 };                         //该区域宽度(含框内左右留白)
    };
    //按用户配置的顺序收集当前可见的显示区域并计算各自宽度
    std::vector<FieldPlan> PlanFields(CDC* pDC, const DrawContext& ctx) const;

private:
    mutable std::mutex m_font_mutex;    //保护m_fonts：GetItemWidthEx/DrawItem都是const接口，但会向缓存插入字体
    mutable std::map<int, std::unique_ptr<CFont>> m_fonts;   //按字体像素高度缓存字体(key为正值)
};
