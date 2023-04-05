#include "crosimxcams.h"

struct imxreg {
	UINT16 reg;
	UINT8 val;
};

#include "imx319.h"
#include "imx355.h"

#define IMX_REG_MODE_SELECT		0x0100
#define IMX_MODE_STANDBY		0x00
#define IMX_MODE_STREAMING		0x01

#define bool int

static ULONG CrosImxDebugLevel = 100;
static ULONG CrosImxDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	CrosImxPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, CrosImxEvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		CrosImxPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

NTSTATUS imx_reg_read(
	_In_ PCROSIMX_CONTEXT pDevice,
	UINT16 reg,
	UINT32 len,
	UINT32* val
) {
	UINT8 addr_buf[2];
	UINT8 data_buf[4] = { 0 };

	if (len > 4) {
		return STATUS_INVALID_PARAMETER;
	}

	put_unaligned_be16(reg, addr_buf);

	NTSTATUS status = SpbXferDataSynchronously(&pDevice->I2CContext, addr_buf, sizeof(addr_buf), &data_buf[4 - len], len);
	
	*val = get_unaligned_be32(data_buf);

	return status;
}

NTSTATUS imx_reg_write(
	_In_ PCROSIMX_CONTEXT pDevice,
	UINT16 reg,
	UINT32 len,
	UINT32 val
) {
	UINT8 buf[6];

	if (len > 4) {
		return STATUS_INVALID_PARAMETER;
	}

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);

	return SpbWriteDataSynchronously(&pDevice->I2CContext, buf, sizeof(buf));
}

NTSTATUS
GetDeviceHID(
	_In_ WDFDEVICE FxDevice
)
{
	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"_HID"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 32;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	if (outputBuffer->Count < 1) {
		goto Exit;
	}

	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	UINT32 chipId;

	if (strncmp(outputBuffer->Argument[0].Data, "SONY319A", outputBuffer->Argument[0].DataLength) == 0) {
		pDevice->chipModel = IMX319_CHIP_ID;
		pDevice->cameraType = CSCameraEndpointTypeFront;

		status = imx_reg_read(pDevice, IMX319_REG_CHIP_ID, 2, &chipId);
		if (NT_SUCCESS(status)) {
			if (chipId != IMX319_CHIP_ID) {
				DbgPrint("Chip Id mismatch! %x != %x", IMX319_CHIP_ID, chipId);
				status = STATUS_DEVICE_POWER_FAILURE;
			}
		}
	}
	else if (strncmp(outputBuffer->Argument[0].Data, "SONY355A", outputBuffer->Argument[0].DataLength) == 0) {
		pDevice->chipModel = IMX355_CHIP_ID;
		pDevice->cameraType = CSCameraEndpointTypeRear;

		status = imx_reg_read(pDevice, IMX355_REG_CHIP_ID, 2, &chipId);
		if (NT_SUCCESS(status)) {
			if (chipId != IMX355_CHIP_ID) {
				DbgPrint("Chip Id mismatch! %x != %x", IMX355_CHIP_ID, chipId);
				status = STATUS_DEVICE_POWER_FAILURE;
			}
		}
	}
	else {
		status = STATUS_ACPI_INVALID_ARGUMENT;
	}

Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}
int CsCameraArg2 = 1;

static NTSTATUS GetIntegerProperty(
	_In_ WDFDEVICE FxDevice,
	char *propertyStr,
	UINT16 *property
) {
	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	WDFMEMORY outputMemory = WDF_NO_HANDLE;

	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;

	size_t inputBufferLen = sizeof(ACPI_GET_DEVICE_SPECIFIC_DATA) + strlen(propertyStr) + 1;
	ACPI_GET_DEVICE_SPECIFIC_DATA* inputBuffer = ExAllocatePoolZero(NonPagedPool, inputBufferLen, CROSIMX_POOL_TAG);
	if (!inputBuffer) {
		goto Exit;
	}
	RtlZeroMemory(inputBuffer, inputBufferLen);

	inputBuffer->Signature = IOCTL_ACPI_GET_DEVICE_SPECIFIC_DATA_SIGNATURE;

	unsigned char uuidend[] = { 0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01 };

	inputBuffer->Section.Data1 = 0xdaffd814;
	inputBuffer->Section.Data2 = 0x6eba;
	inputBuffer->Section.Data3 = 0x4d8c;
	memcpy(inputBuffer->Section.Data4, uuidend, sizeof(uuidend)); //Avoid Windows defender false positive

	strcpy(inputBuffer->PropertyName, propertyStr);
	inputBuffer->PropertyNameLength = strlen(propertyStr) + 1;

	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 8;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + sizeof(ACPI_METHOD_ARGUMENT_V1) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;
	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		&outputBuffer);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, inputBuffer, (ULONG)inputBufferLen);
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_GET_DEVICE_SPECIFIC_DATA,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		CrosImxPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Error getting device data - 0x%x\n",
			status);
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE_V1 &&
		outputBuffer->Count < 1 &&
		outputBuffer->Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER &&
		outputBuffer->Argument->DataLength < 1) {
		status = STATUS_ACPI_INVALID_ARGUMENT;
		goto Exit;
	}

	if (property) {
		*property = outputBuffer->Argument->Data[0] & 0xF;
	}

