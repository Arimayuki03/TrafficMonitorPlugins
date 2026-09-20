#pragma once
#include <map>
#include <vector>

//格式化当前时间

//格式符列表：函数内static，首次调用时构造一次，避免在每个编译单元中生成一份副本
const std::vector<CString>& FormularList();


class CDataTimeFormatHelper
{
public:
    CDataTimeFormatHelper();
    ~CDataTimeFormatHelper();

    static CString GetDateTimeByFormat(const CString& formular, SYSTEMTIME sys_time);

private:
    static CString GetFormualrValue(CString formular_type, SYSTEMTIME sys_time);
    static CString GetWeekString(WORD day_of_week);
    static CString GetWeekStringShort(WORD day_of_week);
    static CString GetMonthString(DWORD month);
    static CString GetMonthStringShort(DWORD month);

};

