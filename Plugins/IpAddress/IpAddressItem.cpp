#include "pch.h"
#include "IpAddressItem.h"
#include "DataManager.h"

const wchar_t* CIpAddressItem::GetItemName() const
{
    return g_data.StringRes(IDS_PLUGIN_ITEM_NAME);
}

const wchar_t* CIpAddressItem::GetItemId() const
{
    return L"Fd0b18cq";
}

const wchar_t* CIpAddressItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CIpAddressItem::GetItemValueText() const
{
    //锁内拷出IPv4快照副本，不在绘制路径重算或写共享数据；无匹配连接时与tooltip一致显示N/A
    std::wstring ip = g_data.GetCurrentIPv4Address();
    m_value_text_ui = ip.empty() ? L"N/A" : ip;
    return m_value_text_ui.c_str();
}

const wchar_t* CIpAddressItem::GetItemValueSampleText() const
{
    //样本文本决定显示区宽度，固定为比最长IPv4更长的占位文本，避免DHCP分配更长IP后显示被截断
    return L"000.000.000.000";
}
