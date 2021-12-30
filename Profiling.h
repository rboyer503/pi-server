#ifndef PROFILING_H_
#define PROFILING_H_

// Global variables and macros for profiling.
namespace
{
    boost::posix_time::ptime g_start;
    boost::posix_time::time_duration g_diff;
}

#define PROFILE_START g_start = boost::posix_time::microsec_clock::local_time();

#define PROFILE_LOG(tag) \
    do { \
        g_diff = boost::posix_time::microsec_clock::local_time() - g_start; \
        cout << "Profile (" << #tag << "): " << g_diff.total_microseconds() << " us" << endl; \
    } while (0)

#define PROFILE_DIFF (boost::posix_time::microsec_clock::local_time() - g_start).total_microseconds()

#endif /* PROFILING_H_ */

