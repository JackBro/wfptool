// Wfp Tool
// Copyright (c) 2016 Henry++

#ifndef __MAIN_H__
#define __MAIN_H__

#include <windows.h>
#include "resource.h"
#include <algorithm>

// config
#define APP_NAME L"Wfp Tool"
#define APP_NAME_SHORT L"wfptool"
#define APP_VERSION L"1.3.8"
#define APP_VERSION_RES 1,3,8,0
#define APP_COPYRIGHT L"(c) 2016 " _APP_AUTHOR L". All Rights Reserved."

#define WM_TRAYICON WM_APP + 1
#define UID 1984

// guid
static const GUID GUID_WfpProvider =
{0xb0d553e2, 0xc6a0, 0x4a9a, {0xae, 0xb8, 0xc7, 0x52, 0x48, 0x3e, 0xd6, 0x2f}};

static const GUID GUID_WfpSublayer =
{0x9fee6f59, 0xb951, 0x4f9a, {0xb5, 0x2f, 0x13, 0x3d, 0xcf, 0x7a, 0x42, 0x79}};

static const GUID GUID_WfpOutboundCallout4 =
{0xf1251f1a, 0xab09, 0x4ce7, {0xba, 0xe3, 0x6c, 0xcc, 0xce, 0xf2, 0xc8, 0xca}};

static const GUID GUID_WfpInboundCallout4 =
{0xefc879ce, 0x3066, 0x45bb, {0x8a, 0x70, 0x17, 0xfe, 0x29, 0x78, 0x53, 0xc0}};

static const GUID GUID_WfpOutboundCallout6 =
{0xfd497f2e, 0x46f5, 0x486d, {0xb0, 0xc, 0x3f, 0x7f, 0xe0, 0x7a, 0x94, 0xa6}};

static const GUID GUID_WfpInboundCallout6 =
{0xd0420299, 0x52d8, 0x4f18, {0xbc, 0x80, 0x47, 0x3a, 0x24, 0x93, 0xf2, 0x69}};

static const GUID GUID_WfpListenCallout4 =
{0x51fa679d, 0x578b, 0x4835, {0xa6, 0x3e, 0xca, 0xd7, 0x68, 0x7f, 0x74, 0x95}};

static const GUID GUID_WfpListenCallout6 =
{0xa02187ca, 0xe655, 0x4adb, {0xa1, 0xf2, 0x47, 0xa2, 0xc9, 0x78, 0xf9, 0xce}};

// {7f35b047-7e29-43dc-bad8-6b4ebc843167}
static const GUID GUID_OldCalloutDelete1 =
{0xf35b047, 0x7e29, 0x43dc, {0xba, 0xd8, 0x6b, 0x4e, 0xbc, 0x84, 0x31, 0x67}};

// {b0d553e2-c6a0-4a9a-aeb8-c752483ed62f}
static const GUID GUID_OldCalloutDelete2 =
{0xb0d553e2, 0xc6a0, 0x4a9a, {0xae, 0xb8, 0xc7, 0x52, 0x48, 0x3e, 0xd6, 0x2f}};

// vista fallback
typedef DWORD (WINAPI *FWPMNES0) (HANDLE, const FWPM_NET_EVENT_SUBSCRIPTION0*, FWPM_NET_EVENT_CALLBACK0, LPVOID, HANDLE*); // FwpmNetEventSubscribe0
typedef DWORD (WINAPI *FWPMNEU0) (HANDLE, HANDLE); // FwpmNetEventUnsubscribe0

// libs
#pragma comment(lib, "fwpuclnt.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ws2_32.lib")

enum EnumDirection
{
	Out,
	In,
	Both,
};

enum EnumMode
{
	Whitelist,
	Blacklist,
	TrustNoOne,
};

struct STATIC_DATA
{
	BOOL is_admin = FALSE;
	BOOL is_firstapply = FALSE;

	HANDLE hengine = nullptr;
	HANDLE hevent = nullptr;
	HANDLE hlog = nullptr;

	HANDLE hthread = nullptr;
	HANDLE apply_evt = nullptr;
	HANDLE stop_evt = nullptr;

	HWND hfind = nullptr;

