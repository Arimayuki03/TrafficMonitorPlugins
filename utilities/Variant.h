#pragma once
#include <string>

namespace utilities
{
	class CVariant
	{
	public:
		CVariant(int value);
		CVariant(size_t value);
		CVariant(double value);
		CVariant(const wchar_t* value);
		CVariant(const std::wstring& value);

		~CVariant();

		std::wstring ToString() const;

	private:
		enum class eType { INT, UINT, UINT64, DOUBLE, STRING };

		int m_value_int{};
		unsigned __int64 m_value_uint64{};
		double m_value_double{};
		std::wstring m_value_string;
		eType m_type{};

	};
}

