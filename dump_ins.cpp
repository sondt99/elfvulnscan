#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <regex>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std;

string normalizeAddress(string addr)
{
    // Remove all "0x" prefixes
    while (addr.rfind("0x", 0) == 0 || addr.rfind("0X", 0) == 0)
        addr = addr.substr(2);

    // Remove leading zeroes
    addr.erase(0, addr.find_first_not_of('0'));

    // Lowercase
    transform(addr.begin(), addr.end(), addr.begin(), ::tolower);

    return addr;
}

vector<string> runObjdumpSafely(const string &binary)
{
    vector<string> lines;
    regex safe("^[a-zA-Z0-9._/-]+$");
    if (!regex_match(binary, safe))
    {
        cerr << "Invalid binary name.\n";
        return lines;
    }

    ifstream testFile(binary);
    if (!testFile)
    {
        cerr << "Error: Cannot open binary file '" << binary << "'\n";
        return lines;
    }
    testFile.close();

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        return lines;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return lines;
    }

    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        const char *argv[] = {"objdump", "-d", binary.c_str(), nullptr};
        execvp("objdump", (char *const *)argv);
        perror("execvp");
        _exit(1);
    }
    else
    {
        close(pipefd[1]);
        char buffer[4096];
        FILE *readPipe = fdopen(pipefd[0], "r");
        while (fgets(buffer, sizeof(buffer), readPipe))
        {
            lines.emplace_back(buffer);
        }
        fclose(readPipe);
        int status;
        waitpid(pid, &status, 0);
    }

    return lines;
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 5)
    {
        cerr << "Usage:\n"
             << "  " << argv[0] << " <binary_path> <address> [lines_before] [lines_after]\n"
             << "  " << argv[0] << " <binary_path> <address> full\n";
        return 1;
    }

    string binary = argv[1];
    string input_addr = argv[2];
    string target_addr = normalizeAddress(input_addr);

    bool full_function = false;
    int lines_before = 10, lines_after = 10;

    if (argc == 4 && string(argv[3]) == "full")
    {
        full_function = true;
    }
    else if (argc == 5)
    {
        lines_before = stoi(argv[3]);
        lines_after = stoi(argv[4]);
    }

    // Run objdump
    vector<string> lines = runObjdumpSafely(binary);
    if (lines.empty())
    {
        cerr << "Disassembly failed or binary invalid.\n";
        return 1;
    }
    
    // Find the target line and containing function
    regex func_header_regex("^\\s*([0-9a-f]+)\\s+<([^>]+)>:");
    string current_func = "UNKNOWN";
    int func_start = -1;
    int func_end = -1;
    int target_line = -1;

    for (size_t i = 0; i < lines.size(); ++i)
    {
        smatch match;
        if (regex_search(lines[i], match, func_header_regex))
        {
            if (func_start != -1 && target_line != -1 && func_end == -1)
                func_end = i;
            func_start = i;
            current_func = match[2];
        }

        // Search for line containing normalized address
        smatch addr_match;
        regex addr_regex("^\\s*" + target_addr + ":");
        if (regex_search(lines[i], addr_match, addr_regex))
        {
            target_line = i;
            cout << "Function: " << current_func << endl;

            if (full_function)
            {
                // Find the next 'ret' instruction after target
                for (size_t j = target_line; j < lines.size(); ++j)
                {
                    if (lines[j].find("ret") != string::npos)
                    {
                        func_end = j + 1;
                        break;
                    }
                }

                int start = func_start >= 0 ? func_start : max(0, target_line - 10);
                int end = (func_end != -1) ? func_end : min((int)lines.size(), target_line + 20);
                for (int j = start; j < end; ++j)
                    cout << lines[j];
                return 0;
            }
            else
            {
                int start = max(0, target_line - lines_before);
                int end = min((int)lines.size(), target_line + lines_after + 1);
                for (int j = start; j < end; ++j)
                    cout << lines[j];
                return 0;
            }
        }
    }

    cout << "Address 0x" << target_addr << " not found in disassembly." << endl;
    return 1;
}