	WCHAR blocklist_path[MAX_PATH] = {0};
	WCHAR config_path[MAX_PATH] = {0};
	WCHAR rules_path[MAX_PATH] = {0};

	WCHAR windows_dir[MAX_PATH] = {0};
	WCHAR search_string[MAX_PATH] = {0};

	size_t wd_length = 0;

	size_t myself_hash = 0;
	WCHAR myself_path[MAX_PATH] = {0};

	size_t svchost_hash = 0;
	WCHAR svchost_path[MAX_PATH] = {0};

	size_t system_hash = 0;
	size_t last_hash = 0;

	INT statusbar_height = 0;

	volatile LONG lock_add = 0;
	volatile LONG lock_apply = 0;
	volatile LONG lock_profile = 0;
	volatile LONG lock_refresh = 0;
	volatile LONG lock_writelog = 0;
};

struct ITEM_APPLICATION
{
	size_t icon_id = 0;

	BOOL is_checked = FALSE;
	BOOL is_network = FALSE;
	BOOL is_system = FALSE;
	BOOL is_success = FALSE;
	UINT is_silent = 0;

	WCHAR file_name[MAX_PATH] = {0};
	WCHAR file_dir[MAX_PATH] = {0};

	WCHAR full_path[MAX_PATH] = {0};

	WCHAR author[MAX_PATH] = {0};
	WCHAR description[MAX_PATH] = {0};
	WCHAR version[MAX_PATH] = {0};
};

struct ITEM_RULE_SYSTEM
{
	BOOL is_enabled = FALSE;
	UINT locale_id = 0;

	size_t group_id = 0;

	WCHAR locale_sid[64] = {0};
	WCHAR config[64] = {0};
};

struct ITEM_RULE_SPECIAL
{
	EnumDirection direction = Out;

	BOOL is_enabled = FALSE;
	BOOL is_block = FALSE;

	WCHAR name[128] = {0};
	WCHAR rule[1024] = {0};
};

struct ITEM_LOG
{
	__time64_t timestamp = 0;

	size_t hash = 0;

	WCHAR protocol[16] = {0};
	WCHAR direction[16] = {0};

	WCHAR date[64] = {0};

	WCHAR domain[128] = {0};
	WCHAR username[128] = {0};

	WCHAR address[MAX_PATH] = {0};
	WCHAR full_path[MAX_PATH] = {0};

	WCHAR name[MAX_PATH] = {0};
};

struct ITEM_PROCESS
{
	HBITMAP hbmp = nullptr;

	WCHAR display_path[64] = {0};
	WCHAR file_path[MAX_PATH] = {0};
};

struct ITEM_COLOR
{
	BOOL is_enabled = FALSE;

	UINT locale_id = 0;

	COLORREF default_clr;
	COLORREF clr;

	WCHAR locale_sid[64] = {0};
	WCHAR config[MAX_PATH] = {0};
	WCHAR config_color[MAX_PATH] = {0};
};

struct PARSE_ADDRESS_RESULT
{
	ADDRESS_FAMILY af = 0;

	USHORT port = 0;

	UINT8 v6prefix = 0;
	UINT8 v6address[FWP_V6_ADDR_SIZE] = {0};

	UINT32 v4mask = 0;
	UINT32 v4address = 0;

	FWP_V4_ADDR_AND_MASK* ptr4 = nullptr;
	FWP_V6_ADDR_AND_MASK* ptr6 = nullptr;
};

// default colors
#define LISTVIEW_COLOR_SYSTEM RGB(204, 255, 255)
#define LISTVIEW_COLOR_NETWORK RGB (204, 187, 255)
#define LISTVIEW_COLOR_SILENT RGB (255, 248, 202)
#define LISTVIEW_COLOR_INVALID RGB (255, 192, 203)

#define LISTVIEW_COLOR_ALLOW RGB (206, 254, 206)
#define LISTVIEW_COLOR_BLOCK RGB (254, 190, 190)

// filter weights
#define FILTER_WEIGHT_HIGHEST 0xF
#define FILTER_WEIGHT_APPLICATION 0xE
#define FILTER_WEIGHT_ALLOWBLOCK 0xD

// exclude masks
#define SILENT_NOTIFICATION 0x01
#define SILENT_LOG 0x02

