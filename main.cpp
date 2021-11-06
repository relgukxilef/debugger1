#include <iostream>
#include <cassert>

#define NOMINMAX
#include <Windows.h>
#include <debugapi.h>
#include <dbghelp.h>

void check(bool sucess) {
    if (!sucess) {
        throw std::exception();
    }
}

struct unique_debug_process {
    unique_debug_process(long process_id) : process_id(process_id) {
        check(DebugActiveProcess(process_id));
        check(DebugSetProcessKillOnExit(false));
    }
    ~unique_debug_process() {
        DebugActiveProcessStop(process_id);
    }
    long process_id;
};

struct unique_debug_event {
    unique_debug_event(long sample_duration_milliseconds) {
        check(WaitForDebugEvent(&debug_event, sample_duration_milliseconds));
    }
    ~unique_debug_event() {
        // Resume executing the thread that reported the debugging event.
        ContinueDebugEvent(
            debug_event.dwProcessId, debug_event.dwThreadId, continue_status
        );
    }
    DEBUG_EVENT debug_event;
    DWORD continue_status = DBG_CONTINUE; // exception continuation
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Specify a process id" << std::endl;
        return -1;
    }
    long process_id = std::atol(argv[1]);

    try {

        unique_debug_process process(process_id);

        int sample_duration_milliseconds = INFINITY;

        HANDLE process_handle;

        for (int i = 0; i < 10; i++) {
            // Wait for a debugging event to occur.

            unique_debug_event event(sample_duration_milliseconds);

            DEBUG_EVENT &debug_event = event.debug_event;

            // Process the debugging event code.

            switch (debug_event.dwDebugEventCode) {
            case EXCEPTION_DEBUG_EVENT:
                // Process the exception code. When handling
                // exceptions, remember to set the continuation
                // status parameter (dwContinueStatus). This value
                // is used by the ContinueDebugEvent function.

                switch(debug_event.u.Exception.ExceptionRecord.ExceptionCode) {
                case EXCEPTION_ACCESS_VIOLATION:
                    // First chance: Pass this on to the system.
                    // Last chance: Display an appropriate error.
                    break;

                case EXCEPTION_BREAKPOINT:
                    // First chance: Display the current
                    // instruction and register values.
                    break;

                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    // First chance: Pass this on to the system.
                    // Last chance: Display an appropriate error.
                    break;

                case EXCEPTION_SINGLE_STEP:
                    // First chance: Update the display of the
                    // current instruction and register values.
                    break;

                case DBG_CONTROL_C:
                    // First chance: Pass this on to the system.
                    // Last chance: Display an appropriate error.
                    break;

                default:
                    // Handle other exceptions.
                    break;
                }

                break;

            case CREATE_THREAD_DEBUG_EVENT: {
                // As needed, examine or change the thread's registers
                // with the GetThreadContext and SetThreadContext functions;
                // and suspend and resume thread execution with the
                // SuspendThread and ResumeThread functions.

                std::cout << "created thread" << std::endl;

                auto &info = debug_event.u.CreateThread;

                CONTEXT context = { .ContextFlags = CONTEXT_FULL, };

                check(GetThreadContext(info.hThread, &context));

                STACKFRAME_EX stack_frame = {
                    .AddrPC = { .Offset = context.Rip, .Mode = AddrModeFlat, },
                    .AddrFrame = {
                        .Offset = context.Rsp, .Mode = AddrModeFlat,
                    },
                    .AddrStack = {
                        .Offset = context.Rsp, .Mode = AddrModeFlat,
                    },
                };

                for (int i = 0; i < 20; i++) {

                    if (!StackWalkEx(
                        IMAGE_FILE_MACHINE_AMD64, process_handle, info.hThread,
                        &stack_frame, &context, nullptr,
                        SymFunctionTableAccess64,
                        SymGetModuleBase64, nullptr,
                        SYM_STKWALK_DEFAULT
                    )) {
                        break;
                    }

                    auto program_counter = stack_frame.AddrPC.Offset;

                    std::cout << std::hex << program_counter << ", ";

                    if (stack_frame.AddrReturn.Offset == 0) {
                        break;
                    }
                }

                std::cout << std::endl;

                break;
            }
            case CREATE_PROCESS_DEBUG_EVENT: {
                // As needed, examine or change the registers of the
                // process's initial thread with the GetThreadContext and
                // SetThreadContext functions; read from and write to the
                // process's virtual memory with the ReadProcessMemory and
                // WriteProcessMemory functions; and suspend and resume
                // thread execution with the SuspendThread and ResumeThread
                // functions. Be sure to close the handle to the process image
                // file with CloseHandle.

                std::cout << "created process" << std::endl;

                auto &info = debug_event.u.CreateProcessInfo;

                process_handle = info.hProcess;

                break;
            }
            case EXIT_THREAD_DEBUG_EVENT:
                // Display the thread's exit code.

                break;

            case EXIT_PROCESS_DEBUG_EVENT:
                // Display the process's exit code.

                break;

            case LOAD_DLL_DEBUG_EVENT:
                // Read the debugging information included in the newly
                // loaded DLL. Be sure to close the handle to the loaded DLL
                // with CloseHandle.

                break;

            case UNLOAD_DLL_DEBUG_EVENT:
                // Display a message that the DLL has been unloaded.

                break;

            case OUTPUT_DEBUG_STRING_EVENT:
                // Display the output debugging string.

                break;

            case RIP_EVENT:
                break;
            }
        }

    }  catch (std::exception e) {

    }
}
