// Utility functions

#include "SAMdisk.h"
#include "utils.h"

namespace util
{

std::ofstream log;
LogHelper cout(&std::cout);


std::string fmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);

    std::vector<char> bytes(len + 1); // +1 for \0

    va_start(args, fmt);
    std::vsnprintf(&bytes[0], bytes.size(), fmt, args);
    va_end(args);

    return std::string(bytes.data(), len);
}

std::vector<std::string> split(const std::string& str, char delim, bool skip_empty)
{
    std::vector<std::string> items;
    std::stringstream ss(str);
    std::string s;

    while (std::getline(ss, s, delim))
    {
        if (!skip_empty || !s.empty())
            items.emplace_back(std::move(s));
    }

    return items;
}

std::string trim(const std::string& str)
{
    std::string s(str.c_str());
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

void bit_reverse(uint8_t* pb, int len)
{
    while (len-- > 0)
    {
        auto& b = *pb++;
        b = ((b & 1) << 7) |
            ((b & 2) << 5) |
            ((b & 4) << 3) |
            ((b & 8) << 1) |
            ((b & 16) >> 1) |
            ((b & 32) >> 3) |
            ((b & 64) >> 5) |
            ((b & 128) >> 7);
    }
}

std::string resource_dir()
{
#if defined(_WIN32)
    char sz[MAX_PATH];
    if (GetModuleFileName(NULL, sz, arraysize(sz)))
    {
        auto s = std::string(sz);
        s.erase(s.find_last_of('\\') + 1);
        return s;
    }
#elif defined(RESOURCE_DIR)
    return RESOURCE_DIR;
#endif

    return "";
}

bool is_stdout_a_tty()
{
#ifdef _WIN32
    static bool ret = _isatty(_fileno(stdout)) != 0;
#else
    static bool ret = isatty(fileno(stdout)) != 0;
#endif
    return ret || opt.tty;
}


std::string lowercase(const std::string& str)
{
    std::string ret = str;
    ret.reserve(str.length());
    std::transform(str.cbegin(), str.cend(), ret.begin(), [](char c) {
        return static_cast<uint8_t>(std::tolower(static_cast<int>(c)));
        });
    return ret;
}


LogHelper& operator<<(LogHelper& h, colour c)
{
    // Colours are screen only
    if (util::is_stdout_a_tty())
    {
#ifdef _WIN32
        h.screen->flush();
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), static_cast<int>(c));
#else
        auto val = static_cast<int>(c);
        if (val & 0x80)
            *h.screen << "\x1b[" << (val & 0x7f) << ";1m";
        else
            *h.screen << "\x1b[0;" << (val & 0x7f) << 'm';
#endif
    }
    return h;
}

LogHelper& operator<<(LogHelper& h, ttycmd cmd)
{
    if (util::is_stdout_a_tty())
    {
        switch (cmd)
        {
        case ttycmd::statusbegin:
            h.statusmsg = true;
            break;

        case ttycmd::statusend:
            h.statusmsg = false;
            h.clearline = true;
            break;

        case ttycmd::clearline:
            h << "\r" << ttycmd::cleartoeol;
            h.clearline = false;
            break;

        case ttycmd::cleartoeol:
#ifdef _WIN32
            h.screen->flush();

            DWORD dwWritten;
            CONSOLE_SCREEN_BUFFER_INFO csbi{};
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

            if (GetConsoleScreenBufferInfo(hConsole, &csbi))
                FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X - csbi.dwCursorPosition.X, csbi.dwCursorPosition, &dwWritten);
#else
            * h.screen << "\x1b[0K";
#endif
            break;
        }
    }
    return h;
}

} // namespace util
