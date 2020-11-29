#include <iostream>
#include <string>
#include <vector>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
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
    client(boost::asio::io_context& c_io_context, tcp::socket& panel_socket, query query_info, int server_num)
        : c_io_context_(c_io_context),
          panel_socket_(panel_socket),
          socket_(c_io_context),
          file_stream_("./test_case/" + query_info.file),
          server_num_(server_num){
    }

    // ~client(){
    //     file_stream_.close();
    // }

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
                    cerr << "[ERROR]\t" << ec.message() << endl;
                }
            });
        }
    }

    void do_read(){
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(data_), "% ",
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    outputShell(data_);
                    data_.clear();
                    do_write();
                }
                else if(ec.value() == 2){
                    socket_.close();
                }
                else{
                    cerr << "[ERROR]\t" << ec.message() << endl;
                }
            });
    }

    void do_write(){
        auto self(shared_from_this());
        getline(file_stream_, cmd_);
        string cmd = cmd_;
        cmd_.clear();
        outputCmd(cmd + "\n");
        usleep(200000);
        boost::asio::async_write(socket_, boost::asio::buffer(cmd, cmd.length()),
            [this, self](boost::system::error_code ec, size_t /*length*/){
                if(!ec){
                    do_read();
                }
            });
    }

    void panel_write(string resp){
        boost::asio::async_write(panel_socket_, boost::asio::buffer(resp, resp.length()),
            [this](boost::system::error_code ec, size_t /*length*/){
                if(!ec){
                    // No err
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
        panel_write("<script>document.getElementById('s" + to_string(server_num_) + "').innerHTML += '" + replaceHtmlSpecialChar(msg) + "';</script>");
    }

    void outputCmd(string msg){
        panel_write("<script>document.getElementById('s" + to_string(server_num_) + "').innerHTML += '<b>" + replaceHtmlSpecialChar(msg) + "</b>';</script>");
    }

    boost::asio::io_context& c_io_context_;
    tcp::socket& panel_socket_;
    tcp::socket socket_;
    ifstream file_stream_;
    int server_num_;
    tcp::resolver::results_type endpoints_;
    string data_;
    string cmd_;
};


vector<query> parseQueryString(string q_str_from_env){
    vector<string> token;
    vector<query> query_string;

    boost::split(token, q_str_from_env, boost::is_any_of("&"), boost::token_compress_on);

    for(unsigned int i = 0; i < token.size(); i+=3){
        query q_tmp;
        if(token.at(i).find("=")+1 != token.at(i).size()){
            q_tmp.host = token.at(i).substr(token.at(i).find("=")+1);
            q_tmp.port = token.at(i+1).substr(token.at(i+1).find("=")+1);
            q_tmp.file = token.at(i+2).substr(token.at(i+2).find("=")+1);

            query_string.push_back(q_tmp);
        }
    }

    return query_string;
}

vector<string> getServers(vector<query> query_string){
    vector<string> servers;
    for(unsigned int i = 0; i < query_string.size(); i++){
        string server_tmp = "";
        server_tmp += query_string.at(i).host;
        server_tmp += ":";
        server_tmp += query_string.at(i).port;

        servers.push_back(server_tmp);
    }

    return servers;
}

string generateConsoleHtml(vector<string> servers){
    string html = \
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "  <head>\n"
    "    <meta charset=\"UTF-8\" />\n"
    "    <title>NP Project 3 Sample Console</title>\n"
    "    <link\n"
    "      rel=\"stylesheet\"\n"
    "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
    "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
    "      crossorigin=\"anonymous\"\n"
    "    />\n"
    "    <link\n"
    "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
    "      rel=\"stylesheet\"\n"
    "    />\n"
    "    <link\n"
    "      rel=\"icon\"\n"
    "      type=\"image/png\"\n"
    "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
    "    />\n"
    "    <style>\n"
    "      * {\n"
    "        font-family: 'Source Code Pro', monospace;\n"
    "        font-size: 1rem !important;\n"
    "      }\n"
    "      body {\n"
    "        background-color: #212529;\n"
    "      }\n"
    "      pre {\n"
    "        color: #cccccc;\n"
    "      }\n"
    "      b {\n"
    "        color: #01b468;\n"
    "      }\n"
    "    </style>\n"
    "  </head>\n"
    "  <body>\n"
    "    <table class=\"table table-dark table-bordered\">\n"
    "      <thead>\n"
    "        <tr>\n";
    for(unsigned int i = 0; i < servers.size(); i++){
        html += "          <th scope=\"col\">";
        html += servers.at(i);
        html += "</th>\n";
    }
    html += \
    "        </tr>\n"
    "      </thead>\n"
    "      <tbody>\n"
    "        <tr>\n";
    for(unsigned int i = 0; i < servers.size(); i++){
        html += "          <td><pre id=\"s";
        html += to_string(i);
        html += "\" class=\"mb-0\"></pre></td>";
    }
    html += \
    "        </tr>\n"
    "      </tbody>\n"
    "    </table>\n"
    "  </body>\n"
    "</html>\n";

    return html;
}

string hostMenu(){
    string host_menu;
    for(int i = 1; i <= 12; i++){
        host_menu += "                                    <option value=\"nplinux" + to_string(i) + ".cs.nctu.edu.tw\">nplinux" + to_string(i) + "</option>\n";
    }
    return host_menu;
}

string testCaseMenu(){
    string test_case_menu;
    for(int i = 1; i <= 10; i++){
        test_case_menu += "                                <option value=\"t" + to_string(i) + ".txt\">t" + to_string(i) + ".txt</option>";
    }
    return test_case_menu;
}

string generatePanelHtml(){
    string html = \
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">"
    "    <head>\n"
    "        <title>NP Project 3 Panel</title>\n"
    "        <link\n"
    "            rel=\"stylesheet\"\n"
    "            href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
    "            integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
    "            crossorigin=\"anonymous\"\n"
    "        />\n"
    "        <link\n"
    "            href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
    "            rel=\"stylesheet\"\n"
    "        />\n"
    "        <link\n"
    "            rel=\"icon\"\n"
    "            type=\"image/png\"\n"
    "            href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\"\n"
    "        />\n"
    "        <style>\n"
    "            * {\n"
    "                font-family: 'Source Code Pro', monospace;\n"
    "            }\n"
    "        </style>\n"
    "    </head>\n"
    "    <body class=\"bg-secondary pt-5\">\n"
    "        <form action=\"console.cgi\" method=\"GET\">\n"
    "        <table class=\"table mx-auto bg-light\" style=\"width: inherit\">\n"
    "            <thead class=\"thead-dark\">\n"
    "                <tr>\n"
    "                    <th scope=\"col\">#</th>\n"
    "                    <th scope=\"col\">Host</th>\n"
    "                    <th scope=\"col\">Port</th>\n"
    "                    <th scope=\"col\">Input File</th>\n"
    "                </tr>\n"
    "            </thead>\n"
    "            <tbody>\n";
    for(unsigned int i = 0; i < 5; i++){
        html += "                    <tr>\n";
        html += "                        <th scope=\"row\" class=\"align-middle\">Session " + to_string(i+1) + "</th>\n";
        html += "                        <td>\n";
        html += "                            <div class=\"input-group\">\n";
        html += "                                <select name=\"h" + to_string(i) + "\" class=\"custom-select\">\n";
        html += "                                    <option></option>\n";
        html += hostMenu();
        html += "                                </select>\n";
        html += "                                <div class=\"input-group-append\">\n";
        html += "                                    <span class=\"input-group-text\">.cs.nctu.edu.tw</span>\n";
        html += "                                </div>\n";
        html += "                            </div>\n";
        html += "                        </td>\n";
        html += "                        <td>\n";
        html += "                            <input name=\"p" + to_string(i) + "\" type=\"text\" class=\"form-control\" size=\"5\" />\n";
        html += "                        </td>\n";
        html += "                        <td>\n";
        html += "                            <select name=\"f" + to_string(i) + "\" class=\"custom-select\">\n";
        html += "                                <option></option>\n";
        html += testCaseMenu();
        html += "                            </select>\n";
        html += "                        </td>\n";
        html += "                    </tr>\n";
    }
    html += \
    "                    <tr>\n"
    "                        <td colspan=\"3\"></td>\n"
    "                        <td>\n"
    "                            <button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>\n"
    "                        </td>\n"
    "                    </tr>\n"
    "                </tbody>\n"
    "            </table>\n"
    "        </form>\n"
    "    </body>\n"
    "</html>\n";

    return html;
}
