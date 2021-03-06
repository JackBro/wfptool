// Wfp Tool
// Copyright (c) 2016 Henry++

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windns.h>
#include <mstcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <subauth.h>
#include <fwpmu.h>
#include <dbt.h>

#include "main.h"
#include "rapp.h"
#include "routine.h"

#include "pugixml\pugixml.hpp"

#include "resource.h"

CONST UINT WM_FINDMSGSTRING = RegisterWindowMessage (FINDMSGSTRING);

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

std::unordered_map<size_t, ITEM_APPLICATION> applications;
std::unordered_map<size_t, __time64_t> notifications;

std::vector<ITEM_COLOR> colors;
std::vector<ITEM_PROCESS> processes;
std::vector<ITEM_RULE_SYSTEM> rules_system;
std::vector<ITEM_RULE_SPECIAL> rules_special;
std::vector<ITEM_RULE_SPECIAL> rules_blocklist;

STATIC_DATA config;

#define MPS_SERVICE1 L"mpssvc"
#define MPS_SERVICE2 L"mpsdrv"

BOOL Mps_IsEnabled ()
{
	BOOL result = FALSE;
	SC_HANDLE scm = OpenSCManager (nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

	if (!scm)
	{
		WDBG (L"OpenSCManager failed. Return value: 0x%.8lx.", GetLastError ());
	}
	else
	{
		LPCWSTR arr[] = {MPS_SERVICE1, MPS_SERVICE2};

		for (INT i = 0; i < _countof (arr); i++)
		{
			SC_HANDLE sc = OpenService (scm, arr[i], SERVICE_QUERY_CONFIG);

			if (!sc)
			{
				WDBG (L"OpenService failed. Return value: 0x%.8lx.", GetLastError ());
			}
			else
			{
				LPVOID buff = nullptr;
				DWORD size = 0;
				DWORD needed = 0;

				while (TRUE)
				{
					if (QueryServiceConfig (sc, (LPQUERY_SERVICE_CONFIG)buff, size, &needed))
					{
						LPQUERY_SERVICE_CONFIG ca = (LPQUERY_SERVICE_CONFIG)buff;

						result = (ca->dwStartType != SERVICE_DISABLED);

						break;
					}

					if (GetLastError () != ERROR_INSUFFICIENT_BUFFER)
					{
						WDBG (L"QueryServiceConfig failed. Return value: 0x%.8lx.", GetLastError ());
						free (buff);

						break;
					}

					size += needed;
					buff = realloc (buff, size);
				}

				CloseServiceHandle (sc);

				// if one of them enabled - leave!
				if (result)
					break;
			}
		}

		CloseServiceHandle (scm);
	}

	return result;
}

DWORD Mps_Stop (BOOL is_stop)
{
	DWORD result = 0;
	SC_HANDLE scm = OpenSCManager (nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

	if (!scm)
	{
		WDBG (L"OpenSCManager failed. Return value: 0x%.8lx.", GetLastError ());
	}
	else
	{
		LPCWSTR arr[] = {MPS_SERVICE1, MPS_SERVICE2};

		for (INT i = 0; i < _countof (arr); i++)
		{
			SC_HANDLE sc = OpenService (scm, arr[i], SERVICE_CHANGE_CONFIG | SERVICE_QUERY_STATUS | SERVICE_STOP);

			if (!sc)
			{
				WDBG (L"OpenService failed. Return value: 0x%.8lx.", GetLastError ());
			}
			else
			{
				if (is_stop)
				{
					DWORD dwBytesNeeded = 0;
					SERVICE_STATUS_PROCESS ssp = {0};

					if (!QueryServiceStatusEx (sc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof (ssp), &dwBytesNeeded))
					{
						WDBG (L"QueryServiceStatusEx failed. Return value: 0x%.8lx.", GetLastError ());
					}
					else
					{
						if (ssp.dwCurrentState != SERVICE_STOPPED)
						{
							if (!ControlService (sc, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp))
							{
								WDBG (L"ControlService failed. Return value: 0x%.8lx.", GetLastError ());
							}
							else
							{
								while (ssp.dwCurrentState == SERVICE_STOP_PENDING)
								{
									Sleep (50);

									if (!QueryServiceStatusEx (sc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof (ssp), &dwBytesNeeded))
									{
										WDBG (L"QueryServiceStatusEx failed. Return value: 0x%.8lx.", GetLastError ());
										break;
									}
								}
							}
						}
					}
				}

				if (!ChangeServiceConfig (sc, SERVICE_NO_CHANGE, is_stop ? SERVICE_DISABLED : SERVICE_AUTO_START, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
				{
					WDBG (L"ChangeServiceConfig failed. Return value: 0x%.8lx.", GetLastError ());
				}

				CloseServiceHandle (sc);
			}
		}

		// start services
		if (!is_stop)
		{
			for (INT i = 0; i < _countof (arr); i++)
			{
				SC_HANDLE sc = OpenService (scm, arr[i], SERVICE_QUERY_STATUS | SERVICE_START);

				if (!sc)
				{
					WDBG (L"OpenService failed. Return value: 0x%.8lx.", GetLastError ());
				}
				else
				{
					DWORD dwBytesNeeded = 0;
					SERVICE_STATUS_PROCESS ssp = {0};

					if (!QueryServiceStatusEx (sc, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof (ssp), &dwBytesNeeded))
					{
						WDBG (L"QueryServiceStatusEx failed. Return value: 0x%.8lx.", GetLastError ());
					}
					else
					{
						if (ssp.dwCurrentState != SERVICE_RUNNING)
						{
							if (!StartService (sc, 0, nullptr))
							{
								WDBG (L"StartService failed. Return value: 0x%.8lx.", GetLastError ());
							}
						}

						CloseServiceHandle (sc);
					}
				}
			}
		}

		CloseServiceHandle (scm);
	}

	return result;
}

VOID _app_refreshstatus (HWND hwnd)
{
	_r_status_settext (hwnd, IDC_STATUSBAR, 0, _r_fmt (I18N (&app, IDS_STATUS_TOTAL, 0), applications.size ()));

	switch (app.ConfigGet (L"Mode", Whitelist).AsUint ())
	{
		case Whitelist:
		{
			_r_status_settext (hwnd, IDC_STATUSBAR, 1, I18N (&app, IDS_MODE_WHITELIST, 0));
			break;
		}

		case Blacklist:
		{
			_r_status_settext (hwnd, IDC_STATUSBAR, 1, I18N (&app, IDS_MODE_BLACKLIST, 0));
			break;
		}

		case TrustNoOne:
		{
			_r_status_settext (hwnd, IDC_STATUSBAR, 1, I18N (&app, IDS_MODE_TRUSTNOONE, 0));
			break;
		}
	}
}

VOID _app_getinfo (ITEM_APPLICATION* ptr)
{
	if (ptr)
	{
		if (app.ConfigGet (L"IsIconsHidden", FALSE).AsBool ())
		{
			ptr->icon_id = LAST_VALUE;
		}
		else
		{
			SHFILEINFO shfi = {0};
			SHGetFileInfo (ptr->full_path, 0, &shfi, sizeof (shfi), SHGFI_SYSICONINDEX);

			ptr->icon_id = shfi.iIcon;
		}

		HINSTANCE h = LoadLibraryEx (ptr->full_path, nullptr, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);

		if (h)
		{
			HRSRC hv = FindResource (h, MAKEINTRESOURCE (VS_VERSION_INFO), RT_VERSION);

			if (hv)
			{
				HGLOBAL hg = LoadResource (h, hv);

				if (hg)
				{
					LPVOID versionInfo = LockResource (hg);

					if (versionInfo)
					{
						UINT vLen = 0, langD = 0;
						LPVOID retbuf = nullptr;

						WCHAR author_entry[MAX_PATH] = {0};
						WCHAR description_entry[MAX_PATH] = {0};
						WCHAR version_entry[MAX_PATH] = {0};

						BOOL result = VerQueryValue (versionInfo, L"\\VarFileInfo\\Translation", &retbuf, &vLen);

						if (result && vLen == 4)
						{
							memcpy (&langD, retbuf, 4);
							StringCchPrintf (author_entry, _countof (author_entry), L"\\StringFileInfo\\%02X%02X%02X%02X\\CompanyName", (langD & 0xff00) >> 8, langD & 0xff, (langD & 0xff000000) >> 24, (langD & 0xff0000) >> 16);
							StringCchPrintf (description_entry, _countof (description_entry), L"\\StringFileInfo\\%02X%02X%02X%02X\\FileDescription", (langD & 0xff00) >> 8, langD & 0xff, (langD & 0xff000000) >> 24, (langD & 0xff0000) >> 16);
							StringCchPrintf (version_entry, _countof (version_entry), L"\\StringFileInfo\\%02X%02X%02X%02X\\FileVersion", (langD & 0xff00) >> 8, langD & 0xff, (langD & 0xff000000) >> 24, (langD & 0xff0000) >> 16);
						}
						else
						{
							StringCchPrintf (author_entry, _countof (author_entry), L"\\StringFileInfo\\%04X04B0\\CompanyName", GetUserDefaultLangID ());
							StringCchPrintf (description_entry, _countof (description_entry), L"\\StringFileInfo\\%04X04B0\\FileDescription", GetUserDefaultLangID ());
							StringCchPrintf (version_entry, _countof (version_entry), L"\\StringFileInfo\\%04X04B0\\FileVersion", GetUserDefaultLangID ());
						}

						if (VerQueryValue (versionInfo, author_entry, &retbuf, &vLen))
						{
							StringCchCopy (ptr->author, _countof (ptr->author), static_cast<LPCWSTR>(retbuf));
						}

						if (VerQueryValue (versionInfo, description_entry, &retbuf, &vLen))
						{
							StringCchCopy (ptr->description, _countof (ptr->description), static_cast<LPCWSTR>(retbuf));
						}

						if (VerQueryValue (versionInfo, version_entry, &retbuf, &vLen))
						{
							StringCchCopy (ptr->version, _countof (ptr->version), static_cast<LPCWSTR>(retbuf));
						}
					}
				}

				// free memory
				UnlockResource (hg);
				FreeResource (hg);
			}

			FreeLibrary (h); // free memory
		}
	}
}

size_t _app_getposition (HWND hwnd, size_t hash)
{
	for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_LISTVIEW); i++)
	{
		if ((size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i) == hash)
			return i;
	}

	return LAST_VALUE;
}

size_t _app_addapplication (HWND hwnd, rstring path, UINT is_silent, BOOL is_checked)
{
	if (path.IsEmpty ())
		return 0;

	_R_SPINLOCK (config.lock_add);

	const size_t hash = path.Hash ();

	if (applications.find (hash) == applications.end ())
	{
		ITEM_APPLICATION* ptr = &applications[hash]; // application pointer

		// save config
		StringCchCopy (ptr->full_path, _countof (ptr->full_path), path);
		StringCchCopy (ptr->file_dir, _countof (ptr->file_dir), path);
		StringCchCopy (ptr->file_name, _countof (ptr->file_name), PathFindFileName (path));

		PathRemoveFileSpec (ptr->file_dir);

		ptr->is_success = TRUE;
		ptr->is_checked = is_checked;
		ptr->is_silent = is_silent;
		ptr->is_system = (_wcsnicmp (path, config.windows_dir, config.wd_length) == 0);
		ptr->is_network = PathIsNetworkPath (ptr->file_dir);

		_app_getinfo (ptr); // read file information

		size_t item = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);

		config.is_firstapply = FALSE; // lock checkbox notifications

		_r_listview_additem (hwnd, IDC_LISTVIEW, path, item, 0, ptr->icon_id, LAST_VALUE, hash);
		_r_listview_setcheckstate (hwnd, IDC_LISTVIEW, item, is_checked);

		config.is_firstapply = TRUE; // unlock checkbox notifications
	}

	_R_SPINUNLOCK (config.lock_add);

	return hash;
}

VOID _wfp_destroyfilters ()
{
	HANDLE henum = nullptr;
	DWORD result = 0;

	if (!config.hengine || !config.is_admin)
		return;

	for (auto& p : applications)
		p.second.is_success = TRUE;

	result = FwpmFilterCreateEnumHandle (config.hengine, nullptr, &henum);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmFilterCreateEnumHandle failed. Return value: 0x%.8lx.", result);
	}
	else
	{
		UINT32 count = 0;
		FWPM_FILTER** matchingFwpFilter = nullptr;

		result = FwpmFilterEnum (config.hengine, henum, 0xFFFFFFFF, &matchingFwpFilter, &count);

		if (result != ERROR_SUCCESS)
		{
			WDBG (L"FwpmFilterEnum failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			if (matchingFwpFilter)
			{
				for (UINT32 i = 0; i < count; i++)
				{
					if (matchingFwpFilter[i]->providerKey && memcmp (matchingFwpFilter[i]->providerKey, &GUID_WfpProvider, sizeof (GUID)) == 0)
						FwpmFilterDeleteById (config.hengine, matchingFwpFilter[i]->filterId);
				}

				FwpmFreeMemory ((LPVOID*)&matchingFwpFilter);
			}
		}
	}

	if (henum)
		FwpmFilterDestroyEnumHandle (config.hengine, henum);
}

VOID _wfp_createfilter (LPCWSTR name, FWPM_FILTER_CONDITION* lpcond, UINT32 const count, UINT8 weight, GUID layer, GUID callout, FWP_ACTION_TYPE action, UINT32 flags = 0)
{
	if (!config.is_admin || !config.hengine)
		return;

	FWPM_FILTER filter = {0};

	filter.flags = flags ? flags : FWPM_FILTER_FLAG_PERSISTENT;

	WCHAR fltr_name[MAX_PATH] = {0};
	WCHAR fltr_desc[MAX_PATH] = {0};

	if (!name)
		StringCchCopy (fltr_name, _countof (fltr_name), action == FWP_ACTION_BLOCK ? L"Block" : L"Permit");
	else
		StringCchCopy (fltr_name, _countof (fltr_name), name);

	StringCchPrintf (fltr_desc, _countof (fltr_desc), APP_NAME_SHORT L" - %s", fltr_name);

	filter.displayData.name = APP_NAME;
	filter.displayData.description = fltr_desc;

	filter.providerKey = (LPGUID)&GUID_WfpProvider;
	filter.layerKey = layer;
	filter.subLayerKey = GUID_WfpSublayer;

	filter.numFilterConditions = count;
	filter.filterCondition = lpcond;
	filter.action.type = action;
	filter.action.calloutKey = callout;

	filter.weight.type = FWP_UINT8;
	filter.weight.uint8 = weight;

	UINT64 filter_id = 0;
	DWORD result = FwpmFilterAdd (config.hengine, &filter, nullptr, &filter_id);

	if (result != ERROR_SUCCESS)
		WDBG (L"FwpmFilterAdd failed. Return value: 0x%.8lx (%s).", result, name);
}

INT CALLBACK _app_listviewcompare (LPARAM lp1, LPARAM lp2, LPARAM sortParam)
{
	BOOL isAsc = HIWORD (sortParam);
	BOOL isByFN = LOWORD (sortParam);

	size_t item1 = static_cast<size_t>(lp1);
	size_t item2 = static_cast<size_t>(lp2);

	INT result = 0;

	if (applications.find (item1) == applications.end () || applications.find (item2) == applications.end ())
		return 0;

	const ITEM_APPLICATION* app1 = &applications[item1];
	const ITEM_APPLICATION* app2 = &applications[item2];

	if (app1->is_checked && !app2->is_checked)
	{
		result = -1;
	}
	else if (!app1->is_checked && app2->is_checked)
	{
		result = 1;
	}
	else
	{
		result = _wcsicmp (isByFN ? app1->file_name : app1->file_dir, isByFN ? app2->file_name : app2->file_dir);
	}

	return isAsc ? -result : result;
}

VOID _app_listviewsort (HWND hwnd)
{
	LPARAM lparam = MAKELPARAM (app.ConfigGet (L"SortMode", 1).AsUint (), app.ConfigGet (L"IsSortDescending", FALSE).AsBool ());

	CheckMenuRadioItem (GetMenu (hwnd), IDM_SORTBYFNAME, IDM_SORTBYFDIR, (LOWORD (lparam) ? IDM_SORTBYFNAME : IDM_SORTBYFDIR), MF_BYCOMMAND);
	CheckMenuItem (GetMenu (hwnd), IDM_SORTISDESCEND, MF_BYCOMMAND | (HIWORD (lparam) ? MF_CHECKED : MF_UNCHECKED));

	SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SORTITEMS, lparam, (LPARAM)&_app_listviewcompare);
}

BOOL _app_parseaddress (LPCWSTR address, PARSE_ADDRESS_RESULT* ptr)
{
	NET_ADDRESS_INFO ni;
	SecureZeroMemory (&ni, sizeof (ni));

	BYTE prefix = 0;
	DWORD result = ParseNetworkString (address, NET_STRING_IP_ADDRESS | NET_STRING_IP_NETWORK | NET_STRING_IP_ADDRESS_NO_SCOPE | NET_STRING_IP_SERVICE, &ni, &ptr->port, &prefix);

	if (result == ERROR_SUCCESS)
	{
		if (ptr)
		{
			ptr->af = ni.IpAddress.sa_family;

			if (ni.IpAddress.sa_family == AF_INET)
			{
				ConvertLengthToIpv4Mask (prefix, (PULONG)&ptr->v4mask);

				ptr->v4mask = ntohl (ptr->v4mask);
				ptr->v4address = ntohl (ni.Ipv4Address.sin_addr.S_un.S_addr);

				if (ptr->ptr4)
				{
					ptr->ptr4->mask = ptr->v4mask;
					ptr->ptr4->addr = ptr->v4address;
				}
			}
			else if (ni.IpAddress.sa_family == AF_INET6)
			{
				ptr->v6prefix = prefix;
				memcpy (ptr->v6address, ni.Ipv6Address.sin6_addr.u.Byte, FWP_V6_ADDR_SIZE);

				if (ptr->ptr6)
				{
					ptr->ptr6->prefixLength = prefix;
					memcpy (ptr->ptr6->addr, ptr->v6address, FWP_V6_ADDR_SIZE);
				}
			}
		}

		return TRUE;
	}
	else
	{
		WDBG (L"ParseNetworkString failed. Return value: 0x%.8lx. (%s)", result, address);
	}

	return FALSE;
}

bool IsPort (LPCWSTR rule)
{
	size_t length = wcslen (rule);

	for (size_t i = 0; i < length; i++)
	{
		if (iswdigit (rule[i]) == 0 && rule[i] != L'-')
			return false;
	}

	return true;
}

