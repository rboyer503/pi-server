#include <stdio.h>
#include <termios.h>

#include "PiMgr.h"


void config_canonical_mode(bool enable)
{
    // Get current STDIN terminal attributes and update it to enable or disable canonical mode.
    struct termios ttystate;
    tcgetattr(STDIN_FILENO, &ttystate);

    if (enable)
    {
        ttystate.c_lflag |= ICANON;
        ttystate.c_lflag |= ECHO;
        ttystate.c_lflag &= ~ECHONL;
    }
    else
    {
        ttystate.c_lflag &= ~ICANON;
        ttystate.c_cc[VMIN] = 1; // Wait for 1 character minimum
        ttystate.c_lflag &= ~ECHO;
        ttystate.c_lflag |= ECHONL;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}

bool kbhit()
{
    // Use select on STDIN to check if a character is ready.
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return (FD_ISSET(STDIN_FILENO, &fds) != 0);
}

int main(void)
{
    PiMgr piMgr;
    if (!piMgr.Initialize())
        return piMgr.GetErrorCode();

    // Disable canonical mode so that keyboard input can be received character by character.
    config_canonical_mode(false);

    // Keep checking for cancellation request until manager thread has been joined.
    while (piMgr.IsRunning())
    {
        if (kbhit())
        {
            // Trigger termination logic once user presses 'q'.
            char c;
            if ( (c = fgetc(stdin)) == 'q' )
                piMgr.Terminate();
            else if (c == 's')
                piMgr.OutputStatus();
            else if (c == 'c')
                piMgr.OutputConfig();
            else if (c == 'm')
                piMgr.UpdateIPM();
            else if (c == 'p')
                piMgr.UpdatePage();
            else if (c == 'd')
                piMgr.DebugCommand();
            else if (c == '[')
                piMgr.UpdateParam(1, false);
            else if (c == ']')
                piMgr.UpdateParam(1, true);
            else if (c == '{')
                piMgr.UpdateParam(2, false);
            else if (c == '}')
                piMgr.UpdateParam(2, true);
        }

        boost::this_thread::sleep(boost::posix_time::milliseconds(5));
    }

    config_canonical_mode(true);
    return piMgr.GetErrorCode();
}
