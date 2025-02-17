/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
// Get "_daylight: has bad storage class" in time.h
#define	_INC_TIME


#include <sys/kstat.h>
#include <sys/kstat_windows.h>

#include <ntddk.h>
#include <storport.h>

#include <sys/taskq.h>
#include <sys/wzvol.h>

#include "Trace.h"

DRIVER_INITIALIZE DriverEntry;

extern int initDbgCircularBuffer(void);
extern int finiDbgCircularBuffer(void);
extern int spl_start(PUNICODE_STRING RegistryPath);
extern int spl_stop(void);
extern int zfs_start(void);
extern void zfs_stop(void);
extern void windows_delay(int ticks);
extern int zfs_vfsops_init(void);
extern int zfs_vfsops_fini(void);
int zfs_kmod_init(void);
void zfs_kmod_fini(void);

#ifdef __clang__
#error "This file should be compiled with MSVC not Clang"
#endif

PDRIVER_OBJECT WIN_DriverObject = NULL;
PDRIVER_UNLOAD STOR_DriverUnload;
PDRIVER_DISPATCH STOR_MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];

wzvolDriverInfo STOR_wzvolDriverInfo;

DRIVER_UNLOAD OpenZFS_Fini;

void
OpenZFS_Fini(PDRIVER_OBJECT DriverObject)
{
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "OpenZFS_Fini\n"));

	zfs_vfsops_fini();

	if (STOR_DriverUnload != NULL) {
		STOR_DriverUnload(DriverObject);
		STOR_DriverUnload = NULL;
	}

	zfs_kmod_fini();

	system_taskq_fini();

	kstat_windows_fini();
	spl_stop();
	finiDbgCircularBuffer();

	if (STOR_wzvolDriverInfo.zvContextArray) {
		ExFreePoolWithTag(STOR_wzvolDriverInfo.zvContextArray,
		    MP_TAG_GENERAL);
		STOR_wzvolDriverInfo.zvContextArray = NULL;
	}
	ZFSWppCleanup(DriverObject);
}

/*
 * Setup a Storage Miniport Driver, used only by ZVOL to create virtual disks.
 */
NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING pRegistryPath)
{
	NTSTATUS status;
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
	    "OpenZFS: DriverEntry\n"));

	ZFSWppInit(DriverObject, pRegistryPath);

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
	    "OpenZFS: DriverEntry\n"));

	// Setup global so zfs_ioctl.c can setup devnode
	WIN_DriverObject = DriverObject;

	/* Setup print buffer, since we print from SPL */
	initDbgCircularBuffer();

#ifdef DBG
	// DEBUG build, let's be noisy by default.
	extern int zfs_flags;
	zfs_flags |= 1;
#endif

	spl_start(pRegistryPath);

	kstat_windows_init(pRegistryPath);

	system_taskq_init();

	/*
	 * Initialise storport for the ZVOL virtual disks. This also
	 * sets the Driver Callbacks, so we make a copy of them, so
	 * that Dispatcher can use them.
	 */
	status = zvol_start(DriverObject, pRegistryPath);

	if (STATUS_SUCCESS != status) {
		/* If we failed, we carryon without ZVOL support. */
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "OpenZFS: StorPortInitialize() failed, no ZVOL. %d/0x%x\n",
		    status, status));
		memset(STOR_MajorFunction, 0, sizeof (STOR_MajorFunction));
		STOR_DriverUnload = NULL;
	} else {
		/* Make a copy of the Driver Callbacks for miniport */
		memcpy(STOR_MajorFunction, WIN_DriverObject->MajorFunction,
		    sizeof (STOR_MajorFunction));
		STOR_DriverUnload = WIN_DriverObject->DriverUnload;
	}

	/* Now set the Driver Callbacks to dispatcher and start ZFS */
	WIN_DriverObject->DriverUnload = OpenZFS_Fini;

	/* Start ZFS itself */
	zfs_kmod_init();

	/* Register fs with Win */
	zfs_vfsops_init();

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZFS: Started\n"));
	return (STATUS_SUCCESS);
}

extern int random_get_bytes(void *ptr, unsigned long len);

