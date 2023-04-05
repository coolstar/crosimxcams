#if !defined(_CROSIMX_H_)
#define _CROSIMX_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#include <acpiioct.h>
#include <ntstrsafe.h>

#include "spb.h"

//
// String definitions
//

#define DRIVERNAME                 "crosimxcams.sys: "

#define CROSIMX_POOL_TAG            (ULONG) 'GIMX'

static inline UINT32 get_unaligned_be32(const void* p)
{
	UINT32* pptr = (UINT32 *)(p);
	return _byteswap_ulong (*pptr);
}

static inline void put_unaligned_be16(UINT16 val, PVOID p) {
	UINT16* pptr = (UINT16 *)(p);
	*pptr = _byteswap_ushort(val);
}

static inline void put_unaligned_be32(UINT32 val, PVOID p) {
	UINT32* pptr = (UINT32 *)(p);
	*pptr = _byteswap_ulong(val);
}

#define true 1
#define false 0

typedef enum {
	CSCameraEndpointTypeCIO,
	CSCameraEndpointTypeFront,
	CSCameraEndpointTypeRear
} CSCameraEndpointType;

typedef enum {
	CSCameraEndpointRegister,
	CSCameraEndpointStart,
	CSCameraEndpointStop,
	CSAudioEndpointOverrideFormat
} CSAudioEndpointRequest;

typedef struct CSAUDIOFORMATOVERRIDE {
	UINT16 channels;
	UINT16 frequency;
	UINT16 bitsPerSample;
	UINT16 validBitsPerSample;
} CsAudioFormatOverride;

typedef struct CSCAMERAARG {
	UINT32 argSz;
	CSCameraEndpointType endpointType;
	CSAudioEndpointRequest endpointRequest;
	union {
		CsAudioFormatOverride formatOverride;
	};
} CsCameraArg, * PCsCameraArg;

typedef struct _CROSIMX_CONTEXT
{

	WDFDEVICE FxDevice;

	WDFQUEUE ReportQueue;

	SPB_CONTEXT I2CContext;

	UINT32 chipModel;
	UINT32 cameraType;

	BOOLEAN DeviceStreaming;

	PCALLBACK_OBJECT CSCameraAPICallback;
	PVOID CSCameraAPICallbackObj;

} CROSIMX_CONTEXT, *PCROSIMX_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(CROSIMX_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD CrosImxDriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD CrosImxEvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS CrosImxEvtWdmPreprocessMnQueryId;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CrosImxEvtInternalDeviceControl;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 1
#define CrosImxPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (CrosImxDebugLevel >= dbglevel &&                         \
        (CrosImxDebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define CrosImxPrint(dbglevel, fmt, ...) {                       \
}
#endif

#endif