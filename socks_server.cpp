#include <cstdlib>
#include <fstream>
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
#include <boost/regex.hpp>
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
        memset(data_, '\0', sizeof(data_));
        datalen = 0;
        read_req();
    }

private:
    void read_req(){
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
                    datalen = length;
                    timer_.cancel();
                    do_fork();
                }
            });
    }

    void do_connect(){
        boost::asio::io_context io_context_conn;
        tcp::socket s_conn(io_context_conn);
        tcp::resolver r(io_context_conn);
        tcp::resolver::query q(curr_conn_env["D_IP"], curr_conn_env["D_PORT"]);
        tcp::endpoint endpoint = r.resolve(q)->endpoint();

        auto self(shared_from_this());
        // s_conn.connect(endpoint, [this, self](boost::system::error_code ec){
        //     if(!ec){
        //         do_read();
        //     }
        //     else{
        //         cout << "<script>console.log(\"" << ec.message() << "\")</script>" << flush;
        //     }
        // });
    }

    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length-1),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    
                }
            });
    }

    void do_write(string resp){
        auto self(shared_from_this());

        boost::asio::async_write(socket_, boost::asio::buffer(resp, resp.length()),
            [this, self](boost::system::error_code ec, size_t /*length*/){
                if(!ec){
                    // No err
                }
            });

        return;
    }

    void printSocksMsg(){
        cout << "<S_IP>: " << curr_conn_env["S_IP"] << endl;
        cout << "<S_PORT>: " << curr_conn_env["S_PORT"] << endl;
        cout << "<D_IP>: " << curr_conn_env["D_IP"] << endl;
        cout << "<D_PORT>: " << curr_conn_env["D_PORT"] << endl;
        cout << "<Command>: " << curr_conn_env["CMD"] << endl;
        cout << "<Reply>: " << curr_conn_env["REPLY"] << endl;
    }

    void do_parse(){
        curr_conn_env["VN"] = to_string(data_[0]);
        curr_conn_env["CD"] = to_string(data_[1]);
        curr_conn_env["CMD"] = (uint)data_[1] == 1 ? "CONNECT" : "BIND";
        curr_conn_env["D_PORT"] = to_string((uint)((data_[2]<<8) | data_[3]));
        if((uint)data_[4] == 0 && (uint)data_[5] == 0 && (uint)data_[6] == 0 && (uint)data_[7] !=0){
            int null_ctr = 0;
            string domain;
            for(uint i = 8; i < datalen; i++){
                if(data_[i] == '\0'){
                    null_ctr++;
                    continue;
                }
                if(null_ctr == 1){
                    domain += data_[i];
                }
                else if(null_ctr == 2){
                    break;
                }
            }
            tcp::resolver r(io_context_);
            tcp::resolver::query q(domain, curr_conn_env["D_PORT"]);
            tcp::endpoint endpoint = r.resolve(q)->endpoint();
            curr_conn_env["PROTO"] = "SOCKS4A";
            curr_conn_env["D_IP"] = endpoint.address().to_string();
        }
        else{
            curr_conn_env["PROTO"] = "SOCKS4";
            curr_conn_env["D_IP"] = to_string((uint)data_[4]) + "." + to_string((uint)data_[5]) + "." + to_string((uint)data_[6]) + "." + to_string((uint)data_[7]);
        }
        curr_conn_env["S_IP"] = socket_.remote_endpoint().address().to_string();
        curr_conn_env["S_PORT"] = to_string(socket_.remote_endpoint().port());
        curr_conn_env["REPLY"] = "Reject";

        return;
    }

    void check_firewall(){
        fstream file;
        file.open("./socks.conf", ios::in);
        if(file){
            char buffer_[1024];
            while(!file.eof()){
                string buffer;
                string s = curr_conn_env["CD"] == "1" ? "permit c " : "permit b ";
                s += curr_conn_env["D_IP"];
                file.getline(buffer_, sizeof(buffer_));
                buffer = buffer_;
                boost::replace_all(buffer, "*", "[0-9]+");
                boost::replace_all(buffer, ".", "\\.");

                boost::regex expr(buffer);
                if(boost::regex_match(s, expr)){
                    curr_conn_env["REPLY"] = "Accept";
                    break;
                }
            }
        }
        file.close();

        return;
    }

    string generate_reply(){
        string resp;
        resp += '\0';
        if(curr_conn_env["REPLY"] == "Accept"){
            resp += static_cast<unsigned char>(90);
        }
        else{
            resp += static_cast<unsigned char>(91);
        }
        resp += data_[2];
        resp += data_[3];
        resp += data_[4];
        resp += data_[5];
        resp += data_[6];
        resp += data_[7];
        for(unsigned int i = 0; i < resp.length(); i++){
            bitset<8> x(resp.at(i));
            cout << x << endl;
        }

        return resp;
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
            do_parse();
            check_firewall();
            printSocksMsg();
            do_write(generate_reply());
            if(curr_conn_env["REPLY"] == "Reject"){
                exit(EXIT_SUCCESS);
            }
            memset(data_, '\0', sizeof(data_));
            // do_connect();
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

    tcp::socket socket_;
    boost::asio::io_context& io_context_;
    boost::asio::signal_set& signal_;
    boost::asio::steady_timer timer_;
    enum {max_length = 1024};
    unsigned char data_[max_length];
    uint datalen;
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