Exit:
	if (inputBuffer) {
		ExFreePoolWithTag(inputBuffer, CROSIMX_POOL_TAG);
	}
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

NTSTATUS
StartStreaming(
	PCROSIMX_CONTEXT pDevice
) {
	NTSTATUS status = STATUS_SUCCESS;

	pDevice->DeviceStreaming = TRUE;
	return status;
}

NTSTATUS
StopStreaming(
	PCROSIMX_CONTEXT pDevice
) {
	NTSTATUS status = STATUS_SUCCESS;

	status = imx_reg_write(pDevice, IMX_REG_MODE_SELECT, 1, IMX_MODE_STANDBY);
	
	pDevice->DeviceStreaming = FALSE;
	return status;
}

VOID
CSCameraRegisterEndpoint(
	PCROSIMX_CONTEXT pDevice
) {
	return;

	CsCameraArg arg;
	RtlZeroMemory(&arg, sizeof(CsCameraArg));
	arg.argSz = sizeof(CsCameraArg);
	arg.endpointType = pDevice->cameraType;
	arg.endpointRequest = CSCameraEndpointRegister;
	ExNotifyCallback(pDevice->CSCameraAPICallback, &arg, &CsCameraArg2);
}

VOID
CsCameraCallbackFunction(
	IN PCROSIMX_CONTEXT  pDevice,
	CsCameraArg* arg,
	PVOID Argument2
) {
	if (!pDevice) {
		return;
	}

	if (Argument2 == &CsCameraArg2) {
		return;
	}

	CsCameraArg localArg;
	RtlZeroMemory(&localArg, sizeof(CsCameraArg));
	RtlCopyMemory(&localArg, arg, min(arg->argSz, sizeof(CsCameraArg)));

	if (localArg.endpointType == CSCameraEndpointTypeCIO && localArg.endpointRequest == CSCameraEndpointRegister) {
		CSCameraRegisterEndpoint(pDevice);
	}
	else if (localArg.endpointType != pDevice->cameraType) {
		return;
	}

	if (localArg.endpointRequest == CSCameraEndpointStop) {
		StopStreaming(pDevice);
	}
	if (localArg.endpointRequest == CSCameraEndpointStart) {
		StartStreaming(pDevice);
	}
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = GetDeviceHID(FxDevice);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	if (pDevice->CSCameraAPICallbackObj) {
		ExUnregisterCallback(pDevice->CSCameraAPICallbackObj);
		pDevice->CSCameraAPICallbackObj = NULL;
	}

	if (pDevice->CSCameraAPICallback) {
		ObfDereferenceObject(pDevice->CSCameraAPICallback);
		pDevice->CSCameraAPICallback = NULL;
	}

	return status;
}

NTSTATUS
OnSelfManagedIoInit(
	_In_
	WDFDEVICE FxDevice
) {
	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	// CS Camera Callback

	UNICODE_STRING CSCameraCallbackAPI;
	RtlInitUnicodeString(&CSCameraCallbackAPI, L"\\CallBack\\CsCameraCallbackAPI");


	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes,
		&CSCameraCallbackAPI,
		OBJ_KERNEL_HANDLE | OBJ_OPENIF | OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
		NULL,
		NULL
	);
	status = ExCreateCallback(&pDevice->CSCameraAPICallback, &attributes, TRUE, TRUE);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	pDevice->CSCameraAPICallbackObj = ExRegisterCallback(pDevice->CSCameraAPICallback,
		CsCameraCallbackFunction,
		pDevice
	);
	if (!pDevice->CSCameraAPICallbackObj) {

		return STATUS_NO_CALLBACK_ACTIVE;
	}

	CSCameraRegisterEndpoint(pDevice);

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PCROSIMX_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	status = StopStreaming(pDevice);

	return STATUS_SUCCESS;
}

NTSTATUS
CrosImxEvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDFQUEUE                      queue;
	PCROSIMX_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	CrosImxPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"CrosImxEvtDeviceAdd called\n");

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceSelfManagedIoInit = OnSelfManagedIoInit;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CROSIMX_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		CrosImxPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = CrosImxEvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status))
	{
		CrosImxPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
	);

	if (!NT_SUCCESS(status))
	{
		CrosImxPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	return status;
}

VOID
CrosImxEvtInternalDeviceControl(
	IN WDFQUEUE     Queue,
	IN WDFREQUEST   Request,
	IN size_t       OutputBufferLength,
	IN size_t       InputBufferLength,
	IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PCROSIMX_CONTEXT     devContext;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	switch (IoControlCode)
	{
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	WdfRequestComplete(Request, status);

	return;
}