void
spl_create_hostid(HANDLE h, PUNICODE_STRING pRegistryPath)
{
	NTSTATUS Status;

	UNICODE_STRING AttachKey;
	RtlInitUnicodeString(&AttachKey, L"hostid");

	random_get_bytes(&spl_hostid, sizeof (spl_hostid));

	Status =  ZwSetValueKey(
	    h,
	    &AttachKey,
	    0,
	    REG_DWORD,
	    &spl_hostid,
	    sizeof (spl_hostid));

	if (!NT_SUCCESS(Status)) {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "%s: Unable to create Registry %wZ/hostid: 0x%x."
		    " hostid unset.\n", __func__, pRegistryPath, Status));
		spl_hostid = 0;
	}

	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "SPL: created hostid 0x%04x\n", spl_hostid));
}

// Whenever we start up, write the version string to registry.

void
spl_update_version(HANDLE h, PUNICODE_STRING pRegistryPath)
{
	NTSTATUS Status;

	UNICODE_STRING AttachKey;
	UNICODE_STRING ValueKey;
	// FIX me
	RtlInitUnicodeString(&AttachKey, L"version");
	RtlInitUnicodeString(&ValueKey, L"fixme");
	// RtlInitUnicodeString(&ValueKey,
	//    L"fixme"ZFS_META_VERSION "-" ZFS_META_RELEASE);

	Status = ZwSetValueKey(
	    h,
	    &AttachKey,
	    0,
	    REG_SZ,
	    ValueKey.Buffer,
	    ValueKey.Length);

	if (!NT_SUCCESS(Status)) {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "%s: Unable to create Registry %wZ/version: 0x%x."
		    " hostid unset.\n", __func__, pRegistryPath, Status));
	}
}

int
spl_check_assign_types(kstat_named_t *kold,
    PKEY_VALUE_FULL_INFORMATION regBuffer)
{

	switch (kold->data_type) {

	case KSTAT_DATA_UINT64:
	case KSTAT_DATA_INT64:
	{
		if (regBuffer->Type != REG_QWORD ||
		    regBuffer->DataLength != sizeof (uint64_t)) {
			KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			    "%s: registry '%s' matched in kstat. "
			    "Type needs to be REG_QWORD. (8 bytes)\n", __func__,
			    kold->name));
			return (0);
		}
		uint64_t newvalue =
		    *(uint64_t *)((uint8_t *)regBuffer + regBuffer->DataOffset);
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "%s: kstat '%s': 0x%llx -> 0x%llx\n", __func__,
		    kold->name,
		    kold->value.ui64,
		    newvalue));
		kold->value.ui64 = newvalue;
		return (1);
	}

	case KSTAT_DATA_UINT32:
	case KSTAT_DATA_INT32:
	{
		if (regBuffer->Type != REG_DWORD ||
		    regBuffer->DataLength != sizeof (uint32_t)) {
			KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			    "%s: registry '%s' matched in kstat. "
			    "Type needs to be REG_DWORD. (4 bytes)\n", __func__,
			    kold->name));
			return (0);
		}
		uint32_t newvalue =
		    *(uint32_t *)((uint8_t *)regBuffer + regBuffer->DataOffset);
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "%s: kstat '%s': 0x%lx -> 0x%lx\n", __func__,
		    kold->name,
		    kold->value.ui32,
		    newvalue));
		kold->value.ui32 = newvalue;
		return (1);
	}
	default:
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "%s: registry '%s' matched in kstat of unsupported type."
		    "Only INT32 and INT64 types supported.\n", __func__,
		    kold->name));
	}
	return (0);
}

