#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include "plugin.hpp"

using boost::asio::ip::tcp;
using namespace std;

class session
    : public enable_shared_from_this<session>{
public:
    session(tcp::socket server_socket, boost::asio::io_context& io_context)
        : socket_(move(server_socket)),
          io_context_(io_context),
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
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    read_flag = true;
                    timer_.cancel();
                    do_parse();
                    do_exe();
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
    }

    void do_parse(){
        string data = data_;
        memset(data_, '\0', sizeof(data_));
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

    void do_exe(){
        string uri = curr_conn_env["REQUEST_URI"];
        string file = "." + (uri.find("?") != string::npos ? uri.substr(0, uri.find("?")) : uri);

        if(file.compare("./panel.cgi") == 0){
            string resp = "HTTP/1.1 200 OK\r\n";
            do_write(resp);
            panel();
            socket_.close();
        }
        else if(file.compare("./console.cgi") == 0){
            string resp = "HTTP/1.1 200 OK\r\n";
            do_write(resp);
            console();
            socket_.close();
        }
        else{
            string resp = "HTTP/1.1 404 NOT FOUND\r\n\r\n";
            do_write(resp);
            socket_.close();
        }
    }

    void panel(){
        string resp;
        resp = "Content-type: text/html\r\n\r\n";
        do_write(resp);
        resp = generatePanelHtml();
        do_write(resp);
    }

    void console(){
        string resp;
        vector<query> query_string = parseQueryString(curr_conn_env["QUERY_STRING"]);
        vector<string> servers = getServers(query_string);
        resp = "Content-type: text/html\r\n\r\n";
        do_write(resp);
        resp = generateConsoleHtml(servers);
        do_write(resp);

        createClients(query_string);
    }

    void createClients(vector<query> query_string){
        boost::asio::io_context c_io_context;
        for(unsigned int i = 0; i < query_string.size(); i++){
            tcp::resolver r(c_io_context);
            make_shared<client>(c_io_context, socket_, query_string.at(i), i)->start(r.resolve(query_string.at(i).host, query_string.at(i).port));
        }
        c_io_context.run();
    }

    tcp::socket socket_;
    boost::asio::io_context& io_context_;
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
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){
        acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
        do_accept();
    }

private:
    void do_accept(){
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket server_socket){
            if(!ec){
                cout << "[INFO]\t(" << getpid() << "): Connection from (sock:" << server_socket.native_handle() << "): " << flush;
                cout << server_socket.remote_endpoint().address() << ":" << server_socket.remote_endpoint().port() << endl;
                make_shared<session>(move(server_socket), io_context_)->start();
            }
            else{
                cerr << "[ERROR]\t" << ec.message() << endl;
            }

            do_accept();
        });
    }

    boost::asio::io_context& io_context_;
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

    cerr << "[ERROR]\t" << endl;

    return 0;
}