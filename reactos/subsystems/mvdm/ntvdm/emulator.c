/*
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Virtual DOS Machine
 * FILE:            emulator.c
 * PURPOSE:         Minimal x86 machine emulator for the VDM
 * PROGRAMMERS:     Aleksandar Andrejevic <theflash AT sdf DOT lonestar DOT org>
 */

/* INCLUDES *******************************************************************/

#define NDEBUG

#include "ntvdm.h"
#include "emulator.h"
#include "memory.h"

#include "cpu/callback.h"
#include "cpu/cpu.h"
#include "cpu/bop.h"
#include <isvbop.h>

#include "int32.h"

#include "clock.h"
#include "bios/rom.h"
#include "hardware/cmos.h"
#include "hardware/dma.h"
#include "hardware/keyboard.h"
#include "hardware/mouse.h"
#include "hardware/pic.h"
#include "hardware/ps2.h"
#include "hardware/sound/speaker.h"
#include "hardware/pit.h"
#include "hardware/video/vga.h"

#include "vddsup.h"
#include "io.h"

/* PRIVATE VARIABLES **********************************************************/

LPVOID  BaseAddress = NULL;
BOOLEAN VdmRunning  = TRUE;

static BOOLEAN A20Line   = FALSE;
static BYTE Port61hState = 0x00;

static HANDLE InputThread = NULL;

LPCWSTR ExceptionName[] =
{
    L"Division By Zero",
    L"Debug",
    L"Unexpected Error",
    L"Breakpoint",
    L"Integer Overflow",
    L"Bound Range Exceeded",
    L"Invalid Opcode",
    L"FPU Not Available"
};

/* BOP Identifiers */
#define BOP_DEBUGGER    0x56    // Break into the debugger from a 16-bit app

/* PRIVATE FUNCTIONS **********************************************************/

VOID WINAPI EmulatorReadMemory(PFAST486_STATE State, ULONG Address, PVOID Buffer, ULONG Size)
{
    UNREFERENCED_PARAMETER(State);

    /* Mirror 0x000FFFF0 at 0xFFFFFFF0 */
    if (Address >= 0xFFFFFFF0) Address -= 0xFFF00000;

    /* If the A20 line is disabled, mask bit 20 */
    if (!A20Line) Address &= ~(1 << 20); 

    if (Address >= MAX_ADDRESS) return;
    Size = min(Size, MAX_ADDRESS - Address);

    /* Read while calling fast memory hooks */
    MemRead(Address, Buffer, Size);
}

VOID WINAPI EmulatorWriteMemory(PFAST486_STATE State, ULONG Address, PVOID Buffer, ULONG Size)
{
    UNREFERENCED_PARAMETER(State);

    /* If the A20 line is disabled, mask bit 20 */
    if (!A20Line) Address &= ~(1 << 20); 

    if (Address >= MAX_ADDRESS) return;
    Size = min(Size, MAX_ADDRESS - Address);

    /* Write while calling fast memory hooks */
    MemWrite(Address, Buffer, Size);
}

UCHAR WINAPI EmulatorIntAcknowledge(PFAST486_STATE State)
{
    UNREFERENCED_PARAMETER(State);

    /* Get the interrupt number from the PIC */
    return PicGetInterrupt();
}

VOID WINAPI EmulatorFpu(PFAST486_STATE State)
{
    /* The FPU is wired to IRQ 13 */
    PicInterruptRequest(13);
}

