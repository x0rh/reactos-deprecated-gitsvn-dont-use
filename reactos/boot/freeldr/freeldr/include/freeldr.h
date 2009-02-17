/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __FREELDR_H
#define __FREELDR_H

#define UINT64_C(val) val##ULL
#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))

#define ROUND_DOWN(n, align) \
    (((ULONG)n) & ~((align) - 1l))

#define ROUND_UP(n, align) \
    ROUND_DOWN(((ULONG)n) + (align) - 1, (align))

#define NTOSAPI
#define printf TuiPrintf
#include <ntddk.h>
#include <ioaccess.h>
#include <arc/arc.h>
#include <ketypes.h>
#include <mmtypes.h>
#include <ndk/asm.h>
#include <ndk/rtlfuncs.h>
#include <ndk/ldrtypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <rosldr.h>
#include <arch.h>
#include <rtl.h>
#include <disk.h>
#include <fs.h>
#include <ui.h>
#include <multiboot.h>
#include <mm.h>
#include <cache.h>
#include <machine.h>
#include <inifile.h>
#include <inffile.h>
#include <video.h>
#include <ramdisk.h>
#include <reactos.h>
#include <registry.h>
#include <winldr.h>
#include <fsrec.h>
/* file system headers */
#include <fs/ext2.h>
#include <fs/fat.h>
#include <fs/ntfs.h>
#include <fs/iso.h>
/* ui support */
#include <ui/gui.h>
#include <ui/minitui.h>
#include <ui/noui.h>
#include <ui/tui.h>
/* arch files */
#if defined(_M_IX86)
#include <arch/i386/hardware.h>
#include <arch/i386/i386.h>
#include <arch/i386/machpc.h>
#include <arch/i386/machxbox.h>
#include <internal/i386/intrin_i.h>
#include <internal/i386/ke.h>
#elif defined(_M_PPC)
#include <arch/powerpc/hardware.h>
#elif defined(_M_ARM)
#include <arch/arm/hardware.h>
#elif defined(_M_MIPS)
#include <arch/mips/arcbios.h>
#elif defined(_M_AMD64)
#include <arch/amd64/hardware.h>
#include <arch/amd64/machpc.h>
#endif
/* misc files */
#include <keycodes.h>
#include <ver.h>
#include <cmdline.h>
#include <bget.h>
#include <winerror.h>
/* Needed by boot manager */
#include <bootmgr.h>
#include <oslist.h>
#include <drivemap.h>
#include <miscboot.h>
#include <options.h>
#include <linux.h>
/* Externals */
#include <reactos/rossym.h>
#include <reactos/buildno.h>
/* Needed if debuging is enabled */
#include <comm.h>
/* Swap */
#include <bytesex.h>

/* arch defines */
#ifdef _X86_
#define Ke386EraseFlags(x)     __asm__ __volatile__("pushl $0 ; popfl\n")
#endif

#ifdef _M_AMD64
#define KeAmd64EraseFlags(x)     __asm__ __volatile__("pushq $0 ; popfq\n")
#endif

VOID BootMain(LPSTR CmdLine);
VOID RunLoader(VOID);

/* Special hack for ReactOS setup OS type */
VOID LoadReactOSSetup(VOID);

#endif  // defined __FREELDR_H
