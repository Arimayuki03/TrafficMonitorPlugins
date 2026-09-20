#pragma once
#include "PluginInterface.h"
#include <string>

class CIpAddressItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;

private:
    //GetItemValueText等绘制接口仅由宿主UI线程调用，此缓冲仅UI线程写入，用于返回稳定的指针
    mutable std::wstring m_value_text_ui;
};