VOID EmulatorException(BYTE ExceptionNumber, LPWORD Stack)
{
    WORD CodeSegment, InstructionPointer;
    PBYTE Opcode;

    ASSERT(ExceptionNumber < 8);

    /* Get the CS:IP */
    InstructionPointer = Stack[STACK_IP];
    CodeSegment = Stack[STACK_CS];
    Opcode = (PBYTE)SEG_OFF_TO_PTR(CodeSegment, InstructionPointer);

    /* Display a message to the user */
    DisplayMessage(L"Exception: %s occured at %04X:%04X\n"
                   L"Opcode: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                   ExceptionName[ExceptionNumber],
                   CodeSegment,
                   InstructionPointer,
                   Opcode[0],
                   Opcode[1],
                   Opcode[2],
                   Opcode[3],
                   Opcode[4],
                   Opcode[5],
                   Opcode[6],
                   Opcode[7],
                   Opcode[8],
                   Opcode[9]);

    Fast486DumpState(&EmulatorContext);

    /* Stop the VDM */
    EmulatorTerminate();
    return;
}

VOID EmulatorTerminate(VOID)
{
    /* Stop the VDM */
    CpuUnsimulate(); // Halt the CPU
    VdmRunning = FALSE;
}

VOID EmulatorInterruptSignal(VOID)
{
    /* Call the Fast486 API */
    Fast486InterruptSignal(&EmulatorContext);
}

VOID EmulatorSetA20(BOOLEAN Enabled)
{
    A20Line = Enabled;
}

BOOLEAN EmulatorGetA20(VOID)
{
    return A20Line;
}

static VOID WINAPI EmulatorDebugBreakBop(LPWORD Stack)
{
    DPRINT1("NTVDM: BOP_DEBUGGER\n");
    DebugBreak();
}

static BYTE WINAPI Port61hRead(USHORT Port)
{
    return Port61hState;
}

static VOID WINAPI Port61hWrite(USHORT Port, BYTE Data)
{
    // BOOLEAN SpeakerStateChange = FALSE;
    BYTE OldPort61hState = Port61hState;

    /* Only the four lowest bytes can be written */
    Port61hState = (Port61hState & 0xF0) | (Data & 0x0F);

    if ((OldPort61hState ^ Port61hState) & 0x01)
    {
        DPRINT("PIT 2 Gate %s\n", Port61hState & 0x01 ? "on" : "off");
        PitSetGate(2, !!(Port61hState & 0x01));
        // SpeakerStateChange = TRUE;
    }

    if ((OldPort61hState ^ Port61hState) & 0x02)
    {
        /* There were some change for the speaker... */
        DPRINT("Speaker %s\n", Port61hState & 0x02 ? "on" : "off");
        // SpeakerStateChange = TRUE;
    }
    // if (SpeakerStateChange) SpeakerChange(Port61hState);
    SpeakerChange(Port61hState);
}

static VOID WINAPI PitChan0Out(LPVOID Param, BOOLEAN State)
{
    if (State)
    {
        DPRINT("PicInterruptRequest\n");
        PicInterruptRequest(0); // Raise IRQ 0
    }
    // else < Lower IRQ 0 >
}

static VOID WINAPI PitChan1Out(LPVOID Param, BOOLEAN State)
{
#if 0
    if (State)
    {
        /* Set bit 4 of Port 61h */
        Port61hState |= 1 << 4;
    }
    else
    {
        /* Clear bit 4 of Port 61h */
        Port61hState &= ~(1 << 4);
    }
#else
    Port61hState = (Port61hState & 0xEF) | (State << 4);
#endif
}

static VOID WINAPI PitChan2Out(LPVOID Param, BOOLEAN State)
{
    BYTE OldPort61hState = Port61hState;

#if 0
    if (State)
    {
        /* Set bit 5 of Port 61h */
        Port61hState |= 1 << 5;
    }
    else
    {
        /* Clear bit 5 of Port 61h */
        Port61hState &= ~(1 << 5);
    }
#else
    Port61hState = (Port61hState & 0xDF) | (State << 5);
#endif

    if ((OldPort61hState ^ Port61hState) & 0x20)
    {
        DPRINT("PitChan2Out -- Port61hState changed\n");
        SpeakerChange(Port61hState);
    }
}


