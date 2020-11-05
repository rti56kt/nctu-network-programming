#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <map>
#include <vector>
#include <sstream>
#include <iostream>

using namespace std;

struct pipeinfo{
    int in;             // The fd number of the input of pipe
    int out;            // The fd number of the output of pipe
    int line_cnt;       // Counting the remaining line that is going to pipe to. (Only used in number pipe)
    int uid;            // Recording uid of user pipe (Only used in user pipe)
    int pipe_type;      // 0: input redirection  1: output redirection  2: normal pipe  3: number pipe  4: number pipe with stderr  5: user send pipe  6: user recv pipe
    int behind_cmd_idx; // The index of cmds that the pipe is behind of
};

struct userpipe{
    int src[2];         // 0: sock  1: uid
    int dst[2];         // 0: sock  1: uid
    int pipe_in;
    int pipe_out;
};

struct userinfo{
    int uid;
    string name;
    string conn_info;
    map<string, string> envvar;
    vector<pipeinfo> pipe_table;
};

void dupToStdIOE(int in, int out, int err){
    dup2(in, 0);
    dup2(out, 1);
    dup2(err, 2);

    return;
}

void broadcast(int recover_ioe_fd[3], map<int, userinfo> &user_info_list, string msg){
    for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
        dupToStdIOE((it->first), (it->first), (it->first));

        cout << msg << endl;
    }
    dupToStdIOE(recover_ioe_fd[0], recover_ioe_fd[1], recover_ioe_fd[2]);
    return;
}

