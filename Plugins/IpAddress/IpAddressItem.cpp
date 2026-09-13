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
    g_data.GetCurrentIPv4Address();
    return g_data.m_current_ipv4.c_str();
}

const wchar_t* CIpAddressItem::GetItemValueSampleText() const
{
    g_data.GetCurrentIPv4Address();
    if (!g_data.m_current_ipv4.empty())
        return g_data.m_current_ipv4.c_str();
    return L"000.000.000.000";
}
