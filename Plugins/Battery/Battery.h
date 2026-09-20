#pragma once
#include "PluginInterface.h"
#include "BatteryItem.h"
#include <string>
#include <mutex>

class CBattery : public ITMPlugin
{
private:
    CBattery();

public:
    static CBattery& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void* GetPluginIcon() override;

private:
    static CBattery m_instance;
    CBatteryItem m_item;
    std::wstring m_tooltop_info;
    //m_tooltop_info由监控线程DataRequired写、UI线程GetTooltipInfo读，加锁保护；
    //UI侧不直接使用成员，而是锁内拷贝到副本再返回
    std::mutex m_tooltip_mutex;
    std::wstring m_tooltop_info_display;
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
