#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <sstream>
#include <iostream>

using namespace std;

void doFork(){
    // do Fork
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

vector<vector<string>> parseCmd(string user_input, bool &pipe_in_end){
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
    for(vector<string>::iterator iter = tokens.begin(); iter != tokens.end(); iter++){
        if(*iter == "<"){
            // Input redirection
            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;
        }else if(*iter == ">"){
            // Output redirection
            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;
        }else if(*iter == "|"){
            // Normal pipe
            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;
        }else if((*iter).at(0) == '|' && isdigit((*iter).at(1)) || (*iter).at(0) == '!' && isdigit((*iter).at(1))){
            // Number pipe
            cmds.push_back(cmd);
            cmd.clear();
            pipe_in_end = true;
        }else{
            // Cmd + args
            cmd.push_back(*iter);
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
    while(true){
        bool pipe_in_end;
        string user_input;
        vector<vector<string>> cmds;  // Outer vector: different cmds; Inner vector: single cmd + args

        // Print prompt
        cout << "% " << flush;

        user_input = readInput();

        if(user_input.size() >= 1) cmds = parseCmd(user_input, pipe_in_end);
        else continue;  // Means the input is a blank line

        if(cmds.at(0).at(0).compare("exit") == 0 || cmds.at(0).at(0).compare("setenv") == 0 || cmds.at(0).at(0).compare("printenv") == 0) buildInCmd(cmds.at(0));
        else doFork();
    }
}

int main(int argc, char **argv, char **envp){
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