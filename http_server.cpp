#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
using namespace std;

class session
    : public enable_shared_from_this<session>{
public:
    session(tcp::socket socket, boost::asio::io_context& io_context, boost::asio::signal_set& signal)
        : socket_(move(socket)),
          io_context_(io_context),
          signal_(signal),
          timer_(io_context){
    }

    void start(){
        do_read();
    }

private:
    void do_read(){
        auto self(shared_from_this());
        timer_.expires_from_now(chrono::seconds(10));
        timer_.async_wait([this, self](auto ec){
            if(!read_flag){
                cout << "[INFO]\t(" << getpid() << "): Timeout(10s): Close connection from (sock:" << socket_.native_handle() << "): " << flush;
                cout << socket_.remote_endpoint().address() << ":" << socket_.remote_endpoint().port() << endl;
                socket_.close();
            }
        });
        socket_.async_read_some(boost::asio::buffer(data_, max_length-1),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    read_flag = true;
                    timer_.cancel();
                    do_parse();
                    do_fork();
                }
            });
    }

    void do_write(bool exist){
        auto self(shared_from_this());
        string resp;
        if(exist){
            resp += "HTTP/1.1 200 OK\r\n";
        }
        else{
            resp += "HTTP/1.1 404 NOT FOUND\r\n\r\n";
        }
        // resp += "\r\n";
        boost::asio::async_write(socket_, boost::asio::buffer(resp, resp.length()),
            [this, self](boost::system::error_code ec, size_t /*length*/){
                if(!ec){
                    // No err
                }
            });
    }

    void do_parse(){
        string data = data_;
        boost::replace_all(data, "\r", "");
        istringstream datastream(data);
        for(int i = 0; i < 2; i++){
            string headerline;
            vector<string> token;
            getline(datastream, headerline);
            boost::split(token, headerline, boost::is_any_of(" "), boost::token_compress_on);
            if(i == 0){
                curr_conn_env["REQUEST_METHOD"] = token.at(0);
                curr_conn_env["REQUEST_URI"] = token.at(1);
                curr_conn_env["SERVER_PROTOCOL"] = token.at(2);
                if(token.at(1).find("?") != string::npos){
                    curr_conn_env["QUERY_STRING"] = token.at(1).substr(token.at(1).find("?")+1);
                }
                else curr_conn_env["QUERY_STRING"] = "";
            }
            else if(i == 1){
                curr_conn_env["HTTP_HOST"] = token.at(1);
            }
        }
        curr_conn_env["REMOTE_ADDR"] = socket_.remote_endpoint().address().to_string();
        curr_conn_env["REMOTE_PORT"] = to_string(socket_.remote_endpoint().port());
        curr_conn_env["SERVER_ADDR"] = socket_.local_endpoint().address().to_string();
        curr_conn_env["SERVER_PORT"] = to_string(socket_.local_endpoint().port());

        cout << "[INFO]\t(" << getpid() << "): " << curr_conn_env["REQUEST_METHOD"] << " " << curr_conn_env["REQUEST_URI"] << endl;
    }

    void do_fork(){
        int pid = 0;

        io_context_.notify_fork(boost::asio::io_context::fork_prepare);
        pid = fork();
        if(pid < 0){
            io_context_.notify_fork(boost::asio::io_context::fork_parent);
            cerr << "[ERROR]\thttp_server: Fork error" << endl;
            socket_.close();
        }
        else if(pid == 0){
            // Inform the io_context that the fork is finished and that this
            // is the child process. The io_context uses this opportunity to
            // create any internal file descriptors that must be private to
            // the new process.
            io_context_.notify_fork(boost::asio::io_context::fork_child);
            signal_.cancel();
            set_env();
            do_exec();
        }
        else if(pid > 0){
            // Inform the io_context that the fork is finished (or failed)
            // and that this is the parent process. The io_context uses this
            // opportunity to recreate any internal resources that were
            // cleaned up during preparation for the fork.
            io_context_.notify_fork(boost::asio::io_context::fork_parent);
            cout << "[INFO]\t(" << getpid() << "): Create child: " << pid << endl;
            // The parent process can now close the newly accepted socket. It
            // remains open in the child.
            socket_.close();
        }
    }

    void do_exec(){
        string uri = curr_conn_env["REQUEST_URI"];
        string file = "." + (uri.find("?") != string::npos ? uri.substr(0, uri.find("?")) : uri);
        char* arg[] = {strdup(file.c_str()), NULL};

        if(check_exist(file)){
            do_write(true);
            dup_fd();
            socket_.close();
            execvp(file.c_str(), arg);
        }
        else{
            do_write(false);
            socket_.close();
            exit(EXIT_SUCCESS);
        }
    }

    bool check_exist(string file){
        struct stat buf;
        return stat(file.c_str(), &buf) != -1 && S_ISREG(buf.st_mode) ? true : false;
    }

    void set_env(){
        clearenv();
        for(map<string, string>::iterator iter = curr_conn_env.begin(); iter != curr_conn_env.end(); iter++){
            setenv((iter->first).c_str(), (iter->second).c_str(), 1);
        }
    }

    void dup_fd(){
        dup2(socket_.native_handle(), 0);
        dup2(socket_.native_handle(), 1);
    }

    tcp::socket socket_;
    boost::asio::io_context& io_context_;
    boost::asio::signal_set& signal_;
    boost::asio::steady_timer timer_;
    enum {max_length = 1024};
    char data_[max_length];
    bool read_flag = false;
    map<string, string> curr_conn_env;
};

class server{
public:
    server(boost::asio::io_context& io_context, short port)
        : io_context_(io_context),
          signal_(io_context, SIGCHLD),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        wait_for_signal();
        do_accept();
    }

private:
    void wait_for_signal(){
        signal_.async_wait([this](boost::system::error_code /*ec*/, int /*signo*/){
            // Only the parent process should check for this signal. We can
            // determine whether we are in the parent by checking if the acceptor
            // is still open.
            if(acceptor_.is_open()){
                // Reap completed child processes so that we don't end up with
                // zombies.
                int status = 0;
                while (waitpid(-1, &status, WNOHANG) > 0) {}

                wait_for_signal();
            }
        });
    }

    void do_accept(){
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket){
            if(!ec){
                cout << "[INFO]\t(" << getpid() << "): Connection from (sock:" << socket.native_handle() << "): " << flush;
                cout << socket.remote_endpoint().address() << ":" << socket.remote_endpoint().port() << endl;
                make_shared<session>(move(socket), io_context_, signal_)->start();
            }

            do_accept();
        });
    }

    boost::asio::io_context& io_context_;
    boost::asio::signal_set signal_;
    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]){
    try{
        if(argc != 2){
            cerr << "Usage: http_server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;

        server s(io_context, atoi(argv[1]));

        io_context.run();
    }
    catch(exception& e){
        cerr << "[Error]\thttp_server: Exception: " << e.what() << "\n";
    }

    return 0;
}