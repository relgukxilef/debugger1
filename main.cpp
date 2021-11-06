#include <iostream>
#include <cassert>
#include <unordered_map>

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
    unique_debug_event(unsigned long sample_duration_milliseconds) {
        timed_out =
            !WaitForDebugEvent(&debug_event, sample_duration_milliseconds);
    }
    ~unique_debug_event() {
        // Resume executing the thread that reported the debugging event.
        if (!timed_out) {
            ContinueDebugEvent(
                debug_event.dwProcessId, debug_event.dwThreadId, continue_status
            );
        }
    }
    bool timed_out;
    DEBUG_EVENT debug_event;
    DWORD continue_status = DBG_CONTINUE; // exception continuation
};

void write_stack_trace(HANDLE process, HANDLE thread) {

    // ContextFlags specifies what parts to include
    CONTEXT context = { .ContextFlags = CONTEXT_CONTROL, };

    check(GetThreadContext(thread, &context));

    STACKFRAME_EX stack_frame = {};

    for (int i = 0; i < 20; i++) {

        if (!StackWalkEx(
            IMAGE_FILE_MACHINE_AMD64, process, thread,
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
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Specify a process id" << std::endl;
        return -1;
    }
    long process_id = std::atol(argv[1]);

    try {

        unique_debug_process process(process_id);

        auto sample_duration_milliseconds = 100;

        HANDLE process_handle;

        std::unordered_map<DWORD, HANDLE> threads;

        while (true) {
            // Wait for a debugging event to occur.

            unique_debug_event event(sample_duration_milliseconds);

            if (event.timed_out) {
                DebugBreakProcess(process_handle);
                continue;
            }

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
                    break;

                case EXCEPTION_BREAKPOINT:
                    for (auto &thread : threads) {
                        write_stack_trace(process_handle, thread.second);
                    }
                    break;

                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    break;

                case EXCEPTION_SINGLE_STEP:
                    break;

                case DBG_CONTROL_C:
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

                threads.insert({
                    debug_event.dwThreadId, debug_event.u.CreateThread.hThread
                });

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

                // main thread doesn't trigger CREATE_THREAD_DEBUG_EVENT
                threads.insert({debug_event.dwThreadId, info.hThread});

                break;
            }
            case EXIT_THREAD_DEBUG_EVENT:
                threads.erase(debug_event.dwThreadId);
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
