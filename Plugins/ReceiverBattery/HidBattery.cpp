#include "HidBattery.h"

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace
{
    const GUID kHidInterfaceGuid = {
        0x4D1E55B2, 0xF16F, 0x11CF,
        { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 }
    };

    constexpr unsigned short kAulaVid = 0x3554;
    constexpr unsigned short kAulaPid = 0xFA09;
    constexpr unsigned short kAtkVid = 0x373B;
    constexpr unsigned short kLogitechVid = 0x046D;
    constexpr BYTE kHidppSwId = 0x01;

    std::wstring GetHidString(HANDLE handle, BOOLEAN(WINAPI* getter)(HANDLE, PVOID, ULONG))
    {
        wchar_t buffer[512]{};
        if (getter != nullptr && getter(handle, buffer, static_cast<ULONG>(std::size(buffer))))
            return buffer;
        return {};
    }

    HANDLE OpenHid(const std::wstring& path, DWORD access, DWORD flags)
    {
        if (path.empty())
            return INVALID_HANDLE_VALUE;
        return CreateFileW(path.c_str(), access,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, flags, nullptr);
    }

    bool WriteOutputReport(HANDLE handle, const BYTE* data, DWORD length)
    {
        // HidD_SetOutputReport 在部分接收器上可用，WriteFile 在另一些上更可靠。
        if (HidD_SetOutputReport(handle, const_cast<PVOID>(static_cast<const void*>(data)), length))
            return true;

        DWORD written = 0;
        return WriteFile(handle, data, length, &written, nullptr) != FALSE;
    }

    bool ReadInputReport(HANDLE handle, BYTE* buffer, DWORD length, DWORD timeout_ms, DWORD& bytes_read)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr)
            return false;

        bytes_read = 0;
        BOOL ok = ReadFile(handle, buffer, length, nullptr, &overlapped);
        if (!ok)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING)
            {
                CloseHandle(overlapped.hEvent);
                return false;
            }

            const DWORD wait_result = WaitForSingleObject(overlapped.hEvent, timeout_ms);
            if (wait_result != WAIT_OBJECT_0)
            {
                // 超时后必须等 CancelIoEx 真正完成并回收 IRP，否则同一个
                // 读句柄上的下一次 ReadFile 可能永远收不到数据。罗技接收器
                // 在设备休眠时经常先超时一次，这里不处理会导致后续所有
                // 设备索引都读取失败。
                CancelIoEx(handle, &overlapped);
                if (WaitForSingleObject(overlapped.hEvent, 1000) == WAIT_OBJECT_0)
                {
                    DWORD canceled = 0;
                    GetOverlappedResult(handle, &overlapped, &canceled, FALSE);
                }
                CloseHandle(overlapped.hEvent);
                return false;
            }
        }

        ok = GetOverlappedResult(handle, &overlapped, &bytes_read, FALSE);
        CloseHandle(overlapped.hEvent);
        return ok != FALSE;
    }

    bool ReadAulaOnce(HANDLE main_handle, HANDLE read_handle, DWORD input_len,
        int& percent, unsigned char& flag, std::wstring& error)
    {
        BYTE packet[20]{};
        packet[0] = 0x13;   // report id
        packet[1] = 0x4A;   // GET_BATTERY
        packet[2] = 1;      // total packets
        packet[3] = 0;      // packet index
        packet[4] = 0;      // read, zero data bytes
        BYTE checksum = 0;
        for (int i = 0; i < 19; ++i)
            checksum = static_cast<BYTE>(checksum + packet[i]);
        packet[19] = checksum;

        if (!WriteOutputReport(main_handle, packet, sizeof(packet)))
        {
            error = L"无法向接收器发送电量查询";
            return false;
        }

        BYTE buffer[64]{};
        const DWORD read_len = input_len != 0 ? input_len : 20;
        if (read_len > sizeof(buffer))
        {
            error = L"接收器报告长度异常";
            return false;
        }

        const ULONGLONG deadline = GetTickCount64() + 700;
        while (GetTickCount64() < deadline)
        {
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
            if (remaining == 0)
                break;

            DWORD got = 0;
            if (!ReadInputReport(read_handle, buffer, read_len, remaining, got))
                break;
            if (got < 20)
                continue;
            if (buffer[0] != 0x13)
                continue;
            if ((buffer[1] & 0x7F) != 0x4A)
                continue;

            BYTE sum = 0;
            for (int i = 0; i < 19; ++i)
                sum = static_cast<BYTE>(sum + buffer[i]);
            if (buffer[19] != sum)
                continue;
            if ((buffer[1] & 0x80) != 0)
            {
                error = L"设备拒绝读取电量";
                return false;
            }

            const int data_len = buffer[4] & 0x0F;
            if (data_len < 2)
                continue;

            const int value = buffer[5];
            if (value < 0 || value > 100)
                continue;

            percent = value;
            flag = buffer[6];
            error.clear();
            return true;
        }

        if (error.empty())
            error = L"设备无响应（键盘可能已休眠）";
        return false;
    }

    BatteryReading QueryAula(const HidDeviceInfo& device)
    {
        BatteryReading result;
        result.device_name = device.DisplayName();
        result.source = L"AULA / Compx 2.4G";

        HANDLE main_handle = OpenHid(device.path, GENERIC_READ | GENERIC_WRITE, 0);
        if (main_handle == INVALID_HANDLE_VALUE)
        {
            result.error = L"无法打开接收器（可能被其他程序占用）";
            return result;
        }

        HANDLE read_handle = OpenHid(device.path, GENERIC_READ, FILE_FLAG_OVERLAPPED);
        if (read_handle == INVALID_HANDLE_VALUE)
        {
            CloseHandle(main_handle);
            result.error = L"无法建立接收器读取通道";
            return result;
        }

        // 丢弃上一次查询残留的输入报告，避免把旧数据当成本次结果。
        BYTE drain[64]{};
        DWORD drained = 0;
        const DWORD read_len = device.input_len != 0 ? device.input_len : 20;
        for (int i = 0; i < 8; ++i)
        {
            if (!ReadInputReport(read_handle, drain,
                read_len <= sizeof(drain) ? read_len : static_cast<DWORD>(sizeof(drain)), 2, drained))
                break;
        }

        int percent = -1;
        unsigned char flag = 0;
        std::wstring error;
        if (ReadAulaOnce(main_handle, read_handle, device.input_len, percent, flag, error))
        {
            result.ok = true;
            result.percent = percent;
            result.flag = flag;
            result.charging = (flag & 0x10) != 0;
            result.wired = result.charging;
        }
        else
        {
            result.error = error;
        }

        CloseHandle(read_handle);
        CloseHandle(main_handle);
        return result;
    }

    BatteryReading QueryAtkVxe(const HidDeviceInfo& device)
    {
        BatteryReading result;
        result.device_name = device.DisplayName();
        result.source = L"ATK / VXE / VGN 2.4G";

        HANDLE main_handle = OpenHid(device.path, GENERIC_READ | GENERIC_WRITE, 0);
        if (main_handle == INVALID_HANDLE_VALUE)
        {
            result.error = L"无法打开接收器（可能被其他程序占用）";
            return result;
        }

        HANDLE read_handle = OpenHid(device.path, GENERIC_READ, FILE_FLAG_OVERLAPPED);
        if (read_handle == INVALID_HANDLE_VALUE)
        {
            CloseHandle(main_handle);
            result.error = L"无法建立接收器读取通道";
            return result;
        }

        BYTE drain[128]{};
        DWORD drained = 0;
        for (int i = 0; i < 8; ++i)
        {
            const DWORD len = device.input_len != 0 ? device.input_len : 64;
            if (!ReadInputReport(read_handle, drain,
                len <= sizeof(drain) ? len : static_cast<DWORD>(sizeof(drain)), 2, drained))
                break;
        }

        bool success = false;
        int percent = -1;
        bool wired = false;

        // 协议 1：FF02/0002，17 字节，report id 0x08，命令 0x04。
        if (device.usage_page == 0xFF02 && device.output_len >= 17)
        {
            BYTE packet[17]{};
            packet[0] = 0x08;
            packet[1] = 0x04;
            packet[16] = 0x49;
            if (WriteOutputReport(main_handle, packet, sizeof(packet)))
            {
                BYTE buffer[128]{};
                const DWORD read_len = device.input_len != 0 ? device.input_len : 17;
                const ULONGLONG deadline = GetTickCount64() + 700;
                while (GetTickCount64() < deadline)
                {
                    const ULONGLONG now = GetTickCount64();
                    const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
                    if (remaining == 0)
                        break;
                    DWORD got = 0;
                    if (!ReadInputReport(read_handle, buffer,
                        read_len <= sizeof(buffer) ? read_len : static_cast<DWORD>(sizeof(buffer)),
                        remaining, got))
                        break;
                    if (got < 8)
                        continue;
                    if (buffer[0] != 0x08 || buffer[1] != 0x04)
                        continue;
                    if (buffer[6] > 100)
                        continue;
                    percent = buffer[6];
                    wired = buffer[7] != 0;
                    success = true;
                    break;
                }
            }
        }

        // 协议 2：FF05/0001，64 字节，report id 0x08，命令 0x72。
        if (!success && device.usage_page == 0xFF05 && device.output_len >= 64)
        {
            BYTE packet[64]{};
            packet[0] = 0x08;
            packet[1] = 0x7D;
            packet[2] = 0x72;
            packet[3] = 0x02;
            packet[5] = 0x01;
            packet[6] = 0x07;
            packet[7] = 0x01;
            if (WriteOutputReport(main_handle, packet, sizeof(packet)))
            {
                BYTE buffer[128]{};
                const DWORD read_len = device.input_len != 0 ? device.input_len : 64;
                const ULONGLONG deadline = GetTickCount64() + 700;
                while (GetTickCount64() < deadline)
                {
                    const ULONGLONG now = GetTickCount64();
                    const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
                    if (remaining == 0)
                        break;
                    DWORD got = 0;
                    if (!ReadInputReport(read_handle, buffer,
                        read_len <= sizeof(buffer) ? read_len : static_cast<DWORD>(sizeof(buffer)),
                        remaining, got))
                        break;
                    if (got < 8)
                        continue;
                    if (buffer[1] != 0x72 || buffer[5] != 0x07)
                        continue;
                    if (buffer[7] > 100)
                        continue;
                    percent = buffer[7];
                    wired = false;
                    success = true;
                    break;
                }
            }
        }

        if (success)
        {
            result.ok = true;
            result.percent = percent;
            result.wired = wired;
            result.charging = false;
        }
        else
        {
            result.error = L"设备无响应（鼠标可能已休眠）";
        }

        CloseHandle(read_handle);
        CloseHandle(main_handle);
        return result;
    }

    void DrainInputReports(HANDLE read_handle, DWORD input_len)
    {
        BYTE buffer[64]{};
        DWORD got = 0;
        for (int i = 0; i < 4; ++i)
        {
            const DWORD length = input_len <= sizeof(buffer)
                ? input_len : static_cast<DWORD>(sizeof(buffer));
            if (!ReadInputReport(read_handle, buffer, length, 1, got))
                break;
        }
    }

    int BatteryPercentFromVoltage(int millivolt)
    {
        // 罗技锂电池放电曲线（与 Solaar 的电压估算一致）。
        static const struct VoltagePoint
        {
            int millivolt;
            int percent;
        } curve[] = {
            { 4186, 100 }, { 4067, 90 }, { 3989, 80 }, { 3922, 70 },
            { 3859, 60 }, { 3811, 50 }, { 3778, 40 }, { 3751, 30 },
            { 3717, 20 }, { 3671, 10 }, { 3646, 5 }, { 3579, 2 },
            { 3500, 0 }
        };
        constexpr int count = static_cast<int>(std::size(curve));
        if (millivolt >= curve[0].millivolt)
            return 100;
        if (millivolt <= curve[count - 1].millivolt)
            return 0;
        for (int i = 0; i < count - 1; ++i)
        {
            const int high_mv = curve[i].millivolt;
            const int low_mv = curve[i + 1].millivolt;
            if (millivolt <= high_mv && millivolt >= low_mv)
            {
                const int high_percent = curve[i].percent;
                const int low_percent = curve[i + 1].percent;
                const double value = low_percent +
                    (high_percent - low_percent) *
                    (static_cast<double>(millivolt - low_mv) / (high_mv - low_mv));
                return static_cast<int>(value + 0.5);
            }
        }
        return 0;
    }

    bool HidppCall(HANDLE main_handle, HANDLE read_handle, DWORD input_len, BYTE report_id,
        BYTE device_index, BYTE feature_index, BYTE function,
        const BYTE* params, size_t param_count, std::vector<BYTE>& reply, DWORD timeout_ms = 140)
    {
        const bool long_message = report_id == 0x11;
        const DWORD packet_len = long_message ? 20 : 7;
        if (input_len < 4 || packet_len > 64)
            return false;

        BYTE packet[20]{};
        packet[0] = report_id;
        packet[1] = device_index;
        packet[2] = feature_index;
        packet[3] = static_cast<BYTE>(((function & 0x0F) << 4) | kHidppSwId);
        for (size_t i = 0; i < param_count && 4 + i < packet_len; ++i)
            packet[4 + i] = params[i];

        // 清掉上一次请求残留的迟到回复，避免把旧回复当成本次回复。
        DrainInputReports(read_handle, input_len);
        if (!WriteOutputReport(main_handle, packet, packet_len))
            return false;

        const ULONGLONG deadline = GetTickCount64() + timeout_ms;
        BYTE buffer[64]{};
        const DWORD read_len = input_len <= sizeof(buffer) ? input_len : static_cast<DWORD>(sizeof(buffer));
        while (GetTickCount64() < deadline)
        {
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
            if (remaining == 0)
                break;

            DWORD got = 0;
            if (!ReadInputReport(read_handle, buffer, read_len, remaining, got))
                break;
            if (got < 4)
                continue;

            // 错误回复：feature 字节为 0xFF。
            if (buffer[1] == device_index && buffer[2] == 0xFF)
                return false;

            if (buffer[0] != report_id || buffer[1] != device_index ||
                buffer[2] != feature_index)
                continue;
            if (((buffer[3] >> 4) & 0x0F) != function)
                continue;
            if ((buffer[3] & 0x80) != 0)
                return false;

            reply.assign(buffer, buffer + got);
            return true;
        }
        return false;
    }

    int HidppGetFeatureIndex(HANDLE main_handle, HANDLE read_handle, DWORD input_len,
        BYTE report_id, BYTE device_index, unsigned short feature_id)
    {
        const BYTE params[2] = {
            static_cast<BYTE>(feature_id >> 8),
            static_cast<BYTE>(feature_id & 0xFF)
        };
        std::vector<BYTE> reply;
        if (!HidppCall(main_handle, read_handle, input_len, report_id, device_index,
            0x00, 0x00, params, 2, reply))
            return 0;
        if (reply.size() < 5)
            return 0;

        int index = reply[4];
        if (index == 0 && reply.size() > 6)
            index = reply[6];
        if (index == 0 && reply.size() > 5)
            index = reply[5];
        return index;
    }

    std::wstring HidppGetDeviceName(HANDLE main_handle, HANDLE read_handle, DWORD input_len,
        BYTE report_id, BYTE device_index)
    {
        const int feature_index = HidppGetFeatureIndex(main_handle, read_handle, input_len,
            report_id, device_index, 0x0005);
        if (feature_index <= 0)
            return {};

        std::vector<BYTE> reply;
        if (!HidppCall(main_handle, read_handle, input_len, report_id, device_index,
            static_cast<BYTE>(feature_index), 0x00, nullptr, 0, reply))
            return {};
        if (reply.size() < 5)
            return {};
        const int name_length = reply[4];
        if (name_length <= 0 || name_length > 128)
            return {};

        std::string name;
        name.reserve(static_cast<size_t>(name_length));
        while (static_cast<int>(name.size()) < name_length)
        {
            const BYTE offset = static_cast<BYTE>(name.size());
            std::vector<BYTE> part;
            if (!HidppCall(main_handle, read_handle, input_len, report_id, device_index,
                static_cast<BYTE>(feature_index), 0x01, &offset, 1, part))
                break;
            if (part.size() <= 4)
                break;
            const size_t available = std::min<size_t>(part.size() - 4,
                static_cast<size_t>(name_length) - name.size());
            if (available == 0)
                break;
            name.append(reinterpret_cast<const char*>(&part[4]), available);
        }

        if (name.empty())
            return {};
        const int wide_length = MultiByteToWideChar(CP_UTF8, 0, name.data(),
            static_cast<int>(name.size()), nullptr, 0);
        if (wide_length <= 0)
            return {};
        std::wstring wide(static_cast<size_t>(wide_length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()),
            wide.data(), wide_length);
        return wide;
    }

    bool HidppReadFeatureBattery(HANDLE main_handle, HANDLE read_handle, DWORD input_len,
        BYTE report_id, BYTE device_index, unsigned short feature_id,
        int& percent, bool& charging, std::wstring& detail)
    {
        const int feature_index = HidppGetFeatureIndex(main_handle, read_handle, input_len,
            report_id, device_index, feature_id);
        if (feature_index <= 0)
            return false;

        // 0x1004 的统一电池功能：function 0 是 get_capabilities，
        // 返回的是支持的档位数（常见值为 15），不是电量；电量必须用 function 1
        // get_status 读取。0x1000 的 function 0 才是电池状态。
        const BYTE function = feature_id == 0x1004 ? 0x01 : 0x00;
        std::vector<BYTE> reply;
        if (!HidppCall(main_handle, read_handle, input_len, report_id, device_index,
            static_cast<BYTE>(feature_index), function, nullptr, 0, reply))
            return false;

        int level = -1;
        int status = -1;
        if (feature_id == 0x1004)
        {
            // 统一电池 get_status 的 payload：
            // [0] 电量百分比，[1] 粗略档位，[2] 充电状态，[3] 保留。
            if (reply.size() < 7)
                return false;
            level = reply[4];
            const int coarse_level = reply[5];
            status = reply[6];
            if (level == 0 && coarse_level > 0)
            {
                switch (coarse_level)
                {
                case 8: level = 90; break;   // full
                case 4: level = 50; break;   // good
                case 2: level = 20; break;   // low
                case 1: level = 5; break;    // critical
                default: break;
                }
            }
        }
        else if (feature_id == 0x1000)
        {
            // 电池状态 payload：[0] 电量百分比，[1] 下一档位，[2] 充电状态。
            if (reply.size() < 7)
                return false;
            level = reply[4];
            status = reply[6];
        }
        else if (feature_id == 0x1001)
        {
            // 电池电压 payload：[0..1] 大端电压(mV)，[2] 充电标志。
            if (reply.size() < 7)
                return false;
            const int millivolt = (reply[4] << 8) | reply[5];
            if (millivolt < 2000 || millivolt > 5000)
                return false;
            const BYTE flags = reply[6];
            level = BatteryPercentFromVoltage(millivolt);
            charging = (flags & 0x80) != 0 || (flags & 0x10) != 0;
            if (charging && (flags & 0x03) == 0x01)
            {
                // ChargeStatus::FULL
                level = 100;
                charging = false;
            }
            wchar_t buffer[128]{};
            swprintf_s(buffer, L"设备索引 0x%02X，电压 %d mV%s",
                static_cast<unsigned>(device_index), millivolt,
                charging ? L"，充电中" : L"");
            detail = buffer;
            percent = level;
            return true;
        }
        else
        {
            return false;
        }

        if (level < 0 || level > 100)
            return false;

        if (level == 0 && status == 3)
            level = 100;

        static const wchar_t* status_text[] = {
            L"放电中", L"充电中", L"充电中（末段）", L"已充满",
            L"充电中（慢速）", L"电池状态无效", L"温度异常", L"充电异常"
        };
        const wchar_t* text = (status >= 0 && status <= 7) ? status_text[status] : L"状态未知";

        wchar_t buffer[128]{};
        swprintf_s(buffer, L"设备索引 0x%02X，状态：%s", static_cast<unsigned>(device_index), text);
        detail = buffer;
        percent = level;
        charging = status == 1 || status == 2 || status == 4;
        return true;
    }

    bool HidppRegisterRead(HANDLE main_handle, HANDLE read_handle, DWORD input_len,
        BYTE device_index, BYTE reg, BYTE* params)
    {
        if (input_len < 7)
            return false;

        BYTE packet[7]{};
        packet[0] = 0x10;
        packet[1] = device_index;
        packet[2] = 0x81;
        packet[3] = reg;
        if (!WriteOutputReport(main_handle, packet, sizeof(packet)))
            return false;

        const ULONGLONG deadline = GetTickCount64() + 160;
        BYTE buffer[64]{};
        while (GetTickCount64() < deadline)
        {
            const ULONGLONG now = GetTickCount64();
            const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
            if (remaining == 0)
                break;
            DWORD got = 0;
            if (!ReadInputReport(read_handle, buffer,
                input_len <= sizeof(buffer) ? input_len : static_cast<DWORD>(sizeof(buffer)),
                remaining, got))
                break;
            if (got < 7)
                continue;
            if (buffer[0] != 0x10 || buffer[1] != device_index ||
                buffer[2] != 0x81 || buffer[3] != reg)
                continue;
            params[0] = buffer[4];
            params[1] = buffer[5];
            params[2] = buffer[6];
            return true;
        }
        return false;
    }

    bool HidppReadRegisterBattery(HANDLE main_handle, HANDLE read_handle, DWORD input_len,
        BYTE device_index, int& percent, bool& charging, std::wstring& detail)
    {
        BYTE params[3]{};
        if (HidppRegisterRead(main_handle, read_handle, input_len, device_index, 0x0D, params))
        {
            const int level = params[0];
            const BYTE status = static_cast<BYTE>(params[2] & 0xF0);
            if (level >= 0 && level <= 100)
            {
                percent = level;
                charging = status == 0x50;
                if (status == 0x90)
                    percent = 100;
                detail = L"HID++ 1.0 寄存器 0x0D";
                return true;
            }
        }

        if (HidppRegisterRead(main_handle, read_handle, input_len, device_index, 0x07, params))
        {
            static const int level_map[] = { 0, 10, 0, 30, 0, 80, 0, 100 };
            const int code = params[0] & 0x07;
            const BYTE charging_byte = params[1];
            percent = level_map[code];
            charging = (charging_byte & 0x21) == 0x21;
            if ((charging_byte & 0x22) == 0x22)
            {
                percent = 100;
                charging = false;
            }
            detail = L"HID++ 1.0 寄存器 0x07";
            return true;
        }
        return false;
    }

    BatteryReading QueryLogitechHidpp(const HidDeviceInfo& device)
    {
        BatteryReading result;
        result.device_name = device.DisplayName();
        result.source = L"罗技 HID++ 2.0";

        HANDLE main_handle = OpenHid(device.path, GENERIC_READ | GENERIC_WRITE, 0);
        if (main_handle == INVALID_HANDLE_VALUE)
        {
            result.error = L"无法打开罗技设备（可能被 G HUB 等程序占用）";
            return result;
        }

        HANDLE read_handle = OpenHid(device.path, GENERIC_READ, FILE_FLAG_OVERLAPPED);
        if (read_handle == INVALID_HANDLE_VALUE)
        {
            CloseHandle(main_handle);
            result.error = L"无法建立罗技设备读取通道";
            return result;
        }

        const bool long_message = device.output_len >= 20;
        const BYTE report_id = long_message ? 0x11 : 0x10;
        const DWORD input_len = device.input_len != 0 ? device.input_len : (long_message ? 20 : 7);

        BYTE drain[64]{};
        DWORD drained = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (!ReadInputReport(read_handle, drain,
                input_len <= sizeof(drain) ? input_len : static_cast<DWORD>(sizeof(drain)), 2, drained))
                break;
        }

        const BYTE indexes[] = { 0xFF, 1, 2, 3, 4, 5, 6 };
        int percent = -1;
        bool charging = false;
        std::wstring detail;
        BYTE selected_index = 0;

        for (BYTE index : indexes)
        {
            if (HidppReadFeatureBattery(main_handle, read_handle, input_len, report_id, index,
                0x1004, percent, charging, detail))
            {
                selected_index = index;
                result.ok = true;
                break;
            }
            if (HidppReadFeatureBattery(main_handle, read_handle, input_len, report_id, index,
                0x1000, percent, charging, detail))
            {
                selected_index = index;
                result.ok = true;
                break;
            }
            if (HidppReadFeatureBattery(main_handle, read_handle, input_len, report_id, index,
                0x1001, percent, charging, detail))
            {
                selected_index = index;
                result.ok = true;
                result.source = L"罗技 HID++ 2.0（电压）";
                break;
            }
        }

        if (!result.ok && !long_message)
        {
            for (BYTE index : indexes)
            {
                if (HidppReadRegisterBattery(main_handle, read_handle, input_len, index,
                    percent, charging, detail))
                {
                    selected_index = index;
                    result.ok = true;
                    result.source = L"罗技 HID++ 1.0";
                    break;
                }
            }
        }

        if (result.ok)
        {
            if (selected_index != 0)
            {
                const std::wstring device_name = HidppGetDeviceName(main_handle, read_handle,
                    input_len, report_id, selected_index);
                if (!device_name.empty())
                    result.device_name = device_name;
            }
            result.percent = percent;
            result.charging = charging;
            result.detail = detail;
        }
        else
        {
            result.error = L"罗技设备无响应（可能已休眠或已被其他程序占用）";
        }

        CloseHandle(read_handle);
        CloseHandle(main_handle);
        return result;
    }

    bool FindBatteryFeatureCaps(HIDP_VALUE_CAPS& battery_cap, PHIDP_PREPARSED_DATA preparsed, USHORT feature_caps_count)
    {
        if (preparsed == nullptr || feature_caps_count == 0)
            return false;

        std::vector<HIDP_VALUE_CAPS> caps(feature_caps_count + 8);
        USHORT count = static_cast<USHORT>(caps.size());
        if (HidP_GetValueCaps(HidP_Feature, caps.data(), &count, preparsed) != HIDP_STATUS_SUCCESS)
            return false;

        for (USHORT i = 0; i < count; ++i)
        {
            if (caps[i].IsRange)
                continue;
            const USAGE usage = caps[i].NotRange.Usage;
            if ((caps[i].UsagePage == 0x0006 && usage == 0x0020) ||
                (caps[i].UsagePage == 0x000D && usage == 0x003B))
            {
                battery_cap = caps[i];
                return true;
            }
        }
        return false;
    }

    BatteryReading QueryGenericHid(const HidDeviceInfo& device)
    {
        BatteryReading result;
        result.device_name = device.DisplayName();
        result.source = L"标准 HID 电量";

        HANDLE handle = OpenHid(device.path, GENERIC_READ, 0);
        if (handle == INVALID_HANDLE_VALUE)
            handle = OpenHid(device.path, 0, 0);
        if (handle == INVALID_HANDLE_VALUE)
        {
            result.error = L"无法打开 HID 设备";
            return result;
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (!HidD_GetPreparsedData(handle, &preparsed) ||
            HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS)
        {
            if (preparsed != nullptr)
                HidD_FreePreparsedData(preparsed);
            CloseHandle(handle);
            result.error = L"无法读取 HID 报告描述符";
            return result;
        }

        HIDP_VALUE_CAPS battery_cap{};
        const bool found = FindBatteryFeatureCaps(battery_cap, preparsed, caps.NumberFeatureValueCaps);
        if (!found || caps.FeatureReportByteLength == 0)
        {
            HidD_FreePreparsedData(preparsed);
            CloseHandle(handle);
            result.error = L"该设备没有 Feature 报告形式的电量字段";
            return result;
        }

        std::vector<BYTE> buffer(caps.FeatureReportByteLength);
        if (battery_cap.ReportID != 0)
            buffer[0] = static_cast<BYTE>(battery_cap.ReportID);
        if (!HidD_GetFeature(handle, buffer.data(), static_cast<ULONG>(buffer.size())))
        {
            HidD_FreePreparsedData(preparsed);
            CloseHandle(handle);
            result.error = L"读取 HID 电量 Feature 报告失败";
            return result;
        }

        ULONG raw = 0;
        const NTSTATUS status = HidP_GetUsageValue(HidP_Feature, battery_cap.UsagePage,
            battery_cap.LinkCollection, battery_cap.NotRange.Usage, &raw,
            preparsed, reinterpret_cast<PCHAR>(buffer.data()), static_cast<ULONG>(buffer.size()));

        HidD_FreePreparsedData(preparsed);
        CloseHandle(handle);
        if (status != HIDP_STATUS_SUCCESS)
        {
            result.error = L"无法解析 HID 电量字段";
            return result;
        }

        const LONG logical_min = battery_cap.LogicalMin;
        const LONG logical_max = battery_cap.LogicalMax;
        int percent = 0;
        if (logical_max > logical_min)
        {
            const double scaled = (static_cast<double>(raw) - logical_min) * 100.0 /
                (static_cast<double>(logical_max) - logical_min);
            percent = static_cast<int>(scaled + 0.5);
        }
        else
        {
            percent = static_cast<int>(raw);
        }

        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;

        result.ok = true;
        result.percent = percent;
        return result;
    }
}

