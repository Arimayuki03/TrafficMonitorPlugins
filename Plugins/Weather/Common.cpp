#include "pch.h"
#include "Common.h"
#include <afxinet.h>    //用于支持使用网络相关的类
#include <algorithm>

std::wstring CCommon::StrToUnicode(const char* str, bool utf8)
{
    if (str == nullptr)
        return std::wstring();
    std::wstring result;
    int size;
    size = MultiByteToWideChar((utf8 ? CP_UTF8 : CP_ACP), 0, str, -1, NULL, 0);
    if (size <= 0) return std::wstring();
    wchar_t* str_unicode = new wchar_t[size + 1];
    MultiByteToWideChar((utf8 ? CP_UTF8 : CP_ACP), 0, str, -1, str_unicode, size);
    result.assign(str_unicode);
    delete[] str_unicode;
    return result;
}

std::string CCommon::UnicodeToStr(const wchar_t* wstr, bool utf8)
{
    if (wstr == nullptr)
        return std::string();
    std::string result;
    int size{ 0 };
    size = WideCharToMultiByte((utf8 ? CP_UTF8 : CP_ACP), 0, wstr, -1, NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    char* str = new char[size + 1];
    WideCharToMultiByte((utf8 ? CP_UTF8 : CP_ACP), 0, wstr, -1, str, size, NULL, NULL);
    result.assign(str);
    delete[] str;
    return result;
}

bool CCommon::GetURL(const std::wstring& url, std::string& result, const std::wstring& user_agent, bool force_reload)
{
    bool succeed{ false };
    CInternetSession* pSession{};
    CHttpFile* pfile{};
    try
    {
        pSession = new CInternetSession(user_agent.c_str());
        DWORD dwFlags = INTERNET_FLAG_TRANSFER_ASCII;
        if (force_reload)
        {
            dwFlags |= INTERNET_FLAG_RELOAD;
            dwFlags |= INTERNET_FLAG_DONT_CACHE;
        }
        pfile = (CHttpFile*)pSession->OpenURL(url.c_str(), 1, dwFlags);
        DWORD dwStatusCode;
        pfile->QueryInfoStatusCode(dwStatusCode);
        if (dwStatusCode == HTTP_STATUS_OK)
        {
            //按原始字节读取响应体。不能把Unicode构建的CString按char*重解释（见修复文档FIX-003）
            result.clear();
            char buff[4096];
            UINT read_count = 0;
            while ((read_count = pfile->Read(buff, sizeof(buff))) > 0)
            {
                result.append(buff, read_count);
            }
            //与原按行读取的行为保持一致：去掉换行符
            result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
            result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
            succeed = true;
        }
        pfile->Close();
        delete pfile;
        pfile = nullptr;
        pSession->Close();
    }
    catch (CInternetException* e)
    {
        succeed = false;
        //Close()自身可能抛出CInternetException，异常逃出catch会直接终止宿主进程，必须再捕获一层
        try
        {
            if (pfile != nullptr)
                pfile->Close();
            if (pSession != nullptr)
                pSession->Close();
        }
        catch (CInternetException* e2)
        {
            e2->Delete();
        }
        delete pfile;       //析构函数会释放句柄，不会抛出
        pfile = nullptr;
        e->Delete();        //没有这句会造成内存泄露
        SAFE_DELETE(pSession);
    }
    SAFE_DELETE(pSession);
    return succeed;
}

std::wstring CCommon::URLEncode(const std::wstring& wstr)
{
    std::string str_utf8;
    std::wstring result{};
    wchar_t buff[4];
    str_utf8 = CCommon::UnicodeToStr(wstr.c_str(), true);
    for (const auto& ch : str_utf8)
    {
        if (ch == ' ')
            result.push_back(L'+');
        else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            result.push_back(static_cast<wchar_t>(ch));
        else if (ch == '-' || ch == '_' || ch == '.' || ch == '!' || ch == '~' || ch == '*'/* || ch == '\''*/ || ch == '(' || ch == ')')
            result.push_back(static_cast<wchar_t>(ch));
        else
        {
            swprintf_s(buff, L"%%%x", static_cast<unsigned char>(ch));
            result += buff;
        }
    }
    return result;
}

CTime CCommon::GetDateOnly(CTime time)
{
    // 将时间设置为 00:00:00，只保留日期部分
    return CTime(
        time.GetYear(),
        time.GetMonth(),
        time.GetDay(),
        0, 0, 0, 0 // 将小时、分钟、秒、毫秒设置为 0
    );
}

int CCommon::CaculateWeekDay(int y, int m, int d)
{
    if (m <= 2)
    {
        m += 12;
        y--;
    }
    return (d + 2 * m + 3 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400 + 1) % 7;
}

CString CCommon::GetTextResource(UINT id, int code_type)
{
    CString res_str;
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(id), _T("TEXT"));
    auto err = GetLastError();
    if (hRes != NULL)
    {
        DWORD resSize = SizeofResource(hModule, hRes);  // 获取资源的大小
        HGLOBAL hglobal = LoadResource(hModule, hRes);
        if (hglobal != NULL)
        {
            LPVOID pResourceData = LockResource(hglobal);  // 获取资源数据的指针
            if (code_type == 2)
            {
                // 资源是宽字符字符串
                res_str = CString((const wchar_t*)pResourceData, resSize / sizeof(wchar_t));
            }
            else
            {
                // 资源是窄字符字符串
                std::string strData((const char*)pResourceData, resSize);
                res_str = CCommon::StrToUnicode(strData.c_str(), (code_type != 0)).c_str();
            }
        }
    }
    return res_str;
}
