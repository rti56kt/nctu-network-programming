#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <sstream>
#include <iostream>

using namespace std;

struct pipeinfo{
    int in;             // The fd number of the input of pipe
    int out;            // The fd number of the output of pipe
    int line_cnt;       // Counting the remaining line that is going to pipe to. (Only used in number pipe)
    int pipe_type;      // 0: input redirection  1: output redirection  2: normal pipe  3: number pipe  4: number pipe with stderr
    int behind_cmd_idx; // The index of cmds that the pipe is behind of
};

void doFork(vector<vector<string>> cmds, vector<pipeinfo> &pipe_table, bool &pipe_in_end){
    pid_t pid;
    vector<pid_t> pid_list;

    for(int i = 0; i < cmds.size(); i++){
        // Select a existing same pipe (look up if there's same destination cmd/line by surveying whole pipe_table)
        // Create a new pipe if there's no existing same pipe
        // Store the pipe's fd to pipe_table
        for(int j = 0; j < pipe_table.size(); j++){
            if(pipe_table.at(j).behind_cmd_idx == i){
                // If pipe_j is behind cmds_i
                bool same_pipe = false;

                if(pipe_table.at(j).pipe_type >= 3){
                    // If pipe_j is a number pipe
                    for(int k = 0; k < j; k++){
                        if(pipe_table.at(j).line_cnt == pipe_table.at(k).line_cnt){
                            // If there's a same number pipe existing in pipe_table
                            same_pipe = true;
                            pipe_table.at(j).in = pipe_table.at(k).in;
                            pipe_table.at(j).out = pipe_table.at(k).out;
                            break;
                        }
                    }
                }
                if(!same_pipe){
                    // If pipe_j is not a number pipe
                    int pipe_tmp[2];

                    if(pipe(pipe_tmp) < 0){
                        // if failed to create a pipe
                        cerr << "npsh: pipe error" << endl;
                    }

                    // Assign the new pipe's fd to pipe_table 
                    pipe_table.at(j).in = pipe_tmp[1];
                    pipe_table.at(j).out = pipe_tmp[0];
                }
                break;
            }
        }

        pid = fork();
        pid_list.push_back(pid);

        if(pid < 0){
            // If failed to fork
            pid_list.pop_back();
            usleep(1000);
            i--;
            continue;
        }else if(pid == 0){
            // Child
            bool is_file = false;
            vector<char *> cmd_arg_tmp;

            // Convert string vector cmd to char** to put into execvp
            for(int j = 0; j < cmds.at(i).size(); j++){
                cmd_arg_tmp.push_back(strdup(cmds.at(i).at(j).c_str()));
            }
            cmd_arg_tmp.push_back(NULL);
            char **cmd_arg = &cmd_arg_tmp[0];

            for(int j = 0; j < pipe_table.size(); j++){
                if(pipe_table.at(j).behind_cmd_idx == i){
                    if(pipe_table.at(j).pipe_type == 0){
                        // Intput redirection
                        int in_file_fd;

                        // Open the file which is behind current cmd and change current cmd's stdin fd to file's fd
                        in_file_fd = open((cmds.at(i+1).at(0)).c_str(), O_RDONLY, 0);
                        dup2(in_file_fd, 0);
                    }else if(pipe_table.at(j).pipe_type == 1){
                        // Output redirection
                        int out_file_fd;

                        // Open the file which is behind current cmd and change current cmd's stdout fd to file's fd
                        out_file_fd = open((cmds.at(i+1).at(0)).c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
                        dup2(out_file_fd, 1);
                    }else if(pipe_table.at(j).pipe_type == 2){
                        // Normal pipe
                        dup2(pipe_table.at(j).in, 1);  // Change current cmd's stdout fd to pipe's input fd
                    }else if(pipe_table.at(j).pipe_type == 3){
                        // Number pipe
                        dup2(pipe_table.at(j).in, 1);  // Change current cmd's stdout fd to pipe's input fd
                    }else if(pipe_table.at(j).pipe_type == 4){
                        // Number pipe with stderr
                        dup2(pipe_table.at(j).in, 1);  // Change current cmd's stdout fd to pipe's input fd
                        dup2(pipe_table.at(j).in, 2);  // Change current cmd's stderr fd to pipe's input fd
                    }
                }

                if(i != 0 && pipe_table.at(j).behind_cmd_idx == i-1 && (pipe_table.at(j).pipe_type == 0 || pipe_table.at(j).pipe_type == 1)){
                    // If current cmd is NOT a command but a file name then do nothing
                    is_file = true;
                }else if(i != 0 && pipe_table.at(j).behind_cmd_idx == i-1 && pipe_table.at(j).pipe_type == 2){
                    // If current cmd is normal pipe's output then change its stdin fd to pipe's output fd
                    dup2(pipe_table.at(j).out, 0);
                }else if(i == 0 && pipe_table.at(j).line_cnt == 0 && (pipe_table.at(j).pipe_type == 3 || pipe_table.at(j).pipe_type == 4)){
                    // If current cmd is number pipe's output then change its stdin fd to pipe's output fd
                    dup2(pipe_table.at(j).out, 0);
                }
            }

            for(int j = 0; j < pipe_table.size(); j++){
                // Child now can close all useless fd since all necessary pipe fd has already connected to corresponding cmd's stdin/stdout/stderr
                close(pipe_table.at(j).out);
                close(pipe_table.at(j).in);
            }

            if(!is_file){
                int ret = 0;
                // If current cmd is not a filename but a command then execute the command
                ret = execvp(cmds.at(i).at(0).c_str(), cmd_arg);
                if(ret == -1 && errno == 2){
                    // If exec is failed and errno equals 2 (No such file or directory) means the system cannnot find the command
                    cerr << "Unknown command: [" << cmds.at(i).at(0).c_str() << "]." << endl;
                }
                exit(EXIT_FAILURE);
            }else{
                // If current cmd is a filename then no need to execute, just close current child process
                exit(EXIT_SUCCESS);
            }
        }else{
            // Parent
        }

        for(int j = 0; j < pipe_table.size(); ){
            if((i != 0 && pipe_table.at(j).behind_cmd_idx == i-1) || (i == 0 && pipe_table.at(j).line_cnt == 0)){
                // If current cmd is pipe's output, the pipe's input and output should be all connected by child process
                // Thus parent can close these pipe's fd
                // Parent now can close all useless fd since all necessary pipe fd has already connected to corresponding cmd's stdin/stdout/stderr
                close(pipe_table.at(j).out);
                close(pipe_table.at(j).in);
                pipe_table.erase(pipe_table.begin() + j);
            }else{
                j++;
            }
        }
    }

    // All number pipe's line_cnt should minus one since the process of this user's input line is finished
    for(int i = 0; i < pipe_table.size(); i++){
        pipe_table.at(i).behind_cmd_idx = -1;
        if(pipe_table.at(i).line_cnt != -1){
            pipe_table.at(i).line_cnt--;
        }
    }

    // Prevent prompt ("% ") is printed before child process prints its output
    if(!pipe_in_end){
        vector<pid_t>::iterator iter = pid_list.begin();
        while(iter != pid_list.end()){
            int status;
            waitpid((*iter), &status, 0);
            iter = pid_list.erase(iter);
        }
    }

    return;
}

void buildInCmd(vector<string> cmd){
    if(cmd.at(0).compare("exit") == 0){
        exit(EXIT_SUCCESS);
    }else if(cmd.at(0).compare("setenv") == 0){
        if(cmd.size() == 3){
            char env_name[cmd.at(1).size() + 1];
            char env_val[cmd.at(2).size() + 1];

            strcpy(env_name, cmd.at(1).c_str());
            strcpy(env_val, cmd.at(2).c_str());

            setenv(env_name, env_val, 1);
        }else{
            cerr << "usage: setenv [variable name] [value to assign]" << "\n";
        }
    }else if(cmd.at(0).compare("printenv") == 0){
        if(cmd.size() == 2){
            char env_name[cmd.at(1).size() + 1];

            strcpy(env_name, cmd.at(1).c_str());

            if(getenv(env_name) != NULL) cout << getenv(env_name) << "\n";
        }else{
            cerr << "usage: printenv [variable name]" << "\n";
        }
    }

    return;
}

vector<vector<string>> parseCmd(string user_input, vector<pipeinfo> &pipe_table, bool &pipe_in_end){
    int pipe_cnt = 0;
    string token;
    vector<string> tokens;
    vector<string> cmd;
    vector<vector<string>> cmds;
    stringstream user_input_stream(user_input);

    // Convert user_input to a stream
    // Use getline() to parse the input by setting a single space char ' ' as delimeter
    // Store all token to a vector named "tokens"
    while(getline(user_input_stream, token, ' ')){
        tokens.push_back(token);
    }

    // Divide tokens into cmd + args, pipe, number pipe, and io redirection
    for(int i = 0; i < tokens.size(); i++){
        if(tokens.at(i) == "<"){
            // Input redirection
            struct pipeinfo pipe_info_tmp;

            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            pipe_info_tmp.pipe_type = 0;
            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else if(tokens.at(i) == ">"){
            // Output redirection
            struct pipeinfo pipe_info_tmp;

            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            pipe_info_tmp.pipe_type = 1;
            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else if(tokens.at(i) == "|"){
            // Normal pipe
            struct pipeinfo pipe_info_tmp;

            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            pipe_info_tmp.pipe_type = 2;
            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else if(tokens.at(i).at(0) == '|' && isdigit(tokens.at(i).at(1)) || tokens.at(i).at(0) == '!' && isdigit(tokens.at(i).at(1))){
            // Number pipe
            int pipe_number = 0;
            struct pipeinfo pipe_info_tmp;

            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Parse the number behind the pipe
            pipe_number = atoi(tokens.at(i).substr(1, tokens.at(i).size()).c_str());

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            if(tokens.at(i).at(0) == '|') pipe_info_tmp.pipe_type = 3;
            else if(tokens.at(i).at(0) == '!') pipe_info_tmp.pipe_type = 4;
            pipe_info_tmp.line_cnt = pipe_number;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else{
            // Cmd + args
            cmd.push_back(tokens.at(i));
            pipe_in_end = false;
        }
    }

    if(!pipe_in_end){
        cmds.push_back(cmd);
        cmd.clear();
    }

    return cmds;
}

string readInput(){
    string user_input;

    getline(cin, user_input, '\n');
    if(cin.eof()) exit(EXIT_SUCCESS);

    return user_input;
}

void npsh(){
    vector<pipeinfo> pipe_table;
    while(true){
        bool pipe_in_end;
        string user_input;
        vector<vector<string>> cmds;  // Outer vector: different cmds; Inner vector: single cmd + args

        // Print prompt
        cout << "% " << flush;

        user_input = readInput();

        if(user_input.size() >= 1) cmds = parseCmd(user_input, pipe_table, pipe_in_end);
        else continue;  // Means the input is a blank line

        if(cmds.at(0).at(0).compare("exit") == 0 || cmds.at(0).at(0).compare("setenv") == 0 || cmds.at(0).at(0).compare("printenv") == 0) buildInCmd(cmds.at(0));
        else doFork(cmds, pipe_table, pipe_in_end);
    }
}

void childHandler(int signo){
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0){
        //do nothing
    }
}

int main(int argc, char **argv, char **envp){
    signal(SIGCHLD, childHandler);
    // Fetch all env var and unset them
    for(char **env = envp; *env != 0; env++){
        char *envname;
        envname = strtok(*env, "=");
        unsetenv(envname);
    }
    // Only set PATH as env var, and init it to "bin:."
    setenv("PATH", "bin:.", 1);

    npsh();

    return 0;
}