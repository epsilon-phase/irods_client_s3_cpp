#include "./s3_api.hpp"
#include "../connection.hpp"

#include "../authentication.hpp"
#include "../bucket.hpp"

#include <boost/beast/core/error.hpp>

#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/read.hpp>
#include <irods/dstream.hpp>
#include <irods/transport/default_transport.hpp>

#include <iostream>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace fs = irods::experimental::filesystem;

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"
asio::awaitable<void> irods::s3::actions::handle_putobject(
    asio::ip::tcp::socket& socket,
    static_buffer_request_parser& parser,
    const boost::urls::url_view& url)
{
    boost::beast::error_code ec;
    connection_handle connection = get_connection();
    std::cout << "Hi! About to auth" << std::endl;
    if (!irods::s3::authentication::authenticates(*connection, parser, url)) {
        beast::http::response<boost::beast::http::empty_body> response;
        response.result(boost::beast::http::status::forbidden);
        beast::http::write(socket, response);
        beast::http::write(socket, response);
        std::cout << "Failed to auth" << std::endl;
        co_return;
    }

    if (parser.get()[beast::http::field::expect] == "100-continue") {
        beast::http::response<beast::http::empty_body> resp;
        resp.version(11);
        resp.result(beast::http::status::continue_);
        resp.set(beast::http::field::server, parser.get()["Host"]);
        beast::http::write(socket, resp, ec);
        std::cout << "Sent 100-continue" << std::endl;
    }

    fs::path path;
    if (auto bucket = irods::s3::resolve_bucket(*connection, url.segments()); bucket.has_value()) {
        path = std::move(bucket.value());
        path = irods::s3::finish_path(path, url.segments());
    }
    else {
        beast::http::response<beast::http::empty_body> response;
        response.result(beast::http::status::not_found);
        std::cerr << "No bucket found" << std::endl;
        beast::http::write(socket, response);
        co_return;
    }
    std::cout << "Path: [" << path << "]" << std::endl;
    {
        beast::flat_buffer buffer;
        beast::http::response<beast::http::empty_body> response;
        response.result(beast::http::status::ok);

        irods::experimental::io::client::default_transport xtrans{*connection};

        fs::client::create_collections(*connection, path.parent_path());
        irods::experimental::io::odstream d{
            xtrans, path, irods::experimental::io::root_resource_name{irods::s3::get_resource()}, std::ios_base::out};

        char buf[4096];
        parser.get().body().data = buf;
        parser.get().body().size = sizeof(buf);

        response.set("Etag", path.c_str());
        response.set("Connection", "close");

        while (!parser.is_done() || !parser.get().body().more) {
            parser.get().body().data = buf;
            parser.get().body().size = sizeof(buf);
            //            auto read = co_await beast::http::async_read_some(socket, buffer, parser,
            //            asio::use_awaitable);
            auto read = beast::http::read_some(socket, buffer, parser);
            size_t read_bytes = sizeof(buf) - parser.get().body().size;

            std::cout << "Read " << read_bytes << std::endl;
            //            std::cout.write((char*) buf, read_bytes);
            try {
                d.write((char*) buf, read_bytes);
            }
            catch (std::exception& e) {
                std::cout << e.what() << std::endl;
            }

            if (ec == beast::http::error::need_buffer) {
                ec = {};
            }
            else if (ec) {
                std::cout << ec.what() << std::endl;
                co_return;
            }
        }

        beast::http::write(socket, response, ec);
        if (ec) {
            std::cout << "Error! " << ec.what() << std::endl;
        }
        std::cout << "wrote response" << std::endl;
    }
    co_return;
}
#pragma clang diagnostic pop