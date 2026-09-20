#include "pch.h"
#include "ChapterParser.h"
#include "DataManager.h"


CChapterParser::CChapterParser(const std::wstring& contents)
    : m_contents{ contents }
{
}


CChapterParser::~CChapterParser()
{
}

void CChapterParser::Parse()
{
    m_chapter = ParseChapterData(m_contents);
}

std::map<int, std::wstring> CChapterParser::ParseChapterData(const std::wstring& contents)
{
    std::map<int, std::wstring> chapter;
    size_t index = -1;
    while (true)
    {
        index = contents.find(L"第", index + 1);
        if (index == std::wstring::npos)
            break;
        size_t index1 = contents.find(L"章", index + 2);
        if (index1 == std::wstring::npos)
            break;
        if (index1 - index < 10)
        {
            size_t index2 = contents.find_first_of(L"\r\n", index1 + 1);
            if (index2 - index1 < 60)
            {
                std::wstring title = contents.substr(index, index2 - index);
                chapter[static_cast<int>(index)] = title;
                //从换行符处继续查找。若从index2+1开始，LF-only文件中紧邻换行符的下一章标题会被跳过(每隔一章漏一章)
                index = index2;
            }
        }

    }

    if (chapter.empty())
    {
        index = -1;
        while (true)
        {
            index = contents.find(L"Chapter", index + 1);
            if (index == std::wstring::npos)
                break;
            size_t index2 = contents.find_first_of(L"\r\n", index + 1);
            if (index2 - index < 60)
            {
                std::wstring title = contents.substr(index, index2 - index);
                chapter[static_cast<int>(index)] = title;
                index = index2;
            }
        }

    }
    return chapter;
}

void CChapterParser::SetChapterData(std::map<int, std::wstring>&& data)
{
    m_chapter = std::move(data);
}

const std::map<int, std::wstring>& CChapterParser::GetChapterData() const
{
    return m_chapter;
}

std::wstring CChapterParser::GetChapterTitle(int chapter_index) const
{
    if (chapter_index >= 0)
    {
        auto iter = m_chapter.find(chapter_index);
        if (iter != m_chapter.end())
            return iter->second;
    }
    return std::wstring();
}

std::wstring CChapterParser::GetCurrentChapterTitle() const
{
    int chapter_index = GetChapterIndexByPos(g_data.m_setting_data.current_position);
    return GetChapterTitle(chapter_index);
}

int CChapterParser::GetChapterIndexByPos(int pos) const
{
    if (m_chapter.empty())
        return -1;
    //用upper_bound找到最后一个起点不大于pos的章节。原来的">="写法在pos恰为章节起点时会返回上一章
    auto iter = m_chapter.upper_bound(pos);
    if (iter == m_chapter.begin())
        return iter->first;     //pos在第一章起点之前，返回第一章
    --iter;
    return iter->first;
}

std::wstring CChapterParser::GetChapterByPos(int pos) const
{
    int chapter_index = GetChapterIndexByPos(pos);
    return GetChapterTitle(chapter_index);
}