void doFork(vector<vector<string>> cmds, vector<pipeinfo> &pipe_table, bool &pipe_in_end, int ssock, map<int, userinfo> &user_info_list, vector<userpipe> &user_pipe_table, string user_input){
    bool user_send_pipe = false, user_recv_pipe = false;
    pid_t pid;
    vector<pid_t> pid_list;

    for(int i = 0; i < cmds.size(); i++){
        bool err = false;
        // Select a existing same pipe (look up if there's same destination cmd/line by surveying whole pipe_table)
        // Create a new pipe if there's no existing same pipe
        // Store the pipe's fd to pipe_table
        for(int j = 0; j < pipe_table.size(); j++){
            if(pipe_table.at(j).behind_cmd_idx == i){
                // If pipe_j is behind cmds_i
                bool same_pipe = false;

                if(pipe_table.at(j).pipe_type == 3 || pipe_table.at(j).pipe_type == 4){
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
                if(pipe_table.at(j).pipe_type == 6){
                    // If pipe_j is a user recv pipe
                    bool find_user = false, find_pipe = false;
                    for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                        if((it->second).uid == pipe_table.at(j).uid){
                            find_user = true;
                            break;
                        }
                    }
                    if(!find_user){
                        cerr << "*** Error: user #" << pipe_table.at(j).uid << " does not exist yet. ***" << endl;
                        err = true;
                        break;
                    }
                    if(!err){
                        for(int k = 0; k < user_pipe_table.size(); k++){
                            if(user_info_list[ssock].uid == user_pipe_table.at(k).dst[1] && pipe_table.at(j).uid == user_pipe_table.at(k).src[1]){
                                user_pipe_table.at(k).dst[0] = ssock;
                                pipe_table.at(j).in = user_pipe_table.at(k).pipe_in;
                                pipe_table.at(j).out = user_pipe_table.at(k).pipe_out;
                                user_recv_pipe = true;
                                find_pipe = true;
                                break;
                            }
                        }
                        if(!find_pipe){
                            cerr << "*** Error: the pipe #" << pipe_table.at(j).uid << "->#" << user_info_list[ssock].uid << " does not exist yet. ***" << endl;
                            err = true;
                        }
                    }
                    same_pipe = true;
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
                    if(pipe_table.at(j).pipe_type == 5){
                        bool find_user = false;
                        for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                            if((it->second).uid == pipe_table.at(j).uid){
                                find_user = true;
                                break;
                            }
                        }
                        if(!find_user){
                            cerr << "*** Error: user #" << pipe_table.at(j).uid << " does not exist yet. ***" << endl;
                            err = true;
                            break;
                        }
                        if(!err){
                            for(int k = 0; k < user_pipe_table.size(); k++){
                                if(user_info_list[ssock].uid == user_pipe_table.at(k).src[1] && pipe_table.at(j).uid == user_pipe_table.at(k).dst[1]){
                                    cerr << "*** Error: the pipe #" << user_info_list[ssock].uid << "->#" << pipe_table.at(j).uid << " already exists. ***" << endl;
                                    err = true;
                                    break;
                                }
                            }
                        }
                        if(!err){
                            struct userpipe user_pipe_tmp;
                            user_pipe_tmp.src[0] = ssock;
                            user_pipe_tmp.src[1] = user_info_list[ssock].uid;
                            user_pipe_tmp.dst[1] = pipe_table.at(j).uid;
                            user_pipe_tmp.pipe_in = pipe_tmp[1];
                            user_pipe_tmp.pipe_out = pipe_tmp[0];
                            user_pipe_table.push_back(user_pipe_tmp);
                            user_send_pipe = true;
                        }
                    }
                }
                if(pipe_table.at(j).behind_cmd_idx == i+1) break;
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
                    }else if(pipe_table.at(j).pipe_type == 5){
                        // User send pipe
                        if(err){
                            int fd = open("/dev/null", O_RDWR, 0);
                            dup2(fd, 1);
                        }else{
                            int recover_ioe_fd[3]; 
                            string msg, src_name, src_uid, dst_name, dst_uid;
                            src_name = user_info_list[ssock].name;
                            src_uid = to_string(user_info_list[ssock].uid);
                            for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                                if((it->second).uid == pipe_table.at(j).uid){
                                    dst_name = (it->second).name;
                                    dst_uid = to_string((it->second).uid);
                                }
                            }

                            dup2(pipe_table.at(j).in, 1);

                            recover_ioe_fd[0] = dup(0);
                            recover_ioe_fd[1] = dup(1);
                            recover_ioe_fd[2] = dup(2);
                            msg = "*** " + src_name + " (#" + src_uid + ") just piped '" + user_input + "' to " + dst_name + " (#" + dst_uid + ") ***";
                            broadcast(recover_ioe_fd, user_info_list, msg);
                            close(recover_ioe_fd[0]);
                            close(recover_ioe_fd[1]);
                            close(recover_ioe_fd[2]);
                        }
                    }else if(pipe_table.at(j).pipe_type == 6){
                        // User recv pipe
                        if(err){
                            int fd = open("/dev/null", O_RDONLY, 0);
                            dup2(fd, 0);
                        }else{
                            int recover_ioe_fd[3]; 
                            string msg, src_name, src_uid, dst_name, dst_uid;
                            for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                                if((it->second).uid == pipe_table.at(j).uid){
                                    src_name = (it->second).name;
                                    src_uid = to_string((it->second).uid);
                                }
                            }
                            dst_name = user_info_list[ssock].name;
                            dst_uid = to_string(user_info_list[ssock].uid);

                            dup2(pipe_table.at(j).out, 0);

                            recover_ioe_fd[0] = dup(0);
                            recover_ioe_fd[1] = dup(1);
                            recover_ioe_fd[2] = dup(2);
                            msg = "*** " + dst_name + " (#" + dst_uid + ") just received from " + src_name + " (#" + src_uid + ") by '" + user_input + "' ***";
                            broadcast(recover_ioe_fd, user_info_list, msg);
                            close(recover_ioe_fd[0]);
                            close(recover_ioe_fd[1]);
                            close(recover_ioe_fd[2]);
                        }
                    }
                }

                if(i != 0 && pipe_table.at(j).behind_cmd_idx == i-1 && (pipe_table.at(j).pipe_type == 0 || pipe_table.at(j).pipe_type == 1)){
                    // If current cmd is NOT a command but a file name then do nothing
                    is_file = true;
                }else if(i != 0 && pipe_table.at(j).behind_cmd_idx == i-1 && (pipe_table.at(j).pipe_type == 2 || pipe_table.at(j).pipe_type == 5)){
                    // If current cmd is normal pipe's output then change its stdin fd to pipe's output fd
                    dup2(pipe_table.at(j).out, 0);
                }else if(i == 0 && pipe_table.at(j).line_cnt == 0 && (pipe_table.at(j).pipe_type == 3 || pipe_table.at(j).pipe_type == 4)){
                    // If current cmd is number pipe's output then change its stdin fd to pipe's output fd
                    dup2(pipe_table.at(j).out, 0);
                }
            }

            for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                if(it->first != ssock) close(it->first);
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
            if(((i != 0 && pipe_table.at(j).behind_cmd_idx == i-1) || (i == 0 && pipe_table.at(j).line_cnt == 0)) && pipe_table.at(j).pipe_type <= 4){
                // If current cmd is pipe's output, the pipe's input and output should be all connected by child process
                // Thus parent can close these pipe's fd
                // Parent now can close all useless fd since all necessary pipe fd has already connected to corresponding cmd's stdin/stdout/stderr
                close(pipe_table.at(j).out);
                close(pipe_table.at(j).in);
                pipe_table.erase(pipe_table.begin() + j);
            }else if(pipe_table.at(j).behind_cmd_idx == i && err){
                pipe_table.erase(pipe_table.begin() + j);
            }else if(pipe_table.at(j).behind_cmd_idx == i && pipe_table.at(j).pipe_type == 6 && !err){
                // close and clear
                int src_uid, dst_uid, src_sock;
                src_uid = pipe_table.at(j).uid;
                dst_uid = user_info_list[ssock].uid;

                for(int k = 0; k < user_pipe_table.size(); k++){
                    if(user_pipe_table.at(k).src[1] == src_uid && user_pipe_table.at(k).dst[1] == dst_uid){
                        src_sock = user_pipe_table.at(k).src[0];
                        close(user_pipe_table.at(k).pipe_in);
                        close(user_pipe_table.at(k).pipe_out);
                        user_pipe_table.erase(user_pipe_table.begin() + k);
                        break;
                    }
                }
                for(int k = 0; k < user_info_list[src_sock].pipe_table.size(); k++){
                    if(user_info_list[src_sock].pipe_table.at(k).pipe_type == 5 && user_info_list[src_sock].pipe_table.at(k).uid == dst_uid){
                        user_info_list[src_sock].pipe_table.erase(user_info_list[src_sock].pipe_table.begin() + k);
                        break;
                    }
                }
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

int buildInCmd(int ssock, map<int, userinfo> &user_info_list, vector<string> cmd){
    if(cmd.at(0).compare("exit") == 0){
        return 1;
    }else if(cmd.at(0).compare("setenv") == 0){
        if(cmd.size() == 3) user_info_list[ssock].envvar[cmd.at(1)] = cmd.at(2);
        else cerr << "usage: setenv [variable name] [value to assign]" << endl;
    }else if(cmd.at(0).compare("printenv") == 0){
        if(cmd.size() == 2){
            if(user_info_list[ssock].envvar.find(cmd.at(1)) != user_info_list[ssock].envvar.end()) cout << user_info_list[ssock].envvar.at(cmd.at(1)) << "\n";
        }
        else cerr << "usage: printenv [variable name]" << endl;
    }else if(cmd.at(0).compare("who") == 0){
        if(cmd.size() == 1){
            cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>" << endl;
            for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                if(it->first == ssock) cout << (it->second).uid << "\t" << (it->second).name << "\t" << (it->second).conn_info << "\t<-me" << endl;
                else cout << (it->second).uid << "\t" << (it->second).name << "\t" << (it->second).conn_info << endl;
            }
        }
        else cerr << "usage: who" << endl;
    }else if(cmd.at(0).compare("tell") == 0){
        if(cmd.size() >= 3){
            for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                if((it->second).uid == atoi(cmd.at(1).c_str())){
                    string msg = "*** " + user_info_list[ssock].name + " told you ***: ";
                    for(int i = 2; i < cmd.size(); i++){
                        msg += cmd.at(i);
                        msg += " ";
                    }
                    msg.pop_back();
                    dupToStdIOE((it->first), (it->first), (it->first));
                    cout << msg << endl;
                    dupToStdIOE(ssock, ssock, ssock);
                    return 0;
                }
            }
            cout << "*** Error: user #" + cmd.at(1) + " does not exist yet. ***" << endl;
        }
        else cerr << "usage: tell [user id] [message]" << endl;
    }else if(cmd.at(0).compare("yell") == 0){
        if(cmd.size() >= 2){
            int recover_ioe_fd[3] = {ssock, ssock, ssock};
            string msg = "*** " + user_info_list[ssock].name + " yelled ***: ";
            for(int i = 1; i < cmd.size(); i++){
                msg += cmd.at(i);
                msg += " ";
            }
            msg.pop_back();
            broadcast(recover_ioe_fd, user_info_list, msg);
        }
        else cerr << "usage: yell [message]" << endl;
    }else if(cmd.at(0).compare("name") == 0){
        if(cmd.size() == 2){
            int recover_ioe_fd[3] = {ssock, ssock, ssock};
            for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
                if((it->second).name.compare(cmd.at(1)) == 0){
                    cout << "*** User '" + cmd.at(1) + "' already exists. ***" << endl;
                    return 0;
                }
            }
            string msg = " *** User from " + user_info_list[ssock].conn_info + " is named '" + cmd.at(1) + "'. ***";
            user_info_list[ssock].name = cmd.at(1);
            broadcast(recover_ioe_fd, user_info_list, msg);
        }
        else cerr << "usage: name [newname]" << endl;
    }

    return 0;
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

    // Check for build in cmds
    if(tokens.at(0).compare("exit") == 0 || tokens.at(0).compare("setenv") == 0 || tokens.at(0).compare("printenv") == 0 || tokens.at(0).compare("who") == 0 || tokens.at(0).compare("tell") == 0 || tokens.at(0).compare("yell") == 0 || tokens.at(0).compare("name") == 0){
        for(int i = 0; i < tokens.size(); i++){
            cmd.push_back(tokens.at(i));
        }
        cmds.push_back(cmd);

        return cmds;
    }

    // Divide tokens into cmd + args, pipe, number pipe, and io redirection
    for(int i = 0; i < tokens.size(); i++){
        if(tokens.at(i) == "<"){
            // Input redirection
            struct pipeinfo pipe_info_tmp;

            if(!cmd.empty()) cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            pipe_info_tmp.pipe_type = 0;
            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.uid = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else if(tokens.at(i) == ">"){
            // Output redirection
            struct pipeinfo pipe_info_tmp;

            if(!cmd.empty()) cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            pipe_info_tmp.pipe_type = 1;
            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.uid = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else if(tokens.at(i) == "|"){
            // Normal pipe
            struct pipeinfo pipe_info_tmp;

            if(!cmd.empty()) cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            pipe_info_tmp.pipe_type = 2;
            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.uid = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
            pipe_cnt++;
        }else if(tokens.at(i).at(0) == '|' && isdigit(tokens.at(i).at(1)) || tokens.at(i).at(0) == '!' && isdigit(tokens.at(i).at(1))){
            // Number pipe
            int pipe_number = 0;
            struct pipeinfo pipe_info_tmp;

            if(!cmd.empty()) cmds.push_back(cmd);
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
            pipe_info_tmp.uid = -1;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            pipe_table.push_back(pipe_info_tmp);
        }else if(tokens.at(i).at(0) == '>' && isdigit(tokens.at(i).at(1)) || tokens.at(i).at(0) == '<' && isdigit(tokens.at(i).at(1))){
            // User pipe
            int uid = 0;
            struct pipeinfo pipe_info_tmp;

            if(!cmd.empty()) cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;

            // Parse the number behind the pipe
            uid = atoi(tokens.at(i).substr(1, tokens.at(i).size()).c_str());

            // Initalize pipe_info_tmp that is going to push to pipe_table
            pipe_info_tmp.in = -1;
            pipe_info_tmp.out = -1;
            if(tokens.at(i).at(0) == '>') pipe_info_tmp.pipe_type = 5;
            else if(tokens.at(i).at(0) == '<') pipe_info_tmp.pipe_type = 6;

            pipe_info_tmp.line_cnt = -1;
            pipe_info_tmp.uid = uid;
            pipe_info_tmp.behind_cmd_idx = pipe_cnt;
            if(pipe_table.size() != 0 && pipe_info_tmp.pipe_type == 6 && pipe_table.back().pipe_type == 5){
                vector<pipeinfo>::iterator it = pipe_table.end();
                pipe_table.insert(--it, pipe_info_tmp);
            }
            else pipe_table.push_back(pipe_info_tmp);
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
    if(pipe_table.size() != 0){
        if(pipe_table.back().pipe_type == 6) pipe_in_end = false;
    }

    return cmds;
}

string readInput(int ssock){
    string user_input;

    getline(cin, user_input, '\n');

    return user_input;
}

int npsh(int ssock, map<int, userinfo> &user_info_list, vector<userpipe> &user_pipe_table){
    bool pipe_in_end;
    string user_input;
    vector<vector<string>> cmds;  // Outer vector: different cmds; Inner vector: single cmd + args

    user_input = readInput(ssock);
    while(user_input.back() == '\r' || user_input.back() == '\n'){
        user_input.pop_back();
    }

    if(user_input.size() >= 1) cmds = parseCmd(user_input, user_info_list[ssock].pipe_table, pipe_in_end);
    else return 0;  // Means the input is a blank line

    if(cmds.at(0).at(0).compare("exit") == 0 || cmds.at(0).at(0).compare("setenv") == 0 || cmds.at(0).at(0).compare("printenv") == 0 || cmds.at(0).at(0).compare("who") == 0 || cmds.at(0).at(0).compare("tell") == 0 || cmds.at(0).at(0).compare("yell") == 0 || cmds.at(0).at(0).compare("name") == 0){
        if(buildInCmd(ssock, user_info_list, cmds.at(0)) != 0) return 1;
    }
    else doFork(cmds, user_info_list[ssock].pipe_table, pipe_in_end, ssock, user_info_list, user_pipe_table, user_input);

    return 0;
}

void clearPipeTable(int ssock, map<int, userinfo> &user_info_list, vector<userpipe> & user_pipe_table){
    for(int i = 0; i < user_pipe_table.size(); ){
        if(user_pipe_table.at(i).src[0] == ssock){
            close(user_pipe_table.at(i).pipe_in);
            close(user_pipe_table.at(i).pipe_out);
            user_pipe_table.erase(user_pipe_table.begin() + i);
        }else if(user_pipe_table.at(i).dst[0] == ssock){
            int src_sock = user_pipe_table.at(i).src[0];
            for(int j = 0; j < user_info_list[src_sock].pipe_table.size(); ){
                if(user_info_list[src_sock].pipe_table.at(j).uid == user_pipe_table.at(i).dst[1]){
                    user_info_list[src_sock].pipe_table.erase(user_info_list[src_sock].pipe_table.begin() + j);
                    close(user_pipe_table.at(i).pipe_in);
                    close(user_pipe_table.at(i).pipe_out);
                    user_pipe_table.erase(user_pipe_table.begin() + i);
                }else{
                    j++;
                }
            }
        }else{
            i++;
        }
    }

    return;
}

void initUserEnv(int ssock, map<int, userinfo> &user_info_list){
    map<string, string> envvar = user_info_list[ssock].envvar;
    for(map<string, string>::iterator it = envvar.begin(); it != envvar.end(); it++){
        char env_name[(it->first).size()+1];
        char env_val[(it->second).size()+1];

        strcpy(env_name, (it->first).c_str());
        strcpy(env_val, (it->second).c_str());

        setenv(env_name, env_val, 1);
    }

    return;
}

void printWelcome(int ssock){
    cout << "****************************************" << endl;
    cout << "** Welcome to the information server. **" << endl;
    cout << "****************************************" << endl;

    return;
}

void insertNewUser(int ssock, map<int, userinfo> &user_info_list, sockaddr_in slave){
    char ip[INET_ADDRSTRLEN];
    userinfo user_info_tmp;

    user_info_tmp.name = "(no name)";
    user_info_tmp.envvar["PATH"] = "bin:.";
    inet_ntop(AF_INET, &(slave.sin_addr), ip, INET_ADDRSTRLEN);
    user_info_tmp.conn_info = ip + (string)":" + to_string(ntohs(slave.sin_port));
    if(user_info_list.size() == 0){
        user_info_tmp.uid = 1;
        user_info_list[ssock] = user_info_tmp;
    }else{
        int uid = 1;
        for(map<int, userinfo>::iterator it = user_info_list.begin(); it != user_info_list.end(); it++){
            if((it->second).uid != uid){
                user_info_tmp.uid = uid;
                user_info_list.insert(it, pair<int, userinfo>(ssock, user_info_tmp));

                return;
            }
            uid += 1;
        }
        user_info_tmp.uid = uid;
        user_info_list.insert(user_info_list.end(), pair<int, userinfo>(ssock, user_info_tmp));
    }

    return;
}

void initServer(int port){
    int msock = 0;
    vector<userpipe> user_pipe_table;
    map<int, userinfo> user_info_list;

    msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(msock == -1){
        cerr << "npsh: cannot create socket (" << strerror(errno) << ")" << endl;
        exit(EXIT_FAILURE);
    }

    int flag = 1;
    sockaddr_in master;

    // Init master socket address
    master.sin_family = AF_INET;
    master.sin_port = htons(port);
    master.sin_addr.s_addr = INADDR_ANY; 

    setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));
    if(bind(msock, (sockaddr *)&master, sizeof(sockaddr_in)) == -1){
        cerr << "npsh: cannot bind to port " << port << " (" << strerror(errno) << ")" << endl;
        exit(EXIT_FAILURE);
    }
    if(listen(msock, 1) == -1){
        cerr << "npsh: cannot listen on TCP port " << port << " (" << strerror(errno) << ")" << endl;
        exit(EXIT_FAILURE);
    }

    int fd_set_num = FD_SETSIZE;
    fd_set readonly_fds, active_fds;
    timeval timeout = {0, 5000};

    // Init active fd set
    FD_ZERO(&active_fds);
    FD_SET(msock, &active_fds);

    while(1){
        memcpy(&readonly_fds, &active_fds, sizeof(active_fds));

        if(select(fd_set_num, &readonly_fds, NULL, NULL, &timeout) < 0){
            if(errno != EINTR) cerr << "npsh: select failed (" << strerror(errno) << ")" << endl;
            continue;
        }

        if(FD_ISSET(msock, &readonly_fds)){
            int ssock = 0, ssocklen = 0, recover_ioe_fd[3] = {ssock, ssock, ssock};
            string login_msg;
            sockaddr_in slave;

            ssocklen = sizeof(sockaddr_in);
            ssock = accept(msock, (sockaddr *)&slave, (socklen_t *)&ssocklen);
            if(ssock == -1){
                cerr << "npsh: cannot accept socket (" << strerror(errno) << ")" << endl;
                continue;
            }
            FD_SET(ssock, &active_fds);

            insertNewUser(ssock, user_info_list, slave);
            dupToStdIOE(ssock, ssock, ssock);
            printWelcome(ssock);
            login_msg = "*** User '" + user_info_list[ssock].name + "' entered from " + user_info_list[ssock].conn_info + ". ***";
            broadcast(recover_ioe_fd, user_info_list, login_msg);
            // Print prompt
            cout << "% " << flush;
        }
        for(int ssock = 0; ssock < fd_set_num; ssock++){
            if(ssock != msock && FD_ISSET(ssock, &readonly_fds)){
                clearenv();
                dupToStdIOE(ssock, ssock, ssock);
                initUserEnv(ssock, user_info_list);
                if(npsh(ssock, user_info_list, user_pipe_table) == 0) cout << "% " << flush;
                else{
                    int recover_ioe_fd[3] = {ssock, ssock, ssock};
                    string logout_msg;

                    logout_msg = "*** User '" + user_info_list[ssock].name + "' left. ***";
                    broadcast(recover_ioe_fd, user_info_list, logout_msg);

                    dupToStdIOE(msock, msock, msock);
                    close(ssock);
                    FD_CLR(ssock, &active_fds);
                    clearPipeTable(ssock, user_info_list, user_pipe_table);
                    user_info_list.erase(ssock);
                };
            }
        }
    }
    return;
}

void childHandler(int signo){
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0){
        //do nothing
    }
}

int main(int argc, char **argv){
    // Check if user provide port number
    if(argc != 2){
        cerr << "usage: np_simple [port]" << endl;
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);

    // Create a signal handler
    signal(SIGCHLD, childHandler);
    // Clear all env
    clearenv();
    // Only set PATH as env var, and init it to "bin:."
    setenv("PATH", "bin:.", 1);

    initServer(port);

    return 0;
}
