#pragma once
#include "PluginInterface.h"
#include "DataManager.h"

class CBatteryItem : public IPluginItem
{
public:
    CBatteryItem();
    ~CBatteryItem();

    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override;
    virtual int GetItemWidthEx(void* hDC) const override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

private:
    //充电动画的定时器。不能在构造函数中创建（构造发生在DllMain期间），改为在首次绘制时创建
    void EnsureAnimationTimer();
    UINT_PTR m_anim_timer_id{ 0 };
};
