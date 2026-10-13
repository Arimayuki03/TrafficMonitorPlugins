#pragma once
#include "PluginInterface.h"
#include <map>
#include <memory>
#include <mutex>

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
        int item_space{ 0 };        //单个指示器占用的宽度 = text_gap + box_space，用于测量时预留宽度
    };
    DrawContext PrepareDrawContext(HDC hDC) const;
    CFont* GetFontFor(int font_height) const;

private:
    mutable std::mutex m_font_mutex;    //保护m_fonts：GetItemWidthEx/DrawItem都是const接口，但会向缓存插入字体
    mutable std::map<int, std::unique_ptr<CFont>> m_fonts;   //按字体像素高度缓存字体(key为正值)
};
