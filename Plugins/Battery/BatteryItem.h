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
    void StopAnimationTimer();      //关闭充电动画选项或监控项不再显示时停用定时器
    //定时器回调必须是无捕获lambda（需转换为函数指针），定时器ID用静态成员以便在回调内复位
    static UINT_PTR m_anim_timer_id;
    //GetItemValueText为const接口，缓存值保存在mutable成员中，避免使用函数级static导致所有实例共享同一个缓冲区
    mutable std::wstring m_item_value_cache;
};
