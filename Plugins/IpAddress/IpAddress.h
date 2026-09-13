#pragma once
#include "PluginInterface.h"
#include "IpAddressItem.h"
#include <string>

class CIpAddress : public ITMPlugin
{
private:
    CIpAddress();

public:
    static CIpAddress& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual const wchar_t* GetTooltipInfo() override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    virtual void* GetPluginIcon() override;
    virtual int GetCommandCount() override;
    virtual const wchar_t* GetCommandName(int command_index) override;
    virtual void OnPluginCommand(int command_index, void* hWnd, void* para) override;
    virtual void* GetCommandIcon(int command_index) override;

private:

private:
    static CIpAddress m_instance;
    CIpAddressItem m_item;
    std::wstring m_tooltip_info;
    DWORD m_last_refresh_tick{ 0 };     //上次自动刷新网络连接的时间
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
