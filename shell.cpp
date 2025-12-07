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
More build in command

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
        if (!in_quote && (c == '"' || c == '\'')) { in_quote = true; qc = c; continue; }
        if (in_quote && c == qc) { in_quote = false; continue; }
        if (!in_quote && isspace((unsigned char)c)) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) tokens.push_back(cur);
    for (auto& s : tokens) {
        if (s.size() >= 2 && ((s[0] == '"' && s.back() == '"') || (s[0] == '\'' && s.back() == '\'')))
            s = s.substr(1, s.size() - 2);
    }
    return tokens;
}

string get_prompt() {
    char cwd[4096];
    return getcwd(cwd, sizeof(cwd)) ? string(cwd) + "> " : "miniShell> ";
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

        auto tokens = tokenize(trimmed);
        if (tokens.empty()) continue;

        string in_file, out_file;
        vector<string> args;

        for (size_t i = 0; i < tokens.size(); ) {
            if (tokens[i] == "<" && i + 1 < tokens.size()) { in_file = tokens[i + 1]; i += 2; }
            else if (tokens[i] == ">" && i + 1 < tokens.size()) { out_file = tokens[i + 1]; i += 2; }
            else { args.push_back(tokens[i]); ++i; }
        }

        bool bg = false;
        if (!args.empty() && args.back() == "&") { bg = true; args.pop_back(); }
        if (args.empty()) continue;

        const string& cmd = args[0];

        // === BUILT-INS THAT DON'T NEED REDIRECTION ===
        if (cmd == "exit") return args.size() > 1 ? stoi(args[1]) : 0;
        if (cmd == "cd") {
            const char* p = args.size() > 1 ? args[1].c_str() : getenv("HOME");
            if (chdir(p ? p : "~") != 0) perror("cd");
            continue;
        }
        if (cmd == "pwd") {
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd))) cout << cwd << '\n';
            else perror("pwd");
            continue;
        }
        if (cmd == "export" && args.size() >= 2) {
            for (size_t i = 1; i < args.size(); ++i) {
                string s = args[i];
                size_t eq = s.find('=');
                if (eq != string::npos)
                    setenv(s.substr(0, eq).c_str(), s.substr(eq + 1).c_str(), 1);
                else
                    setenv(s.c_str(), "", 1);
            }
            continue;
        }

        // === echo: if there is redirection → run external /bin/echo ===
        if (cmd == "echo" && (in_file.empty() && out_file.empty())) {
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) cout << " ";
                cout << args[i];
            }
            cout << '\n';
            continue;
        }

        // === ALL OTHER CASES (including echo with > or <) → external command ===
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

            vector<char*> argv;
            for (const auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
            argv.push_back(nullptr);

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
    return 0;
}
