#include "pch.h"
#include "AdapterCommon.h"


CAdapterCommon::CAdapterCommon()
{
}


CAdapterCommon::~CAdapterCommon()
{
}

void CAdapterCommon::GetAdapterInfo(std::vector<NetWorkConection>& adapters)
{
	adapters.clear();
	unsigned long stSize = sizeof(IP_ADAPTER_INFO);		//得到结构体大小,用于GetAdaptersInfo参数
	PIP_ADAPTER_INFO pIpAdapterInfo = (PIP_ADAPTER_INFO)new BYTE[stSize];		//PIP_ADAPTER_INFO结构体指针存储本机网卡信息
	int nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);	//调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量;其中stSize参数既是一个输入量也是一个输出量

	//如果返回ERROR_BUFFER_OVERFLOW说明缓冲区不够，stSize为需要的空间大小；最多重试3次，以应对两次调用之间网卡列表变化的情况
	int retry_count = 0;
	while (ERROR_BUFFER_OVERFLOW == nRel && retry_count < 3)
	{
		delete[] (BYTE*)pIpAdapterInfo;	//释放原来的内存空间
		pIpAdapterInfo = (PIP_ADAPTER_INFO)new BYTE[stSize];	//重新申请内存空间用来存储所有网卡信息
		nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);		//再次调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量
		retry_count++;
	}

	PIP_ADAPTER_INFO pIpAdapterInfoHead = pIpAdapterInfo;	//保存pIpAdapterInfo链表中第一个元素的地址
	if (ERROR_SUCCESS == nRel)
	{
		while (pIpAdapterInfo)
		{
			NetWorkConection connection;
			connection.description = CCommon::StrToUnicode(pIpAdapterInfo->Description);
			connection.ip_address = CCommon::StrToUnicode(pIpAdapterInfo->IpAddressList.IpAddress.String);
			connection.subnet_mask = CCommon::StrToUnicode(pIpAdapterInfo->IpAddressList.IpMask.String);
			connection.default_gateway = CCommon::StrToUnicode(pIpAdapterInfo->GatewayList.IpAddress.String);

			adapters.push_back(connection);
			pIpAdapterInfo = pIpAdapterInfo->Next;
		}
	}
	//释放内存空间
	if (pIpAdapterInfoHead)
	{
		delete[] (BYTE*)pIpAdapterInfoHead;
	}
	if (adapters.empty())
	{
		NetWorkConection connection{};
		connection.description = L"<no connection>";
		adapters.push_back(connection);
	}
}

void CAdapterCommon::RefreshIpAddress(std::vector<NetWorkConection>& adapters)
{
    std::vector<NetWorkConection> adapters_tmp;
	GetAdapterInfo(adapters_tmp);
	for (const auto& adapter_tmp : adapters_tmp)
	{
		for (auto& adapter : adapters)
		{
			if (adapter_tmp.description == adapter.description)
			{
				adapter.ip_address = adapter_tmp.ip_address;
				adapter.subnet_mask = adapter_tmp.subnet_mask;
				adapter.default_gateway = adapter_tmp.default_gateway;
			}
		}
	}
}