static DWORD
WINAPI
PumpConsoleInput(LPVOID Parameter)
{
    HANDLE ConsoleInput = (HANDLE)Parameter;
    INPUT_RECORD InputRecord;
    DWORD Count;

    while (VdmRunning)
    {
        /* Make sure the task event is signaled */
        WaitForSingleObject(VdmTaskEvent, INFINITE);

        /* Wait for an input record */
        if (!ReadConsoleInput(ConsoleInput, &InputRecord, 1, &Count))
        {
            DWORD LastError = GetLastError();
            DPRINT1("Error reading console input (0x%p, %lu) - Error %lu\n", ConsoleInput, Count, LastError);
            return LastError;
        }

        ASSERT(Count != 0);

        /* Check the event type */
        switch (InputRecord.EventType)
        {
            /*
             * Hardware events
             */
            case KEY_EVENT:
                KeyboardEventHandler(&InputRecord.Event.KeyEvent);
                break;

            case MOUSE_EVENT:
                MouseEventHandler(&InputRecord.Event.MouseEvent);
                break;

            case WINDOW_BUFFER_SIZE_EVENT:
                ScreenEventHandler(&InputRecord.Event.WindowBufferSizeEvent);
                break;

            /*
             * Interface events
             */
            case MENU_EVENT:
                MenuEventHandler(&InputRecord.Event.MenuEvent);
                break;

            case FOCUS_EVENT:
                FocusEventHandler(&InputRecord.Event.FocusEvent);
                break;

            default:
                break;
        }
    }

    return 0;
}

/* PUBLIC FUNCTIONS ***********************************************************/

static VOID
DumpMemoryRaw(HANDLE hFile)
{
    PVOID  Buffer;
    SIZE_T Size;

    /* Dump the VM memory */
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    Buffer = REAL_TO_PHYS(NULL);
    Size   = MAX_ADDRESS - (ULONG_PTR)(NULL);
    WriteFile(hFile, Buffer, Size, &Size, NULL);
}

static VOID
DumpMemoryTxt(HANDLE hFile)
{
#define LINE_SIZE   75 + 2
    ULONG  i;
    PBYTE  Ptr1, Ptr2;
    CHAR   LineBuffer[LINE_SIZE];
    PCHAR  Line;
    SIZE_T LineSize;

    /* Dump the VM memory */
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    Ptr1 = Ptr2 = REAL_TO_PHYS(NULL);
    while (MAX_ADDRESS - (ULONG_PTR)PHYS_TO_REAL(Ptr1) > 0)
    {
        Ptr1 = Ptr2;
        Line = LineBuffer;

        /* Print the address */
        Line += snprintf(Line, LINE_SIZE + LineBuffer - Line, "%08x ", PHYS_TO_REAL(Ptr1));

        /* Print up to 16 bytes... */

        /* ... in hexadecimal form first... */
        i = 0;
        while (i++ <= 0x0F && (MAX_ADDRESS - (ULONG_PTR)PHYS_TO_REAL(Ptr1) > 0))
        {
            Line += snprintf(Line, LINE_SIZE + LineBuffer - Line, " %02x", *Ptr1);
            ++Ptr1;
        }

        /* ... align with spaces if needed... */
        RtlFillMemory(Line, 0x0F + 4 - i, ' ');
        Line += 0x0F + 4 - i;

        /* ... then in character form. */
        i = 0;
        while (i++ <= 0x0F && (MAX_ADDRESS - (ULONG_PTR)PHYS_TO_REAL(Ptr2) > 0))
        {
            *Line++ = ((*Ptr2 >= 0x20 && *Ptr2 <= 0x7E) || (*Ptr2 >= 0x80 && *Ptr2 < 0xFF) ? *Ptr2 : '.');
            ++Ptr2;
        }

        /* Newline */
        *Line++ = '\r';
        *Line++ = '\n';

        /* Finally write the line to the file */
        LineSize = Line - LineBuffer;
        WriteFile(hFile, LineBuffer, LineSize, &LineSize, NULL);
    }
}

