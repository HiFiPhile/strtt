#include <atomic>
#include <vector>
#include <chrono>
#include <thread>
#include <signal.h>

#include "strtt.h"
#include "log.h"
#include "inputparser.h"

// #define SYSVIEW

#ifdef SYSVIEW
#include "sysview.h"
#endif

#ifdef __linux__

#include <sys/resource.h>
#include "kbhit.h"

#elif __APPLE__
#include <sys/resource.h>
#include "kbhit.h"
#elif _WIN32
#include <conio.h>
#else

#endif

// CONSTANTS //////////////////////////////////////////////

const int SYSVIEW_COMM_SERVER_PORT = 19111;
const int DEFAULT_RAM_SIZE_KB = 16;
const uint32_t RAM_START_DEFAULT = RAM_START;
const uint8_t DEFAULT_AP_NUM = 0;
const int RTT_RETRY_DELAY_MS = 500;
const int RTT_TERMINAL_CHANNEL = 0;
const int RTT_SYSVIEW_CHANNEL = 1;

#ifdef __linux__
const int PROCESS_PRIORITY = -11;
#endif

// GLOBAL VARIABLES ///////////////////////////////////////

std::atomic_bool stopApp;

// HELPER MACROS //////////////////////////////////////////

#define START_TS auto __start_ts = std::chrono::high_resolution_clock::now()
#define STOP_TS cycleDuration = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - __start_ts).count()

// SIGNAL HANDLER /////////////////////////////////////////

void signalHandler(int signum)
{
    LOG_INFO("Interrupt signal %d received", signum);
    stopApp = true;
}

// HELPER FUNCTIONS ///////////////////////////////////////

int parseIntegerOption(const std::string& optionValue)
{
    if (optionValue.size() > 1 && optionValue[0] == '0' && (optionValue[1] == 'x' || optionValue[1] == 'X'))
    {
        return std::stoi(optionValue, nullptr, 16);
    }
    return std::stoi(optionValue, nullptr, 0);
}

bool findRttWithRetry(StRtt* strtt, int ramSizeKB, bool loopMode)
{
    bool rttFound = false;
    do
    {
        int result = strtt->findRtt(ramSizeKB);
        if (result == ERROR_OK)
        {
            rttFound = true;
            LOG_INFO("RTT found successfully");
        }
        else
        {
            if (loopMode)
            {
                LOG_INFO("RTT not found, retrying...");
                std::this_thread::sleep_for(std::chrono::milliseconds(RTT_RETRY_DELAY_MS));
            }
            else
            {
                LOG_ERROR("failed to find RTT (%d)", result);
                exit(-1);
            }
        }
    } while (!rttFound && loopMode && !stopApp);

    if (!rttFound)
    {
        LOG_ERROR("RTT search cancelled");
        exit(-1);
    }

    return rttFound;
}

void setupChannelHandlers(StRtt* strtt
#ifdef SYSVIEW
    , SysView* sysView
#endif
)
{
    strtt->addChannelHandler([&](const int index, const std::vector<uint8_t>* buffer)
    {
        if (index == RTT_TERMINAL_CHANNEL)
        {
            // TERMINAL, print to console
            for (uint8_t ch : *buffer)
            {
                fputc(ch, stdout);
            }
            fflush(stdout);
        }
#ifdef SYSVIEW
        else if (index == RTT_SYSVIEW_CHANNEL)
        {
            LOG_OUTPUT("SysView size: %d ", (int)buffer->size());
            sysView->saveFromSTM(buffer);
        }
#endif
    });
}

void handleRttReadError(StRtt* strtt, int ramSizeKB, bool loopMode, int errorCode)
{
    if (!loopMode)
    {
        LOG_ERROR("readRtt returned error %d, program is exiting", errorCode);
        stopApp = true;
        return;
    }

    LOG_INFO("readRtt failed (%d), attempting to re-find RTT...", errorCode);

    bool rttFound = false;
    do
    {
        int result = strtt->findRtt(ramSizeKB);
        if (result == ERROR_OK)
        {
            rttFound = true;
            LOG_INFO("RTT re-found successfully");
            strtt->getRttDesc();
        }
        else
        {
            LOG_INFO("RTT not found, retrying...");
            std::this_thread::sleep_for(std::chrono::milliseconds(RTT_RETRY_DELAY_MS));
        }
    } while (!rttFound && loopMode && !stopApp);

    if (!rttFound)
    {
        LOG_ERROR("Failed to re-find RTT, exiting");
        stopApp = true;
    }
}