std::wstring HidDeviceInfo::DisplayName() const
{
    if (kind == DeviceKind::Aula2G4)
        return L"AULA / Compx 2.4G 接收器";
    if (kind == DeviceKind::AtkVxe2G4)
        return L"ATK / VXE / VGN 2.4G 接收器";
    if (kind == DeviceKind::LogitechHidpp)
    {
        if (!product.empty() && product != L"USB Receiver")
            return L"罗技 " + product;
        if (path.find(L"bthledevice") != std::wstring::npos ||
            path.find(L"BTHLEDEVICE") != std::wstring::npos)
            return L"罗技蓝牙设备";
        return L"罗技 USB 接收器";
    }
    if (!product.empty())
        return product;
    if (!manufacturer.empty())
        return manufacturer + L" HID 设备";
    return L"HID 电量设备";
}

std::wstring HidDeviceInfo::IdString() const
{
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04X:%04X", vid, pid);
    return buffer;
}

std::vector<HidDeviceInfo> EnumerateHidDevices()
{
    std::vector<HidDeviceInfo> result;
    HDEVINFO device_info_set = SetupDiGetClassDevsW(
        &kHidInterfaceGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info_set == INVALID_HANDLE_VALUE)
        return result;

    for (DWORD index = 0;; ++index)
    {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(device_info_set, nullptr, &kHidInterfaceGuid,
            index, &interface_data))
            break;

        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(device_info_set, &interface_data, nullptr, 0, &needed, nullptr);
        if (needed == 0)
            continue;

        std::vector<BYTE> detail_buffer(needed);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detail_buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(device_info_set, &interface_data, detail,
            needed, nullptr, nullptr))
            continue;

        HidDeviceInfo info;
        info.path = detail->DevicePath;
        HANDLE handle = OpenHid(info.path, 0, 0);
        if (handle == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        HidD_GetAttributes(handle, &attributes);
        info.vid = attributes.VendorID;
        info.pid = attributes.ProductID;
        info.product = GetHidString(handle, HidD_GetProductString);
        info.manufacturer = GetHidString(handle, HidD_GetManufacturerString);

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(handle, &preparsed))
        {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
            {
                info.usage_page = caps.UsagePage;
                info.usage = caps.Usage;
                info.input_len = caps.InputReportByteLength;
                info.output_len = caps.OutputReportByteLength;
                info.feature_len = caps.FeatureReportByteLength;

                if (caps.NumberFeatureValueCaps > 0)
                {
                    std::vector<HIDP_VALUE_CAPS> value_caps(caps.NumberFeatureValueCaps + 8);
                    USHORT count = static_cast<USHORT>(value_caps.size());
                    if (HidP_GetValueCaps(HidP_Feature, value_caps.data(), &count, preparsed) == HIDP_STATUS_SUCCESS)
                    {
                        for (USHORT i = 0; i < count; ++i)
                        {
                            if (value_caps[i].IsRange)
                                continue;
                            const USAGE usage = value_caps[i].NotRange.Usage;
                            if ((value_caps[i].UsagePage == 0x0006 && usage == 0x0020) ||
                                (value_caps[i].UsagePage == 0x000D && usage == 0x003B))
                            {
                                info.has_hid_battery = true;
                                break;
                            }
                        }
                    }
                }
            }
            HidD_FreePreparsedData(preparsed);
        }
        CloseHandle(handle);

        info.is_aula = info.vid == kAulaVid && info.pid == kAulaPid &&
            info.usage_page == 0xFF02 && info.usage == 0x0002 && info.output_len >= 20;

        info.is_atk = !info.is_aula &&
            (info.vid == kAtkVid || info.vid == kAulaVid) &&
            (info.usage_page == 0xFF02 || info.usage_page == 0xFF05) &&
            info.output_len >= 17;

        info.is_logitech = info.vid == kLogitechVid && info.usage_page >= 0xFF00 &&
            (info.output_len == 7 || info.output_len == 20);

        if (info.is_logitech)
            info.kind = DeviceKind::LogitechHidpp;
        else if (info.is_aula)
            info.kind = DeviceKind::Aula2G4;
        else if (info.is_atk)
            info.kind = DeviceKind::AtkVxe2G4;
        else if (info.has_hid_battery)
            info.kind = DeviceKind::GenericHid;
        else
            continue;

        result.push_back(std::move(info));
    }

    SetupDiDestroyDeviceInfoList(device_info_set);

    std::sort(result.begin(), result.end(), [](const HidDeviceInfo& a, const HidDeviceInfo& b)
    {
        if (a.kind != b.kind)
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.DisplayName() < b.DisplayName();
    });
    return result;
}

BatteryReading ReadBatteryFromDevice(const HidDeviceInfo& device)
{
    if (device.kind == DeviceKind::Aula2G4)
        return QueryAula(device);
    if (device.kind == DeviceKind::AtkVxe2G4)
        return QueryAtkVxe(device);
    if (device.kind == DeviceKind::LogitechHidpp)
        return QueryLogitechHidpp(device);
    if (device.kind == DeviceKind::GenericHid)
        return QueryGenericHid(device);

    BatteryReading result;
    result.device_name = device.DisplayName();
    result.error = L"不支持的设备类型";
    return result;
}