void _wfp_createrulefilter (LPCWSTR name, const rstring rule, FWPM_FILTER_CONDITION* pcond, const EnumDirection direction, const BOOL is_block)
{
	UINT32 count = 0;

	const BOOL is_port = IsPort (rule);
	const size_t is_range = rule.Find (L'-');
	const FWP_ACTION_TYPE action = is_block ? FWP_ACTION_BLOCK : FWP_ACTION_PERMIT;

	FWP_V4_ADDR_AND_MASK addrmask4 = {0};
	FWP_V6_ADDR_AND_MASK addrmask6 = {0};

	PARSE_ADDRESS_RESULT addr;
	SecureZeroMemory (&addr, sizeof (addr));

	WCHAR range_start[128] = {0};
	WCHAR range_end[128] = {0};

	FWP_RANGE0 range;
	SecureZeroMemory (&range, sizeof (range));

	UINT32 addr_index = 0;
	UINT32 port_index = 0;

	if (is_range != rstring::npos)
	{
		StringCchCopy (range_start, _countof (range_start), rule.Midded (0, is_range));
		StringCchCopy (range_end, _countof (range_end), rule.Midded (is_range + 1));

		pcond[count].matchType = FWP_MATCH_RANGE;
		pcond[count].conditionValue.type = FWP_RANGE_TYPE;
		pcond[count].conditionValue.rangeValue = &range;

		addr_index = count;
		count += 1;
	}

	if (is_port)
	{
		// ...port
		if (is_range != rstring::npos)
		{
			range.valueLow.type = FWP_UINT16;
			range.valueLow.uint16 = (UINT16)wcstol (range_start, nullptr, 10);

			range.valueHigh.type = FWP_UINT16;
			range.valueHigh.uint16 = (UINT16)wcstol (range_end, nullptr, 10);
		}
		else
		{
			//pcond[count].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
			pcond[count].matchType = FWP_MATCH_EQUAL;
			pcond[count].conditionValue.type = FWP_UINT16;
			pcond[count].conditionValue.uint16 = (UINT16)rule.AsUint ();

			addr_index = count;
			count += 1;
		}
	}
	else
	{
		// ...address
		if (is_range != rstring::npos)
		{
			// parse range start
			if (_app_parseaddress (range_start, &addr))
			{
				if (addr.af == AF_INET)
				{
					range.valueLow.type = FWP_UINT32;
					range.valueLow.uint32 = addr.v4address;
				}
				else if (addr.af == AF_INET6)
				{
					range.valueLow.type = FWP_BYTE_ARRAY16_TYPE;
					memcpy (range.valueLow.byteArray16->byteArray16, addr.v6address, FWP_V6_ADDR_SIZE);
				}
			}
			else
			{
				WDBG (L"Special rule failed. Name: \"%s\" (%s)", name, range_start);
			}

			// parse range end
			if (_app_parseaddress (range_end, &addr))
			{
				if (addr.af == AF_INET)
				{
					range.valueHigh.type = FWP_UINT32;
					range.valueHigh.uint32 = addr.v4address;
				}
				else if (addr.af == AF_INET6)
				{
					range.valueHigh.type = FWP_BYTE_ARRAY16_TYPE;
					memcpy (range.valueHigh.byteArray16->byteArray16, addr.v6address, FWP_V6_ADDR_SIZE);
				}
			}
			else
			{
				WDBG (L"Special rule failed. Name: \"%s\" (%s)", name, range_end);
			}
		}
		else
		{
			addr.ptr4 = &addrmask4;
			addr.ptr6 = &addrmask6;

			if (_app_parseaddress (rule, &addr))
			{
				//pcond[count].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
				pcond[count].matchType = FWP_MATCH_EQUAL;

				if (addr.af == AF_INET)
				{
					pcond[count].conditionValue.type = FWP_V4_ADDR_MASK;
					pcond[count].conditionValue.v4AddrMask = &addrmask4;
				}
				else if (addr.af == AF_INET6)
				{
					pcond[count].conditionValue.type = FWP_V6_ADDR_MASK;
					pcond[count].conditionValue.v6AddrMask = &addrmask6;
				}

				addr_index = count;
				count += 1;

				// set port if available
				if (addr.port)
				{
					pcond[count].matchType = FWP_MATCH_EQUAL;
					pcond[count].conditionValue.type = FWP_UINT16;
					pcond[count].conditionValue.uint16 = addr.port;

					port_index = count;
					count += 1;
				}
			}
			else
			{
				WDBG (L"Special rule failed. Name: \"%s\" (%s)", name, rule);
			}
		}
	}

	// create filters
	if (direction == Out || direction == Both)
	{
		if (is_port)
		{
			pcond[addr_index].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
		}
		else
		{
			pcond[addr_index].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;

			if (port_index)
				pcond[port_index].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
		}

		if (is_port || addr.af == AF_INET)
			_wfp_createfilter (name, pcond, count, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);

		if (is_port || addr.af == AF_INET6)
			_wfp_createfilter (name, pcond, count, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
	}

	if (direction == In || direction == Both)
	{
		if (is_port)
		{
			pcond[addr_index].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
		}
		else
		{
			pcond[addr_index].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;

			if (port_index)
				pcond[port_index].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
		}

		if (is_port || addr.af == AF_INET)
			_wfp_createfilter (name, pcond, count, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);

		if (is_port || addr.af == AF_INET6)
			_wfp_createfilter (name, pcond, count, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

		// listen rule only for port
		if (is_port)
		{
			_wfp_createfilter (name, pcond, count, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, action);
			_wfp_createfilter (name, pcond, count, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, action);
		}
	}
}

VOID _app_loadrules ()
{
	// clear all
	rules_blocklist.clear ();
	rules_special.clear ();

	// load blocklist
	if (app.ConfigGet (L"UseBlocklist2", FALSE).AsBool ())
	{
		pugi::xml_document doc;

		if (doc.load_file (config.blocklist_path, pugi::parse_default, pugi::encoding_auto))
		{
			pugi::xml_node root = doc.child (L"root");

			if (root)
			{
				for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
				{
					ITEM_RULE_SPECIAL rule_ext;
					SecureZeroMemory (&rule_ext, sizeof (rule_ext));

					StringCchCopy (rule_ext.name, _countof (rule_ext.name), item.attribute (L"name").as_string ());
					StringCchCopy (rule_ext.rule, _countof (rule_ext.rule), item.attribute (L"rule").as_string ());

					rule_ext.direction = (EnumDirection)item.attribute (L"direction").as_uint ();
					rule_ext.is_block = item.attribute (L"is_block").as_bool ();
					rule_ext.is_enabled = item.attribute (L"is_enabled").as_bool ();

					rules_blocklist.push_back (rule_ext);
				}
			}
		}
	}

	// load user rules
	{
		pugi::xml_document doc;

		if (doc.load_file (config.rules_path, pugi::parse_default, pugi::encoding_auto))
		{
			pugi::xml_node root = doc.child (L"root");

			if (root)
			{
				for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
				{
					ITEM_RULE_SPECIAL rule_usr;
					SecureZeroMemory (&rule_usr, sizeof (rule_usr));

					StringCchCopy (rule_usr.name, _countof (rule_usr.name), item.attribute (L"name").as_string ());
					StringCchCopy (rule_usr.rule, _countof (rule_usr.rule), item.attribute (L"rule").as_string ());

					rule_usr.direction = (EnumDirection)item.attribute (L"direction").as_uint ();
					rule_usr.is_block = item.attribute (L"is_block").as_bool ();
					rule_usr.is_enabled = item.attribute (L"is_enabled").as_bool ();

					rules_special.push_back (rule_usr);
				}
			}
		}
	}
}

VOID _app_profilesave (HWND hwnd)
{
	_R_SPINLOCK (config.lock_profile);

	{
		pugi::xml_document doc;
		pugi::xml_node node = doc.append_child (L"root");

		if (node)
		{
			for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_LISTVIEW); i++)
			{
				const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

				if (!hash || applications.find (hash) == applications.end ())
					continue;

				ITEM_APPLICATION const* ptr = &applications[hash];

				pugi::xml_node item = node.append_child (L"item");

				if (item)
				{
					item.append_attribute (L"path").set_value (ptr->full_path);
					item.append_attribute (L"is_silent").set_value (ptr->is_silent);
					item.append_attribute (L"is_enabled").set_value (ptr->is_checked);
				}
			}

			doc.save_file (config.config_path, L"\t", pugi::format_indent | pugi::format_write_bom, pugi::encoding_utf16);
		}
	}

	{
		pugi::xml_document doc;
		pugi::xml_node node = doc.append_child (L"root");

		if (node)
		{
			for (size_t i = 0; i < rules_special.size (); i++)
			{
				ITEM_RULE_SPECIAL const* ptr = &rules_special.at (i);

				pugi::xml_node item = node.append_child (L"item");

				if (item)
				{
					item.append_attribute (L"name").set_value (ptr->name);
					item.append_attribute (L"rule").set_value (ptr->rule);
					item.append_attribute (L"direction").set_value (ptr->direction);
					item.append_attribute (L"is_block").set_value (ptr->is_block);
					item.append_attribute (L"is_enabled").set_value (ptr->is_enabled);
				}
			}

			doc.save_file (config.rules_path, L"\t", pugi::format_indent | pugi::format_write_bom, pugi::encoding_utf16);
		}
	}

	_R_SPINUNLOCK (config.lock_profile);
}

VOID _wfp_createportfilter (LPCWSTR name, FWPM_FILTER_CONDITION* pcond, UINT8 weight, FWP_BYTE_BLOB* blob, UINT16 port, UINT8 protocol, EnumDirection direction, ADDRESS_FAMILY af, FWP_ACTION_TYPE action)
{
	UINT32 count = 0;
	UINT32 port_idx = 0;

	if (pcond)
	{
		if (blob)
		{
			pcond[count].fieldKey = FWPM_CONDITION_ALE_APP_ID;
			pcond[count].matchType = FWP_MATCH_EQUAL;
			pcond[count].conditionValue.type = FWP_BYTE_BLOB_TYPE;
			pcond[count].conditionValue.byteBlob = blob;

			count += 1;
		}

		if (protocol)
		{
			pcond[count].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
			pcond[count].matchType = FWP_MATCH_EQUAL;
			pcond[count].conditionValue.type = FWP_UINT8;
			pcond[count].conditionValue.uint8 = protocol;

			count += 1;
		}

		if (port)
		{
			//pcond[count].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
			pcond[count].matchType = FWP_MATCH_EQUAL;
			pcond[count].conditionValue.type = FWP_UINT16;
			pcond[count].conditionValue.uint16 = port;

			port_idx = count;
			count += 1;
		}

		if (count)
		{
			if (af == AF_INET || af == AF_UNSPEC)
			{
				if (direction == Out || direction == Both)
				{
					if (port_idx)
						pcond[port_idx].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;

					_wfp_createfilter (name, pcond, count, weight, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				}

				if (direction == In || direction == Both)
				{
					if (port_idx)
						pcond[port_idx].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;

					_wfp_createfilter (name, pcond, count, weight, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
					//_wfp_createfilter (name, pcond, count, weight, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, action);
				}
			}

			if (af == AF_INET6 || af == AF_UNSPEC)
			{
				if (direction == Out || direction == Both)
				{
					if (port_idx)
						pcond[port_idx].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;

					_wfp_createfilter (name, pcond, count, weight, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
				}

				if (direction == In || direction == Both)
				{
					if (port_idx)
						pcond[port_idx].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;

					_wfp_createfilter (name, pcond, count, weight, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
					//_wfp_createfilter (name, pcond, count, weight, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, action);
				}
			}
		}
	}
}

BOOL _wfp_applypath (LPCWSTR name, LPCWSTR path, FWP_ACTION_TYPE action, BOOL is_service)
{
	FWP_BYTE_BLOB* blob = nullptr;
	BOOL is_success = FALSE;
	const UINT8 weight = FILTER_WEIGHT_APPLICATION;

	DWORD result = FwpmGetAppIdFromFileName (path, &blob);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmGetAppIdFromFileName failed. Return value: 0x%.8lx. (%s)", result, path);
	}
	else
	{
		FWPM_FILTER_CONDITION fwfc[8] = {0};

		fwfc[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
		fwfc[0].matchType = FWP_MATCH_EQUAL;
		fwfc[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
		fwfc[0].conditionValue.byteBlob = blob;

		if (!is_service)
		{
			// set outbound connections
			_wfp_createfilter (name, fwfc, 1, weight, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
			_wfp_createfilter (name, fwfc, 1, weight, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

			// set inbound connections
			_wfp_createfilter (name, fwfc, 1, weight, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
			_wfp_createfilter (name, fwfc, 1, weight, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

			// set listen connections
			_wfp_createfilter (name, fwfc, 1, weight, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, action);
			_wfp_createfilter (name, fwfc, 1, weight, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, action);
		}
		else
		{
			// allow dhcp service
			if (app.ConfigGet (L"AllowDhcpService", TRUE).AsBool ())
			{
				_wfp_createportfilter (name, fwfc, weight, blob, 67, IPPROTO_UDP, Both, AF_INET, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 68, IPPROTO_UDP, Both, AF_INET, action);

				_wfp_createportfilter (name, fwfc, weight, blob, 546, IPPROTO_UDP, Both, AF_INET6, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 547, IPPROTO_UDP, Both, AF_INET6, action);
			}

			// allow dns service
			if (app.ConfigGet (L"AllowDnsService", TRUE).AsBool ())
			{
				// dns protocol
				_wfp_createportfilter (name, fwfc, weight, blob, 53, IPPROTO_UDP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 53, IPPROTO_TCP, Both, AF_UNSPEC, action);

				// llmnr protocol (local dns)
				_wfp_createportfilter (name, fwfc, weight, blob, 5355, IPPROTO_UDP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 5355, IPPROTO_TCP, Both, AF_UNSPEC, action);
			}

			// allow network discovery service
			if (app.ConfigGet (L"AllowNetworkDiscoveryService", TRUE).AsBool ())
			{
				// microsoft directory
				_wfp_createportfilter (name, fwfc, weight, blob, 445, IPPROTO_TCP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 445, IPPROTO_UDP, Both, AF_UNSPEC, action);

				// netbios name
				_wfp_createportfilter (name, fwfc, weight, blob, 137, IPPROTO_TCP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 137, IPPROTO_UDP, Both, AF_UNSPEC, action);

				// netbios datagram
				_wfp_createportfilter (name, fwfc, weight, blob, 138, IPPROTO_UDP, Both, AF_UNSPEC, action);

				// netbios session
				_wfp_createportfilter (name, fwfc, weight, blob, 139, IPPROTO_UDP, Both, AF_UNSPEC, action);

				// web services dynamic discovery
				_wfp_createportfilter (name, fwfc, weight, blob, 3702, IPPROTO_TCP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 3702, IPPROTO_UDP, Both, AF_UNSPEC, action);
			}

			// allow ntp service
			if (app.ConfigGet (L"AllowNtpService", TRUE).AsBool ())
			{
				_wfp_createportfilter (name, fwfc, weight, blob, 123, IPPROTO_UDP, Out, AF_UNSPEC, action);
			}

			// allow snmp service
			if (app.ConfigGet (L"AllowSnmpService", FALSE).AsBool ())
			{
				_wfp_createportfilter (name, fwfc, weight, blob, 161, IPPROTO_UDP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 162, IPPROTO_UDP, Both, AF_UNSPEC, action);
			}

			// allow ssdp service (upnp)
			if (app.ConfigGet (L"AllowSsdpService", TRUE).AsBool ())
			{
				_wfp_createportfilter (name, fwfc, weight, blob, 1900, IPPROTO_UDP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 1900, IPPROTO_TCP, Both, AF_UNSPEC, action);
			}

			// allow windows update service
			if (app.ConfigGet (L"AllowWindowUpdateService", FALSE).AsBool ())
			{
				_wfp_createportfilter (name, fwfc, weight, blob, 80, IPPROTO_TCP, Both, AF_UNSPEC, action);
				_wfp_createportfilter (name, fwfc, weight, blob, 443, IPPROTO_TCP, Both, AF_UNSPEC, action);
			}
		}

		is_success = TRUE;

		FwpmFreeMemory ((LPVOID*)&blob);
	}

	return is_success;
}

VOID _wfp_applyfilters (HWND hwnd)
{
	if (!config.hengine || !config.is_admin)
		return;

	_R_SPINLOCK (config.lock_apply);

	const EnumMode mode = (EnumMode)app.ConfigGet (L"Mode", Whitelist).AsUint ();

	FWPM_FILTER_CONDITION fwfc[3] = {0};

	DWORD result = FwpmTransactionBegin (config.hengine, 0);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmTransactionBegin failed. Return value: 0x%.8lx.", result);
	}
	else
	{
		_wfp_destroyfilters ();

		if (mode != TrustNoOne)
		{
			fwfc[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
			fwfc[0].matchType = FWP_MATCH_EQUAL;
			fwfc[0].conditionValue.type = FWP_UINT8;
			//fwfc[0].conditionValue.uint8 = IPPROTO_UDP;

			// allow outbound icmp protocol
			if (app.ConfigGet (L"AllowOutboundIcmp", TRUE).AsBool ())
			{
				fwfc[0].conditionValue.uint8 = IPPROTO_ICMP;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT);

				fwfc[0].conditionValue.uint8 = IPPROTO_ICMPV6;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT);
			}

			// allow inbound icmp protocol
			if (app.ConfigGet (L"AllowInboundIcmp", FALSE).AsBool ())
			{
				fwfc[0].conditionValue.uint8 = IPPROTO_ICMP;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT);

				fwfc[0].conditionValue.uint8 = IPPROTO_ICMPV6;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT);
			}

			// add loopback connections permission
			{
				FWP_V4_ADDR_AND_MASK addrmask4 = {0};
				FWP_V6_ADDR_AND_MASK addrmask6 = {0};

				// First condition. Match only unicast addresses.
				fwfc[0].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS_TYPE;
				fwfc[0].matchType = FWP_MATCH_EQUAL;
				fwfc[0].conditionValue.type = FWP_UINT8;
				fwfc[0].conditionValue.uint8 = NlatUnicast;

				// Second condition. Match all loopback (localhost) data.
				fwfc[1].fieldKey = FWPM_CONDITION_FLAGS;
				fwfc[1].matchType = FWP_MATCH_EQUAL;
				fwfc[1].conditionValue.type = FWP_UINT32;
				fwfc[1].conditionValue.uint32 = FWP_CONDITION_FLAG_IS_LOOPBACK;

				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT);
				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT);

				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT);
				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT);

				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, FWP_ACTION_PERMIT);
				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, FWP_ACTION_PERMIT);

				// boot-time filters loopback permission
				if (app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool ())
				{
					_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);
					_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);

					_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);
					_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);

					_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);
					_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);
				}

				// ipv4/ipv6 loopback
				LPCWSTR ip_list[] = {L"10.0.0.0/8", L"172.16.0.0/12", L"169.254.0.0/16", L"192.168.0.0/16", L"fd00::/8", L"fe80::/10"};

				for (size_t i = 0; i < _countof (ip_list); i++)
				{
					PARSE_ADDRESS_RESULT addr;

					SecureZeroMemory (&addr, sizeof (addr));

					SecureZeroMemory (&addrmask4, sizeof (addrmask4));
					SecureZeroMemory (&addrmask6, sizeof (addrmask6));

					addr.ptr4 = &addrmask4;
					addr.ptr6 = &addrmask6;

					if (_app_parseaddress (ip_list[i], &addr))
					{
						//fwfc[2].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
						fwfc[2].matchType = FWP_MATCH_EQUAL;

						if (addr.af == AF_INET)
						{
							fwfc[2].conditionValue.type = FWP_V4_ADDR_MASK;
							fwfc[2].conditionValue.v4AddrMask = &addrmask4;

							fwfc[2].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
							_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT);

							if (app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool ())
								_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);

							fwfc[2].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;
							_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT);

							if (app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool ())
								_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);
						}
						else if (addr.af == AF_INET6)
						{
							fwfc[2].conditionValue.type = FWP_V6_ADDR_MASK;
							fwfc[2].conditionValue.v6AddrMask = &addrmask6;

							fwfc[2].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
							_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT);

							if (app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool ())
								_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);

							fwfc[2].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;
							_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT);

							if (app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool ())
								_wfp_createfilter (nullptr, fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT, FWPM_FILTER_FLAG_BOOTTIME);
						}
					}
					else
					{
						WDBG (L"Special rule failed. Name: \"%s\"", ip_list[i]);
					}
				}
			}

			// apply blocklist rules
			if (app.ConfigGet (L"UseBlocklist2", FALSE).AsBool ())
			{
				for (size_t i = 0; i < rules_blocklist.size (); i++)
				{
					if (!rules_blocklist.at (i).is_enabled)
						continue;

					const rstring arr = rules_blocklist.at (i).rule;
					rstring::rvector vc = arr.AsVector (L",");

					for (size_t j = 0; j < vc.size (); j++)
					{
						vc.at (j).Trim (L"\r\n "); // trim whitespace

						_wfp_createrulefilter (rules_blocklist.at (i).name, vc.at (j), fwfc, rules_blocklist.at (i).direction, rules_blocklist.at (i).is_block);
					}
				}
			}

			// apply special rules
			for (size_t i = 0; i < rules_special.size (); i++)
			{
				if (!rules_special.at (i).is_enabled)
					continue;

				const rstring arr = rules_special.at (i).rule;
				rstring::rvector vc = arr.AsVector (L",");

				for (size_t j = 0; j < vc.size (); j++)
				{
					vc.at (j).Trim (L"\r\n "); // trim whitespace

					_wfp_createrulefilter (rules_special.at (i).name, vc.at (j), fwfc, rules_special.at (i).direction, rules_special.at (i).is_block);
				}
			}

			// application filters
			{
				BOOL is_svchostallowed = FALSE;
				BOOL is_systemallowed = FALSE;
				BOOL is_myselfallowed = FALSE;

				for (auto& p : applications)
				{
					if (!p.second.is_checked)
						continue;

					if (mode == Whitelist)
					{
						if (config.svchost_hash == p.first)
							is_svchostallowed = TRUE;

						if (config.system_hash == p.first)
							is_systemallowed = TRUE;

						if (config.myself_hash == p.first && app.ConfigGet (L"AllowInternetAccess", TRUE).AsBool ())
							is_myselfallowed = TRUE;
					}

					p.second.is_success = _wfp_applypath (nullptr, p.second.full_path, (mode == Whitelist) ? FWP_ACTION_PERMIT : FWP_ACTION_BLOCK, FALSE);
				}

				// unlock itself
				if (!is_myselfallowed && app.ConfigGet (L"AllowInternetAccess", TRUE).AsBool ())
					_wfp_applypath (nullptr, config.myself_path, FWP_ACTION_PERMIT, FALSE);

				// allow services
				if (!is_svchostallowed)
					_wfp_applypath (nullptr, config.svchost_path, FWP_ACTION_PERMIT, TRUE);

				if (!is_systemallowed)
					_wfp_applypath (nullptr, L"System", FWP_ACTION_PERMIT, TRUE);
			}
		}

		// block all other traffic (only on "whitelist" & "trust no one" mode)
		if (mode == Whitelist || mode == TrustNoOne)
		{
			UINT8 weight = (mode == Whitelist) ? FILTER_WEIGHT_ALLOWBLOCK : FILTER_WEIGHT_HIGHEST;

			_wfp_createfilter (nullptr, nullptr, 0, weight, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_BLOCK);
			_wfp_createfilter (nullptr, nullptr, 0, weight, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_BLOCK);

			if (mode == TrustNoOne || app.ConfigGet (L"AllowInboundConnections", FALSE).AsBool () == FALSE)
			{
				_wfp_createfilter (nullptr, nullptr, 0, weight, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_BLOCK);
				_wfp_createfilter (nullptr, nullptr, 0, weight, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_BLOCK);

				_wfp_createfilter (nullptr, nullptr, 0, weight, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, FWP_ACTION_BLOCK);
				_wfp_createfilter (nullptr, nullptr, 0, weight, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, FWP_ACTION_BLOCK);
			}
		}

		// install boot-time filters (enforced at boot-time, even before "Base Filtering Engine" service starts.)FWPM_FILTER_FLAG_BOOTTIME
		if (app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool ())
		{
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_BLOCK, FWPM_FILTER_FLAG_BOOTTIME);
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_BLOCK, FWPM_FILTER_FLAG_BOOTTIME);

			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_BLOCK, FWPM_FILTER_FLAG_BOOTTIME);
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_BLOCK, FWPM_FILTER_FLAG_BOOTTIME);

			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4, FWP_ACTION_BLOCK, FWPM_FILTER_FLAG_BOOTTIME);
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6, FWP_ACTION_BLOCK, FWPM_FILTER_FLAG_BOOTTIME);
		}

		FwpmTransactionCommit (config.hengine);
	}

	_app_listviewsort (hwnd);
	_app_profilesave (hwnd);

	_R_SPINUNLOCK (config.lock_apply);
}

