#pragma once
#include "PluginInterface.h"
#include "FloatingWnd.h"

class StockItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
	virtual bool IsCustomDraw() const override;
	virtual int GetItemWidthEx(void* hDC) const override;

    int index;
    std::wstring stock_id;
    bool enable;

private:
    //GetItemName/GetItemId为const成员，返回值缓存在mutable成员中，
    //避免使用函数级static导致所有实例共享同一个缓冲区
    mutable std::wstring m_item_name_cache;
    mutable std::wstring m_item_id_cache;
};
