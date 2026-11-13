#include "ReceiverBatteryItem.h"
#include "ReceiverBattery.h"

#include <string>

const wchar_t* CReceiverBatteryItem::GetItemName() const
{
    return L"外设电量";
}

const wchar_t* CReceiverBatteryItem::GetItemId() const
{
    return L"ReceiverBattery";
}

const wchar_t* CReceiverBatteryItem::GetItemLableText() const
{
    return L"外设: ";
}

const wchar_t* CReceiverBatteryItem::GetItemValueText() const
{
    thread_local std::wstring text;
    text = g_data.GetDisplayText();
    return text.c_str();
}

const wchar_t* CReceiverBatteryItem::GetItemValueSampleText() const
{
    const int mask = g_data.GetSettings().display_mask & 0x07;
    if (mask == 0)
        return L"2.4G 100%";

    thread_local std::wstring sample;
    sample.clear();
    auto append = [&](int bit, const wchar_t* text)
    {
        if ((mask & bit) == 0)
            return;
        if (!sample.empty())
            sample += L" | ";
        sample += text;
    };
    append(0x01, L"2.4G 100%");
    append(0x02, L"罗技 100%");
    append(0x04, L"手柄 有线");
    return sample.empty() ? L"2.4G 100%" : sample.c_str();
}