VOID _wfp_createcallout (HANDLE h, const GUID layer_key, const GUID callout_key)
{
	FWPM_CALLOUT0 callout = {0};
	UINT32 callout_id = 0;

	callout.displayData.name = APP_NAME;
	callout.displayData.description = APP_NAME;

	callout.flags = FWPM_CALLOUT_FLAG_PERSISTENT;

	callout.providerKey = (LPGUID)&GUID_WfpProvider;
	callout.calloutKey = callout_key;
	callout.applicableLayer = layer_key;

	DWORD result = FwpmCalloutAdd (h, &callout, nullptr, &callout_id);

	if (result != ERROR_SUCCESS && result != FWP_E_ALREADY_EXISTS)
		WDBG (L"FwpmCalloutAdd failed. Return value: 0x%.8lx.", result);
}

// append log-line
VOID LogAppend (ITEM_LOG* ptr)
{
	// check display settings
	if (applications.find (ptr->hash) != applications.end ())
	{
		ITEM_APPLICATION const* ptr2 = &applications[ptr->hash];

		if ((ptr2->is_silent & SILENT_LOG) != 0)
			return;
	}

	WCHAR buffer[2048] = {0};
	StringCchPrintf (buffer, _countof (buffer), L"[%s] %s (%s\\%s) [%s:%s] (%s) %s\r\n", ptr->date, ptr->full_path, ptr->domain, ptr->username, ptr->protocol, ptr->address, /*ptr->provider, */ptr->name, ptr->direction);

	if (config.hlog != nullptr && config.hlog != INVALID_HANDLE_VALUE)
	{
		_R_SPINLOCK (config.lock_writelog);

		if (config.hlog != nullptr && config.hlog != INVALID_HANDLE_VALUE) // check handle twice (required!)
		{
			DWORD written = 0;
			WriteFile (config.hlog, buffer, DWORD (wcslen (buffer) * sizeof (WCHAR)), &written, nullptr);
		}

		_R_SPINUNLOCK (config.lock_writelog);
	}
}

// show dropped packets notification
VOID NotificationShow (ITEM_LOG* ptr)
{
	// check display settings
	if (applications.find (ptr->hash) != applications.end ())
	{
		ITEM_APPLICATION const* ptr2 = &applications[ptr->hash];

		if ((ptr2->is_silent & SILENT_NOTIFICATION) != 0)
			return;
	}

	if ((_r_unixtime_now () - notifications[ptr->hash]) >= (app.ConfigGet (L"NotificationsTimeout", 10).AsUint ())) // check for timeout (sec.)
	{
		app.TrayPopup (NIIF_WARNING | (app.ConfigGet (L"IsNotificationsSilent", FALSE).AsBool () ? NIIF_NOSOUND : 0), APP_NAME, _r_fmt (L"%s: %s\r\n%s: %s\r\n%s: %s (%s) [%s]\r\n%s: %s", I18N (&app, IDS_DATE, 0), ptr->date, I18N (&app, IDS_FILE, 0), ptr->full_path, I18N (&app, IDS_ADDRESS, 0), ptr->address, ptr->protocol, ptr->direction, I18N (&app, IDS_NAME, 0), ptr->name));
		notifications[ptr->hash] = _r_unixtime_now ();
		config.last_hash = ptr->hash;
	}
}

// Author: Elmue
// http://stackoverflow.com/questions/65170/how-to-get-name-associated-with-open-handle/18792477#18792477
rstring GetDosPathFromNtPath (LPCWSTR nt_path)
{
	rstring result = nt_path;

	if (_wcsnicmp (nt_path, L"\\Device\\Mup\\", 12) == 0) // Win 7
	{
		result = L"\\\\";
		result.Append (nt_path + 12);
	}
	else if (_wcsnicmp (nt_path, L"\\Device\\LanmanRedirector\\", 25) == 0) // Win XP
	{
		result = L"\\\\";
		result.Append (nt_path + 25);
	}
	else
	{
		WCHAR drives[300] = {0};

		if (GetLogicalDriveStrings (_countof (drives), drives))
		{
			LPWSTR drv = drives;

			while (drv[0])
			{
				LPWSTR drv_next = drv + wcslen (drv) + 1;

				drv[2] = 0; // the backslash is not allowed for QueryDosDevice()

				WCHAR u16_NtVolume[1000];
				u16_NtVolume[0] = 0;

				// may return multiple strings!
				// returns very weird strings for network shares
				if (QueryDosDevice (drv, u16_NtVolume, sizeof (u16_NtVolume) / 2))
				{
					size_t s32_Len = wcslen (u16_NtVolume);
					if (s32_Len > 0 && _wcsnicmp (nt_path, u16_NtVolume, s32_Len) == 0)
					{
						result = drv;
						result.Append (nt_path + s32_Len);

						break;
					}
				}

				drv = drv_next;
			}
		}
	}

	return result;
}

VOID CALLBACK DropEventCallback (LPVOID, const FWPM_NET_EVENT1* pEvent)
{
	if (!pEvent || ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_APP_ID_SET) == 0) || !pEvent->header.appId.data)
		return;

	ITEM_LOG item;
	SecureZeroMemory (&item, sizeof (item));

	// copy date and time
	item.timestamp = _r_unixtime_from_filetime (&pEvent->header.timeStamp);
	StringCchCopy (item.date, _countof (item.date), _r_fmt_date (item.timestamp, FDTF_SHORTDATE | FDTF_LONGTIME));

	// copy converted nt device path into win32
	rstring path = GetDosPathFromNtPath (LPCWSTR (pEvent->header.appId.data));
	item.hash = path.Hash ();
	StringCchCopy (item.full_path, _countof (item.full_path), path);

	// get username & domain
	if (pEvent->header.userId)
	{
		SID_NAME_USE sid_type;
		SecureZeroMemory (&sid_type, sizeof (sid_type));

		DWORD length1 = _countof (item.username);
		DWORD length2 = _countof (item.domain);

		LookupAccountSid (nullptr, pEvent->header.userId, item.username, &length1, item.domain, &length2, &sid_type);
	}

	if (pEvent->classifyDrop)
	{
		// read filter information
		if (pEvent->classifyDrop->filterId)
		{
			FWPM_FILTER* filter = nullptr;

			DWORD result = FwpmFilterGetById (config.hengine, pEvent->classifyDrop->filterId, &filter);

			if (result == ERROR_SUCCESS)
			{
				StringCchCopy (item.name, _countof (item.name), filter->displayData.description);

				FwpmFreeMemory ((LPVOID*)&filter);
			}
			else
			{
				WDBG (L"FwpmFilterGetById failed. Return value: 0x%.8lx. (%d)", result, pEvent->classifyDrop->filterId);
			}
		}
	}

	// filter name fallback
	if (!item.name[0])
		StringCchCopy (item.name, _countof (item.name), L"n/a");

	// apps collector
	if (app.ConfigGet (L"IsAppsCollectorEnabled", TRUE).AsBool ())
	{
		if (applications.find (item.hash) == applications.end ())
		{
			_app_addapplication (app.GetHWND (), item.full_path, 0, FALSE);

			_app_listviewsort (app.GetHWND ());
			_app_profilesave (app.GetHWND ());
		}
	}

	// copy protocol
	if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_IP_PROTOCOL_SET) != 0)
	{
#define SWITCH_HELPER(x) case (x): {StringCchCopy (item.protocol, _countof (item.protocol), (L#x) + 8); break;}

		switch (pEvent->header.ipProtocol)
		{
			SWITCH_HELPER (IPPROTO_ICMP);
			SWITCH_HELPER (IPPROTO_IGMP);
			SWITCH_HELPER (IPPROTO_GGP);
			SWITCH_HELPER (IPPROTO_IPV4);
			SWITCH_HELPER (IPPROTO_ST);
			SWITCH_HELPER (IPPROTO_TCP);
			SWITCH_HELPER (IPPROTO_UDP);
			SWITCH_HELPER (IPPROTO_RDP);
			SWITCH_HELPER (IPPROTO_IPV6);
			SWITCH_HELPER (IPPROTO_ICMPV6);
			SWITCH_HELPER (IPPROTO_L2TP);
			SWITCH_HELPER (IPPROTO_SCTP);
			SWITCH_HELPER (IPPROTO_RAW);

			default:
			{
				StringCchCopy (item.protocol, _countof (item.protocol), L"n/a");
				break;
			}
		}
	}

	// ipv4 address
	if (pEvent->header.ipVersion == FWP_IP_VERSION_V4)
	{
		if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0 || (pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET) != 0)
		{
			if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET) != 0)
			{
				StringCchCopy (item.direction, _countof (item.direction), L"Remote"); // remote address

				StringCchPrintf (item.address, _countof (item.address), L"%d.%d.%d.%d",
					pEvent->header.remoteAddrV6.byteArray16[3],
					pEvent->header.remoteAddrV6.byteArray16[2],
					pEvent->header.remoteAddrV6.byteArray16[1],
					pEvent->header.remoteAddrV6.byteArray16[0]
				);

				if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_PORT_SET) != 0)
					StringCchCat (item.address, _countof (item.address), _r_fmt (L":%d", pEvent->header.remotePort));

				if (app.ConfigGet (L"IsLogEnabled", FALSE).AsBool ())
					LogAppend (&item);

				if (app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool ())
					NotificationShow (&item);
			}

			if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0)
			{
				StringCchCopy (item.direction, _countof (item.direction), L"Local"); // local address

				StringCchPrintf (item.address, _countof (item.address), L"%d.%d.%d.%d",
					pEvent->header.localAddrV6.byteArray16[3],
					pEvent->header.localAddrV6.byteArray16[2],
					pEvent->header.localAddrV6.byteArray16[1],
					pEvent->header.localAddrV6.byteArray16[0]
				);

				if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_PORT_SET) != 0)
					StringCchCat (item.address, _countof (item.address), _r_fmt (L":%d", pEvent->header.localPort));

				if (app.ConfigGet (L"IsLogEnabled", FALSE).AsBool ())
					LogAppend (&item);

				if (app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool ())
					NotificationShow (&item);
			}
		}
	}
	else if (pEvent->header.ipVersion == FWP_IP_VERSION_V6)
	{
		if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0 || (pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET) != 0)
		{
			if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0)
			{
				StringCchCopy (item.direction, _countof (item.direction), L"Remote"); // remote address

				StringCchPrintf (item.address, _countof (item.address), L"%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
					pEvent->header.remoteAddrV6.byteArray16[0],
					pEvent->header.remoteAddrV6.byteArray16[1],
					pEvent->header.remoteAddrV6.byteArray16[2],
					pEvent->header.remoteAddrV6.byteArray16[3],
					pEvent->header.remoteAddrV6.byteArray16[4],
					pEvent->header.remoteAddrV6.byteArray16[5],
					pEvent->header.remoteAddrV6.byteArray16[6],
					pEvent->header.remoteAddrV6.byteArray16[7],
					pEvent->header.remoteAddrV6.byteArray16[8],
					pEvent->header.remoteAddrV6.byteArray16[9],
					pEvent->header.remoteAddrV6.byteArray16[10],
					pEvent->header.remoteAddrV6.byteArray16[11],
					pEvent->header.remoteAddrV6.byteArray16[12],
					pEvent->header.remoteAddrV6.byteArray16[13],
					pEvent->header.remoteAddrV6.byteArray16[14],
					pEvent->header.remoteAddrV6.byteArray16[15]
				);

				if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_PORT_SET) != 0)
					StringCchCat (item.address, _countof (item.address), _r_fmt (L":%d", pEvent->header.remotePort));

				if (app.ConfigGet (L"IsLogEnabled", FALSE).AsBool ())
					LogAppend (&item);

				if (app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool ())
					NotificationShow (&item);
			}

			if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0)
			{
				StringCchCopy (item.direction, _countof (item.direction), L"Local"); // local address

				StringCchPrintf (item.address, _countof (item.address), L"%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
					pEvent->header.localAddrV6.byteArray16[0],
					pEvent->header.localAddrV6.byteArray16[1],
					pEvent->header.localAddrV6.byteArray16[2],
					pEvent->header.localAddrV6.byteArray16[3],
					pEvent->header.localAddrV6.byteArray16[4],
					pEvent->header.localAddrV6.byteArray16[5],
					pEvent->header.localAddrV6.byteArray16[6],
					pEvent->header.localAddrV6.byteArray16[7],
					pEvent->header.localAddrV6.byteArray16[8],
					pEvent->header.localAddrV6.byteArray16[9],
					pEvent->header.localAddrV6.byteArray16[10],
					pEvent->header.localAddrV6.byteArray16[11],
					pEvent->header.localAddrV6.byteArray16[12],
					pEvent->header.localAddrV6.byteArray16[13],
					pEvent->header.localAddrV6.byteArray16[14],
					pEvent->header.localAddrV6.byteArray16[15]
				);

				if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_PORT_SET) != 0)
					StringCchCat (item.address, _countof (item.address), _r_fmt (L":%d", pEvent->header.localPort));

				if (app.ConfigGet (L"IsLogEnabled", FALSE).AsBool ())
					LogAppend (&item);

				if (app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool ())
					NotificationShow (&item);
			}
		}
	}
}

VOID _wfp_setevents (BOOL is_install)
{
	if (!_r_sys_validversion (6, 1))
		return;

	// reset all handles
	_R_SPINLOCK (config.lock_writelog);

	if (config.hlog != nullptr && config.hlog != INVALID_HANDLE_VALUE)
	{
		CloseHandle (config.hlog);
		config.hlog = nullptr;
	}

	_R_SPINUNLOCK (config.lock_writelog);

	if (!is_install)
		return; // already closed

	if (!config.hengine || !config.is_admin)
		return;

	// check if log enabled
	if (is_install && app.ConfigGet (L"IsLogEnabled", FALSE).AsBool () == FALSE)
		return;

	if (is_install)
	{
		// open log file
		config.hlog = CreateFile (_r_path_expand (app.ConfigGet (L"LogPath", L"%userprofile%\\wfptool.log")), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);

		if (config.hlog == INVALID_HANDLE_VALUE)
		{
			WDBG (L"CreateFile failed. Return value: 0x%.8lx.", GetLastError ());
		}
		else
		{
			_R_SPINLOCK (config.lock_writelog);

			if (GetLastError () != ERROR_ALREADY_EXISTS)
			{
				DWORD written = 0;
				static const BYTE bom[] = {0xFF, 0xFE};

				WriteFile (config.hlog, bom, sizeof (bom), &written, nullptr); // write utf-16 le byte order mask
			}
			else
			{
				SetFilePointer (config.hlog, 0, nullptr, FILE_END);
			}

			_R_SPINUNLOCK (config.lock_writelog);
		}
	}
}

UINT WINAPI ApplyThread (LPVOID)
{
	HANDLE evts[] = {config.apply_evt, config.stop_evt};

	while (TRUE)
	{
		DWORD state = WaitForMultipleObjectsEx (_countof (evts), evts, FALSE, INFINITE, FALSE);

		if (state == WAIT_OBJECT_0) // apply event
		{
			_wfp_applyfilters (app.GetHWND ());
		}
		else if (state == WAIT_OBJECT_0 + 2) // stop event
		{
			break;
		}
	}

	return ERROR_SUCCESS;
}

