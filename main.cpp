#include <iostream>
#include <cassert>
#include <unordered_map>
#include <thread>
#include <string>

#define NOMINMAX
#include <Windows.h>
#include <debugapi.h>
#include <dbghelp.h>
#include <OAIdl.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <boost/lockfree/spsc_queue.hpp>

std::string to_string(std::wstring_view wstring) {
    int size = WideCharToMultiByte(
        CP_UTF8, 0, wstring.data(), static_cast<int>(wstring.length()),
        NULL, 0, NULL, NULL
    );
    std::string string(size, 0);
    WideCharToMultiByte(
        CP_UTF8, 0, wstring.data(), static_cast<int>(wstring.length()),
        string.data(), size, NULL, NULL
    );
    return string;
}

const char* get_error_message(DWORD code) {
    switch (code) {
        case 6: return "The handle is invalid.";
        default: return "Unknown.";
    }
}

void check(bool sucess) {
    if (!sucess) {
        auto error = GetLastError();
        std::cout << get_error_message(error) <<
            '(' << error << ')' << std::endl;
        throw std::exception();
    }
}

struct unique_glfw {
    unique_glfw() {
        if (!glfwInit()) {
            throw std::runtime_error("Couldn't initialize GLFW");
        }
    }

    ~unique_glfw() {
        glfwTerminate();
    }
};

struct unique_debug_process {
    unique_debug_process(DWORD process_id) : process_id(process_id) {
        check(DebugActiveProcess(process_id));
        check(DebugSetProcessKillOnExit(false));
    }
    ~unique_debug_process() {
        DebugActiveProcessStop(process_id);
    }
    DWORD process_id;
};

struct unique_debug_event {
    unique_debug_event(DWORD sample_duration_milliseconds) {
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
    DEBUG_EVENT debug_event;
    DWORD continue_status = DBG_CONTINUE; // exception continuation
    bool timed_out;
};

struct sampler {
    sampler(DWORD process_id) : process_id(process_id) {}

    void sample_process() {
        std::vector<DWORD64> threads_frames_program_counter;
        std::vector<unsigned int> threads_frame_size;

        try {
            unique_debug_process process(process_id);

            DWORD sample_duration_milliseconds = 1;

            HANDLE process_handle = {};

            std::unordered_map<DWORD, HANDLE> threads;

            while (!exit) {
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

                    switch(
                        debug_event.u.Exception.ExceptionRecord.ExceptionCode
                    ) {
                    case EXCEPTION_ACCESS_VIOLATION:
                        break;

                    case EXCEPTION_BREAKPOINT:
                        threads_frames_program_counter.clear();
                        threads_frame_size.clear();

                        for (auto &thread : threads) {
                            // ContextFlags specifies what parts to include
                            CONTEXT context = {
                                .ContextFlags = CONTEXT_CONTROL,
                            };

                            check(GetThreadContext(thread.second, &context));

                            STACKFRAME_EX stack_frame = {};

                            unsigned stack_size = 0;

                            for (int i = 0; i < 20; i++) {

                                if (!StackWalkEx(
                                    IMAGE_FILE_MACHINE_AMD64, process_handle,
                                    thread.second,
                                    &stack_frame, &context, nullptr,
                                    SymFunctionTableAccess64,
                                    SymGetModuleBase64, nullptr,
                                    SYM_STKWALK_DEFAULT
                                )) {
                                    break;
                                }

                                auto program_counter =
                                    stack_frame.AddrPC.Offset;

                                threads_frames_program_counter.push_back(
                                    program_counter
                                );
                                stack_size++;

                                if (stack_frame.AddrReturn.Offset == 0) {
                                    break;
                                }
                            }

                            threads_frame_size.push_back(stack_size);
                        }

                        // if there is enough space for this sample
                        if (
                            samples_threads_frames_program_counter.
                                write_available() >=
                            threads_frames_program_counter.size() &&
                            samples_threads_frame_size.write_available() >=
                            threads_frame_size.size() &&
                            samples_thread_size.write_available() > 0
                        ) {
                            // write the sample to queue
                            samples_threads_frames_program_counter.push(
                                threads_frames_program_counter.begin(),
                                threads_frames_program_counter.end()
                            );
                            samples_threads_frame_size.push(
                                threads_frame_size.begin(),
                                threads_frame_size.end()
                            );
                            samples_thread_size.push(
                                static_cast<unsigned int>(threads.size())
                            );
                        }
                        break;
                    case EXCEPTION_DATATYPE_MISALIGNMENT:
                    case EXCEPTION_SINGLE_STEP:
                    case DBG_CONTROL_C:
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

                    //std::cout << "created thread" << std::endl;

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

                case EXIT_PROCESS_DEBUG_EVENT:
                case LOAD_DLL_DEBUG_EVENT:
                case UNLOAD_DLL_DEBUG_EVENT:
                case OUTPUT_DEBUG_STRING_EVENT:
                case RIP_EVENT:
                    break;
                }
            }

        }  catch (std::exception e) {

        }
    }

    boost::lockfree::spsc_queue<unsigned int, boost::lockfree::capacity<128>>
        samples_thread_size;
    boost::lockfree::spsc_queue<unsigned int, boost::lockfree::capacity<128>>
        samples_threads_frame_size;
    boost::lockfree::spsc_queue<DWORD64, boost::lockfree::capacity<1024>>
        samples_threads_frames_program_counter;

    unsigned long process_id;

    std::atomic_bool exit = false;
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Specify a process id" << std::endl;
        return -1;
    }
    DWORD process_id = static_cast<DWORD>(std::atol(argv[1]));

    unique_glfw glfw;

    GLFWwindow* window = glfwCreateWindow(
        1280, 720, "demo", nullptr, nullptr
    );
    if (!window) {
        throw std::runtime_error("Couldn't create window");
    }

    glfwMakeContextCurrent(window);

    sampler s(process_id);

    std::thread sampler_thread([&] () { s.sample_process(); });

    std::vector<unsigned int> samples_thread_size;
    std::vector<unsigned int> threads_frame_size;
    std::vector<DWORD64> frames_program_counter;

    while (!glfwWindowShouldClose(window)) {

        samples_thread_size.resize(s.samples_thread_size.read_available());
        s.samples_thread_size.pop(
            samples_thread_size.data(), samples_thread_size.size()
        );
        for (auto thread_size : samples_thread_size) {

            threads_frame_size.resize(thread_size);
            s.samples_threads_frame_size.pop(
                threads_frame_size.data(), thread_size
            );

            for (auto frame_size : threads_frame_size) {
                frames_program_counter.resize(frame_size);
                s.samples_threads_frames_program_counter.pop(
                    frames_program_counter.data(), frame_size
                );
                std::cout << frame_size << ' ';
            }
            std::cout << std::endl;
        }

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(255, 255, 255, 255);
        glClear(GL_COLOR_BUFFER_BIT);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    s.exit = true;

    sampler_thread.join();
}