EXTERN_C const IID IID_IImageList;

// rev
// private
// source: http://www.microsoft.com/whdc/system/Sysinternals/MoreThan64proc.mspx
enum SYSTEM_INFORMATION_CLASS
{
	SystemBasicInformation, // q: SYSTEM_BASIC_INFORMATION
	SystemProcessorInformation, // q: SYSTEM_PROCESSOR_INFORMATION
	SystemPerformanceInformation, // q: SYSTEM_PERFORMANCE_INFORMATION
	SystemTimeOfDayInformation, // q: SYSTEM_TIMEOFDAY_INFORMATION
	SystemPathInformation, // not implemented
	SystemProcessInformation, // q: SYSTEM_PROCESS_INFORMATION
	SystemCallCountInformation, // q: SYSTEM_CALL_COUNT_INFORMATION
	SystemDeviceInformation, // q: SYSTEM_DEVICE_INFORMATION
	SystemProcessorPerformanceInformation, // q: SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION
	SystemFlagsInformation, // q: SYSTEM_FLAGS_INFORMATION
	SystemCallTimeInformation, // 10, not implemented
	SystemModuleInformation, // q: RTL_PROCESS_MODULES
	SystemLocksInformation,
	SystemStackTraceInformation,
	SystemPagedPoolInformation, // not implemented
	SystemNonPagedPoolInformation, // not implemented
	SystemHandleInformation, // q: SYSTEM_HANDLE_INFORMATION
	SystemObjectInformation, // q: SYSTEM_OBJECTTYPE_INFORMATION mixed with SYSTEM_OBJECT_INFORMATION
	SystemPageFileInformation, // q: SYSTEM_PAGEFILE_INFORMATION
	SystemVdmInstemulInformation, // q
	SystemVdmBopInformation, // 20, not implemented
	SystemFileCacheInformation, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypeSystemCache)
	SystemPoolTagInformation, // q: SYSTEM_POOLTAG_INFORMATION
	SystemInterruptInformation, // q: SYSTEM_INTERRUPT_INFORMATION
	SystemDpcBehaviorInformation, // q: SYSTEM_DPC_BEHAVIOR_INFORMATION; s: SYSTEM_DPC_BEHAVIOR_INFORMATION (requires SeLoadDriverPrivilege)
	SystemFullMemoryInformation, // not implemented
	SystemLoadGdiDriverInformation, // s (kernel-mode only)
	SystemUnloadGdiDriverInformation, // s (kernel-mode only)
	SystemTimeAdjustmentInformation, // q: SYSTEM_QUERY_TIME_ADJUST_INFORMATION; s: SYSTEM_SET_TIME_ADJUST_INFORMATION (requires SeSystemtimePrivilege)
	SystemSummaryMemoryInformation, // not implemented
	SystemMirrorMemoryInformation, // 30, s (requires license value "Kernel-MemoryMirroringSupported") (requires SeShutdownPrivilege)
	SystemPerformanceTraceInformation, // s
	SystemObsolete0, // not implemented
	SystemExceptionInformation, // q: SYSTEM_EXCEPTION_INFORMATION
	SystemCrashDumpStateInformation, // s (requires SeDebugPrivilege)
	SystemKernelDebuggerInformation, // q: SYSTEM_KERNEL_DEBUGGER_INFORMATION
	SystemContextSwitchInformation, // q: SYSTEM_CONTEXT_SWITCH_INFORMATION
	SystemRegistryQuotaInformation, // q: SYSTEM_REGISTRY_QUOTA_INFORMATION; s (requires SeIncreaseQuotaPrivilege)
	SystemExtendServiceTableInformation, // s (requires SeLoadDriverPrivilege) // loads win32k only
	SystemPrioritySeperation, // s (requires SeTcbPrivilege)
	SystemVerifierAddDriverInformation, // 40, s (requires SeDebugPrivilege)
	SystemVerifierRemoveDriverInformation, // s (requires SeDebugPrivilege)
	SystemProcessorIdleInformation, // q: SYSTEM_PROCESSOR_IDLE_INFORMATION
	SystemLegacyDriverInformation, // q: SYSTEM_LEGACY_DRIVER_INFORMATION
	SystemCurrentTimeZoneInformation, // q
	SystemLookasideInformation, // q: SYSTEM_LOOKASIDE_INFORMATION
	SystemTimeSlipNotification, // s (requires SeSystemtimePrivilege)
	SystemSessionCreate, // not implemented
	SystemSessionDetach, // not implemented
	SystemSessionInformation, // not implemented
	SystemRangeStartInformation, // 50, q
	SystemVerifierInformation, // q: SYSTEM_VERIFIER_INFORMATION; s (requires SeDebugPrivilege)
	SystemVerifierThunkExtend, // s (kernel-mode only)
	SystemSessionProcessInformation, // q: SYSTEM_SESSION_PROCESS_INFORMATION
	SystemLoadGdiDriverInSystemSpace, // s (kernel-mode only) (same as SystemLoadGdiDriverInformation)
	SystemNumaProcessorMap, // q
	SystemPrefetcherInformation, // q: PREFETCHER_INFORMATION; s: PREFETCHER_INFORMATION // PfSnQueryPrefetcherInformation
	SystemExtendedProcessInformation, // q: SYSTEM_PROCESS_INFORMATION
	SystemRecommendedSharedDataAlignment, // q
	SystemComPlusPackage, // q; s
	SystemNumaAvailableMemory, // 60
	SystemProcessorPowerInformation, // q: SYSTEM_PROCESSOR_POWER_INFORMATION
	SystemEmulationBasicInformation, // q
	SystemEmulationProcessorInformation,
	SystemExtendedHandleInformation, // q: SYSTEM_HANDLE_INFORMATION_EX
	SystemLostDelayedWriteInformation, // q: ULONG
	SystemBigPoolInformation, // q: SYSTEM_BIGPOOL_INFORMATION
	SystemSessionPoolTagInformation, // q: SYSTEM_SESSION_POOLTAG_INFORMATION
	SystemSessionMappedViewInformation, // q: SYSTEM_SESSION_MAPPED_VIEW_INFORMATION
	SystemHotpatchInformation, // q; s
	SystemObjectSecurityMode, // 70, q
	SystemWatchdogTimerHandler, // s (kernel-mode only)
	SystemWatchdogTimerInformation, // q (kernel-mode only); s (kernel-mode only)
	SystemLogicalProcessorInformation, // q: SYSTEM_LOGICAL_PROCESSOR_INFORMATION
	SystemWow64SharedInformationObsolete, // not implemented
	SystemRegisterFirmwareTableInformationHandler, // s (kernel-mode only)
	SystemFirmwareTableInformation, // not implemented
	SystemModuleInformationEx, // q: RTL_PROCESS_MODULE_INFORMATION_EX
	SystemVerifierTriageInformation, // not implemented
	SystemSuperfetchInformation, // q: SUPERFETCH_INFORMATION; s: SUPERFETCH_INFORMATION // PfQuerySuperfetchInformation
	SystemMemoryListInformation, // 80, q: SYSTEM_MEMORY_LIST_INFORMATION; s: SYSTEM_MEMORY_LIST_COMMAND (requires SeProfileSingleProcessPrivilege)
	SystemFileCacheInformationEx, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (same as SystemFileCacheInformation)
	SystemThreadPriorityClientIdInformation, // s: SYSTEM_THREAD_CID_PRIORITY_INFORMATION (requires SeIncreaseBasePriorityPrivilege)
	SystemProcessorIdleCycleTimeInformation, // q: SYSTEM_PROCESSOR_IDLE_CYCLE_TIME_INFORMATION[]
	SystemVerifierCancellationInformation, // not implemented // name:wow64:whNT32QuerySystemVerifierCancellationInformation
	SystemProcessorPowerInformationEx, // not implemented
	SystemRefTraceInformation, // q; s // ObQueryRefTraceInformation
	SystemSpecialPoolInformation, // q; s (requires SeDebugPrivilege) // MmSpecialPoolTag, then MmSpecialPoolCatchOverruns != 0
	SystemProcessIdInformation, // q: SYSTEM_PROCESS_ID_INFORMATION
	SystemErrorPortInformation, // s (requires SeTcbPrivilege)
	SystemBootEnvironmentInformation, // 90, q: SYSTEM_BOOT_ENVIRONMENT_INFORMATION
	SystemHypervisorInformation, // q; s (kernel-mode only)
	SystemVerifierInformationEx, // q; s
	SystemTimeZoneInformation, // s (requires SeTimeZonePrivilege)
	SystemImageFileExecutionOptionsInformation, // s: SYSTEM_IMAGE_FILE_EXECUTION_OPTIONS_INFORMATION (requires SeTcbPrivilege)
	SystemCoverageInformation, // q; s // name:wow64:whNT32QuerySystemCoverageInformation; ExpCovQueryInformation
	SystemPrefetchPatchInformation, // not implemented
	SystemVerifierFaultsInformation, // s (requires SeDebugPrivilege)
	SystemSystemPartitionInformation, // q: SYSTEM_SYSTEM_PARTITION_INFORMATION
	SystemSystemDiskInformation, // q: SYSTEM_SYSTEM_DISK_INFORMATION
	SystemProcessorPerformanceDistribution, // 100, q: SYSTEM_PROCESSOR_PERFORMANCE_DISTRIBUTION
	SystemNumaProximityNodeInformation, // q
	SystemDynamicTimeZoneInformation, // q; s (requires SeTimeZonePrivilege)
	SystemCodeIntegrityInformation, // q // SeCodeIntegrityQueryInformation
	SystemProcessorMicrocodeUpdateInformation, // s
	SystemProcessorBrandString, // q // HaliQuerySystemInformation -> HalpGetProcessorBrandString, info class 23
	SystemVirtualAddressInformation, // q: SYSTEM_VA_LIST_INFORMATION[]; s: SYSTEM_VA_LIST_INFORMATION[] (requires SeIncreaseQuotaPrivilege) // MmQuerySystemVaInformation
	SystemLogicalProcessorAndGroupInformation, // q: SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX // since WIN7 // KeQueryLogicalProcessorRelationship
	SystemProcessorCycleTimeInformation, // q: SYSTEM_PROCESSOR_CYCLE_TIME_INFORMATION[]
	SystemStoreInformation, // q; s // SmQueryStoreInformation
	SystemRegistryAppendString, // 110, s: SYSTEM_REGISTRY_APPEND_STRING_PARAMETERS
	SystemAitSamplingValue, // s: ULONG (requires SeProfileSingleProcessPrivilege)
	SystemVhdBootInformation, // q: SYSTEM_VHD_BOOT_INFORMATION
	SystemCpuQuotaInformation, // q; s // PsQueryCpuQuotaInformation
	SystemNativeBasicInformation, // not implemented
	SystemSpare1, // not implemented
	SystemLowPriorityIoInformation, // q: SYSTEM_LOW_PRIORITY_IO_INFORMATION
	SystemTpmBootEntropyInformation, // q: TPM_BOOT_ENTROPY_NT_RESULT // ExQueryTpmBootEntropyInformation
	SystemVerifierCountersInformation, // q: SYSTEM_VERIFIER_COUNTERS_INFORMATION
	SystemPagedPoolInformationEx, // q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypePagedPool)
	SystemSystemPtesInformationEx, // 120, q: SYSTEM_FILECACHE_INFORMATION; s (requires SeIncreaseQuotaPrivilege) (info for WorkingSetTypeSystemPtes)
	SystemNodeDistanceInformation, // q
	SystemAcpiAuditInformation, // q: SYSTEM_ACPI_AUDIT_INFORMATION // HaliQuerySystemInformation -> HalpAuditQueryResults, info class 26
	SystemBasicPerformanceInformation, // q: SYSTEM_BASIC_PERFORMANCE_INFORMATION // name:wow64:whNtQuerySystemInformation_SystemBasicPerformanceInformation
	SystemQueryPerformanceCounterInformation, // q: SYSTEM_QUERY_PERFORMANCE_COUNTER_INFORMATION // since WIN7 SP1
	SystemSessionBigPoolInformation, // since WIN8
	SystemBootGraphicsInformation,
	SystemScrubPhysicalMemoryInformation,
	SystemBadPageInformation,
	SystemProcessorProfileControlArea,
	SystemCombinePhysicalMemoryInformation, // 130
	SystemEntropyInterruptTimingCallback,
	SystemConsoleInformation,
	SystemPlatformBinaryInformation,
	SystemThrottleNotificationInformation,
	SystemHypervisorProcessorCountInformation,
	SystemDeviceDataInformation,
	SystemDeviceDataEnumerationInformation,
	SystemMemoryTopologyInformation,
	SystemMemoryChannelInformation,
	SystemBootLogoInformation, // 140
	SystemProcessorPerformanceInformationEx, // since WINBLUE
	SystemSpare0,
	SystemSecureBootPolicyInformation,
	SystemPageFileInformationEx,
	SystemSecureBootInformation,
	SystemEntropyInterruptTimingRawInformation,
	SystemPortableWorkspaceEfiLauncherInformation,
	SystemFullProcessInformation, // q: SYSTEM_PROCESS_INFORMATION with SYSTEM_PROCESS_INFORMATION_EXTENSION (requires admin)
	SystemKernelDebuggerInformationEx,
	SystemBootMetadataInformation, // 150
	SystemSoftRebootInformation,
	SystemElamCertificateInformation,
	SystemOfflineDumpConfigInformation,
	SystemProcessorFeaturesInformation,
	SystemRegistryReconciliationInformation,
	SystemEdidInformation,
	MaxSystemInfoClass
};