VOID _wfp_start ()
{
	if (config.hengine || !config.is_admin)
		return;

	FWPM_SESSION session = {0};

	session.displayData.name = APP_NAME;
	session.displayData.description = APP_NAME;

	DWORD result = FwpmEngineOpen (nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &config.hengine);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmEngineOpen failed. Return value: 0x%.8lx.", result);
	}
	else
	{
		result = FwpmTransactionBegin (config.hengine, 0);

		if (result != ERROR_SUCCESS)
		{
			WDBG (L"FwpmTransactionBegin failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			FWPM_PROVIDER provider = {0};

			provider.displayData.name = APP_NAME;
			provider.displayData.description = APP_NAME;

			provider.providerKey = GUID_WfpProvider;
			provider.flags = FWPM_PROVIDER_FLAG_PERSISTENT;

			result = FwpmProviderAdd (config.hengine, &provider, nullptr);

			if (result != ERROR_SUCCESS && result != FWP_E_ALREADY_EXISTS)
			{
				WDBG (L"FwpmProviderAdd failed. Return value: 0x%.8lx.", result);
				FwpmTransactionAbort (config.hengine);
			}
			else
			{
				FWPM_SUBLAYER sublayer = {0};

				sublayer.displayData.name = APP_NAME;
				sublayer.displayData.description = APP_NAME;

				sublayer.providerKey = (LPGUID)&GUID_WfpProvider;
				sublayer.subLayerKey = GUID_WfpSublayer;
				sublayer.flags = FWPM_SUBLAYER_FLAG_PERSISTENT;
				sublayer.weight = (UINT16)app.ConfigGet (L"SublayerWeight", 0x0000ffff).AsUint ();

				result = FwpmSubLayerAdd (config.hengine, &sublayer, nullptr);

				if (result != ERROR_SUCCESS && result != FWP_E_ALREADY_EXISTS)
				{
					WDBG (L"FwpmSubLayerAdd failed. Return value: 0x%.8lx.", result);
					FwpmTransactionAbort (config.hengine);
				}
				else
				{
					// create outbound callouts
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4);
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6);

					// create inbound callouts
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4);
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6);

					// create listen callouts
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_LISTEN_V4, GUID_WfpListenCallout4);
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_LISTEN_V6, GUID_WfpListenCallout6);

					result = FwpmTransactionCommit (config.hengine);

					if (result != ERROR_SUCCESS)
					{
						WDBG (L"FwpmTransactionCommit failed. Return value: 0x%.8lx.", result);
					}
					else
					{
						// net events subscribe (win7 and above)
						if (_r_sys_validversion (6, 1))
						{
							FWP_VALUE val;
							SecureZeroMemory (&val, sizeof (val));

							val.type = FWP_UINT32;
							val.uint32 = 1;

							result = FwpmEngineSetOption (config.hengine, FWPM_ENGINE_COLLECT_NET_EVENTS, &val);

							if (result != ERROR_SUCCESS)
							{
								WDBG (L"FwpmEngineSetOption failed. Return value: 0x%.8lx.", result);
							}
							else
							{
								// Collect inbound multicast/broadcast network events

								//val.type = FWP_UINT32;
								//val.uint32 = FWPM_NET_EVENT_KEYWORD_INBOUND_MCAST | FWPM_NET_EVENT_KEYWORD_INBOUND_BCAST;

								//result = FwpmEngineSetOption (config.hengine, FWPM_ENGINE_NET_EVENT_MATCH_ANY_KEYWORDS, &val);

								//if (result != ERROR_SUCCESS)
								//{
								//	WDBG (L"FwpmEngineSetOption failed. Return value: 0x%.8lx.", result);
								//}

								if (!config.hevent)
								{
									FWPMNES0 _FwpmNetEventSubscribe0 = (FWPMNES0)GetProcAddress (GetModuleHandle (L"fwpuclnt.dll"), "FwpmNetEventSubscribe0");

									if (!_FwpmNetEventSubscribe0)
									{
										WDBG (L"GetProcAddress failed. Return value: 0x%.8lx.", GetLastError ());
									}
									else
									{
										FWPM_NET_EVENT_ENUM_TEMPLATE0 enum_template;
										FWPM_NET_EVENT_SUBSCRIPTION0 subscription;

										SecureZeroMemory (&enum_template, sizeof (enum_template));
										SecureZeroMemory (&subscription, sizeof (subscription));

										subscription.sessionKey = session.sessionKey;
										subscription.enumTemplate = &enum_template;

										result = _FwpmNetEventSubscribe0 (config.hengine, &subscription, DropEventCallback, nullptr, &config.hevent);

										if (result != ERROR_SUCCESS)
											WDBG (L"FwpmNetEventSubscribe0 failed. Return value: 0x%.8lx.", result);
									}
								}
							}
						}

						if (!config.hthread)
							config.hthread = (HANDLE)_beginthreadex (nullptr, 0, &ApplyThread, nullptr, 0, nullptr);

						app.SetIcon (IDI_MAIN);
						app.TraySetInfo (_r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (IDI_MAIN), GetSystemMetrics (SM_CXSMICON)), APP_NAME);

						SetDlgItemText (app.GetHWND (), IDC_START_BTN, I18N (&app, IDS_TRAY_STOP, 0));

						_wfp_setevents (TRUE); // enable dropped packets logging

						SetEvent (config.apply_evt); // apply filters

						return;
					}
				}
			}
		}
	}

	app.TrayPopup (NIIF_ERROR | (app.ConfigGet (L"IsNotificationsSilent", FALSE).AsBool () ? NIIF_NOSOUND : 0), APP_NAME, I18N (&app, IDS_STATUS_FILTERS_FAILED, 0));
}

VOID _wfp_stop ()
{
	DWORD result = 0;

	if (config.hengine)
	{
		if (config.hthread)
		{
			SetEvent (config.stop_evt); // stop thread
			config.hthread = nullptr;
		}

		// net events unsubscribe (win7 and above)
		if (_r_sys_validversion (6, 1))
		{
			if (config.hevent)
			{
				FWPMNEU0 _FwpmNetEventUnsubscribe0 = (FWPMNEU0)GetProcAddress (GetModuleHandle (L"fwpuclnt.dll"), "FwpmNetEventUnsubscribe0");

				if (!_FwpmNetEventUnsubscribe0)
				{
					WDBG (L"GetProcAddress failed. Return value: 0x%.8lx.", GetLastError ());
				}
				else
				{
					result = _FwpmNetEventUnsubscribe0 (config.hengine, config.hevent);

					if (result != ERROR_SUCCESS)
						WDBG (L"FwpmNetEventUnsubscribe0 failed. Return value: 0x%.8lx.", result);
					else
						config.hevent = nullptr;
				}
			}

			FWP_VALUE val;
			SecureZeroMemory (&val, sizeof (val));

			val.type = FWP_UINT32;
			val.uint32 = 0;

			result = FwpmEngineSetOption (config.hengine, FWPM_ENGINE_COLLECT_NET_EVENTS, &val);

			if (result != ERROR_SUCCESS)
				WDBG (L"FwpmEngineSetOption failed. Return value: 0x%.8lx.", result);

		}

		// start transaction
		result = FwpmTransactionBegin (config.hengine, 0);

		if (result != ERROR_SUCCESS)
		{
			WDBG (L"FwpmTransactionBegin failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			_wfp_setevents (FALSE); // disable dropped packets logging
			_wfp_destroyfilters ();

			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpOutboundCallout4);
			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpOutboundCallout6);

			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpInboundCallout4);
			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpInboundCallout6);

			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpListenCallout4);
			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpListenCallout6);

			// old callouts
			FwpmCalloutDeleteByKey (config.hengine, &GUID_OldCalloutDelete1); // {7f35b047-7e29-43dc-bad8-6b4ebc843167}
			FwpmCalloutDeleteByKey (config.hengine, &GUID_OldCalloutDelete2); // {b0d553e2-c6a0-4a9a-aeb8-c752483ed62f}

			FwpmSubLayerDeleteByKey (config.hengine, &GUID_WfpSublayer);

			result = FwpmProviderDeleteByKey (config.hengine, &GUID_WfpProvider);

			if (result != ERROR_SUCCESS)
				WDBG (L"FwpmProviderDeleteByKey failed. Return value: 0x%.8lx.", result);

			result = FwpmTransactionCommit (config.hengine);

			if (result != ERROR_SUCCESS)
			{
				WDBG (L"FwpmTransactionCommit failed. Return value: 0x%.8lx.", result);
			}
			else
			{
				result = FwpmEngineClose (config.hengine);

				if (result != ERROR_SUCCESS)
					WDBG (L"FwpmEngineClose failed. Return value: 0x%.8lx.", result);
				else
					config.hengine = nullptr;

				app.SetIcon (IDI_INACTIVE);
				app.TraySetInfo (_r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (IDI_INACTIVE), GetSystemMetrics (SM_CXSMICON)), APP_NAME);

				SetDlgItemText (app.GetHWND (), IDC_START_BTN, I18N (&app, IDS_TRAY_START, 0));
			}
		}
	}
}

VOID addrule (LPCWSTR locale_sid, UINT locale_id, LPCWSTR cfg, BOOL defval, size_t group_id)
{
	ITEM_RULE_SYSTEM rule;

	rule.is_enabled = defval;
	rule.group_id = group_id;

	rule.locale_id = locale_id;
	StringCchCopy (rule.locale_sid, _countof (rule.locale_sid), locale_sid);

	StringCchCopy (rule.config, _countof (rule.config), cfg);

	rules_system.push_back (rule);
}

VOID addcolor (LPCWSTR locale_sid, UINT locale_id, LPCWSTR cfg, BOOL is_enabled, LPCWSTR config_color, COLORREF default_clr)
{
	ITEM_COLOR color;

	StringCchCopy (color.config, _countof (color.config), cfg);
	StringCchCopy (color.config_color, _countof (color.config_color), config_color);

	color.is_enabled = is_enabled;
	color.default_clr = default_clr;

	color.locale_id = locale_id;
	StringCchCopy (color.locale_sid, _countof (color.locale_sid), locale_sid);

	colors.push_back (color);
}

VOID _app_getprocesslist (std::vector<ITEM_PROCESS>* pvc)
{
	if (!config.is_admin || !pvc)
		return;

	if (pvc)
	{
		for (size_t i = 0; i < pvc->size (); i++)
			DeleteObject (pvc->at (i).hbmp); // free memory

		pvc->clear ();
	}

	NTSTATUS status = 0;

	OBJECT_ATTRIBUTES oa = {0};

	ULONG length = 0x4000;
	PVOID buffer = malloc (length);

	while (TRUE)
	{
		status = NtQuerySystemInformation (SystemProcessInformation, buffer, length, &length);

		if (status == 0xC0000023L /*STATUS_BUFFER_TOO_SMALL*/ || status == 0xc0000004 /*STATUS_INFO_LENGTH_MISMATCH*/)
		{
			PVOID buffer_new = realloc (buffer, length);

			if (!buffer_new)
			{
				break;
			}
			else
			{
				buffer = buffer_new;
			}
		}
		else
		{
			break;
		}
	}

	if (NT_SUCCESS (status))
	{
		PSYSTEM_PROCESS_INFORMATION spi = (PSYSTEM_PROCESS_INFORMATION)buffer;

		std::unordered_map<size_t, BOOL> checker;
		ULONG req_length = 0;

		do
		{
			if (!spi->UniqueProcessId) // skip "system idle process" with 0 pid
				continue;

			HANDLE hprocess = nullptr;
			CLIENT_ID client_id = {0};

			client_id.UniqueProcess = spi->UniqueProcessId;

			InitializeObjectAttributes (&oa, nullptr, 0, nullptr, nullptr);

			status = NtOpenProcess (&hprocess, PROCESS_QUERY_LIMITED_INFORMATION, &oa, &client_id);

			if (NT_SUCCESS (status))
			{
				struct
				{
					UNICODE_STRING str;
					WCHAR buffer[UNICODE_STRING_MAX_CHARS];
				} path;

				SecureZeroMemory (&path, sizeof (path));

				path.str.Length = UNICODE_STRING_MAX_CHARS * 2;
				path.str.MaximumLength = UNICODE_STRING_MAX_CHARS * 2;
				path.str.Buffer = &path.buffer[0];

				status = NtQueryInformationProcess (hprocess, ProcessImageFileNameWin32, &path, sizeof (path), &req_length);

				if (NT_SUCCESS (status))
				{
					const size_t hash = rstring (path.buffer).Hash ();

					if (applications.find (hash) == applications.end () && checker.find (hash) == checker.end ())
					{
						checker[hash] = TRUE;

						ITEM_PROCESS item;
						SecureZeroMemory (&item, sizeof (item));

						StringCchCopy (item.file_path, _countof (item.file_path), path.buffer);

						SHFILEINFO shfi = {0};
						SHGetFileInfo (item.file_path, 0, &shfi, sizeof (shfi), SHGFI_SMALLICON | SHGFI_ICON);

						RECT rc = {0};
						rc.right = GetSystemMetrics (SM_CXSMICON);
						rc.bottom = GetSystemMetrics (SM_CYSMICON);

						HDC hdc = GetDC (nullptr);
						HDC hmemdc = CreateCompatibleDC (hdc);
						HBITMAP hbmp = CreateCompatibleBitmap (hdc, rc.right, rc.bottom);
						ReleaseDC (nullptr, hdc);

						HGDIOBJ old_bmp = SelectObject (hmemdc, hbmp);
						_r_wnd_fillrect (hmemdc, &rc, GetSysColor (COLOR_MENU));
						DrawIconEx (hmemdc, 0, 0, shfi.hIcon, rc.right, rc.bottom, 0, nullptr, DI_NORMAL);
						SelectObject (hmemdc, old_bmp);

						DeleteDC (hmemdc);
						DestroyIcon (shfi.hIcon);

						item.hbmp = hbmp;

						PathCompactPathEx (item.display_path, item.file_path, _countof (item.display_path), 0);

						pvc->push_back (item);
					}
				}
				else
				{
					//WDBG (L"NtQueryInformationProcess failed. Return value: 0x%.8lx. (%s)", status, spi->ImageName.Buffer);
				}

				NtClose (hprocess);
			}
			else
			{
				WDBG (L"NtOpenProcess failed. Return value: 0x%.8lx. (%s)", status, spi->ImageName.Buffer);
			}
		}
		while ((spi = ((spi->NextEntryOffset ? (PSYSTEM_PROCESS_INFORMATION)((PCHAR)(spi)+(spi)->NextEntryOffset) : nullptr))) != nullptr);
	}
	else
	{
		WDBG (L"NtQuerySystemInformation failed. Return value: 0x%.8lx.", status);
	}

	free (buffer); // free the allocated buffer
}

VOID _app_profileload (HWND hwnd)
{
	_R_SPINLOCK (config.lock_profile);

	// clear all
	applications.clear ();
	colors.clear ();
	rules_system.clear ();

	_r_listview_deleteallitems (hwnd, IDC_LISTVIEW);

	// load rules
	_app_loadrules ();

	// load applications
	{
		pugi::xml_document doc;

		if (doc.load_file (config.config_path, pugi::parse_default, pugi::encoding_auto))
		{
			pugi::xml_node root = doc.child (L"root");

			if (root)
			{
				for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
				{
					_app_addapplication (hwnd, item.attribute (L"path").as_string (), item.attribute (L"is_silent").as_int (), item.attribute (L"is_enabled").as_bool ());
				}
			}
		}
	}

	// set default rules
	addrule (L"IDS_RULE_DHCP", IDS_RULE_DHCP, L"AllowDhcpService", TRUE, 0);
	addrule (L"IDS_RULE_DNS", IDS_RULE_DNS, L"AllowDnsService", TRUE, 0);
	addrule (L"IDS_RULE_NETWORKDISCOVERY", IDS_RULE_NETWORKDISCOVERY, L"AllowNetworkDiscoveryService", TRUE, 0);
	addrule (L"IDS_RULE_NTP", IDS_RULE_NTP, L"AllowNtpService", TRUE, 0);
	addrule (L"IDS_RULE_SNMP", IDS_RULE_SNMP, L"AllowSnmpService", FALSE, 0);
	addrule (L"IDS_RULE_SSDP", IDS_RULE_SSDP, L"AllowSsdpService", TRUE, 0);
	addrule (L"IDS_RULE_WUAUSERV", IDS_RULE_WUAUSERV, L"AllowWindowUpdateService", FALSE, 0);
	addrule (L"IDS_RULE_OUTBOUND_ICMP", IDS_RULE_OUTBOUND_ICMP, L"AllowOutboundIcmp", TRUE, 1);
	addrule (L"IDS_RULE_INBOUND_ICMP", IDS_RULE_INBOUND_ICMP, L"AllowInboundIcmp", FALSE, 1);
	addrule (L"IDS_RULE_INBOUND", IDS_RULE_INBOUND, L"AllowInboundConnections", FALSE, 1);

	// set default colors
	addcolor (L"IDS_HIGHLIGHT_SYSTEM", IDS_HIGHLIGHT_SYSTEM, L"IsHighlightSystem", TRUE, L"ColorSystem", LISTVIEW_COLOR_SYSTEM);
	addcolor (L"IDS_HIGHLIGHT_INVALID", IDS_HIGHLIGHT_INVALID, L"IsHighlightInvalid", TRUE, L"ColorInvalid", LISTVIEW_COLOR_INVALID);
	addcolor (L"IDS_HIGHLIGHT_NETWORK", IDS_HIGHLIGHT_NETWORK, L"IsHighlightNetwork", TRUE, L"ColorNetwork", LISTVIEW_COLOR_NETWORK);
	addcolor (L"IDS_HIGHLIGHT_SILENT", IDS_HIGHLIGHT_SILENT, L"IsHighlightSilent", TRUE, L"ColorSilent", LISTVIEW_COLOR_SILENT);

	_R_SPINUNLOCK (config.lock_profile);
}

