#ifndef __NPP_LOG_UTILITIES_CPP__
#define __NPP_LOG_UTILITIES_CPP__

//#include "npp_log_utilities.h"
#include "string.h"
#include <iostream>
#include <stdexcept>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctime>
namespace log_utilities {

    static const uint16_t MAX_SIZE_LINE = 32768; //apparently no way to do it without this parameter
#ifdef FILE_LOG
#ifndef LOGFILE
    static FILE *log_file = fopen("npp_run.log", "w");
#else
    static FILE *log_file = fopen(LOGFILE, "w");
#endif
#endif

    inline std::string time_as_string() {
        time_t rawtime;
        struct tm * timeinfo;
        char buffer[80];

        time( &rawtime);
        timeinfo = localtime( &rawtime);

        strftime(buffer, 80, "%d-%m-%Y - %I:%M:%S", timeinfo);
        const std::string str(buffer);
        return (str);
    }


    inline void print(const char* message, const bool error_stream) {
        std::string message_as_string(message);
        std::string time = time_as_string();
        std::string out_message;
        out_message.append(time);
        out_message.append(" - ");
        //out_message.append(verbosity);
        // out_message.append(" - ");
        out_message.append(message_as_string);
        out_message.append("\n");

#ifdef FILE_LOG
        fprintf(log_file,out_message.c_str());
#endif

        if (error_stream == false) {
            std::cout << out_message << std::flush;
        } else {
            std::cout << out_message << std::flush;
            std::cerr << out_message << std::flush;
        }

    }
    //Error messages are printed in any case
    inline void error(std::string message, ...) {
        char print_message[MAX_SIZE_LINE];
        va_list vl;
        va_start(vl, message);
        message.insert(0, "**ERROR** - ");
        vsprintf(print_message, message.c_str(), vl);
        va_end(vl);
        print(print_message, true);

    }

    //Error messages are printed in any case
    inline void warning(std::string message, ...) {
        char print_message[MAX_SIZE_LINE];
        va_list vl;
        va_start(vl, message);
        message.insert(0, "**WARNING** - ");

        vsprintf(print_message, message.c_str(), vl);
        va_end(vl);
        print(print_message, true);

    }

#ifndef ENABLE_LOG
    //Empty implementation if log is disabled
    inline void none(std::string message, ...) {
    }

    inline void low(std::string message, ...) {
    }

    inline void medium(std::string message, ...) {
    }

    inline void high(std::string message, ...) {
    }

    inline void full(std::string message, ...) {
    }

    inline void debug(std::string message, ...) {
    }

    inline void performance(std::string message, ...) {

    }
#else

//All following functions share some code. We didn't manage to find a simpler way to implement it
//If it exists, would be nice being able to forward the ellipses to a common formatter
inline void none(std::string message, ...) {
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    vsprintf(print_message, message.c_str(), vl);
    va_end(vl);
    print(print_message, false);
}

inline void low(std::string message, ...) {
#if defined (VERBOSITY_LOW) || defined (VERBOSITY_MEDIUM) ||  defined (VERBOSITY_HIGH) ||  defined (VERBOSITY_FULL) || defined (VERBOSITY_DEBUG)
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    vsprintf(print_message,message.c_str(),vl);
    va_end(vl);
    print(print_message,false);
#endif
}

inline void medium(std::string message, ...) {
#if defined (VERBOSITY_MEDIUM) ||  defined (VERBOSITY_HIGH) ||  defined (VERBOSITY_FULL) || defined (VERBOSITY_DEBUG)
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    vsprintf(print_message,message.c_str(),vl);
    va_end(vl);
    print(print_message,false);
#endif
}

inline void high(std::string message, ...) {
#if defined (VERBOSITY_HIGH) ||  defined (VERBOSITY_FULL) ||  defined (VERBOSITY_DEBUG)
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    vsprintf(print_message,message.c_str(),vl);
    va_end(vl);
    print(print_message,false);
#endif
}

inline void full(std::string message, ...) {
#if defined (VERBOSITY_FULL) ||  defined (VERBOSITY_DEBUG)
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    vsprintf(print_message,message.c_str(),vl);
    va_end(vl);
    print(print_message,false);
#endif
}

inline void debug(std::string message, ...) {
#ifdef VERBOSITY_DEBUG
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    vsprintf(print_message,message.c_str(),vl);
    va_end(vl);
    print(print_message,false);
#endif
}

inline void performance(std::string message, ...) {
#ifdef PERFORMANCE_PROFILING
    char print_message[MAX_SIZE_LINE];
    va_list vl;
    va_start(vl, message);
    message.insert(0, "*PERF - ");
    vsprintf(print_message,message.c_str(),vl);
    va_end(vl);
    print(print_message,false);
#endif

}
#endif
}
#endif
