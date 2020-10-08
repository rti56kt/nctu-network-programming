#include <signal.h>
#include <string.h>
#include <stdlib.h>

void npsh(){

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