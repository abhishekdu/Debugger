#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>

using namespace std;

void sigchld_handler(int sig) {
    int saved_errno = errno;
     (void)sig; 
    pid_t pid;
    
    do {
        pid = waitpid(-1, nullptr, WNOHANG);
    } while (pid > 0);

    errno = saved_errno;
}

void setup_signals() {
    struct sigaction sa = {};
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
}

struct Command {
    vector<string> args;
    string in_file;
    string out_file;
    bool append = false;
};

vector<string> tokenize(const string& line) {
    vector<string> tokens;
    string current_token;
    bool in_quote = false;
    char quote_char = 0;
    bool escape = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (escape) {
            current_token += c;
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote = true;
            quote_char = c;
            continue;
        }

        if (in_quote && c == quote_char) {
            in_quote = false;
            continue;
        }

        if (!in_quote && isspace((unsigned char)c)) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            continue;
        }

        if (!in_quote && (c == '|' || c == '&' || c == '<' || c == '>')) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            if (c == '>' && i + 1 < line.size() && line[i + 1] == '>') {
                tokens.push_back(">>");
                ++i;
            } else {
                tokens.push_back(string(1, c));
            }
            continue;
        }

        current_token += c;
    }

    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }

    return tokens;
}

string expand_command_subst(string token) {
    string result;
    size_t i = 0;
    
    while (i < token.size()) {
        if (token[i] == '$' && i + 1 < token.size() && token[i + 1] == '(') {
            size_t j = i + 2;
            int depth = 1;

            while (j < token.size() && depth > 0) {
                if (token[j] == '(') depth++;
                else if (token[j] == ')') depth--;
                j++;
            }

            if (depth == 0) {
                string cmd = token.substr(i + 2, j - i - 3);
                FILE* p = popen(cmd.c_str(), "r");
                string out;

                if (p) {
                    char buf[1024];
                    while (fgets(buf, sizeof(buf), p)) {
                        out += buf;
                    }
                    pclose(p);
                    if (!out.empty() && out.back() == '\n') out.pop_back();
                }

                result += out;
                i = j;
            } else {
                result += token.substr(i);
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
    if (getcwd(cwd, sizeof(cwd))) {
        return string(cwd) + "> ";
    } else {
        return "miniShell> ";
    }
}

void execute_pipeline(vector<Command>& commands, bool background) {
    if (commands.empty()) return;

    int n = commands.size();
    vector<int> pipefds(2 * max(0, n - 1));

    for (int i = 0; i < n - 1; i++) {
        if (pipe(&pipefds[i * 2]) < 0) {
            perror("pipe");
            for (int j = 0; j < i * 2; j++) close(pipefds[j]);
            return;
        }
    }

    vector<pid_t> pids;
    pid_t pgid = 0;

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            for (int fd : pipefds) close(fd);
            for (pid_t p : pids) waitpid(p, nullptr, 0);
            return;
        }

        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            setpgid(0, 0);

            if (i > 0) {
                int in_fd = pipefds[(i - 1) * 2];
                if (dup2(in_fd, STDIN_FILENO) < 0) _exit(1);
            }

            if (i < n - 1) {
                int out_fd = pipefds[i * 2 + 1];
                if (dup2(out_fd, STDOUT_FILENO) < 0) _exit(1);
            }

            for (int fd : pipefds) close(fd);

            if (!commands[i].in_file.empty()) {
                int fd = open(commands[i].in_file.c_str(), O_RDONLY);
                if (fd < 0) _exit(1);
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (!commands[i].out_file.empty()) {
                int flags = O_WRONLY | O_CREAT | (commands[i].append ? O_APPEND : O_TRUNC);
                int fd = open(commands[i].out_file.c_str(), flags, 0644);
                if (fd < 0) _exit(1);
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            vector<char*> argv;
            for (auto& s : commands[i].args) argv.push_back((char*)s.c_str());
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        else {
            if (pgid == 0) pgid = pid;
            setpgid(pid, pgid);
            pids.push_back(pid);
        }
    }

    for (int fd : pipefds) close(fd);

    if (background) {
        cout << "[" << pgid << "]\n";
        return;
    }

    for (pid_t p : pids) {
        int status;
        while (waitpid(p, &status, 0) < 0 && errno == EINTR);
    }
}

bool build_pipeline(const vector<string>& tokens, vector<Command>& pipeline, bool& bg) {
    Command cur;

    for (size_t i = 0; i < tokens.size(); i++) {
        const string& t = tokens[i];

        if (t == "|") {
            if (cur.args.empty()) return false;
            pipeline.push_back(cur);
            cur = Command();
        }
        else if (t == "&") {
            if (i != tokens.size() - 1) return false;
            bg = true;
        }
        else if (t == "<") {
            if (i + 1 >= tokens.size()) return false;
            cur.in_file = tokens[++i];
        }
        else if (t == ">" || t == ">>") {
            if (i + 1 >= tokens.size()) return false;
            cur.out_file = tokens[++i];
            cur.append = (t == ">>");
        }
        else {
            cur.args.push_back(t);
        }
    }

    if (!cur.args.empty() || !cur.in_file.empty() || !cur.out_file.empty())
        pipeline.push_back(cur);

    return !pipeline.empty();
}

string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

int main() {

    setup_signals();

    string line;
    vector<Command> pipeline;
    bool bg = false;

    while (true) 
    {
        cout << get_prompt() << flush;

        if (!getline(cin, line)) {
            cout << "\n";
            break;
        }

        string trimmed = trim(line);
        if (trimmed.empty()) continue;

        vector<string> tokens = tokenize(trimmed);
        if (tokens.empty()) continue;

        for (auto& t : tokens)
            if (t.find("$(") != string::npos)
                t = expand_command_subst(t);


        if (!build_pipeline(tokens, pipeline, bg))
            continue;

        if (pipeline.size() == 1) {
            const auto& args = pipeline[0].args;
            if (!args.empty()) {
                if (args[0] == "exit") return 0;

                if (args[0] == "cd") {
                    const char* path = (args.size() > 1) ? args[1].c_str() : getenv("HOME");
                    if (!path) path = "/";
                    if (chdir(path) < 0) perror("cd");
                    continue;
                }

            if (args[0] == "pwd") {
                char buf[4096];
                if (getcwd(buf, sizeof(buf)))
                    cout << buf << "\n";
                else
                    perror("pwd");
                continue;
            }

            if (args[0] == "echo") {
                for (size_t i = 1; i < args.size(); i++) {
                    cout << args[i];
                    if (i + 1 < args.size()) cout << " ";
                }
                cout << "\n";
                continue;
            }
            }
        }

        execute_pipeline(pipeline, bg);
    }

    return 0;
}
