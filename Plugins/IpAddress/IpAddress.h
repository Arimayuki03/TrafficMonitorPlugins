#pragma once
#include "PluginInterface.h"
#include "IpAddressItem.h"
#include <string>
#include <mutex>

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
    std::wstring m_tooltip_info;            //tooltip文本值快照，仅监控线程在DataRequired中持有m_tooltip_mutex写入
    std::wstring m_tooltip_info_ui;         //UI线程读取tooltip时锁内拷入的副本，避免接口要求返回的指针指向正被重写的缓冲
    mutable std::mutex m_tooltip_mutex;     //保护m_tooltip_info与m_tooltip_info_ui
    DWORD m_last_refresh_tick{ 0 };     //上次自动刷新网络连接的时间
};

#ifdef __cplusplus
extern "C" {
#endif
    __declspec(dllexport) ITMPlugin* TMPluginGetInstance();

#ifdef __cplusplus
}
#endif