typedef enum _PROCESSINFOCLASS
{
	ProcessBasicInformation, // q: PROCESS_BASIC_INFORMATION, PROCESS_EXTENDED_BASIC_INFORMATION
	ProcessQuotaLimits, // qs: QUOTA_LIMITS, QUOTA_LIMITS_EX
	ProcessIoCounters, // q: IO_COUNTERS
	ProcessVmCounters, // q: VM_COUNTERS, VM_COUNTERS_EX, VM_COUNTERS_EX2
	ProcessTimes, // q: KERNEL_USER_TIMES
	ProcessBasePriority, // s: KPRIORITY
	ProcessRaisePriority, // s: ULONG
	ProcessDebugPort, // q: HANDLE
	ProcessExceptionPort, // s: HANDLE
	ProcessAccessToken, // s: PROCESS_ACCESS_TOKEN
	ProcessLdtInformation, // qs: PROCESS_LDT_INFORMATION // 10
	ProcessLdtSize, // s: PROCESS_LDT_SIZE
	ProcessDefaultHardErrorMode, // qs: ULONG
	ProcessIoPortHandlers, // (kernel-mode only)
	ProcessPooledUsageAndLimits, // q: POOLED_USAGE_AND_LIMITS
	ProcessWorkingSetWatch, // q: PROCESS_WS_WATCH_INFORMATION[]; s: void
	ProcessUserModeIOPL,
	ProcessEnableAlignmentFaultFixup, // s: BOOLEAN
	ProcessPriorityClass, // qs: PROCESS_PRIORITY_CLASS
	ProcessWx86Information,
	ProcessHandleCount, // q: ULONG, PROCESS_HANDLE_INFORMATION // 20
	ProcessAffinityMask, // s: KAFFINITY
	ProcessPriorityBoost, // qs: ULONG
	ProcessDeviceMap, // qs: PROCESS_DEVICEMAP_INFORMATION, PROCESS_DEVICEMAP_INFORMATION_EX
	ProcessSessionInformation, // q: PROCESS_SESSION_INFORMATION
	ProcessForegroundInformation, // s: PROCESS_FOREGROUND_BACKGROUND
	ProcessWow64Information, // q: ULONG_PTR
	ProcessImageFileName, // q: UNICODE_STRING
	ProcessLUIDDeviceMapsEnabled, // q: ULONG
	ProcessBreakOnTermination, // qs: ULONG
	ProcessDebugObjectHandle, // q: HANDLE // 30
	ProcessDebugFlags, // qs: ULONG
	ProcessHandleTracing, // q: PROCESS_HANDLE_TRACING_QUERY; s: size 0 disables, otherwise enables
	ProcessIoPriority, // qs: IO_PRIORITY_HINT
	ProcessExecuteFlags, // qs: ULONG
	ProcessResourceManagement,
	ProcessCookie, // q: ULONG
	ProcessImageInformation, // q: SECTION_IMAGE_INFORMATION
	ProcessCycleTime, // q: PROCESS_CYCLE_TIME_INFORMATION // since VISTA
	ProcessPagePriority, // q: ULONG
	ProcessInstrumentationCallback, // 40
	ProcessThreadStackAllocation, // s: PROCESS_STACK_ALLOCATION_INFORMATION, PROCESS_STACK_ALLOCATION_INFORMATION_EX
	ProcessWorkingSetWatchEx, // q: PROCESS_WS_WATCH_INFORMATION_EX[]
	ProcessImageFileNameWin32, // q: UNICODE_STRING
	ProcessImageFileMapping, // q: HANDLE (input)
	ProcessAffinityUpdateMode, // qs: PROCESS_AFFINITY_UPDATE_MODE
	ProcessMemoryAllocationMode, // qs: PROCESS_MEMORY_ALLOCATION_MODE
	ProcessGroupInformation, // q: USHORT[]
	ProcessTokenVirtualizationEnabled, // s: ULONG
	ProcessConsoleHostProcess, // q: ULONG_PTR
	ProcessWindowInformation, // q: PROCESS_WINDOW_INFORMATION // 50
	ProcessHandleInformation, // q: PROCESS_HANDLE_SNAPSHOT_INFORMATION // since WIN8
	ProcessMitigationPolicy, // s: PROCESS_MITIGATION_POLICY_INFORMATION
	ProcessDynamicFunctionTableInformation,
	ProcessHandleCheckingMode,
	ProcessKeepAliveCount, // q: PROCESS_KEEPALIVE_COUNT_INFORMATION
	ProcessRevokeFileHandles, // s: PROCESS_REVOKE_FILE_HANDLES_INFORMATION
	ProcessWorkingSetControl, // s: PROCESS_WORKING_SET_CONTROL
	ProcessHandleTable, // since WINBLUE
	ProcessCheckStackExtentsMode,
	ProcessCommandLineInformation, // q: UNICODE_STRING // 60
	ProcessProtectionInformation, // q: PS_PROTECTION
	ProcessMemoryExhaustion, // PROCESS_MEMORY_EXHAUSTION_INFO // since THRESHOLD
	ProcessFaultInformation, // PROCESS_FAULT_INFORMATION
	ProcessTelemetryIdInformation, // PROCESS_TELEMETRY_ID_INFORMATION
	ProcessCommitReleaseInformation, // PROCESS_COMMIT_RELEASE_INFORMATION
	ProcessDefaultCpuSetsInformation,
	ProcessAllowedCpuSetsInformation,
	ProcessReserved1Information,
	ProcessReserved2Information,
	ProcessSubsystemProcess, // 70
	ProcessJobMemoryInformation, // PROCESS_JOB_MEMORY_INFO
	ProcessInPrivate, // since THRESHOLD2
	ProcessRaiseUMExceptionOnInvalidHandleClose,
	MaxProcessInfoClass
} PROCESSINFOCLASS;