void processKeyboardInput(std::vector<uint8_t>& buffer)
{
    while (_kbhit())
    {
        uint8_t ch = _getch();
        buffer.push_back(ch);
    }
}

// MAIN APPLICATION ///////////////////////////////////////


int main(int argc, char **argv)
{
#ifdef __linux__
    // Opportunistic call to renice us, so we can keep up under
    // higher load conditions. This may fail when run as non-root.
    setpriority(PRIO_PROCESS, 0, PROCESS_PRIORITY);
#endif

    InputParser input(argc, argv);
    signal(SIGINT, signalHandler);

    // Initialize logging
    log_init();
    debug_level = LOG_LVL_ERROR;
    if (input.cmdOptionExists("-v"))
    {
        debug_level = std::stoi(input.getCmdOption("-v"));
    }

    // Parse command line options
    int ramSizeKB = DEFAULT_RAM_SIZE_KB;
    if (input.cmdOptionExists("-ramsize"))
    {
        ramSizeKB = parseIntegerOption(input.getCmdOption("-ramsize"));
    }

    int port = SYSVIEW_COMM_SERVER_PORT;
    if (input.cmdOptionExists("-port"))
    {
        port = std::stoi(input.getCmdOption("-port"));
    }

    uint32_t ramStart = RAM_START_DEFAULT;
    if (input.cmdOptionExists("-ramstart"))
    {
        ramStart = parseIntegerOption(input.getCmdOption("-ramstart"));
    }

    bool showCycleTime = input.cmdOptionExists("-t");
    bool useTCP = input.cmdOptionExists("-tcp");
    bool loopMode = input.cmdOptionExists("-loop");

    uint8_t apNum = DEFAULT_AP_NUM;
    if (input.cmdOptionExists("-ap"))
    {
        apNum = std::stoi(input.getCmdOption("-ap"));
    }

    StRtt *strtt = new StRtt(ramStart, apNum);

    // Open ST-Link connection
    int result = strtt->open(useTCP);
    if (result != ERROR_OK)
    {
        LOG_ERROR("failed to open STLINK (%d)", result);
        exit(-1);
    }

    // Find RTT control block
    findRttWithRetry(strtt, ramSizeKB, loopMode);
    strtt->getRttDesc();

    // Get buffer sizes
    uint32_t sizeRead, sizeWrite;
    result = strtt->getRttBuffSize(RTT_TERMINAL_CHANNEL, &sizeRead, &sizeWrite);

#ifdef SYSVIEW
    SysView* sysView = new SysView(port);
    setupChannelHandlers(strtt, sysView);
#else
    setupChannelHandlers(strtt);
#endif

    // Main processing loop
    std::vector<uint8_t> inputBuffer;
    double cycleDuration;

    while (!stopApp)
    {
        START_TS;

        // Read RTT data from target
        result = strtt->readRtt();
        if (result != ERROR_OK)
        {
            handleRttReadError(strtt, ramSizeKB, loopMode, result);
            continue;
        }

        // Process keyboard input
        processKeyboardInput(inputBuffer);
        if (inputBuffer.size() > 0)
        {
            strtt->writeRtt(RTT_TERMINAL_CHANNEL, &inputBuffer);
        }

#ifdef SYSVIEW
        // Process SysView data
        if (sysView->dataToSTM())
        {
            auto data = sysView->getDataToSTM();
            strtt->writeRtt(RTT_SYSVIEW_CHANNEL, &data);
        }
#endif

        if (showCycleTime)
        {
            STOP_TS;
            LOG_USER("Cycle time: %dms", (int)cycleDuration);
        }
    }

    strtt->close();
    return 0;
}
