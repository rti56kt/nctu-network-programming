#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
using namespace std;

struct connenv{
    string REQUEST_METHOD;
    string REQUEST_URI;
    string QUERY_STRING;
    string SERVER_PROTOCOL;
    string HTTP_HOST;
    string SERVER_ADDR;
    string SERVER_PORT;
    string REMOTE_ADDR;
    string REMOTE_PORT;
};

class session
    : public enable_shared_from_this<session>{
public:
    session(tcp::socket socket)
        : socket_(move(socket)){
    }

    void start(){
        do_read();
    }

private:
    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    // cout << data_ << endl;
                    do_parse();
                    do_write(length);
                }
            });
    }

    void do_write(size_t length){
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(data_, length),
            [this, self](boost::system::error_code ec, size_t /*length*/){
                if(!ec){
                    do_read();
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
                curr_conn_env.REQUEST_METHOD = token.at(0);
                curr_conn_env.REQUEST_URI = token.at(1);
                curr_conn_env.SERVER_PROTOCOL = token.at(2);
                if(token.at(1).find("?") != string::npos){
                    curr_conn_env.QUERY_STRING = token.at(1).substr(token.at(1).find("?")+1);
                }
            }
            else if(i == 1){
                curr_conn_env.HTTP_HOST = token.at(1);
            }
        }
        curr_conn_env.REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
        curr_conn_env.REMOTE_PORT = to_string(socket_.remote_endpoint().port());
        curr_conn_env.SERVER_ADDR = socket_.local_endpoint().address().to_string();
        curr_conn_env.SERVER_PORT = to_string(socket_.local_endpoint().port());

        cout << curr_conn_env.REQUEST_METHOD << endl;
        cout << curr_conn_env.REQUEST_URI << endl;
        cout << curr_conn_env.SERVER_PROTOCOL << endl;
        cout << curr_conn_env.QUERY_STRING << endl;
        cout << curr_conn_env.HTTP_HOST << endl;
        cout << curr_conn_env.REMOTE_ADDR << endl;
        cout << curr_conn_env.REMOTE_PORT << endl;
        cout << curr_conn_env.SERVER_ADDR << endl;
        cout << curr_conn_env.SERVER_PORT << endl;
    }

    tcp::socket socket_;
    enum {max_length = 1024};
    char data_[max_length];
    connenv curr_conn_env;
};

class server{
public:
    server(boost::asio::io_context& io_context, short port)
        : io_context_(io_context),
          signal_(io_context, SIGCHLD),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){
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
                int pid = 0;

                io_context_.notify_fork(boost::asio::io_context::fork_prepare);
                pid = fork();
                if(pid){
                    io_context_.notify_fork(boost::asio::io_context::fork_child);

                    // The child won't be accepting new connections, so we can close
                    // the acceptor. It remains open in the parent.
                    acceptor_.close();

                    // The child process is not interested in processing the SIGCHLD
                    // signal.
                    signal_.cancel();

                    make_shared<session>(move(socket))->start();
                }
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
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}