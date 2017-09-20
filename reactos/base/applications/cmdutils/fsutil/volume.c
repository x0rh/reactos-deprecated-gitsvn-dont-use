/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS FS utility tool
 * FILE:            base/applications/cmdutils/volume.c
 * PURPOSE:         FSutil volumes management
 * PROGRAMMERS:     Pierre Schweitzer <pierre@reactos.org>
 */

#include "fsutil.h"

/* Add handlers here for subcommands */
static HandlerProc DismountMain;
static HandlerItem HandlersList[] =
{
    /* Proc, name, help */
    { DismountMain, _T("dismount"), _T("Dismounts a volume") },
};

static int
LockOrUnlockVolume(HANDLE Volume, BOOLEAN Lock)
{
    DWORD Operation;
    ULONG BytesRead;

    Operation = (Lock ? FSCTL_LOCK_VOLUME : FSCTL_UNLOCK_VOLUME);
    if (DeviceIoControl(Volume, Operation, NULL, 0, NULL, 0,
                        &BytesRead, NULL) == FALSE)
    {
        PrintErrorMessage(GetLastError());
        return 1;
    }

    return 0;
}

static int
DismountMain(int argc, const TCHAR *argv[])
{
    HANDLE Volume;
    ULONG BytesRead;

    /* We need a volume (letter or GUID) */
    if (argc < 2)
    {
        _ftprintf(stderr, _T("Usage: fsutil volume dismount <volume>\n"));
        _ftprintf(stderr, _T("\tFor example: fsutil volume dismount d:\n"));
        return 1;
    }

    /* Get a handle for the volume */
    Volume = OpenVolume(argv[1], FALSE);
    if (Volume == INVALID_HANDLE_VALUE)
    {
        return 1;
    }

    /* Attempt to lock the volume */
    if (LockOrUnlockVolume(Volume, TRUE))
    {
        CloseHandle(Volume);
        return 1;
    }

    /* Issue the dismount command */
    if (DeviceIoControl(Volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL,
                        0, &BytesRead, NULL) == FALSE)
    {
        PrintErrorMessage(GetLastError());
        LockOrUnlockVolume(Volume, FALSE);
        CloseHandle(Volume);
        return 1;
    }

    /* Unlock and quit */
    LockOrUnlockVolume(Volume, FALSE);
    CloseHandle(Volume);

    _ftprintf(stdout, _T("The %s volume has been dismounted\n"), argv[1]);

    return 0;
}

static void
PrintUsage(const TCHAR * Command)
{
    PrintDefaultUsage(_T(" VOLUME "), Command, (HandlerItem *)&HandlersList,
                      (sizeof(HandlersList) / sizeof(HandlersList[0])));
}

int
VolumeMain(int argc, const TCHAR *argv[])
{
    return FindHandler(argc, argv, (HandlerItem *)&HandlersList,
                       (sizeof(HandlersList) / sizeof(HandlersList[0])),
                       PrintUsage);
}
