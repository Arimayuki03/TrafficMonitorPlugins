#include "stdafx.h"
#include "HardwareMonitorItem.h"
#include "HardwareMonitor.h"
#include "HardwareMonitorHelper.h"
#include "Common.h"

using namespace LibreHardwareMonitor::Hardware;

namespace HardwareMonitor
{
    CHardwareMonitorItem::CHardwareMonitorItem(const std::wstring& _identifier, const std::wstring& _item_name, const std::wstring& _label_text)
        : identifier(_identifier), item_name(_item_name), label_text(_label_text)
    {
    }

    const wchar_t* CHardwareMonitorItem::GetItemName() const
    {
        return item_name.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemId() const
    {
        m_item_id_cache = L"7KrUeFVl_" + identifier;
        return m_item_id_cache.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemLableText() const
    {
        return label_text.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemValueText() const
    {
        //item_value_*由监控线程UpdateValue每秒重写，UI线程读侧在锁内拷贝副本，
        //返回副本缓冲的指针满足宿主"返回指针须持续有效"的契约
        std::lock_guard<std::mutex> lock(m_value_mutex);
        if (CHardwareMonitor::GetInstance()->m_tm_settings.is_taskbar)
            m_value_text_cache = item_value_taskbar;
        else
            m_value_text_cache = item_value_main_wnd;
        return m_value_text_cache.c_str();
    }

    const wchar_t* CHardwareMonitorItem::GetItemValueSampleText() const
    {
        std::wstring& sample_text{ m_sample_text_cache };
        //找不到对应配置时使用局部默认值(不用函数级static兜底对象，避免共享写污染)
        const ItemInfo default_item_info{};
        const ItemInfo* p_item_info{ CHardwareMonitor::GetInstance()->m_settings.FindItemInfo(identifier) };
        const ItemInfo& item_info{ p_item_info != nullptr ? *p_item_info : default_item_info };
        if (item_info.specify_value_width)
        {
            sample_text = std::wstring(item_info.value_width, L'0');
            if (item_info.decimal_places > 0)
            {
                sample_text += L'.';
                sample_text += std::wstring(item_info.decimal_places, L'0');
            }
            if (item_info.show_unit)
            {
                if (CHardwareMonitor::GetInstance()->m_tm_settings.sperate_with_space())
                    sample_text += L' ';
                SensorType type = static_cast<SensorType>(sensor_type);
                std::wstring unit;
                if (!item_info.unit.empty())
                    unit = item_info.unit;
                else
                    unit = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorTypeDefaultUnit(type));
                sample_text += unit;
            }
        }
        else
        {
            std::wstring item_value(GetItemValueText());
            if (item_value.empty())
                sample_text = L"0.00";
            else
                sample_text = item_value;
        }
        return sample_text.c_str();
    }

    int CHardwareMonitorItem::IsDrawResourceUsageGraph() const
    {
        SensorType type = static_cast<SensorType>(sensor_type);
        return type == SensorType::Temperature || type == SensorType::Load;
    }

    float CHardwareMonitorItem::GetResourceUsageGraphValue() const
    {
        SensorType type = static_cast<SensorType>(sensor_type);
        if (type == SensorType::Temperature || type == SensorType::Load)
        {
            //温度/负载钳制到0~200再映射到0.0~1.0，防止异常值(负温度或坏数据)超出图表范围
            //item_value_num由监控线程写，锁内拷出副本再使用
            std::lock_guard<std::mutex> lock(m_value_mutex);
            float value = item_value_num;
            if (value < 0.0f)
                value = 0.0f;
            else if (value > 200.0f)
                value = 200.0f;
            return value / 100.0f;
        }
        return 0.0f;
    }

    void CHardwareMonitorItem::UpdateValue()
    {
        ISensor^ sensor = HardwareMonitorHelper::FindSensorByIdentifyer(gcnew String(identifier.c_str()));
        if (sensor != nullptr)
        {
            //按值拷贝配置项。不能持有指向items_info元素的引用跨这段代码使用：
            //UI线程修改items_info(push_back重分配/erase/swap)会使引用悬空
            const ItemInfo item_info{ CHardwareMonitor::GetInstance()->GetItemInfoCopy(identifier) };
            String^ unit;
            if (!item_info.unit.empty())
                unit = gcnew String(item_info.unit.c_str());
            else
                unit = HardwareMonitorHelper::GetSensorTypeDefaultUnit(sensor->SensorType);
            const auto& cfg{ CHardwareMonitor::GetInstance()->m_tm_settings };
            {
                //监控线程写共享显示值，与UI线程读侧(GetItemValueText/GetResourceUsageGraphValue)互斥
                std::lock_guard<std::mutex> lock(m_value_mutex);
                item_value_main_wnd = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorValueText(sensor, unit, item_info.decimal_places, item_info.show_unit, cfg.main_wnd_sperate_with_space));
                item_value_taskbar = Common::StringToStdWstring(HardwareMonitorHelper::GetSensorValueText(sensor, unit, item_info.decimal_places, item_info.show_unit, cfg.taskbar_sperate_with_space));
                try
                {
                    item_value_num = sensor->Value.Value;
                }
                catch (System::Exception^)
                {
                    item_value_num = 0;
                }
            }
            sensor_type = static_cast<int>(sensor->SensorType);

            //判断是否要显示通知
            item_value_with_unit = HardwareMonitorHelper::GetSensorValue(sensor, unit);
            if (item_info.show_notify)
            {
                if (last_item_value_with_unit < item_info.notify_value && item_value_with_unit >= item_info.notify_value)
                {
                    //老版本宿主(API<=6)不会调用OnInitialize，GetMainApp()可能为空，此时跳过通知
                    ITrafficMonitor* pMainApp = CHardwareMonitor::GetInstance()->GetMainApp();
                    if (pMainApp != nullptr)
                    {
                        String^ format_str = MonitorGlobal::Instance()->GetString(L"ValueExceedNotifyMsg");
                        String^ value_with_unit = HardwareMonitorHelper::GetSensorValueText(sensor, unit);
                        String^ str_msg = String::Format(format_str, gcnew String(item_name.c_str()), value_with_unit);
                        pMainApp->ShowNotifyMessage(Common::StringToStdWstring(str_msg).c_str());
                    }
                    last_item_value_with_unit = item_value_with_unit;
                }
            }
            last_item_value_with_unit = item_value_with_unit;
        }
        else
        {
            //传感器不存在(硬件被禁用或已拔出)时清空显示值，避免数值停留在旧值上
            //(与配置加载时传感器缺失的条目表现一致，硬件恢复后下次更新会重新拉取数值)
            std::lock_guard<std::mutex> lock(m_value_mutex);
            item_value_main_wnd.clear();
            item_value_taskbar.clear();
            item_value_num = 0;
        }
    }

    const std::wstring& CHardwareMonitorItem::GetIdentifier() const
    {
        return identifier;
    }
}