BOOL initializer_callback (HWND hwnd, DWORD msg, LPVOID, LPVOID)
{
	switch (msg)
	{
		case _RM_INITIALIZE:
		{
			// set window icon indicator
			app.SetIcon (app.ConfigGet (L"IsFiltersEnabled", FALSE).AsBool () ? IDI_MAIN : IDI_INACTIVE);

			// init tray icon
			app.TrayCreate (UID, WM_TRAYICON, _r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (app.ConfigGet (L"IsFiltersEnabled", FALSE).AsBool () ? IDI_MAIN : IDI_INACTIVE), GetSystemMetrics (SM_CXSMICON)));

			// load profile
			_app_profileload (hwnd);
			_app_listviewsort (hwnd);

			if (app.ConfigGet (L"IsFiltersEnabled", FALSE).AsBool ())
			{
				if (!config.hengine && app.ConfigGet (L"IsFiltersEnabled", FALSE).AsBool ())
				{
					_wfp_start (); // install filters
				}
				else
				{
					_wfp_setevents (TRUE); // enable dropped packets logging (win7 and above)
					SetEvent (config.apply_evt); // apply filters
				}
			}

			CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (app.ConfigGet (L"AlwaysOnTop", FALSE).AsBool () ? MF_CHECKED : MF_UNCHECKED));

			EnableMenuItem (GetMenu (hwnd), IDM_LOGCLEAR, MF_BYCOMMAND | ((config.hlog == nullptr || config.hlog == INVALID_HANDLE_VALUE) ? MF_DISABLED : MF_ENABLED));

			CheckMenuRadioItem (GetMenu (hwnd), IDM_TRAY_MODEWHITELIST, IDM_TRAY_MODETRUSTNOONE, IDM_TRAY_MODEWHITELIST + app.ConfigGet (L"Mode", Whitelist).AsUint (), MF_BYCOMMAND);

			// append system rules
			{
				HMENU submenu = GetSubMenu (GetSubMenu (GetMenu (hwnd), 3), 2);

				// clear menu
				for (UINT i = 0;; i++)
				{
					if (!DeleteMenu (submenu, IDM_RULES_SYSTEM + i, MF_BYCOMMAND))
					{
						DeleteMenu (submenu, 0, MF_BYPOSITION); // delete separator
						break;
					}
				}

				size_t prev_id = 0;

				for (size_t i = 0; i < rules_system.size (); i++)
				{
					if (rules_system.at (i).group_id != prev_id)
						AppendMenu (submenu, MF_SEPARATOR, 0, nullptr);

					AppendMenu (submenu, MF_STRING, IDM_RULES_SYSTEM + i, I18N (&app, rules_system.at (i).locale_id, rules_system.at (i).locale_sid));

					if (app.ConfigGet (rules_system.at (i).config, rules_system.at (i).is_enabled).AsBool ())
						CheckMenuItem (submenu, IDM_RULES_SYSTEM + UINT (i), MF_BYCOMMAND | MF_CHECKED);

					prev_id = rules_system.at (i).group_id;
				}
			}

			// append special rules
			{
				HMENU submenu = GetSubMenu (GetSubMenu (GetMenu (hwnd), 3), 3);

				// clear menu
				for (UINT i = 0;; i++)
				{
					if (!DeleteMenu (submenu, IDM_RULES_SPECIAL + i, MF_BYCOMMAND))
					{
						DeleteMenu (submenu, 0, MF_BYPOSITION); // delete separator
						break;
					}
				}

				for (size_t i = 0; i < rules_special.size (); i++)
				{
					AppendMenu (submenu, MF_STRING, IDM_RULES_SPECIAL + i, rules_special.at (i).name);

					if (rules_special.at (i).is_enabled)
						CheckMenuItem (submenu, IDM_RULES_SPECIAL + UINT (i), MF_BYCOMMAND | MF_CHECKED);
				}
			}

			CheckMenuItem (GetMenu (hwnd), IDM_ENABLELOG_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsLogEnabled", FALSE).AsBool () ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem (GetMenu (hwnd), IDM_ENABLENOTIFICATIONS_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool () ? MF_CHECKED : MF_UNCHECKED));
			CheckMenuItem (GetMenu (hwnd), IDM_ENABLEAPPSCOLLECTOR_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsAppsCollectorEnabled", TRUE).AsBool () ? MF_CHECKED : MF_UNCHECKED));

			if (!config.is_admin || !_r_sys_validversion (6, 1))
			{
				EnableMenuItem (GetMenu (hwnd), IDM_ENABLELOG_CHK, MF_BYCOMMAND | MF_DISABLED);
				EnableMenuItem (GetMenu (hwnd), IDM_ENABLENOTIFICATIONS_CHK, MF_BYCOMMAND | MF_DISABLED);
				EnableMenuItem (GetMenu (hwnd), IDM_ENABLEAPPSCOLLECTOR_CHK, MF_BYCOMMAND | MF_DISABLED);
			}

			break;
		}

		case _RM_LOCALIZE:
		{
			HMENU menu = GetMenu (hwnd);

			app.LocaleMenu (menu, I18N (&app, IDS_FILE, 0), 0, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_SETTINGS, 0) + L"\tCtrl+P", IDM_SETTINGS, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_EXIT, 0) + L"\tAlt+F4", IDM_EXIT, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_EDIT, 0), 1, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_PURGEN, 0) + L"\tCtrl+Del", IDM_PURGEN, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_FIND, 0) + L"\tCtrl+F", IDM_FIND, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_FINDNEXT, 0) + L"\tF3", IDM_FINDNEXT, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_REFRESH, 0) + L"\tF5", IDM_REFRESH, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_VIEW, 0), 2, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_ALWAYSONTOP_CHK, 0), IDM_ALWAYSONTOP_CHK, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 2), I18N (&app, IDS_ICONS, 0), 2, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_ICONSSMALL, 0), IDM_ICONSSMALL, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ICONSLARGE, 0), IDM_ICONSLARGE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ICONSISHIDDEN, 0), IDM_ICONSISHIDDEN, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 2), I18N (&app, IDS_SORT, 0), 3, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_SORTBYFNAME, 0), IDM_SORTBYFNAME, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_SORTBYFDIR, 0), IDM_SORTBYFDIR, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_SORTISDESCEND, 0), IDM_SORTISDESCEND, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 2), I18N (&app, IDS_LANGUAGE, 0), 5, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_SETTINGS, 0), 3, TRUE);

			app.LocaleMenu (GetSubMenu (menu, 3), I18N (&app, IDS_TRAY_MODE, 0), 0, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_MODE_WHITELIST, 0), IDM_TRAY_MODEWHITELIST, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_MODE_BLACKLIST, 0), IDM_TRAY_MODEBLACKLIST, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_MODE_TRUSTNOONE, 0), IDM_TRAY_MODETRUSTNOONE, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 3), I18N (&app, IDS_TRAY_SYSTEM_RULES, 0), 2, TRUE);
			app.LocaleMenu (GetSubMenu (menu, 3), I18N (&app, IDS_TRAY_SPECIAL_RULES, 0), 3, TRUE);
			app.LocaleMenu (GetSubMenu (menu, 3), I18N (&app, IDS_TRAY_LOG, 0), 5, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_ENABLELOG_CHK, 0), IDM_ENABLELOG_CHK, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ENABLENOTIFICATIONS_CHK, 0), IDM_ENABLENOTIFICATIONS_CHK, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ENABLEAPPSCOLLECTOR_CHK, 0), IDM_ENABLEAPPSCOLLECTOR_CHK, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_LOGSHOW, 0) + L"\tCtrl+I", IDM_LOGSHOW, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_LOGCLEAR, 0) + L"\tCtrl+X", IDM_LOGCLEAR, FALSE);

			// append system rules
			{
				HMENU submenu = GetSubMenu (GetSubMenu (GetMenu (hwnd), 3), 2);

				// clear menu
				for (UINT i = 0;; i++)
				{
					if (!DeleteMenu (submenu, IDM_RULES_SYSTEM + i, MF_BYCOMMAND))
					{
						DeleteMenu (submenu, 0, MF_BYPOSITION); // delete separator
						break;
					}
				}

				size_t prev_id = 0;

				for (size_t i = 0; i < rules_system.size (); i++)
				{
					if (rules_system.at (i).group_id != prev_id)
						AppendMenu (submenu, MF_SEPARATOR, 0, nullptr);

					AppendMenu (submenu, MF_STRING, IDM_RULES_SYSTEM + i, I18N (&app, rules_system.at (i).locale_id, rules_system.at (i).locale_sid));

					if (app.ConfigGet (rules_system.at (i).config, rules_system.at (i).is_enabled).AsBool ())
						CheckMenuItem (submenu, IDM_RULES_SYSTEM + UINT (i), MF_BYCOMMAND | MF_CHECKED);

					prev_id = rules_system.at (i).group_id;
				}
			}

			// append special rules
			{
				HMENU submenu = GetSubMenu (GetSubMenu (GetMenu (hwnd), 3), 3);

				// clear menu
				for (UINT i = 0;; i++)
				{
					if (!DeleteMenu (submenu, IDM_RULES_SPECIAL + i, MF_BYCOMMAND))
					{
						DeleteMenu (submenu, 0, MF_BYPOSITION); // delete separator
						break;
					}
				}

				for (size_t i = 0; i < rules_special.size (); i++)
				{
					AppendMenu (submenu, MF_STRING, IDM_RULES_SPECIAL + i, rules_special.at (i).name);

					if (rules_special.at (i).is_enabled)
						CheckMenuItem (submenu, IDM_RULES_SPECIAL + UINT (i), MF_BYCOMMAND | MF_CHECKED);
				}
			}

			app.LocaleMenu (menu, I18N (&app, IDS_HELP, 0), 4, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_WEBSITE, 0), IDM_WEBSITE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_DONATE, 0), IDM_DONATE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_CHECKUPDATES, 0), IDM_CHECKUPDATES, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ABOUT, 0), IDM_ABOUT, FALSE);

			app.LocaleEnum ((HWND)GetSubMenu (menu, 2), 5, TRUE, IDM_DEFAULT); // enum localizations

			SetDlgItemText (hwnd, IDC_START_BTN, I18N (&app, (config.hengine ? IDS_TRAY_STOP : IDS_TRAY_START), config.hengine ? L"IDS_TRAY_STOP" : L"IDS_TRAY_START"));
			SetDlgItemText (hwnd, IDC_SETTINGS_BTN, I18N (&app, IDS_SETTINGS, 0));
			SetDlgItemText (hwnd, IDC_EXIT_BTN, I18N (&app, IDS_EXIT, 0));

			_r_wnd_addstyle (hwnd, IDC_START_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_SETTINGS_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_EXIT_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

			_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);

			_app_refreshstatus (hwnd); // refresh statusbar

			SendDlgItemMessage (hwnd, IDC_LISTVIEW, (LVM_FIRST + 84), 0, 0); // LVM_RESETEMPTYTEXT 

			break;
		}

		case _RM_UNINITIALIZE:
		{
			_wfp_setevents (FALSE); // disable dropped packets logging (win7 and above)
			app.TrayDestroy (UID);

			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK EditorProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	static ITEM_RULE_SPECIAL* ptr = nullptr;

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			ptr = (ITEM_RULE_SPECIAL*)lparam;

			// configure window
			_r_wnd_center (hwnd);

			// localize window
			SetWindowText (hwnd, I18N (&app, IDS_EDITOR, 0));

			SetDlgItemText (hwnd, IDC_NAME, I18N (&app, IDS_NAME, 0));
			SetDlgItemText (hwnd, IDC_DIRECTION, I18N (&app, IDS_DIRECTION, 0));
			SetDlgItemText (hwnd, IDC_ACTION, I18N (&app, IDS_ACTION, 0));
			SetDlgItemText (hwnd, IDC_RULES, I18N (&app, IDS_RULE, 0));

			SetDlgItemText (hwnd, IDC_RULES_LINKS, I18N (&app, IDS_RULES_LINKS, 0));

			SetDlgItemText (hwnd, IDC_ENABLED_CHK, I18N (&app, IDS_ENABLED_CHK, 0));

			SetDlgItemText (hwnd, IDC_APPLY, I18N (&app, IDS_APPLY, 0));
			SetDlgItemText (hwnd, IDC_CLOSE, I18N (&app, IDS_CLOSE, 0));

			_r_wnd_addstyle (hwnd, IDC_APPLY, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_CLOSE, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

			// set data
			SetDlgItemText (hwnd, IDC_NAME_EDIT, ptr->name);
			SetDlgItemText (hwnd, IDC_RULES_EDIT, ptr->rule);

			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_INSERTSTRING, 0, (LPARAM)I18N (&app, IDS_DIRECTION_1, 0).GetString ());
			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_INSERTSTRING, 1, (LPARAM)I18N (&app, IDS_DIRECTION_2, 0).GetString ());
			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_INSERTSTRING, 2, (LPARAM)I18N (&app, IDS_DIRECTION_3, 0).GetString ());

			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_SETCURSEL, (WPARAM)ptr->direction, 0);

			SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_INSERTSTRING, 0, (LPARAM)I18N (&app, IDS_ACTION_1, 0).GetString ());
			SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_INSERTSTRING, 1, (LPARAM)I18N (&app, IDS_ACTION_2, 0).GetString ());

			SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_SETCURSEL, (WPARAM)ptr->is_block, 0);

			CheckDlgButton (hwnd, IDC_ENABLED_CHK, ptr->is_enabled ? BST_CHECKED : BST_UNCHECKED);

			// set limitation
			SendDlgItemMessage (hwnd, IDC_NAME_EDIT, EM_LIMITTEXT, _countof (ptr->name) - 1, 0);
			SendDlgItemMessage (hwnd, IDC_RULES_EDIT, EM_LIMITTEXT, _countof (ptr->rule) - 1, 0);

			_r_ctrl_enable (hwnd, IDC_APPLY, FALSE); // disable apply button

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					if (nmlp->idFrom == IDC_RULES_LINKS)
					{
						PNMLINK nmlink = (PNMLINK)lparam;

						if (nmlink->item.szUrl)
							ShellExecute (hwnd, nullptr, nmlink->item.szUrl, nullptr, nullptr, SW_SHOWDEFAULT);
					}

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			{
				BOOL is_button = (GetWindowLongPtr (GetDlgItem (hwnd, LOWORD (wparam)), GWL_STYLE) & (BS_CHECKBOX | BS_RADIOBUTTON)) != 0;

				if ((HIWORD (wparam) == BN_CLICKED && is_button) || HIWORD (wparam) == EN_CHANGE || HIWORD (wparam) == CBN_SELENDOK)
				{
					BOOL is_enable = (SendDlgItemMessage (hwnd, IDC_NAME_EDIT, WM_GETTEXTLENGTH, 0, 0) > 0) && (SendDlgItemMessage (hwnd, IDC_RULES_EDIT, WM_GETTEXTLENGTH, 0, 0) > 0);

					_r_ctrl_enable (hwnd, IDC_APPLY, is_enable); // enable apply button

					return FALSE;
				}
			}

			switch (LOWORD (wparam))
			{
				case IDOK: // process Enter key
				case IDC_APPLY:
				{
					StringCchCopy (ptr->name, _countof (ptr->name), _r_ctrl_gettext (hwnd, IDC_NAME_EDIT));
					StringCchCopy (ptr->rule, _countof (ptr->rule), _r_ctrl_gettext (hwnd, IDC_RULES_EDIT));

					ptr->direction = (EnumDirection)SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_GETCURSEL, 0, 0);
					ptr->is_block = (BOOL)SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_GETCURSEL, 0, 0);
					ptr->is_enabled = IsDlgButtonChecked (hwnd, IDC_ENABLED_CHK) == BST_CHECKED;

					EndDialog (hwnd, 1);

					break;
				}

				case IDCANCEL: // process Esc key
				case IDC_CLOSE:
				{
					EndDialog (hwnd, 0);
					break;
				}

				case IDC_RULES_HELP:
				{
					_r_ctrl_showtip (hwnd, IDC_RULES_EDIT, nullptr, I18N (&app, IDS_RULES_HELP, 0), 0);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

VOID SetIconsSize (HWND hwnd)
{
	HIMAGELIST h = nullptr;

	const BOOL is_large = app.ConfigGet (L"IsLargeIcons", FALSE).AsBool ();
	const BOOL is_iconshidden = app.ConfigGet (L"IsIconsHidden", FALSE).AsBool ();

	if (!is_iconshidden)
		SHGetImageList (is_large ? SHIL_LARGE : SHIL_SMALL, IID_IImageList, (LPVOID*)&h);

	SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)h);
	SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)h);

	SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SCROLL, 0, GetScrollPos (GetDlgItem (hwnd, IDC_LISTVIEW), SB_VERT)); // scroll-resize-HACK!!!

	CheckMenuRadioItem (GetMenu (hwnd), IDM_ICONSSMALL, IDM_ICONSLARGE, (is_large ? IDM_ICONSLARGE : IDM_ICONSSMALL), MF_BYCOMMAND);
	CheckMenuItem (GetMenu (hwnd), IDM_ICONSISHIDDEN, MF_BYCOMMAND | (is_iconshidden ? MF_CHECKED : MF_UNCHECKED));

	_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);
}

VOID ShowItem (HWND hwnd, UINT ctrl_id, size_t item)
{
	if (item == LAST_VALUE)
		return;

	ListView_SetItemState (GetDlgItem (hwnd, ctrl_id), -1, 0, LVIS_SELECTED); // deselect all
	ListView_SetItemState (GetDlgItem (hwnd, ctrl_id), item, LVIS_SELECTED, LVIS_SELECTED); // select item

	SendDlgItemMessage (hwnd, ctrl_id, LVM_ENSUREVISIBLE, item, TRUE); // ensure him visible
}

