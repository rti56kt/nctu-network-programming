#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <iostream>

using namespace std;

void doFork(){
    // do Fork
}

void buildInCmd(vector<string>){
    
}

vector<vector<string>> parseCmd(string user_input){
    // do parsing
}

string readInput(){
    string user_input;

    getline(cin, user_input, '\n');
    if(cin.eof()) exit(EXIT_SUCCESS);

    return user_input;
}

void npsh(){
    while(true){
        string user_input;
        vector<vector<string>> cmds;  // Outer vector: different cmds; Inner vector: single cmd + args

        // Print prompt
        cout << "%" << flush;

        user_input = readInput();
        if(user_input.size() >= 1) cmds = parseCmd(user_input);
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