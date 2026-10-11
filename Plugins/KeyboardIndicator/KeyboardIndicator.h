#pragma once
#include "PluginInterface.h"
#include "KeyboardIndicatorItem.h"
#include <string>
#include <mutex>

class CKeyboardIndicator : public ITMPlugin
{
private:
    CKeyboardIndicator();

public:
    static CKeyboardIndicator& Instance();

    virtual IPluginItem* GetItem(int item_index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* parent_hwnd) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void* GetPluginIcon() override;

    static bool IsCapsLockOn();
    static bool IsNumLockOn();
    static bool IsScrollLockOn();
private:
    static CKeyboardIndicator m_instance;
    CKeyboardIndicatorItem m_item;
    std::wstring m_tooltip_info;
    std::wstring m_tooltip_info_buffer;     //GetTooltipInfo返回的副本缓冲
    mutable std::mutex m_tooltip_mutex;     //保护m_tooltip_info：监控线程写、UI线程读
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