typedef enum _KWAIT_REASON
{
	Executive,
	FreePage,
	PageIn,
	PoolAllocation,
	DelayExecution,
	Suspended,
	UserRequest,
	WrExecutive,
	WrFreePage,
	WrPageIn,
	WrPoolAllocation,
	WrDelayExecution,
	WrSuspended,
	WrUserRequest,
	WrEventPair,
	WrQueue,
	WrLpcReceive,
	WrLpcReply,
	WrVirtualMemory,
	WrPageOut,
	WrRendezvous,
	WrKeyedEvent,
	WrTerminated,
	WrProcessInSwap,
	WrCpuRateControl,
	WrCalloutStack,
	WrKernel,
	WrResource,
	WrPushLock,
	WrMutex,
	WrQuantumEnd,
	WrDispatchInt,
	WrPreempted,
	WrYieldExecution,
	WrFastMutex,
	WrGuardedMutex,
	WrRundown,
	WrAlertByThreadId,
	WrDeferredPreempt,
	MaximumWaitReason
} KWAIT_REASON, *PKWAIT_REASON;

typedef struct _CLIENT_ID
{
	HANDLE UniqueProcess;
	HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _SYSTEM_THREAD_INFORMATION
{
	LARGE_INTEGER KernelTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER CreateTime;
	ULONG WaitTime;
	PVOID StartAddress;
	CLIENT_ID ClientId;
	LONG Priority;
	LONG BasePriority;
	ULONG ContextSwitches;
	ULONG ThreadState;
	KWAIT_REASON WaitReason;
} SYSTEM_THREAD_INFORMATION, *PSYSTEM_THREAD_INFORMATION;

typedef struct _SYSTEM_PROCESS_INFORMATION
{
	ULONG NextEntryOffset;
	ULONG NumberOfThreads;
	LARGE_INTEGER WorkingSetPrivateSize; // since VISTA
	ULONG HardFaultCount; // since WIN7
	ULONG NumberOfThreadsHighWatermark; // since WIN7
	ULONGLONG CycleTime; // since WIN7
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;
	UNICODE_STRING ImageName;
	LONG BasePriority;
	HANDLE UniqueProcessId;
	HANDLE InheritedFromUniqueProcessId;
	ULONG HandleCount;
	ULONG SessionId;
	ULONG_PTR UniqueProcessKey; // since VISTA (requires SystemExtendedProcessInformation)
	SIZE_T PeakVirtualSize;
	SIZE_T VirtualSize;
	ULONG PageFaultCount;
	SIZE_T PeakWorkingSetSize;
	SIZE_T WorkingSetSize;
	SIZE_T QuotaPeakPagedPoolUsage;
	SIZE_T QuotaPagedPoolUsage;
	SIZE_T QuotaPeakNonPagedPoolUsage;
	SIZE_T QuotaNonPagedPoolUsage;
	SIZE_T PagefileUsage;
	SIZE_T PeakPagefileUsage;
	SIZE_T PrivatePageCount;
	LARGE_INTEGER ReadOperationCount;
	LARGE_INTEGER WriteOperationCount;
	LARGE_INTEGER OtherOperationCount;
	LARGE_INTEGER ReadTransferCount;
	LARGE_INTEGER WriteTransferCount;
	LARGE_INTEGER OtherTransferCount;
	SYSTEM_THREAD_INFORMATION Threads[1];
} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;

typedef struct _OBJECT_ATTRIBUTES
{
	ULONG Length;
	HANDLE RootDirectory;
	PUNICODE_STRING ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor; // PSECURITY_DESCRIPTOR;
	PVOID SecurityQualityOfService; // PSECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#define InitializeObjectAttributes(p, n, a, r, s) { \
	(p)->Length = sizeof(OBJECT_ATTRIBUTES); \
	(p)->RootDirectory = r; \
	(p)->Attributes = a; \
	(p)->ObjectName = n; \
	(p)->SecurityDescriptor = s; \
	(p)->SecurityQualityOfService = NULL; \
}

extern "C" {
	NTSYSCALLAPI
		NTSTATUS
		NTAPI
		NtQuerySystemInformation (
			_In_ SYSTEM_INFORMATION_CLASS SystemInformationClass,
			_Out_writes_bytes_opt_ (SystemInformationLength) PVOID SystemInformation,
			_In_ ULONG SystemInformationLength,
			_Out_opt_ PULONG ReturnLength
		);

	NTSYSCALLAPI
		NTSTATUS
		NTAPI
		NtOpenProcess (
			_Out_ PHANDLE ProcessHandle,
			_In_ ACCESS_MASK DesiredAccess,
			_In_ POBJECT_ATTRIBUTES ObjectAttributes,
			_In_opt_ PCLIENT_ID ClientId
		);

	NTSYSCALLAPI
		NTSTATUS
		NTAPI
		NtQueryInformationProcess (
			_In_ HANDLE ProcessHandle,
			_In_ PROCESSINFOCLASS ProcessInformationClass,
			_Out_writes_bytes_ (ProcessInformationLength) PVOID ProcessInformation,
			_In_ ULONG ProcessInformationLength,
			_Out_opt_ PULONG ReturnLength
		);

	NTSYSCALLAPI
		NTSTATUS
		NTAPI
		NtClose (
			_In_ HANDLE Handle
		);
}

#endif // __MAIN_H__