VOID DumpMemory(BOOLEAN TextFormat)
{
    static ULONG DumpNumber = 0;

    HANDLE hFile;
    WCHAR  FileName[MAX_PATH];

    /* Build a suitable file name */
    _snwprintf(FileName, MAX_PATH,
               L"memdump%lu.%s",
               DumpNumber,
               TextFormat ? L"txt" : L"dat");
    ++DumpNumber;

    DPRINT1("Creating memory dump file '%S'...\n", FileName);

    /* Always create the dump file */
    hFile = CreateFileW(FileName,
                        GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DPRINT1("Error when creating '%S' for memory dumping, GetLastError() = %u\n",
                FileName, GetLastError());
        return;
    }

    /* Dump the VM memory in the chosen format */
    if (TextFormat)
        DumpMemoryTxt(hFile);
    else
        DumpMemoryRaw(hFile);

    /* Close the file */
    CloseHandle(hFile);

    DPRINT1("Memory dump done\n");
}

BOOLEAN EmulatorInitialize(HANDLE ConsoleInput, HANDLE ConsoleOutput)
{
    /* Initialize memory */
    if (!MemInitialize())
    {
        wprintf(L"Memory initialization failed.\n");
        return FALSE;
    }

    /* Initialize I/O ports */
    /* Initialize RAM */

    /* Initialize the CPU */

    /* Initialize the internal clock */
    if (!ClockInitialize())
    {
        wprintf(L"FATAL: Failed to initialize the clock\n");
        EmulatorCleanup();
        return FALSE;
    }

    /* Initialize the CPU */
    CpuInitialize();

    /* Initialize DMA */
    DmaInitialize();

    /* Initialize the PIC, the PIT, the CMOS and the PC Speaker */
    PicInitialize();
    PitInitialize();
    CmosInitialize();
    SpeakerInitialize();

    /* Set output functions */
    PitSetOutFunction(0, NULL, PitChan0Out);
    PitSetOutFunction(1, NULL, PitChan1Out);
    PitSetOutFunction(2, NULL, PitChan2Out);

    /* Register the I/O Ports */
    RegisterIoPort(CONTROL_SYSTEM_PORT61H, Port61hRead, Port61hWrite);

    /* Initialize the PS/2 port */
    PS2Initialize();

    /* Initialize the keyboard and mouse and connect them to their PS/2 ports */
    KeyboardInit(0);
    MouseInit(1);

    /**************** ATTACH INPUT WITH CONSOLE *****************/
    /* Start the input thread */
    InputThread = CreateThread(NULL, 0, &PumpConsoleInput, ConsoleInput, 0, NULL);
    if (InputThread == NULL)
    {
        DisplayMessage(L"Failed to create the console input thread.");
        EmulatorCleanup();
        return FALSE;
    }
    /************************************************************/

    /* Initialize the VGA */
    if (!VgaInitialize(ConsoleOutput))
    {
        DisplayMessage(L"Failed to initialize VGA support.");
        EmulatorCleanup();
        return FALSE;
    }

    /* Initialize the software callback system and register the emulator BOPs */
    InitializeInt32();
    RegisterBop(BOP_DEBUGGER  , EmulatorDebugBreakBop);
    // RegisterBop(BOP_UNSIMULATE, CpuUnsimulateBop);

    /* Initialize VDD support */
    VDDSupInitialize();

    return TRUE;
}

VOID EmulatorCleanup(VOID)
{
    VgaCleanup();

    /* Close the input thread handle */
    if (InputThread != NULL) CloseHandle(InputThread);
    InputThread = NULL;

    PS2Cleanup();

    SpeakerCleanup();
    CmosCleanup();
    // PitCleanup();
    // PicCleanup();

    // DmaCleanup();

    CpuCleanup();
    MemCleanup();
}



VOID
WINAPI
VDDSimulate16(VOID)
{
    CpuSimulate();
}

VOID
WINAPI
VDDTerminateVDM(VOID)
{
    /* Stop the VDM */
    EmulatorTerminate();
}

/* EOF */
