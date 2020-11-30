#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using boost::asio::ip::tcp;
using namespace std;

struct query{
    string host;
    string port;
    string file;
};

class client
    : public enable_shared_from_this<client>{
public:
    client(boost::asio::io_context& io_context, query query_info, int server_num)
        : io_context_(io_context),
          socket_(io_context),
          file_stream_("./test_case/" + query_info.file),
          server_num_(server_num){
    }

    void start(tcp::resolver::results_type endpoints){
        endpoints_ = endpoints;
        do_connect(endpoints_.begin());
    }

private:
    void do_connect(tcp::resolver::results_type::iterator endpoint_iter){
        if(endpoint_iter != endpoints_.end()){
            auto self(shared_from_this());
            socket_.async_connect(endpoint_iter->endpoint(), [this, self](boost::system::error_code ec){
                if(!ec){
                    do_read();
                }
                else{
                    cout << "<script>console.log(\"" << ec.message() << "\")</script>" << flush;
                }
            });
        }
        else{

        }
    }

    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length-1),
            [this, self](boost::system::error_code ec, size_t length){
                string data = data_;
                memset(data_, '\0', sizeof(data_));
                if(!ec){
                    outputShell(data);
                    if(data.find("% ") != string::npos){
                        do_write();
                    }else{
                        do_read();
                    }
                }
                else if(ec.value() == 2){
                    socket_.close();
                }
                else{
                    cout << "<script>console.log(\"" << ec.message() << "\")</script>" << flush;
                }
            });
    }

    void do_write(){
        auto self(shared_from_this());
        getline(file_stream_, cmd_);
        string cmd = cmd_;
        cmd_.clear();
        cmd += "\n";
        outputCmd(cmd);
        boost::asio::async_write(socket_, boost::asio::buffer(cmd, cmd.length()),
            [this, self](boost::system::error_code ec, size_t /*length*/){
                if(!ec){
                    do_read();
                }
            });
    }

    string replaceHtmlSpecialChar(string msg){
        boost::replace_all(msg, "\r", "");
        boost::replace_all(msg, "\n", "&NewLine;");
        boost::replace_all(msg, "\'", "&apos;");
        boost::replace_all(msg, "\"", "&quot;");
        boost::replace_all(msg, "<", "&lt;");
        boost::replace_all(msg, ">", "&gt;");

        return msg;
    }

    void outputShell(string msg){
        cout << "<script>document.getElementById('s" << server_num_ << "').innerHTML += '" << replaceHtmlSpecialChar(msg) << "';</script>" << flush;
    }

    void outputCmd(string msg){
        cout << "<script>document.getElementById('s" << server_num_ << "').innerHTML += '<b>" << replaceHtmlSpecialChar(msg) << "</b>';</script>" << flush;
    }

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    ifstream file_stream_;
    int server_num_;
    tcp::resolver::results_type endpoints_;
    enum {max_length = 1024};
    char data_[max_length];
    string cmd_;
};
