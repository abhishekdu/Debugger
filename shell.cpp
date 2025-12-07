/* 
Commands added:
  exit, cd
Redirection:
  <,>
Signal: 
  Ignoring SIGINT (Ctrl-C) in the shell, which is useful to prevent the shell from terminating when Ctrl-C is pressed.
Tokenization of Input
pipe
*/
/*

PENDING: 

Error handling


*/
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

void sigchld_handler(int) {
    int saved = errno;
    while (waitpid(-1, nullptr, WNOHANG) > 0);
    errno = saved;
}

void setup_signals() {
    struct sigaction sa{};
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, nullptr);
    signal(SIGINT, SIG_IGN);
}

vector<string> tokenize(const string& line) {
    vector<string> tokens;
    string cur;
    bool in_quote = false;
    char qc = 0;

    for (char c : line) {
        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote = true; qc = c; continue;
        }
        if (in_quote && c == qc) {
            if (cur.size() > 0 && cur.back() == qc) {
                cur.back() = qc;  // doubled quote → literal
            } else {
                in_quote = false;
            }
            continue;
        }
        if (!in_quote && isspace((unsigned char)c)) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// Expand $(command) in a token
string expand_command_subst(string token) {
    string result;
    size_t i = 0;
    while (i < token.size()) {
        if (token[i] == '$' && i + 1 < token.size() && token[i+1] == '(') {
            size_t start = i;
            i += 2;
            int depth = 1;
            size_t cmd_start = i;
            while (i < token.size() && depth > 0) {
                if (token[i] == '(') depth++;
                else if (token[i] == ')') depth--;
                i++;
            }
            if (depth == 0) {
                string cmd = token.substr(cmd_start, i - cmd_start - 1);
                FILE* pipe = popen(cmd.c_str(), "r");
                string output;
                if (pipe) {
                    char buf[128];
                    while (fgets(buf, sizeof(buf), pipe)) output += buf;
                    pclose(pipe);
                    if (!output.empty() && output.back() == '\n') output.pop_back();
                }
                result += output;
            } else {
                result += token.substr(start);
                break;
            }
        } else {
            result += token[i++];
        }
    }
    return result;
}

string get_prompt() {
    char cwd[4096];
    return getcwd(cwd, sizeof(cwd)) ? string(cwd) + "> " : "miniShell> ";
}

// Function to parse arguments and handle I/O redirection
void handle_redirection(vector<string>& tokens, string& in_file, string& out_file, bool& append, vector<string>& args) {
    for (size_t i = 0; i < tokens.size(); ) {
        if (tokens[i] == "<" && i + 1 < tokens.size()) { 
            in_file = tokens[i+1]; 
            i += 2; 
        }
        else if (tokens[i] == ">" && i + 1 < tokens.size()) { 
            out_file = tokens[i+1]; 
            append = false; // No append, overwrite mode
            i += 2; 
        }
        else if (tokens[i] == ">>" && i + 1 < tokens.size()) { 
            out_file = tokens[i+1]; 
            append = true; // Append mode
            i += 2; 
        }
        else { 
            args.push_back(tokens[i]); 
            ++i; 
        }
    }
}

// Function to execute the command
void execute_command(vector<string>& args, const string& in_file, const string& out_file, bool append) {
    pid_t pid = fork();
    if (pid == 0) {  // Child process
        signal(SIGINT, SIG_DFL);  // Reset signal handler to default

        // If input redirection is required
        if (!in_file.empty()) {
            int fd = open(in_file.c_str(), O_RDONLY);
            if (fd < 0) { perror("open input"); _exit(127); }
            dup2(fd, 0); close(fd);  // Redirect stdin
        }

        // If output redirection is required
        if (!out_file.empty()) {
            int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
            int fd = open(out_file.c_str(), flags, 0644);
            if (fd < 0) { perror("open output"); _exit(127); }
            dup2(fd, 1); close(fd);  // Redirect stdout
        }

        // Convert arguments to char* array and execute the command
        vector<char*> argv;
        for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);  // Null-terminate the argument list

        execvp(argv[0], argv.data());
        perror("execvp");
        _exit(127);
    }
    else if (pid > 0) {  // Parent process
        waitpid(pid, nullptr, 0);
    }
    else {  // Fork failed
        perror("fork");
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    setup_signals();

    string line;
    while (true) {
        cout << get_prompt() << flush;
        if (!getline(cin, line)) { cout << "\n"; break; }

        size_t l = line.find_first_not_of(" \t\r\n");
        if (l == string::npos) continue;
        size_t r = line.find_last_not_of(" \t\r\n");
        string trimmed = line.substr(l, r - l + 1);

        vector<string> tokens = tokenize(trimmed);
        if (tokens.empty()) continue;

        // === COMMAND SUBSTITUTION $(...) ===
        for (auto& tok : tokens) {
            tok = expand_command_subst(tok);
        }

        string in_file, out_file;
        vector<string> args;
        bool append = false;

        // Handle input/output redirection
        handle_redirection(tokens, in_file, out_file, append, args);
        
        if (args.empty()) continue;

        // === Execute Command ===
        execute_command(args, in_file, out_file, append);
    }
    return 0;
}