extern wchar_t zfs_vdev_protection_filter[64];
int
spl_kstat_registry(void *arg, kstat_t *ksp)
{
	OBJECT_ATTRIBUTES ObjectAttributes;
	HANDLE h;
	NTSTATUS Status;
	PUNICODE_STRING pRegistryPath = arg;

	InitializeObjectAttributes(&ObjectAttributes,
	    pRegistryPath,
	    OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
	    NULL,
	    NULL);

	Status = ZwOpenKey(&h,	// KeyHandle
	    KEY_ALL_ACCESS,	// DesiredAccess
	    &ObjectAttributes);	// ObjectAttributes

	if (!NT_SUCCESS(Status)) {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
		    "%s: Unable to open Registry %wZ: 0x%x."
		    "Going with defaults.\n", __func__, pRegistryPath, Status));
		return (0);
	}

	// Iterate all Registry entries.
	NTSTATUS status = 0;
	ULONG index = 0;
	ULONG length;
	PKEY_VALUE_FULL_INFORMATION    regBuffer;
	char keyname[KSTAT_STRLEN + 1];
	int changed = 0;

	for (index = 0; status != STATUS_NO_MORE_ENTRIES; index++) {
		// Get the buffer size necessary
		status = ZwEnumerateValueKey(h, index, KeyValueFullInformation,
		    NULL, 0, &length);

		if ((status != STATUS_BUFFER_TOO_SMALL) &&
		    (status != STATUS_BUFFER_OVERFLOW))
			break; // Something is wrong - or we finished

		// Allocate space to hold
		regBuffer = (PKEY_VALUE_FULL_INFORMATION)ExAllocatePoolWithTag(
		    NonPagedPoolNx, length, 'zfsr');

		if (regBuffer == NULL)
			continue;

		status = ZwEnumerateValueKey(h, index, KeyValueFullInformation,
		    regBuffer, length, &length);
		if (!NT_SUCCESS(status)) {
			ExFreePool(regBuffer);
			continue;
		}

		// Convert name to straight ascii so we compare with kstat
		ULONG outlen;
		status = RtlUnicodeToUTF8N(keyname, KSTAT_STRLEN, &outlen,
		    regBuffer->Name, regBuffer->NameLength);

		// Conversion failed? move along..
		if (status != STATUS_SUCCESS &&
		    status != STATUS_SOME_NOT_MAPPED) {
			ExFreePool(regBuffer);
			continue;
		}

		// Output string is only null terminated if input is, do so now.
		keyname[outlen] = 0;

		// Support registry values that are not tunable through kstat.
		// Those bypass the kstat name matching loop
		// and get directly set in the corresponding code variable.
		//
		if (strcasecmp("zfs_vdev_protection_filter", keyname) == 0) {
			if (regBuffer->Type != REG_SZ ||
			    regBuffer->DataLength >
			    (sizeof (zfs_vdev_protection_filter) -
			    sizeof (wchar_t))) {
				// will be NULL terminated
				KdPrintEx((DPFLTR_IHVDRIVER_ID,
				    DPFLTR_ERROR_LEVEL,
				    "%s: registry '%s'. "
				    "Type needs to be REG_SZ (63 wchar max)\n",
				    __func__,
				    keyname));
				ExFreePool(regBuffer);
				continue;
			}
			char *newvalue = (char *)((uint8_t *)regBuffer +
			    regBuffer->DataOffset);
			KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
			    "%s: registry '%s': %S\n", __func__,
			    keyname, newvalue));

			bzero(zfs_vdev_protection_filter,
			    sizeof (zfs_vdev_protection_filter));
			bcopy(newvalue, zfs_vdev_protection_filter,
			    min(sizeof (zfs_vdev_protection_filter),
			    regBuffer->DataLength));
		} else {
			// Now iterate kstats and match name with 'keyname'.
			kstat_named_t *kold;
			kold = ksp->ks_data;
			for (unsigned int i = 0; i < ksp->ks_ndata;
			    i++, kold++) {

				// Find name?
				if (kold != NULL &&
				    !strcasecmp(kold->name, keyname)) {

					// Check types match and are supported
					if (!spl_check_assign_types(kold,
					    regBuffer))
						break;

					// Special case 'hostid' is
					// automatically generated if
					// not set, so if we read it
					// in, signal to not set it.
					// KSTAT_UPDATE is called after
					// this function completes.
					if (spl_hostid == 0 &&
					    strcasecmp("hostid", keyname) == 0)
						spl_hostid = 1; // Non-zero

					changed++;
					break;
				}
			}
		}

		ExFreePool(regBuffer);
	} // for() all keys

	// Now check that hostid was read it, if it wasn't,
	// make up a random one.
	if (spl_hostid == 0) {
		spl_create_hostid(h, pRegistryPath);
	}

	// Make sure version is updated
	spl_update_version(h, pRegistryPath);

	ZwClose(h);
	return (changed);
}
