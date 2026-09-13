#pragma once
#include "PluginInterface.h"
#include <string>
#include "CommonData.h"

namespace HardwareMonitor
{
    class CHardwareMonitorItem : public IPluginItem
    {
    public:
        CHardwareMonitorItem(const std::wstring& _identifier, const std::wstring& _item_name, const std::wstring& _label_text);

        // 通过 IPluginItem 继承
        const wchar_t* GetItemName() const override;
        const wchar_t* GetItemId() const override;
        const wchar_t* GetItemLableText() const override;
        const wchar_t* GetItemValueText() const override;
        const wchar_t* GetItemValueSampleText() const override;
        virtual int IsDrawResourceUsageGraph() const override;
        virtual float GetResourceUsageGraphValue() const override;

        void UpdateValue();
        const std::wstring& GetIdentifier() const;

    private:
        std::wstring identifier;
        std::wstring item_name;
        std::wstring item_value_taskbar;
        std::wstring item_value_main_wnd;
        std::wstring label_text;
        int sensor_type{};
        float item_value_num{}; //项目的原始数值
        float item_value_with_unit{};           //根据单位转换过的数值
        float last_item_value_with_unit{};      //上次根据单位转换过的数值
        //GetItemId/GetItemValueSampleText为const成员，缓存值保存在mutable成员中，
        //避免使用函数级static导致所有实例共享同一个缓冲区
        mutable std::wstring m_item_id_cache;
        mutable std::wstring m_sample_text_cache;
    };
}