BOOL settings_callback (HWND hwnd, DWORD msg, LPVOID lpdata1, LPVOID lpdata2)
{
	PAPPLICATION_PAGE const page = (PAPPLICATION_PAGE)lpdata2;

	switch (msg)
	{
		case _RM_INITIALIZE:
		{
			// localize titles
			SetDlgItemText (hwnd, IDC_TITLE_1, I18N (&app, IDS_TITLE_1, 0));
			SetDlgItemText (hwnd, IDC_TITLE_2, I18N (&app, IDS_TITLE_2, 0));
			SetDlgItemText (hwnd, IDC_TITLE_3, I18N (&app, IDS_TITLE_3, 0));
			SetDlgItemText (hwnd, IDC_TITLE_4, I18N (&app, IDS_TITLE_4, 0));
			SetDlgItemText (hwnd, IDC_TITLE_5, I18N (&app, IDS_TITLE_5, 0));
			SetDlgItemText (hwnd, IDC_TITLE_6, I18N (&app, IDS_TITLE_6, 0));

			switch (page->dlg_id)
			{
				case IDD_SETTINGS_1:
				{
					// localize
					SetDlgItemText (hwnd, IDC_ALWAYSONTOP_CHK, I18N (&app, IDS_ALWAYSONTOP_CHK, 0));
					SetDlgItemText (hwnd, IDC_LOADONSTARTUP_CHK, I18N (&app, IDS_LOADONSTARTUP_CHK, 0));
					SetDlgItemText (hwnd, IDC_STARTMINIMIZED_CHK, I18N (&app, IDS_STARTMINIMIZED_CHK, 0));
					SetDlgItemText (hwnd, IDC_SKIPUACWARNING_CHK, I18N (&app, IDS_SKIPUACWARNING_CHK, 0));
					SetDlgItemText (hwnd, IDC_CHECKUPDATES_CHK, I18N (&app, IDS_CHECKUPDATES_CHK, 0));

					SetDlgItemText (hwnd, IDC_LANGUAGE_HINT, I18N (&app, IDS_LANGUAGE_HINT, 0));

					if (!config.is_admin || !app.IsVistaOrLater ())
						_r_ctrl_enable (hwnd, IDC_SKIPUACWARNING_CHK, FALSE);

					CheckDlgButton (hwnd, IDC_ALWAYSONTOP_CHK, app.ConfigGet (L"AlwaysOnTop", FALSE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_LOADONSTARTUP_CHK, app.AutorunIsPresent () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_STARTMINIMIZED_CHK, app.ConfigGet (L"StartMinimized", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
#ifdef _APP_HAVE_SKIPUAC
					if (config.is_admin)
						CheckDlgButton (hwnd, IDC_SKIPUACWARNING_CHK, app.SkipUacIsPresent (FALSE) ? BST_CHECKED : BST_UNCHECKED);
#endif // _APP_HAVE_SKIPUAC
					CheckDlgButton (hwnd, IDC_CHECKUPDATES_CHK, app.ConfigGet (L"CheckUpdates", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					app.LocaleEnum (hwnd, IDC_LANGUAGE, FALSE, 0);

					SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR)SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0)); // check on save

					break;
				}

				case IDD_SETTINGS_2:
				{
					// localize
					SetDlgItemText (hwnd, IDC_USEBLOCKLIST_CHK, I18N (&app, IDS_USEBLOCKLIST_CHK, 0));
					SetDlgItemText (hwnd, IDC_USEBLOCKLIST_HINT, I18N (&app, IDS_USEBLOCKLIST_HINT, 0));

					SetDlgItemText (hwnd, IDC_USEUPDATECHECKING_CHK, _r_fmt (I18N (&app, IDS_USEUPDATECHECKING_CHK, 0), APP_NAME));
					SetDlgItemText (hwnd, IDC_USEUPDATECHECKING_HINT, _r_fmt (I18N (&app, IDS_USEUPDATECHECKING_HINT, 0), APP_NAME));

					SetDlgItemText (hwnd, IDC_INSTALLBOOTTIMEFILTERS_CHK, I18N (&app, IDS_INSTALLBOOTTIMEFILTERS_CHK, 0));
					SetDlgItemText (hwnd, IDC_INSTALLBOOTTIMEFILTERS_HINT, I18N (&app, IDS_INSTALLBOOTTIMEFILTERS_HINT, 0));

					CheckDlgButton (hwnd, IDC_USEBLOCKLIST_CHK, app.ConfigGet (L"UseBlocklist2", FALSE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_INSTALLBOOTTIMEFILTERS_CHK, app.ConfigGet (L"InstallBoottimeFilters", FALSE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_USEUPDATECHECKING_CHK, app.ConfigGet (L"AllowInternetAccess", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					break;
				}

				case IDD_SETTINGS_3:
				{
					// localize
					SetDlgItemText (hwnd, IDC_CONFIRMEXIT_CHK, I18N (&app, IDS_CONFIRMEXIT_CHK, 0));
					SetDlgItemText (hwnd, IDC_CONFIRMDELETE_CHK, I18N (&app, IDS_CONFIRMDELETE_CHK, 0));
					SetDlgItemText (hwnd, IDC_CONFIRMLOGCLEAR_CHK, I18N (&app, IDS_CONFIRMLOGCLEAR_CHK, 0));

					SetDlgItemText (hwnd, IDC_SHOWBALLOONTIPS_CHK, I18N (&app, IDS_SHOWBALLOONTIPS_CHK, 0));
					SetDlgItemText (hwnd, IDC_NOTIFICATIONSILENT_CHK, I18N (&app, IDS_NOTIFICATIONSILENT_CHK, 0));

					CheckDlgButton (hwnd, IDC_CONFIRMEXIT_CHK, app.ConfigGet (L"ConfirmExit", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_CONFIRMDELETE_CHK, app.ConfigGet (L"ConfirmDelete", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_CONFIRMLOGCLEAR_CHK, app.ConfigGet (L"ConfirmLogClear", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					CheckDlgButton (hwnd, IDC_SHOWBALLOONTIPS_CHK, app.ConfigGet (L"ShowBalloonTips", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_NOTIFICATIONSILENT_CHK, app.ConfigGet (L"IsNotificationsSilent", FALSE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					// configure listview
					_r_listview_setstyle (hwnd, IDC_COLORS, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

					_r_listview_deleteallitems (hwnd, IDC_COLORS);
					_r_listview_deleteallcolumns (hwnd, IDC_COLORS);

					_r_listview_addcolumn (hwnd, IDC_COLORS, nullptr, 95, 0, LVCFMT_LEFT);

					for (size_t i = 0; i < colors.size (); i++)
					{
						colors.at (i).clr = app.ConfigGet (colors.at (i).config_color, colors.at (i).default_clr).AsUlong ();

						_r_listview_additem (hwnd, IDC_COLORS, I18N (&app, colors.at (i).locale_id, colors.at (i).locale_sid), LAST_VALUE, 0, LAST_VALUE, LAST_VALUE, i);

						if (app.ConfigGet (colors.at (i).config, colors.at (i).is_enabled).AsBool ())
							_r_listview_setcheckstate (hwnd, IDC_COLORS, LAST_VALUE, TRUE);
					}

					_r_listview_resizeonecolumn (hwnd, IDC_COLORS);

					break;
				}

				case IDD_SETTINGS_4:
				{
					// localize
					SetDlgItemText (hwnd, IDC_ENABLELOG_CHK, I18N (&app, IDS_ENABLELOG_CHK, 0));
					SetDlgItemText (hwnd, IDC_LOGVIEWER_TITLE, I18N (&app, IDS_LOGVIEWER_TITLE, 0));

					SetDlgItemText (hwnd, IDC_ENABLENOTIFICATIONS_CHK, I18N (&app, IDS_ENABLENOTIFICATIONS_CHK, 0));
					SetDlgItemText (hwnd, IDC_NOTIFICATIONTIMEOUT_HINT, I18N (&app, IDS_NOTIFICATIONTIMEOUT_HINT, 0));

					SetDlgItemText (hwnd, IDC_ENABLEAPPSCOLLECTOR_CHK, I18N (&app, IDS_ENABLEAPPSCOLLECTOR_CHK, 0));

					if (!config.is_admin || !_r_sys_validversion (6, 1))
					{
						_r_ctrl_enable (hwnd, IDC_ENABLELOG_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_ENABLENOTIFICATIONS_CHK, FALSE);
						_r_ctrl_enable (hwnd, IDC_ENABLEAPPSCOLLECTOR_CHK, FALSE);
					}

					CheckDlgButton (hwnd, IDC_ENABLELOG_CHK, app.ConfigGet (L"IsLogEnabled", FALSE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					SetDlgItemText (hwnd, IDC_LOGPATH, app.ConfigGet (L"LogPath", L"%userprofile%\\wfptool.log"));
					SetDlgItemText (hwnd, IDC_LOGVIEWER, app.ConfigGet (L"LogViewer", L"notepad.exe"));

					CheckDlgButton (hwnd, IDC_ENABLENOTIFICATIONS_CHK, app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					SendDlgItemMessage (hwnd, IDC_NOTIFICATIONTIMEOUT, UDM_SETRANGE32, 1, 86400);
					SendDlgItemMessage (hwnd, IDC_NOTIFICATIONTIMEOUT, UDM_SETPOS32, 0, app.ConfigGet (L"NotificationsTimeout", 10).AsUint ());

					CheckDlgButton (hwnd, IDC_ENABLEAPPSCOLLECTOR_CHK, app.ConfigGet (L"IsAppsCollectorEnabled", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					_r_wnd_addstyle (hwnd, IDC_LOGPATH_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
					_r_wnd_addstyle (hwnd, IDC_LOGVIEWER_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

					SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_ENABLELOG_CHK, 0), 0);
					SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDC_ENABLENOTIFICATIONS_CHK, 0), 0);

					break;
				}

				case IDD_SETTINGS_5:
				{
					// configure listview
					_r_listview_setstyle (hwnd, IDC_COMMON, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

					_r_listview_deleteallgroups (hwnd, IDC_COMMON);
					_r_listview_deleteallitems (hwnd, IDC_COMMON);
					_r_listview_deleteallcolumns (hwnd, IDC_COMMON);

					_r_listview_addcolumn (hwnd, IDC_COMMON, nullptr, 95, 0, LVCFMT_LEFT);

					_r_listview_addgroup (hwnd, IDC_COMMON, 0, I18N (&app, IDS_GROUP_1, 0));
					_r_listview_addgroup (hwnd, IDC_COMMON, 1, I18N (&app, IDS_GROUP_2, 0));

					for (size_t i = 0; i < rules_system.size (); i++)
					{
						_r_listview_additem (hwnd, IDC_COMMON, I18N (&app, rules_system.at (i).locale_id, rules_system.at (i).locale_sid), i, 0, LAST_VALUE, rules_system.at (i).group_id, i);

						_r_listview_setcheckstate (hwnd, IDC_COMMON, i, app.ConfigGet (rules_system.at (i).config, rules_system.at (i).is_enabled).AsBool ());
					}

					_r_listview_resizeonecolumn (hwnd, IDC_COMMON);

					break;
				}

				case IDD_SETTINGS_6:
				{
					// configure listview
					_r_listview_setstyle (hwnd, IDC_EDITOR, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

					const size_t item = (size_t)SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);

					_r_listview_deleteallitems (hwnd, IDC_EDITOR);
					_r_listview_deleteallcolumns (hwnd, IDC_EDITOR);

					_r_listview_addcolumn (hwnd, IDC_EDITOR, I18N (&app, IDS_NAME, 0), 30, 1, LVCFMT_LEFT);
					_r_listview_addcolumn (hwnd, IDC_EDITOR, I18N (&app, IDS_RULE, 0), 40, 2, LVCFMT_LEFT);
					_r_listview_addcolumn (hwnd, IDC_EDITOR, I18N (&app, IDS_DIRECTION, 0), 25, 3, LVCFMT_RIGHT);

					for (size_t i = 0; i < rules_special.size (); i++)
					{
						_r_listview_additem (hwnd, IDC_EDITOR, rules_special.at (i).name, i, 0, LAST_VALUE, LAST_VALUE, i);
						_r_listview_additem (hwnd, IDC_EDITOR, rules_special.at (i).rule, i, 1);

						if (rules_special.at (i).direction == Out)
							_r_listview_additem (hwnd, IDC_EDITOR, I18N (&app, IDS_DIRECTION_1, 0), i, 2);
						else if (rules_special.at (i).direction == In)
							_r_listview_additem (hwnd, IDC_EDITOR, I18N (&app, IDS_DIRECTION_2, 0), i, 2);
						else if (rules_special.at (i).direction == Both)
							_r_listview_additem (hwnd, IDC_EDITOR, I18N (&app, IDS_DIRECTION_3, 0), i, 2);

						_r_listview_setcheckstate (hwnd, IDC_EDITOR, i, rules_special.at (i).is_enabled);
					}

					ShowItem (hwnd, IDC_EDITOR, item);

					break;
				}
			}

			break;
		}

		case _RM_MESSAGE:
		{
			LPMSG pmsg = (LPMSG)lpdata1;

			switch (pmsg->message)
			{
				case WM_NOTIFY:
				{
					LPNMHDR nmlp = (LPNMHDR)pmsg->lParam;

					switch (nmlp->code)
					{
						case NM_CUSTOMDRAW:
						{
							LONG result = CDRF_DODEFAULT;
							LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)pmsg->lParam;

							if (nmlp->idFrom != IDC_COLORS && nmlp->idFrom != IDC_EDITOR)
								break;

							switch (lpnmlv->nmcd.dwDrawStage)
							{
								case CDDS_PREPAINT:
								{
									result = CDRF_NOTIFYITEMDRAW;
									break;
								}

								case CDDS_ITEMPREPAINT:
								{
									if (nmlp->idFrom == IDC_COLORS)
									{
										const size_t idx = lpnmlv->nmcd.lItemlParam;

										_r_wnd_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, colors.at (idx).clr);
										lpnmlv->clrTextBk = colors.at (idx).clr;

										result = CDRF_NEWFONT;
									}
									else if (nmlp->idFrom == IDC_EDITOR)
									{
										const size_t idx = lpnmlv->nmcd.dwItemSpec;

										COLORREF clr = LISTVIEW_COLOR_ALLOW;

										if (rules_special.at (idx).is_block)
											clr = LISTVIEW_COLOR_BLOCK;

										_r_wnd_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, clr);
										lpnmlv->clrTextBk = clr;

										result = CDRF_NEWFONT;
									}

									break;
								}
							}

							SetWindowLongPtr (hwnd, DWLP_MSGRESULT, result);
							return TRUE;
						}

						case NM_DBLCLK:
						{
							LPNMITEMACTIVATE lpnmlv = (LPNMITEMACTIVATE)pmsg->lParam;

							if (lpnmlv->iItem != -1)
							{
								if (nmlp->idFrom == IDC_COLORS)
								{
									size_t idx = _r_listview_getlparam (hwnd, IDC_COLORS, lpnmlv->iItem);

									CHOOSECOLOR cc = {0};
									COLORREF cust[16] = {LISTVIEW_COLOR_SYSTEM, LISTVIEW_COLOR_INVALID, LISTVIEW_COLOR_NETWORK, LISTVIEW_COLOR_SILENT};

									cc.lStructSize = sizeof (cc);
									cc.Flags = CC_RGBINIT | CC_FULLOPEN;
									cc.hwndOwner = hwnd;
									cc.lpCustColors = cust;
									cc.rgbResult = colors.at (idx).clr;

									if (ChooseColor (&cc))
									{
										colors.at (idx).clr = cc.rgbResult;

										_r_ctrl_enable (GetParent (hwnd), IDC_APPLY, TRUE); // enable apply button (required!)
									}
								}
								else if (nmlp->idFrom == IDC_EDITOR)
								{
									SendMessage (hwnd, WM_COMMAND, MAKELPARAM (IDM_EDIT, 0), 0);
								}
							}

							break;
						}
					}

					break;
				}

				case WM_CONTEXTMENU:
				{
					UINT ctrl_id = GetDlgCtrlID ((HWND)pmsg->wParam);

					if (ctrl_id == IDC_EDITOR)
					{
						HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_EDITOR)), submenu = GetSubMenu (menu, 0);

						// localize
						app.LocaleMenu (submenu, I18N (&app, IDS_ADD, 0), IDM_ADD, FALSE);
						app.LocaleMenu (submenu, I18N (&app, IDS_EDIT2, 0), IDM_EDIT, FALSE);
						app.LocaleMenu (submenu, I18N (&app, IDS_DELETE, 0), IDM_DELETE, FALSE);

						if (!SendDlgItemMessage (hwnd, ctrl_id, LVM_GETSELECTEDCOUNT, 0, 0))
						{
							EnableMenuItem (submenu, IDM_EDIT, MF_BYCOMMAND | MF_DISABLED);
							EnableMenuItem (submenu, IDM_DELETE, MF_BYCOMMAND | MF_DISABLED);
						}

						POINT pt = {0};
						GetCursorPos (&pt);

						TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

						DestroyMenu (menu);
						DestroyMenu (submenu);
					}
				}

				case WM_COMMAND:
				{
					switch (LOWORD (pmsg->wParam))
					{
						case IDC_ENABLELOG_CHK:
						{
							UINT ctrl = LOWORD (pmsg->wParam);

							BOOL is_enabled = IsWindowEnabled (GetDlgItem (hwnd, ctrl)) && (IsDlgButtonChecked (hwnd, ctrl) == BST_CHECKED);

							_r_ctrl_enable (hwnd, IDC_LOGPATH, is_enabled); // input
							_r_ctrl_enable (hwnd, IDC_LOGPATH_BTN, is_enabled); // button
							_r_ctrl_enable (hwnd, IDC_LOGVIEWER, is_enabled); // input
							_r_ctrl_enable (hwnd, IDC_LOGVIEWER_BTN, is_enabled); // button

							break;
						}

						case IDC_ENABLENOTIFICATIONS_CHK:
						{
							UINT ctrl = LOWORD (pmsg->wParam);

							BOOL is_enabled = IsWindowEnabled (GetDlgItem (hwnd, ctrl)) && (IsDlgButtonChecked (hwnd, ctrl) == BST_CHECKED);

							EnableWindow ((HWND)SendDlgItemMessage (hwnd, IDC_NOTIFICATIONTIMEOUT, UDM_GETBUDDY, 0, 0), is_enabled);

							break;
						}

						case IDC_LOGPATH_BTN:
						{
							OPENFILENAME ofn = {0};

							WCHAR path[512] = {0};
							GetDlgItemText (hwnd, IDC_LOGPATH, path, _countof (path));
							StringCchCopy (path, _countof (path), _r_path_expand (path));

							ofn.lStructSize = sizeof (ofn);
							ofn.hwndOwner = hwnd;
							ofn.lpstrFile = path;
							ofn.nMaxFile = _countof (path);
							ofn.lpstrFileTitle = APP_NAME_SHORT;
							ofn.nMaxFile = _countof (path);
							ofn.lpstrFilter = L"*.log\0*.log\0\0";
							ofn.lpstrDefExt = L"log";
							ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;

							if (GetSaveFileName (&ofn))
								SetDlgItemText (hwnd, IDC_LOGPATH, _r_path_unexpand (path));

							break;
						}

						case IDC_LOGVIEWER_BTN:
						{
							OPENFILENAME ofn = {0};

							WCHAR path[512] = {0};
							GetDlgItemText (hwnd, IDC_LOGVIEWER, path, _countof (path));

							ofn.lStructSize = sizeof (ofn);
							ofn.hwndOwner = hwnd;
							ofn.lpstrFile = path;
							ofn.nMaxFile = _countof (path);
							ofn.lpstrFileTitle = APP_NAME_SHORT;
							ofn.nMaxFile = _countof (path);
							ofn.lpstrFilter = L"*.exe\0*.exe\0\0";
							ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_FORCESHOWHIDDEN;

							if (GetOpenFileName (&ofn))
								SetDlgItemText (hwnd, IDC_LOGVIEWER, path);

							break;
						}

						case IDM_ADD:
						{
							ITEM_RULE_SPECIAL* ptr = new ITEM_RULE_SPECIAL;

							if (ptr)
							{
								SecureZeroMemory (ptr, sizeof (ITEM_RULE_SPECIAL));

								// set defaults
								ptr->is_block = TRUE;
								ptr->is_enabled = TRUE;

								if (DialogBoxParam (nullptr, MAKEINTRESOURCE (IDD_EDITOR), hwnd, &EditorProc, (LPARAM)ptr))
								{
									rules_special.push_back (*ptr);
									settings_callback (page->hwnd, _RM_INITIALIZE, nullptr, page); // reinititalize page
								}

								delete ptr;
							}

							break;
						}

						case IDM_EDIT:
						{
							size_t item = (size_t)SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);

							if (item == LAST_VALUE)
								break;

							//ITEM_RULE_SPECIAL* ptr = &rules_special.at ((size_t)_r_listview_getlparam (hwnd, IDC_EDITOR, item));
							ITEM_RULE_SPECIAL* ptr = &rules_special.at (item);

							if (DialogBoxParam (nullptr, MAKEINTRESOURCE (IDD_EDITOR), hwnd, &EditorProc, (LPARAM)ptr))
							{
								settings_callback (page->hwnd, _RM_INITIALIZE, nullptr, page); // re-inititalize page
							}

							break;
						}

						case IDM_DELETE:
						{
							if (app.ConfigGet (L"ConfirmDelete", TRUE).AsBool () && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION, 0)) != IDYES)
								break;

							const size_t count = _r_listview_getitemcount (hwnd, IDC_EDITOR) - 1;

							for (size_t i = count; i != LAST_VALUE; i--)
							{
								if (ListView_GetItemState (GetDlgItem (hwnd, IDC_EDITOR), i, LVNI_SELECTED))
								{
									//size_t idx = _r_listview_getlparam (hwnd, IDC_EDITOR, i);

									rules_special.erase (rules_special.begin () + i);

									SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_DELETEITEM, i, 0);
								}
							}

							SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_REDRAWITEMS, 0, count); // redraw (required!)

							break;
						}
					}

					break;
				}
			}

			break;
		}

		case _RM_SAVE:
		{
			switch (page->dlg_id)
			{
				case IDD_SETTINGS_1:
				{
					app.ConfigSet (L"AlwaysOnTop", DWORD ((IsDlgButtonChecked (hwnd, IDC_ALWAYSONTOP_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.AutorunCreate (IsDlgButtonChecked (hwnd, IDC_LOADONSTARTUP_CHK) == BST_UNCHECKED);
					app.ConfigSet (L"StartMinimized", DWORD ((IsDlgButtonChecked (hwnd, IDC_STARTMINIMIZED_CHK) == BST_CHECKED) ? TRUE : FALSE));

#ifdef _APP_HAVE_SKIPUAC
					if (!_r_sys_uacstate ())
						app.SkipUacCreate (IsDlgButtonChecked (hwnd, IDC_SKIPUACWARNING_CHK) == BST_UNCHECKED);
#endif // _APP_HAVE_SKIPUAC

					app.ConfigSet (L"CheckUpdates", ((IsDlgButtonChecked (hwnd, IDC_CHECKUPDATES_CHK) == BST_CHECKED) ? TRUE : FALSE));

					// set language
					rstring buffer;

					if (SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0) >= 1)
						buffer = _r_ctrl_gettext (hwnd, IDC_LANGUAGE);

					app.ConfigSet (L"Language", buffer);

					if (GetWindowLongPtr (hwnd, GWLP_USERDATA) != (INT)SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0))
						return TRUE; // for restart

					break;
				}

				case IDD_SETTINGS_2:
				{
					app.ConfigSet (L"UseBlocklist2", DWORD ((IsDlgButtonChecked (hwnd, IDC_USEBLOCKLIST_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"AllowInternetAccess", DWORD ((IsDlgButtonChecked (hwnd, IDC_USEUPDATECHECKING_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"InstallBoottimeFilters", DWORD ((IsDlgButtonChecked (hwnd, IDC_INSTALLBOOTTIMEFILTERS_CHK) == BST_CHECKED) ? TRUE : FALSE));

					break;
				}

				case IDD_SETTINGS_3:
				{
					app.ConfigSet (L"ConfirmExit", DWORD ((IsDlgButtonChecked (hwnd, IDC_CONFIRMEXIT_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"ConfirmDelete", DWORD ((IsDlgButtonChecked (hwnd, IDC_CONFIRMDELETE_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"ConfirmLogClear", DWORD ((IsDlgButtonChecked (hwnd, IDC_CONFIRMLOGCLEAR_CHK) == BST_CHECKED) ? TRUE : FALSE));

					app.ConfigSet (L"ShowBalloonTips", DWORD ((IsDlgButtonChecked (hwnd, IDC_SHOWBALLOONTIPS_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"IsNotificationsSilent", DWORD ((IsDlgButtonChecked (hwnd, IDC_NOTIFICATIONSILENT_CHK) == BST_CHECKED) ? TRUE : FALSE));

					for (size_t i = 0; i < colors.size (); i++)
					{
						app.ConfigSet (colors.at (i).config, _r_listview_getcheckstate (hwnd, IDC_COLORS, i));
						app.ConfigSet (colors.at (i).config_color, colors.at (i).clr);
					}

					break;
				}

				case IDD_SETTINGS_4:
				{
					app.ConfigSet (L"IsLogEnabled", DWORD ((IsDlgButtonChecked (hwnd, IDC_ENABLELOG_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"LogPath", _r_ctrl_gettext (hwnd, IDC_LOGPATH));
					app.ConfigSet (L"LogViewer", _r_ctrl_gettext (hwnd, IDC_LOGVIEWER));

					app.ConfigSet (L"IsNotificationsEnabled", DWORD ((IsDlgButtonChecked (hwnd, IDC_ENABLENOTIFICATIONS_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"NotificationsTimeout", (DWORD)SendDlgItemMessage (hwnd, IDC_NOTIFICATIONTIMEOUT, UDM_GETPOS32, 0, 0));

					app.ConfigSet (L"IsAppsCollectorEnabled", DWORD ((IsDlgButtonChecked (hwnd, IDC_ENABLEAPPSCOLLECTOR_CHK) == BST_CHECKED) ? TRUE : FALSE));

					break;
				}

				case IDD_SETTINGS_5:
				{
					for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_COMMON); i++)
					{
						size_t idx = _r_listview_getlparam (hwnd, IDC_COMMON, i);

						app.ConfigSet (rules_system.at (idx).config, _r_listview_getcheckstate (hwnd, IDC_COMMON, i));
					}

					break;
				}

				case IDD_SETTINGS_6:
				{
					for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_EDITOR); i++)
					{
						rules_special.at (i).is_enabled = _r_listview_getcheckstate (hwnd, IDC_EDITOR, i);
					}

					// save profile
					_app_profilesave (app.GetHWND ());

					break;
				}
			}

			break;
		}

		case _RM_UNINITIALIZE:
		{
			_app_profileload (app.GetHWND ());
			break;
		}
	}

	return FALSE;
}

VOID ResizeWindow (HWND hwnd, INT width, INT height)
{
	RECT rc = {0};

	GetClientRect (GetDlgItem (hwnd, IDC_EXIT_BTN), &rc);
	INT button_width = rc.right;

	INT button_top = height - config.statusbar_height - app.GetDPI (1 + 34);

	SetWindowPos (GetDlgItem (hwnd, IDC_LISTVIEW), nullptr, 0, 0, width, height - config.statusbar_height - app.GetDPI (1 + 46), SWP_NOZORDER | SWP_NOACTIVATE);

	SetWindowPos (GetDlgItem (hwnd, IDC_START_BTN), nullptr, app.GetDPI (10), button_top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
	SetWindowPos (GetDlgItem (hwnd, IDC_SETTINGS_BTN), nullptr, width - app.GetDPI (10) - button_width - button_width - app.GetDPI (6), button_top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
	SetWindowPos (GetDlgItem (hwnd, IDC_EXIT_BTN), nullptr, width - app.GetDPI (10) - button_width, button_top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);

	// resize statusbar parts
	INT parts[] = {_R_PERCENT_VAL (50, width), -1};
	SendDlgItemMessage (hwnd, IDC_STATUSBAR, SB_SETPARTS, 2, (LPARAM)parts);

	// resize column width
	_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);

	// resize statusbar
	SendDlgItemMessage (hwnd, IDC_STATUSBAR, WM_SIZE, 0, 0);
}

INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_FINDMSGSTRING)
	{
		LPFINDREPLACE const lpfr = (LPFINDREPLACE)lparam;

		if ((lpfr->Flags & FR_DIALOGTERM) != 0)
		{
			config.hfind = nullptr;
		}
		else if ((lpfr->Flags & FR_FINDNEXT) != 0)
		{
			const size_t total = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);
			const INT start = (INT)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, (WPARAM)total - 1, LVNI_SELECTED | LVNI_DIRECTIONMASK | LVNI_BELOW) + 1;

			for (size_t i = start; i < total; i++)
			{
				const size_t hash = _r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

				ITEM_APPLICATION* const ptr = &applications[hash];

				if (StrStrI (ptr->full_path, lpfr->lpstrFindWhat) != nullptr)
				{
					ShowItem (hwnd, IDC_LISTVIEW, i);
					break;
				}
			}
		}

		return FALSE;
	}

	static DWORD max_width = 0;
	static DWORD max_height = 0;

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			// static initializer
			config.is_admin = _r_sys_adminstate ();
			config.apply_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);
			config.stop_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);
			config.wd_length = GetWindowsDirectory (config.windows_dir, _countof (config.windows_dir));
			StringCchPrintf (config.blocklist_path, _countof (config.blocklist_path), L"%s\\blocklist.xml", app.GetProfileDirectory ());
			StringCchPrintf (config.config_path, _countof (config.config_path), L"%s\\config.xml", app.GetProfileDirectory ());
			StringCchPrintf (config.rules_path, _countof (config.rules_path), L"%s\\rules.xml", app.GetProfileDirectory ());

			StringCchPrintf (config.svchost_path, _countof (config.svchost_path), L"%s\\system32\\svchost.exe", config.windows_dir);

			config.svchost_hash = rstring (config.svchost_path).Hash ();
			config.system_hash = rstring (L"System").Hash ();

			GetModuleFileName (app.GetHINSTANCE (), config.myself_path, _countof (config.myself_path));
			config.myself_hash = rstring (config.myself_path).Hash ();

			// set privileges
			if (config.is_admin)
				_r_sys_setprivilege (SE_DEBUG_NAME, TRUE);

			// configure listview
			_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);
			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, nullptr, 95, 0, LVCFMT_LEFT);

			SetIconsSize (hwnd);

			// configure button
			if (!config.is_admin)
				_r_ctrl_enable (hwnd, IDC_START_BTN, FALSE);

			// drag & drop support
			DragAcceptFiles (hwnd, TRUE);

			// resize support
			RECT rc = {0};
			GetClientRect (GetDlgItem (hwnd, IDC_STATUSBAR), &rc);
			config.statusbar_height = rc.bottom;

			GetWindowRect (hwnd, &rc);

			max_width = (rc.right - rc.left);
			max_height = (rc.bottom - rc.top);

			GetClientRect (hwnd, &rc);
			SendMessage (hwnd, WM_SIZE, 0, MAKELPARAM ((rc.right - rc.left), (rc.bottom - rc.top)));

			// settings
			app.AddSettingsPage (nullptr, IDD_SETTINGS_1, IDS_SETTINGS_1, L"IDS_SETTINGS_1", &settings_callback);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_2, IDS_SETTINGS_2, L"IDS_SETTINGS_2", &settings_callback);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_3, IDS_SETTINGS_3, L"IDS_SETTINGS_3", &settings_callback);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_4, IDS_TRAY_LOG, L"IDS_TRAY_LOG", &settings_callback);

			size_t page_id = app.AddSettingsPage (nullptr, 0, IDS_TRAY_RULES, L"IDS_TRAY_RULES", nullptr);

			app.AddSettingsPage (nullptr, IDD_SETTINGS_5, IDS_TRAY_SYSTEM_RULES, L"IDS_TRAY_SYSTEM_RULES", &settings_callback, page_id);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_6, IDS_TRAY_SPECIAL_RULES, L"IDS_TRAY_SPECIAL_RULES", &settings_callback, page_id);

			// startup parameters
			if (!wcsstr (GetCommandLine (), L"/minimized") && !app.ConfigGet (L"StartMinimized", TRUE).AsBool ())
				_r_wnd_toggle (hwnd, TRUE);

			SetFocus (nullptr);

			break;
		}

		case WM_DROPFILES:
		{
			UINT numfiles = DragQueryFile ((HDROP)wparam, 0xFFFFFFFF, nullptr, 0);
			size_t item = 0;

			for (UINT i = 0; i < numfiles; i++)
			{
				UINT lenname = DragQueryFile ((HDROP)wparam, i, nullptr, 0);

				LPWSTR file = new WCHAR[(lenname + 1) * sizeof (WCHAR)];

				DragQueryFile ((HDROP)wparam, i, file, lenname + 1);

				item = _app_addapplication (hwnd, file, 0, FALSE);

				delete[] file;
			}

			_app_listviewsort (hwnd);
			_app_profilesave (hwnd);

			ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, item));

			DragFinish ((HDROP)wparam);

			break;
		}

		case WM_CLOSE:
		{
			if (app.ConfigGet (L"ConfirmExit", TRUE).AsBool ())
			{
				WCHAR flag[64] = {0};
				WCHAR text[128] = {0};

				INT result = 0;
				BOOL is_flagchecked = 0;

				TASKDIALOGCONFIG tdc = {0};

				tdc.cbSize = sizeof (tdc);
				tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT | TDF_VERIFICATION_FLAG_CHECKED;
				tdc.hwndParent = hwnd;
				tdc.pszWindowTitle = APP_NAME;
				tdc.pfCallback = &_r_msg_callback;
				tdc.pszMainIcon = TD_INFORMATION_ICON;
				tdc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
				tdc.pszMainInstruction = text;
				tdc.pszVerificationText = flag;

				StringCchCopy (text, _countof (text), I18N (&app, IDS_QUESTION_EXIT, 0));
				StringCchCopy (flag, _countof (flag), I18N (&app, IDS_ALWAYSPERFORMTHISCHECK_CHK, 0));

				TaskDialogIndirect (&tdc, &result, nullptr, &is_flagchecked);

				if (result != IDYES)
					return TRUE;

				app.ConfigSet (L"ConfirmExit", is_flagchecked);
			}

			DestroyWindow (hwnd);

			break;
		}

		case WM_DESTROY:
		{
			SetEvent (config.stop_evt); // stop thread
			config.hthread = nullptr;

			PostQuitMessage (0);

			break;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			HDC dc = BeginPaint (hwnd, &ps);

			RECT rc = {0};
			GetWindowRect (GetDlgItem (hwnd, IDC_LISTVIEW), &rc);

			for (INT i = 0; i < rc.right; i++)
			{
				SetPixel (dc, i, rc.bottom - rc.top, GetSysColor (COLOR_APPWORKSPACE));
			}

			EndPaint (hwnd, &ps);

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CUSTOMDRAW:
				{
					if (nmlp->idFrom == IDC_LISTVIEW)
					{
						LONG result = CDRF_DODEFAULT;
						LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;

						switch (lpnmlv->nmcd.dwDrawStage)
						{
							case CDDS_PREPAINT:
							{
								result = CDRF_NOTIFYITEMDRAW;
								break;
							}

							case CDDS_ITEMPREPAINT:
							{
								const size_t hash = lpnmlv->nmcd.lItemlParam;

								if (hash)
								{
									ITEM_APPLICATION const * ptr = &applications[hash];

									COLORREF new_clr = 0;

									if (ptr->is_silent && app.ConfigGet (L"IsHighlightSilent", TRUE).AsBool ())
									{
										new_clr = app.ConfigGet (L"ColorSilent", LISTVIEW_COLOR_SILENT).AsUlong ();
									}
									else if ((ptr->is_system || (hash == config.system_hash)) && app.ConfigGet (L"IsHighlightSystem", TRUE).AsBool ())
									{
										new_clr = app.ConfigGet (L"ColorSystem", LISTVIEW_COLOR_SYSTEM).AsUlong ();
									}
									else if (ptr->is_network && app.ConfigGet (L"IsHighlightNetwork", TRUE).AsBool ())
									{
										new_clr = app.ConfigGet (L"ColorNetwork", LISTVIEW_COLOR_NETWORK).AsUlong ();
									}
									else if (app.ConfigGet (L"IsHighlightInvalid", TRUE).AsBool () && ((ptr->is_checked && !ptr->is_success) || (!ptr->is_checked && !_r_fs_exists (ptr->full_path))))
									{
										new_clr = app.ConfigGet (L"ColorInvalid", LISTVIEW_COLOR_INVALID).AsUlong ();
									}

									if (new_clr)
									{
										_r_wnd_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, new_clr);
										lpnmlv->clrTextBk = new_clr;

										result = CDRF_NEWFONT;
									}
								}

								break;
							}
						}

						SetWindowLongPtr (hwnd, DWLP_MSGRESULT, result);
						return TRUE;
					}

					break;
				}

				case LVN_GETINFOTIP:
				{
					LPNMLVGETINFOTIP lpnmlv = (LPNMLVGETINFOTIP)lparam;

					const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, lpnmlv->iItem);

					if (hash)
					{
						ITEM_APPLICATION* ptr = &applications[hash];

						rstring tmp;

						if (ptr->description[0])
						{
							tmp.Append (ptr->description);
							tmp.Append (L"\r\n");
						}

						if (ptr->author[0])
						{
							tmp.Append (ptr->author);
							tmp.Append (L"\r\n");
						}

						if (ptr->version[0])
						{
							tmp.Append (ptr->version);
							tmp.Append (L"\r\n");
						}

						if (tmp.IsEmpty ())
							tmp = L"n/a";

						StringCchCopy (lpnmlv->pszText, lpnmlv->cchTextMax, tmp);
					}

					break;
				}

				case LVN_ITEMCHANGED:
				{
					LPNMLISTVIEW lpnmlv = reinterpret_cast<LPNMLISTVIEW>(nmlp);

					if (lpnmlv->uNewState == 8192 || lpnmlv->uNewState == 4096)
					{
						const size_t hash = lpnmlv->lParam;

						if (!hash || !config.is_firstapply || applications.find (hash) == applications.end ())
							return FALSE;

						ITEM_APPLICATION* ptr = &applications[hash];
						ptr->is_checked = (lpnmlv->uNewState == 8192) ? TRUE : FALSE;

						if ((EnumMode)app.ConfigGet (L"Mode", Whitelist).AsUint () != TrustNoOne)
							SetEvent (config.apply_evt); // apply filters
						else
							_app_listviewsort (hwnd);
					}

					break;
				}

				case LVN_INSERTITEM:
				case LVN_DELETEITEM:
				{
					_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);
					_app_refreshstatus (hwnd);

					break;
				}

				case LVN_GETEMPTYMARKUP:
				{
					if (nmlp->idFrom == IDC_LISTVIEW)
					{
						NMLVEMPTYMARKUP* lpnmlv = (NMLVEMPTYMARKUP*)lparam;

						lpnmlv->dwFlags = EMF_CENTERED;
						StringCchCopy (lpnmlv->szMarkup, _countof (lpnmlv->szMarkup), I18N (&app, IDS_STATUS_EMPTY, 0));

						SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
						return TRUE;
					}

					break;
				}

				case NM_DBLCLK:
				{
					LPNMITEMACTIVATE lpnmlv = (LPNMITEMACTIVATE)lparam;

					if (lpnmlv->iItem != -1)
						SendMessage (hwnd, WM_COMMAND, MAKELPARAM (IDM_EXPLORE, 0), 0);

					break;
				}
			}

			break;
		}

		case WM_CONTEXTMENU:
		{
			if (GetDlgCtrlID ((HWND)wparam) == IDC_LISTVIEW)
			{
				HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_LISTVIEW)), submenu = GetSubMenu (menu, 0);
				HMENU submenu1 = GetSubMenu (submenu, 1);

				// localize
				app.LocaleMenu (submenu, I18N (&app, IDS_ADD, 0), 0, TRUE);
				app.LocaleMenu (submenu, I18N (&app, IDS_ADD_FILE, 0), IDM_ADD_FILE, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_ADD_PROCESS, 0), 1, TRUE);
				app.LocaleMenu (submenu, I18N (&app, IDS_SETTINGS, 0), 3, TRUE);
				app.LocaleMenu (submenu, I18N (&app, IDS_EXCLUDENOTIFICATIONS, 0), IDM_EXCLUDENOTIFICATIONS, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_EXCLUDELOG, 0), IDM_EXCLUDELOG, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_ALL, 0), IDM_ALL, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_REFRESH, 0) + L"\tF5", IDM_REFRESH2, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_EXPLORE, 0), IDM_EXPLORE, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_COPY, 0), IDM_COPY, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_DELETE, 0) + L"\tDel", IDM_DELETE, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_CHECK, 0), IDM_CHECK, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_UNCHECK, 0), IDM_UNCHECK, FALSE);

				if (!SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETSELECTEDCOUNT, 0, 0))
				{
					EnableMenuItem (submenu, 3, MF_BYPOSITION | MF_DISABLED);
					EnableMenuItem (submenu, IDM_EXPLORE, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_COPY, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_DELETE, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_CHECK, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_UNCHECK, MF_BYCOMMAND | MF_DISABLED);
				}

				// generate processes popup menu
				{
					_app_getprocesslist (&processes);

					for (size_t i = 0; i < processes.size (); i++)
					{
						MENUITEMINFO mii = {0};

						mii.cbSize = sizeof (mii);
						mii.fMask = MIIM_ID | MIIM_CHECKMARKS | MIIM_STRING;
						mii.dwTypeData = processes.at (i).display_path;
						mii.hbmpChecked = processes.at (i).hbmp;
						mii.hbmpUnchecked = processes.at (i).hbmp;
						mii.wID = IDM_ADD_PROCESS + UINT (i);

						InsertMenuItem (submenu1, IDM_ADD_PROCESS + UINT (i), FALSE, &mii);
					}

					if (!processes.size ())
						EnableMenuItem (submenu1, IDM_ALL, MF_BYCOMMAND | MF_DISABLED);
				}

				// show configuration
				{
					size_t item = (size_t)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED); // get first item

					const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, item);

					if (hash)
					{
						ITEM_APPLICATION const* ptr = &applications[hash];

						CheckMenuItem (submenu, IDM_EXCLUDENOTIFICATIONS, MF_BYCOMMAND | (((ptr->is_silent & SILENT_NOTIFICATION) != 0) ? MF_CHECKED : MF_UNCHECKED));
						CheckMenuItem (submenu, IDM_EXCLUDELOG, MF_BYCOMMAND | (((ptr->is_silent & SILENT_LOG) != 0) ? MF_CHECKED : MF_UNCHECKED));
					}
				}

				POINT pt = {0};
				GetCursorPos (&pt);

				TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

				DestroyMenu (menu);
				DestroyMenu (submenu);
			}

			break;
		}

		case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO lpmmi = (LPMINMAXINFO)lparam;

			lpmmi->ptMinTrackSize.x = max_width;
			lpmmi->ptMinTrackSize.y = max_height;

			break;
		}

		case WM_SIZE:
		{
			if (wparam == SIZE_MINIMIZED)
			{
				_r_wnd_toggle (hwnd, FALSE);
				return FALSE;
			}

			ResizeWindow (hwnd, LOWORD (lparam), HIWORD (lparam));
			RedrawWindow (hwnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_ERASE | RDW_INVALIDATE);

			break;
		}

		case WM_SYSCOMMAND:
		{
			if (wparam == SC_CLOSE)
			{
				_r_wnd_toggle (hwnd, FALSE);
				return TRUE;
			}

			break;
		}

		case WM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case NIN_BALLOONUSERCLICK:
				{
					if (config.last_hash)
					{
						_r_wnd_toggle (hwnd, TRUE);

						ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, config.last_hash));

						config.last_hash = 0;
					}

					break;
				}

				case NIN_BALLOONHIDE:
				case NIN_BALLOONTIMEOUT:
				{
					config.last_hash = 0;
					break;
				}

				case WM_MBUTTONDOWN:
				{
					SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDM_LOGSHOW, 0), 0);
					break;
				}

				case WM_LBUTTONUP:
				{
					SetForegroundWindow (hwnd);
					break;
				}

				case WM_LBUTTONDBLCLK:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case WM_RBUTTONUP:
				{
					SetForegroundWindow (hwnd); // don't touch

					HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_TRAY)), submenu = GetSubMenu (menu, 0);

					// localize
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_SHOW, 0), IDM_TRAY_SHOW, FALSE);
					app.LocaleMenu (submenu, I18N (&app, (config.hengine ? IDS_TRAY_STOP : IDS_TRAY_START), config.hengine ? L"IDS_TRAY_STOP" : L"IDS_TRAY_START"), IDM_TRAY_START, FALSE);

					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_MODE, 0), 3, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_MODE_WHITELIST, 0), IDM_TRAY_MODEWHITELIST, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_MODE_BLACKLIST, 0), IDM_TRAY_MODEBLACKLIST, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_MODE_TRUSTNOONE, 0), IDM_TRAY_MODETRUSTNOONE, FALSE);

					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_SYSTEM_RULES, 0), 5, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_SPECIAL_RULES, 0), 6, TRUE);

					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_LOG, 0), 8, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_ENABLELOG_CHK, 0), IDM_TRAY_ENABLELOG_CHK, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_ENABLENOTIFICATIONS_CHK, 0), IDM_TRAY_ENABLENOTIFICATIONS_CHK, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_ENABLEAPPSCOLLECTOR_CHK, 0), IDM_TRAY_ENABLEAPPSCOLLECTOR_CHK, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_LOGSHOW, 0), IDM_TRAY_LOGSHOW, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_LOGCLEAR, 0), IDM_TRAY_LOGCLEAR, FALSE);

					app.LocaleMenu (submenu, I18N (&app, IDS_SETTINGS, 0), IDM_TRAY_SETTINGS, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_WEBSITE, 0), IDM_TRAY_WEBSITE, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_ABOUT, 0), IDM_TRAY_ABOUT, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_EXIT, 0), IDM_TRAY_EXIT, FALSE);

					if (!config.is_admin)
						EnableMenuItem (submenu, IDM_TRAY_START, MF_BYCOMMAND | MF_DISABLED);

					CheckMenuItem (submenu, IDM_TRAY_ENABLELOG_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsLogEnabled", FALSE).AsBool () ? MF_CHECKED : MF_UNCHECKED));
					CheckMenuItem (submenu, IDM_TRAY_ENABLENOTIFICATIONS_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool () ? MF_CHECKED : MF_UNCHECKED));
					CheckMenuItem (submenu, IDM_TRAY_ENABLEAPPSCOLLECTOR_CHK, MF_BYCOMMAND | (app.ConfigGet (L"IsAppsCollectorEnabled", TRUE).AsBool () ? MF_CHECKED : MF_UNCHECKED));

					if (!config.is_admin || !_r_sys_validversion (6, 1))
					{
						EnableMenuItem (submenu, IDM_TRAY_ENABLELOG_CHK, MF_BYCOMMAND | MF_DISABLED);
						EnableMenuItem (submenu, IDM_TRAY_ENABLENOTIFICATIONS_CHK, MF_BYCOMMAND | MF_DISABLED);
						EnableMenuItem (submenu, IDM_TRAY_ENABLEAPPSCOLLECTOR_CHK, MF_BYCOMMAND | MF_DISABLED);
					}

					if (config.hlog == nullptr || config.hlog == INVALID_HANDLE_VALUE)
						EnableMenuItem (submenu, IDM_TRAY_LOGCLEAR, MF_BYCOMMAND | MF_DISABLED);

					CheckMenuRadioItem (submenu, IDM_TRAY_MODEWHITELIST, IDM_TRAY_MODETRUSTNOONE, IDM_TRAY_MODEWHITELIST + app.ConfigGet (L"Mode", Whitelist).AsUint (), MF_BYCOMMAND);

					// append system rules
					{
						const HMENU submenu_sub = GetSubMenu (submenu, 5);

						DeleteMenu (submenu_sub, 0, MF_BYPOSITION);

						size_t prev_id = 0;

						for (size_t i = 0; i < rules_system.size (); i++)
						{
							if (rules_system.at (i).group_id != prev_id)
								AppendMenu (submenu_sub, MF_SEPARATOR, 0, nullptr);

							AppendMenu (submenu_sub, MF_STRING, IDM_RULES_SYSTEM + i, I18N (&app, rules_system.at (i).locale_id, rules_system.at (i).locale_sid));

							if (app.ConfigGet (rules_system.at (i).config, rules_system.at (i).is_enabled).AsBool ())
								CheckMenuItem (submenu_sub, IDM_RULES_SYSTEM + UINT (i), MF_BYCOMMAND | MF_CHECKED);

							prev_id = rules_system.at (i).group_id;
						}
					}

					// append special rules
					{
						const HMENU submenu_sub = GetSubMenu (submenu, 6);

						DeleteMenu (submenu_sub, 0, MF_BYPOSITION);

						for (size_t i = 0; i < rules_special.size (); i++)
						{
							AppendMenu (submenu_sub, MF_STRING, IDM_RULES_SPECIAL + i, rules_special.at (i).name);

							if (rules_special.at (i).is_enabled)
								CheckMenuItem (submenu_sub, IDM_RULES_SPECIAL + UINT (i), MF_BYCOMMAND | MF_CHECKED);
						}
					}

					POINT pt = {0};
					GetCursorPos (&pt);

					TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

					DestroyMenu (submenu);
					DestroyMenu (menu);

					break;
				}
			}

			break;
		}

		case WM_DEVICECHANGE:
		{
			if ((wparam == DBT_DEVICEARRIVAL) || (wparam == DBT_DEVICEREMOVECOMPLETE))
			{
				PDEV_BROADCAST_HDR lphdr = (PDEV_BROADCAST_HDR)lparam;

				if (lphdr->dbch_devicetype == DBT_DEVTYP_VOLUME)
					SetEvent (config.apply_evt); // apply filters
			}

			break;
		}

		case WM_COMMAND:
		{
			if (HIWORD (wparam) == 0 && LOWORD (wparam) >= IDM_DEFAULT && LOWORD (wparam) <= IDM_DEFAULT + app.LocaleGetCount ())
			{
				app.LocaleApplyFromMenu (GetSubMenu (GetSubMenu (GetMenu (hwnd), 2), 5), LOWORD (wparam), IDM_DEFAULT);

				return FALSE;
			}
			else if ((LOWORD (wparam) >= IDM_ADD_PROCESS && LOWORD (wparam) <= IDM_ADD_PROCESS + processes.size ()))
			{
				ITEM_PROCESS const * ptr = &processes.at (LOWORD (wparam) - IDM_ADD_PROCESS);

				const size_t hash = _app_addapplication (hwnd, ptr->file_path, 0, FALSE);

				_app_listviewsort (hwnd);
				_app_profilesave (hwnd);

				ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, hash));

				return FALSE;
			}
			else if ((LOWORD (wparam) >= IDM_RULES_SYSTEM && LOWORD (wparam) <= IDM_RULES_SYSTEM + rules_system.size ()))
			{
				ITEM_RULE_SYSTEM const * ptr = &rules_system.at (LOWORD (wparam) - IDM_RULES_SYSTEM);

				BOOL new_val = !app.ConfigGet (ptr->config, ptr->is_enabled).AsBool ();

				app.ConfigSet (ptr->config, new_val);

				CheckMenuItem (GetMenu (hwnd), IDM_RULES_SYSTEM + (LOWORD (wparam) - IDM_RULES_SYSTEM), MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));

				SetEvent (config.apply_evt); // apply filters

				return FALSE;
			}
			else if ((LOWORD (wparam) >= IDM_RULES_SPECIAL && LOWORD (wparam) <= IDM_RULES_SPECIAL + rules_system.size ()))
			{
				ITEM_RULE_SPECIAL* ptr = &rules_special.at (LOWORD (wparam) - IDM_RULES_SPECIAL);

				ptr->is_enabled = !ptr->is_enabled;

				CheckMenuItem (GetMenu (hwnd), IDM_RULES_SPECIAL + (LOWORD (wparam) - IDM_RULES_SPECIAL), MF_BYCOMMAND | (ptr->is_enabled ? MF_CHECKED : MF_UNCHECKED));

				SetEvent (config.apply_evt); // apply filters
				_app_profilesave (hwnd);

				return FALSE;
			}

			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				case IDC_SETTINGS_BTN:
				{
					app.CreateSettingsWindow ();
					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				case IDC_EXIT_BTN:
				{
					SendMessage (hwnd, WM_CLOSE, 0, 0);
					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, _APP_WEBSITE_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_DONATE:
				{
					ShellExecute (hwnd, nullptr, _APP_DONATION_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_CHECKUPDATES:
				{
					app.CheckForUpdates (FALSE);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					app.CreateAboutWindow ();
					break;
				}

				case IDM_ALWAYSONTOP_CHK:
				{
					BOOL new_val = !app.ConfigGet (L"AlwaysOnTop", FALSE).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"AlwaysOnTop", new_val);

					_r_wnd_top (hwnd, new_val);

					break;
				}

				case IDM_SORTBYFNAME:
				case IDM_SORTBYFDIR:
				{
					app.ConfigSet (L"SortMode", LOWORD (wparam) == IDM_SORTBYFNAME ? 1 : 0);

					_app_listviewsort (hwnd);

					break;
				}

				case IDM_SORTISDESCEND:
				{
					app.ConfigSet (L"IsSortDescending", !app.ConfigGet (L"IsSortDescending", FALSE).AsBool ());

					_app_listviewsort (hwnd);

					break;
				}

				case IDM_ICONSSMALL:
				case IDM_ICONSLARGE:
				{
					app.ConfigSet (L"IsLargeIcons", LOWORD (wparam) == IDM_ICONSLARGE);

					SetIconsSize (hwnd);

					break;
				}

				case IDM_ICONSISHIDDEN:
				{
					app.ConfigSet (L"IsIconsHidden", !app.ConfigGet (L"IsIconsHidden", FALSE).AsBool ());

					SetIconsSize (hwnd);

					_app_profileload (hwnd);

					break;
				}

				case IDM_TRAY_MODEWHITELIST:
				case IDM_TRAY_MODEBLACKLIST:
				case IDM_TRAY_MODETRUSTNOONE:
				{
					EnumMode curr = Whitelist;

					if (LOWORD (wparam) == IDM_TRAY_MODEBLACKLIST)
						curr = Blacklist;
					else if (LOWORD (wparam) == IDM_TRAY_MODETRUSTNOONE)
						curr = TrustNoOne;

					app.ConfigSet (L"Mode", curr);

					_app_refreshstatus (hwnd);

					CheckMenuRadioItem (GetMenu (hwnd), IDM_TRAY_MODEWHITELIST, IDM_TRAY_MODETRUSTNOONE, IDM_TRAY_MODEWHITELIST + app.ConfigGet (L"Mode", Whitelist).AsUint (), MF_BYCOMMAND);

					SetEvent (config.apply_evt); // apply filters

					break;
				}

				case IDM_FIND:
				{
					if (!config.hfind)
					{
						static FINDREPLACE fr = {0};

						fr.lStructSize = sizeof (fr);
						fr.hwndOwner = hwnd;
						fr.lpstrFindWhat = config.search_string;
						fr.wFindWhatLen = _countof (config.search_string) - 1;
						fr.Flags = FR_HIDEWHOLEWORD | FR_HIDEMATCHCASE | FR_HIDEUPDOWN;

						config.hfind = FindText (&fr);
					}
					else
					{
						SetFocus (config.hfind);
					}

					break;
				}

				case IDM_FINDNEXT:
				{
					if (!config.search_string[0])
					{
						SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDM_FIND, 0), 0);
					}
					else
					{
						FINDREPLACE fr = {0};

						fr.Flags = FR_FINDNEXT;
						fr.lpstrFindWhat = config.search_string;

						SendMessage (hwnd, WM_FINDMSGSTRING, 0, (LPARAM)&fr);
					}

					break;
				}

				case IDM_REFRESH:
				case IDM_REFRESH2:
				{
					const size_t item = (size_t)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);

					_app_profileload (hwnd);

					SetEvent (config.apply_evt); // apply filters

					ShowItem (hwnd, IDC_LISTVIEW, item);

					break;
				}

				case IDM_ENABLELOG_CHK:
				case IDM_TRAY_ENABLELOG_CHK:
				{
					BOOL new_val = !app.ConfigGet (L"IsLogEnabled", FALSE).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_ENABLELOG_CHK, MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"IsLogEnabled", new_val);

					_wfp_setevents (new_val);

					EnableMenuItem (GetMenu (hwnd), IDM_LOGCLEAR, MF_BYCOMMAND | ((config.hlog == nullptr || config.hlog == INVALID_HANDLE_VALUE) ? MF_DISABLED : MF_ENABLED));

					break;
				}

				case IDM_ENABLENOTIFICATIONS_CHK:
				case IDM_TRAY_ENABLENOTIFICATIONS_CHK:
				{
					BOOL new_val = !app.ConfigGet (L"IsNotificationsEnabled", TRUE).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_ENABLENOTIFICATIONS_CHK, MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"IsNotificationsEnabled", new_val);

					break;
				}

				case IDM_ENABLEAPPSCOLLECTOR_CHK:
				case IDM_TRAY_ENABLEAPPSCOLLECTOR_CHK:
				{
					BOOL new_val = !app.ConfigGet (L"IsAppsCollectorEnabled", TRUE).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_ENABLEAPPSCOLLECTOR_CHK, MF_BYCOMMAND | (new_val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"IsAppsCollectorEnabled", new_val);

					break;
				}

				case IDM_LOGSHOW:
				case IDM_TRAY_LOGSHOW:
				{
					rstring path = _r_path_expand (app.ConfigGet (L"LogPath", L"%userprofile%\\wfptool.log"));

					if (!_r_fs_exists (path))
						return FALSE;

					_r_run (_r_fmt (L"%s \"%s\"", app.ConfigGet (L"LogViewer", L"notepad.exe"), path));

					break;
				}

				case IDM_LOGCLEAR:
				case IDM_TRAY_LOGCLEAR:
				{
					if (config.hlog != nullptr && config.hlog != INVALID_HANDLE_VALUE)
					{
						if (app.ConfigGet (L"ConfirmLogClear", TRUE).AsBool () && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION, 0)) != IDYES)
							break;

						_R_SPINLOCK (config.lock_writelog);

						SetFilePointer (config.hlog, 2, nullptr, FILE_BEGIN);
						SetEndOfFile (config.hlog);

						_R_SPINUNLOCK (config.lock_writelog);
					}

					break;
				}

				case IDM_TRAY_START:
				case IDC_START_BTN:
				{
					if (!config.is_admin)
					{
						if (app.SkipUacRun ())
							DestroyWindow (hwnd);

						app.TrayPopup (NIIF_ERROR | (app.ConfigGet (L"IsNotificationsSilent", FALSE).AsBool () ? NIIF_NOSOUND : 0), APP_NAME, I18N (&app, IDS_STATUS_NOPRIVILEGES, 0));
					}
					else
					{
						WCHAR flag[128] = {0};
						WCHAR text[512] = {0};

						BOOL status = config.hengine || app.ConfigGet (L"IsFiltersEnabled", FALSE).AsBool ();
						INT result = 0;
						BOOL is_flagchecked = 0;

						TASKDIALOGCONFIG tdc = {0};

						tdc.cbSize = sizeof (tdc);
						tdc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
						tdc.hwndParent = hwnd;
						tdc.pszWindowTitle = APP_NAME;
						tdc.pfCallback = &_r_msg_callback;
						tdc.pszMainIcon = TD_WARNING_ICON;
						tdc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
						tdc.nDefaultButton = IDNO;
						tdc.pszMainInstruction = text;
						tdc.pszVerificationText = flag;

						if (status)
						{
							StringCchCopy (text, _countof (text), I18N (&app, IDS_QUESTION_STOP, 0));
							StringCchCopy (flag, _countof (flag), I18N (&app, IDS_ENABLEWINDOWSFIREWALL_CHK, 0));

							if (app.ConfigGet (L"IsEnableWindowsFirewallChecked", TRUE).AsBool ())
								tdc.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
						}
						else
						{
							StringCchCopy (text, _countof (text), I18N (&app, IDS_QUESTION_START, 0));
							StringCchCopy (flag, _countof (flag), I18N (&app, IDS_DISABLEWINDOWSFIREWALL_CHK, 0));

							if (app.ConfigGet (L"IsDisableWindowsFirewallChecked", TRUE).AsBool ())
								tdc.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
						}

						TaskDialogIndirect (&tdc, &result, nullptr, &is_flagchecked);

						if (result != IDYES)
							break;

						if (status)
						{
							app.ConfigSet (L"IsEnableWindowsFirewallChecked", is_flagchecked);

							if (is_flagchecked)
								Mps_Stop (FALSE);

							_wfp_stop ();
						}
						else
						{
							app.ConfigSet (L"IsDisableWindowsFirewallChecked", is_flagchecked);

							if (is_flagchecked)
								Mps_Stop (TRUE);

							_wfp_start ();
							SetDlgItemText (hwnd, IDC_START_BTN, I18N (&app, IDS_TRAY_STOP, 0));
						}

						app.ConfigSet (L"IsFiltersEnabled", !status);

						initializer_callback (hwnd, _RM_INITIALIZE, nullptr, nullptr);
					}

					break;
				}

				case IDM_ADD_FILE:
				{
					WCHAR files[_R_BUFFER_LENGTH] = {0};
					OPENFILENAME ofn = {0};

					size_t item = 0;

					ofn.lStructSize = sizeof (ofn);
					ofn.hwndOwner = hwnd;
					ofn.lpstrFile = files;
					ofn.nMaxFile = _countof (files);
					ofn.lpstrFilter = L"*.exe\0*.exe\0*.*\0*.*\0\0";
					ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_ENABLESIZING | OFN_PATHMUSTEXIST | OFN_FORCESHOWHIDDEN;

					if (GetOpenFileName (&ofn))
					{
						if (files[ofn.nFileOffset - 1] != 0)
						{
							item = _app_addapplication (hwnd, files, 0, FALSE);
						}
						else
						{
							LPWSTR p = files;
							WCHAR dir[MAX_PATH] = {0};
							GetCurrentDirectory (MAX_PATH, dir);

							while (*p)
							{
								p += wcslen (p) + 1;

								if (*p)
									item = _app_addapplication (hwnd, _r_fmt (L"%s\\%s", dir, p), 0, FALSE);
							}
						}

						_app_listviewsort (hwnd);
						_app_profilesave (hwnd);

						ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, _app_getposition (hwnd, item)));
					}

					break;
				}

				case IDM_ALL:
				{
					if (!processes.size ())
						_app_getprocesslist (&processes);

					for (size_t i = 0; i < processes.size (); i++)
						_app_addapplication (hwnd, processes.at (i).file_path, 0, FALSE);

					_app_listviewsort (hwnd);
					_app_profilesave (hwnd);

					break;
				}

				case IDM_EXPLORE:
				case IDM_COPY:
				case IDM_EXCLUDENOTIFICATIONS:
				case IDM_EXCLUDELOG:
				case IDM_UNCHECK:
				case IDM_CHECK:
				{
					INT item = -1;
					DWORD mask = DWORD (-1);

					rstring buffer;

					while ((item = (INT)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, item, LVNI_SELECTED)) != -1)
					{
						const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, item);

						if (applications.find (hash) == applications.end ())
							continue;

						ITEM_APPLICATION* ptr = &applications[hash];

						if (LOWORD (wparam) == IDM_EXPLORE)
						{
							if (_r_fs_exists (ptr->full_path))
								_r_run (_r_fmt (L"\"explorer.exe\" /select,\"%s\"", ptr->full_path));
							else
								ShellExecute (hwnd, nullptr, ptr->file_dir, nullptr, nullptr, SW_SHOWDEFAULT);
						}
						else if (LOWORD (wparam) == IDM_COPY)
						{
							buffer.Append (ptr->full_path).Append (L"\r\n");
						}
						else if (LOWORD (wparam) == IDM_EXCLUDENOTIFICATIONS || LOWORD (wparam) == IDM_EXCLUDELOG)
						{
							// configure as first item
							if (mask == DWORD (-1))
								mask = (LOWORD (wparam) == IDM_EXCLUDENOTIFICATIONS) ? SILENT_NOTIFICATION : SILENT_LOG;

							if ((ptr->is_silent & mask) != 0)
								ptr->is_silent ^= mask; // remove mask
							else
								ptr->is_silent |= mask; // add mask

							SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_REDRAWITEMS, item, item); // redraw (required!)
						}
						else if (LOWORD (wparam) == IDM_CHECK || LOWORD (wparam) == IDM_UNCHECK)
						{
							ptr->is_checked = LOWORD (wparam) == IDM_CHECK ? TRUE : FALSE;
							_r_listview_setcheckstate (hwnd, IDC_LISTVIEW, item, LOWORD (wparam) == IDM_CHECK ? TRUE : FALSE);
						}
					}

					if (LOWORD (wparam) == IDM_EXCLUDENOTIFICATIONS || LOWORD (wparam) == IDM_EXCLUDELOG)
					{
						_app_profilesave (hwnd);
					}
					else if (LOWORD (wparam) == IDM_COPY)
					{
						buffer.Trim (L"\r\n");
						_r_clipboard_set (hwnd, buffer, buffer.GetLength ());
					}

					break;
				}

				case IDM_DELETE:
				{
					if (!SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETSELECTEDCOUNT, 0, 0) || (app.ConfigGet (L"ConfirmDelete", TRUE).AsBool () && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION, 0)) != IDYES))
						break;

					size_t count = _r_listview_getitemcount (hwnd, IDC_LISTVIEW) - 1;

					BOOL is_checked = FALSE;

					for (size_t i = count; i != LAST_VALUE; i--)
					{
						if (ListView_GetItemState (GetDlgItem (hwnd, IDC_LISTVIEW), i, LVNI_SELECTED))
						{
							const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

							ITEM_APPLICATION* ptr = &applications[hash];

							if (ptr->is_checked)
								is_checked = TRUE;

							ptr = nullptr;

							applications.erase (hash);

							SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_DELETEITEM, i, 0);
						}

						if (is_checked)
							SetEvent (config.apply_evt); // apply filters
						else
							_app_profilesave (hwnd);
					}

					break;
				}

				case IDM_PURGEN:
				{
					const size_t count = _r_listview_getitemcount (hwnd, IDC_LISTVIEW) - 1;

					for (size_t i = count; i != LAST_VALUE; i--)
					{
						const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

						if (hash == config.system_hash)
							continue;

						ITEM_APPLICATION* ptr = &applications[hash];

						if ((!ptr->is_checked && !ptr->is_silent) || (ptr->is_checked && !ptr->is_success) || (!ptr->is_checked && !_r_fs_exists (ptr->full_path)))
						{
							ptr = nullptr;

							applications.erase (hash);

							SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_DELETEITEM, i, 0);
						}
					}

					_app_profilesave (hwnd);

					break;
				}

				case IDM_SELECT_ALL:
				{
					ListView_SetItemState (GetDlgItem (hwnd, IDC_LISTVIEW), -1, LVIS_SELECTED, LVIS_SELECTED);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if (app.CreateMainWindow (&DlgProc, &initializer_callback))
	{
		MSG msg = {0};

		HACCEL haccel = LoadAccelerators (app.GetHINSTANCE (), MAKEINTRESOURCE (IDA_MAIN));

		while (GetMessage (&msg, nullptr, 0, 0) > 0)
		{
			if ((haccel && !TranslateAccelerator (app.GetHWND (), haccel, &msg)) && !IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}

		if (haccel)
			DestroyAcceleratorTable (haccel);
	}

	return ERROR_SUCCESS;
}
