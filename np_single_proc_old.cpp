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
    int pipe_type;      // 0: input redirection  1: output redirection  2: normal pipe  3: number pipe  4: number pipe with stderr
    int behind_cmd_idx; // The index of cmds that the pipe is behind of
};

struct userinfo{
    int uid;
    string name;
    sockaddr_in conn_info;
    map<string, string> envvar;
    vector<pipeinfo> pipe_table;
};

void buildInCmd(int ssock, vector<string> cmd){
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
            string err_msg = "usage: setenv [variable name] [value to assign]\n";
            send(ssock, err_msg.c_str(), err_msg.size(), 0);
        }
    }else if(cmd.at(0).compare("printenv") == 0){
        if(cmd.size() == 2){
            char env_name[cmd.at(1).size() + 1];

            strcpy(env_name, cmd.at(1).c_str());

            if(getenv(env_name) != NULL) cout << getenv(env_name) << "\n";
        }else{
            string err_msg = "usage: printenv [variable name]\n";
            send(ssock, err_msg.c_str(), err_msg.size(), 0);
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

string readInput(int ssock){
    char buffer[15000];
    string user_input;

    recv(ssock, buffer, 15000, 0);
    user_input = buffer;

    return user_input;
}

void npsh(int ssock, map<int, int> &socket_uid_map, vector<userinfo> &user_info_list){
    int uid = socket_uid_map[ssock];
    bool pipe_in_end;
    string user_input;
    vector<vector<string>> cmds;  // Outer vector: different cmds; Inner vector: single cmd + args

    user_input = readInput(ssock);
    while(user_input.back() == '\r' || user_input.back() == '\n'){
        user_input.pop_back();
    }

    if(user_input.size() >= 1) cmds = parseCmd(user_input, user_info_list.at(uid-1).pipe_table, pipe_in_end);
    else return;  // Means the input is a blank line

    if(cmds.at(0).at(0).compare("exit") == 0 || cmds.at(0).at(0).compare("setenv") == 0 || cmds.at(0).at(0).compare("printenv") == 0) buildInCmd(ssock, cmds.at(0));
    // else doFork(cmds, user_info_list.at(uid).pipe_table, pipe_in_end);

    return;
}

void printWellcome(int ssock){
    string wellcome_msg = "";

    wellcome_msg = wellcome_msg + "***************************************\n";
    wellcome_msg = wellcome_msg + "** Welcome to the information server **\n";
    wellcome_msg = wellcome_msg + "***************************************\n";
    send(ssock, wellcome_msg.c_str(), wellcome_msg.size(), 0);

    return;
}

void insertNewUser(int ssock, map<int, int> &socket_uid_map, vector<userinfo> &user_info_list, sockaddr_in slave){
    userinfo user_info_tmp;

    user_info_tmp.name = "no name";
    user_info_tmp.conn_info = slave;
    user_info_tmp.envvar["PATH"] = "bin:.";
    if(user_info_list.size() == 0){
        user_info_tmp.uid = 1;
        socket_uid_map[ssock] = user_info_tmp.uid;
        user_info_list.push_back(user_info_tmp);
    }else{
        for(int i = 0; i < user_info_list.size(); i++){
            if(user_info_list.at(i).uid != i+1){
                user_info_tmp.uid = i+1;
                socket_uid_map[ssock] = user_info_tmp.uid;
                user_info_list.insert(user_info_list.begin()+i, user_info_tmp);
                break;
            }else if(i == user_info_list.size()-1 && user_info_list.at(i).uid == i+1){
                user_info_tmp.uid = i+2;
                socket_uid_map[ssock] = user_info_tmp.uid;
                user_info_list.push_back(user_info_tmp);
                break;
            }
        }
    }

    return;
}

void initServer(int port){
    int msock = 0;
    vector<userinfo> user_info_list;
    map<int, int> socket_uid_map;

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
            cerr << "npsh: select failed (" << strerror(errno) << ")" << endl;
            continue;
        }

        if(FD_ISSET(msock, &readonly_fds)){
            int ssock = 0, ssocklen = 0;
            sockaddr_in slave;

            ssocklen = sizeof(sockaddr_in);
            ssock = accept(msock, (sockaddr *)&slave, (socklen_t *)&ssocklen);
            if(ssock == -1){
                cerr << "npsh: cannot accept socket (" << strerror(errno) << ")" << endl;
                continue;
            }
            FD_SET(ssock, &active_fds);

            insertNewUser(ssock, socket_uid_map, user_info_list, slave);
            printWellcome(ssock);
            // Print prompt
            send(ssock, "% ", 2, 0);
        }
        for(int ssock = 0; ssock < fd_set_num; ssock++){
            if(ssock != msock && FD_ISSET(ssock, &readonly_fds)){
                npsh(ssock, socket_uid_map, user_info_list);
                send(ssock, "% ", 2, 0);
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

int main(int argc, char **argv, char **envp){
    // Check if user provide port number
    if(argc != 2){
        cerr << "usage: np_simple [port]" << endl;
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);

    // Create a signal handler
    signal(SIGCHLD, childHandler);
    // Fetch all env var and unset them
    for(char **env = envp; *env != 0; env++){
        char *envname;
        envname = strtok(*env, "=");
        unsetenv(envname);
    }
    // Only set PATH as env var, and init it to "bin:."
    setenv("PATH", "bin:.", 1);

    initServer(port);

    return 0;
}
