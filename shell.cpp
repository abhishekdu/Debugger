/* 
Commands added:
  exit, cd
Redirection:
  <,>
Signal: 
  Ignoring SIGINT (Ctrl-C) in the shell, which is useful to prevent the shell from terminating when Ctrl-C is pressed.
Tokenization of Input

*/
/*

PENDING: 
Pipe
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

// Tokenizer for parsing commands
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

// Get the prompt for the shell
string get_prompt() {
    char cwd[4096];
    return getcwd(cwd, sizeof(cwd)) ? string(cwd) + "> " : "miniShell> ";
}

// Function to split the command by pipe ('|') symbol
vector<string> split_by_pipe(const string& line) {
    vector<string> commands;
    size_t start = 0;
    for (size_t i = 0; i < line.length(); i++) {
        if (line[i] == '|') {
            commands.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    commands.push_back(line.substr(start));
    return commands;
}

// Parse command into arguments for execvp
vector<char*> parse_command(const vector<string>& args) {
    vector<char*> argv;
    for (const auto& s : args) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
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

        // Handle pipe (|) command splitting
        vector<string> commands = split_by_pipe(trimmed);
        int num_commands = commands.size();
        
        if (num_commands > 1) {
            int pipefd[2 * (num_commands - 1)];
            
            for (int i = 0; i < num_commands - 1; ++i) {
                pipe(pipefd + i * 2);  // Create pipe
            }

            for (int i = 0; i < num_commands; ++i) {
                pid_t pid = fork();
                if (pid == 0) {
                    if (i > 0) {
                        // Connect previous pipe's output to stdin
                        dup2(pipefd[(i - 1) * 2], 0);
                    }
                    if (i < num_commands - 1) {
                        // Connect current pipe's input to stdout
                        dup2(pipefd[i * 2 + 1], 1);
                    }

                    // Close all pipe file descriptors in the child process
                    for (int j = 0; j < 2 * (num_commands - 1); ++j) {
                        close(pipefd[j]);
                    }

                    // Tokenize and execute the current command
                    vector<string> cmd_tokens = tokenize(commands[i]);
                    if (!cmd_tokens.empty()) {
                        const string& cmd = cmd_tokens[0];
                        if (cmd == "exit") return 0; // Handle exit command
                        vector<char*> argv = parse_command(cmd_tokens);
                        execvp(argv[0], argv.data());
                        perror("execvp");
                        _exit(127);
                    }
                }
            }

            // Parent process: Close pipes and wait for all children
            for (int i = 0; i < 2 * (num_commands - 1); ++i) {
                close(pipefd[i]);
            }
            for (int i = 0; i < num_commands; ++i) {
                wait(NULL);
            }
        } else {
            // Single command execution without pipe
            string in_file, out_file;
            vector<string> args;

            for (size_t i = 0; i < tokens.size(); ) {
                if (tokens[i] == "<" && i + 1 < tokens.size()) { in_file = tokens[i+1]; i += 2; }
                else if (tokens[i] == ">" && i + 1 < tokens.size()) { out_file = tokens[i+1]; i += 2; }
                else { args.push_back(tokens[i]); ++i; }
            }

            bool bg = false;
            if (!args.empty() && args.back() == "&") { bg = true; args.pop_back(); }
            if (args.empty()) continue;

            const string& cmd = args[0];

            if (cmd == "exit") return args.size() > 1 ? stoi(args[1]) : 0;
            if (cmd == "cd") {
                const char* p = args.size() > 1 ? args[1].c_str() : getenv("HOME");
                if (chdir(p ? p : "~") != 0) perror("cd");
                continue;
            }
            if (cmd == "pwd") {
                char cwd[4096]; 
                if (getcwd(cwd, sizeof(cwd))) cout << cwd << '\n';
                continue;
            }

            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);
                if (!in_file.empty()) {
                    int fd = open(in_file.c_str(), O_RDONLY);
                    if (fd < 0) { perror("open input"); _exit(127); }
                    dup2(fd, 0); close(fd);
                }
                if (!out_file.empty()) {
                    int fd = open(out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) { perror("open output"); _exit(127); }
                    dup2(fd, 1); close(fd);
                }
                vector<char*> argv = parse_command(args);
                execvp(argv[0], argv.data());
                perror("execvp");
                _exit(127);
            }
            else if (pid > 0) {
                if (!bg) waitpid(pid, nullptr, 0);
                else cout << "[background pid " << pid << "]\n";
            }
            else perror("fork");
        }
    }
    return 0;
}
