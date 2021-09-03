#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include "console.hpp"

using boost::asio::ip::tcp;
using namespace std;

vector<query> parseQueryString(string& socks_host, string& socks_port){
    vector<string> token;
    vector<query> query_string;
    string q_str_from_env = getenv("QUERY_STRING");

    boost::split(token, q_str_from_env, boost::is_any_of("&"), boost::token_compress_on);

    for(uint i = 0; i < token.size()-2; i+=3){
        query q_tmp;
        if(token.at(i).find("=")+1 != token.at(i).size()){
            q_tmp.host = token.at(i).substr(token.at(i).find("=")+1);
            q_tmp.port = token.at(i+1).substr(token.at(i+1).find("=")+1);
            q_tmp.file = token.at(i+2).substr(token.at(i+2).find("=")+1);

            query_string.push_back(q_tmp);
        }
    }
    socks_host = token.at(token.size()-2).substr(token.at(token.size()-2).find("=")+1);
    socks_port = token.at(token.size()-1).substr(token.at(token.size()-1).find("=")+1);

    return query_string;
}

vector<string> getServers(vector<query> query_string){
    vector<string> servers;
    for(uint i = 0; i < query_string.size(); i++){
        string server_tmp = "";
        server_tmp += query_string.at(i).host;
        server_tmp += ":";
        server_tmp += query_string.at(i).port;

        servers.push_back(server_tmp);
    }

    return servers;
}

string generateHtml(vector<string> servers){
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
    for(uint i = 0; i < servers.size(); i++){
        html += "          <th scope=\"col\">";
        html += servers.at(i);
        html += "</th>\n";
    }
    html += \
    "        </tr>\n"
    "      </thead>\n"
    "      <tbody>\n"
    "        <tr>\n";
    for(uint i = 0; i < servers.size(); i++){
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

void createClients(vector<query> query_string, string& socks_host, string& socks_port){
    boost::asio::io_context io_context;
    tcp::resolver r_socks(io_context);
    for(uint i = 0; i < query_string.size(); i++){
        tcp::resolver r_dst(io_context);
        tcp::resolver::query q_dst(query_string.at(i).host, query_string.at(i).port);
        tcp::endpoint endpoint = r_dst.resolve(q_dst)->endpoint();
        make_shared<client>(io_context, query_string.at(i), i, endpoint)->start(r_socks.resolve(socks_host, socks_port));
        // make_shared<client>(io_context, query_string.at(i), i, endpoint)->start(r_socks.resolve(query_string.at(i).host, query_string.at(i).port));
    }
    io_context.run();
}

int main(){
    try{
        string socks_host;
        string socks_port;
        vector<query> query_string = parseQueryString(socks_host, socks_port);
        vector<string> servers = getServers(query_string);
        string html = generateHtml(servers);

        cout << "Content-type: text/html\r\n\r\n" << flush;
        cout << html << flush;

        createClients(query_string, socks_host, socks_port);
    }
    catch(exception& e){
        cerr << "[Error]\thttp_server: Exception: " << e.what() << "\n";
    }

    return 0;
}