#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <json/json.h>

#include <libethcore/Farm.h>
#include <libethcore/Miner.h>
#include <libpoolprotocols/PoolManager.h>

namespace dev
{

/**
 * @brief Handles API connections for mining operations
 */
class ApiConnection : public std::enable_shared_from_this<ApiConnection>
{
public:
    using Disconnected = std::function<void(int const&)>;

    ApiConnection(
        boost::asio::io_service::strand& strand, int id, bool readonly, std::string password);

    ~ApiConnection() = default;

    void start();
    Json::Value getMinerStat1();
    void onDisconnected(Disconnected const& handler) { m_onDisconnected = handler; }
    int getId() const { return m_sessionId; }
    boost::asio::ip::tcp::socket& socket() { return m_socket; }

private:
    void disconnect();
    void processRequest(Json::Value& jRequest, Json::Value& jResponse);
    void recvSocketData();
    void onRecvSocketDataCompleted(
        const boost::system::error_code& ec, std::size_t bytes_transferred);
    void sendSocketData(Json::Value const& jReq, bool disconnect = false);
    void sendSocketData(std::string const& data, bool disconnect = false);
    void onSendSocketDataCompleted(const boost::system::error_code& ec, bool disconnect = false);

    // Process different request types
    void processHttpRequest(const std::smatch& http_matches);
    void processJsonRpcRequest();
    std::string buildHttpResponse(const std::string& http_ver, const std::string& status,
        const std::string& body, const std::string& content_type = "text/plain");

    // Statistical data retrieval
    Json::Value getMinerStatDetail();
    Json::Value getMinerStatDetailPerMiner(
        const eth::TelemetryType& telemetry, std::shared_ptr<eth::Miner> miner);
    std::string getHttpMinerStatDetail();

    // Member variables
    Disconnected m_onDisconnected;
    const int m_sessionId;
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::io_service::strand& m_io_strand;
    boost::asio::streambuf m_sendBuffer;
    boost::asio::streambuf m_recvBuffer;
    Json::StreamWriterBuilder m_jSwBuilder;
    std::string m_message;  // The internal message string buffer
    const bool m_readonly;
    const std::string m_password;
    bool m_is_authenticated{true};
};

/**
 * @brief Server that manages API connections
 */
class ApiServer
{
public:
    ApiServer(std::string address, int portnum, std::string password);
    ~ApiServer() { stop(); }

    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }
    void start();
    void stop();

private:
    void begin_accept();
    void handle_accept(std::shared_ptr<ApiConnection> session, boost::system::error_code ec);

    int m_lastSessionId{0};
    std::thread m_workThread;
    std::atomic<bool> m_readonly{false};
    const std::string m_password;
    std::atomic<bool> m_running{false};
    const std::string m_address;
    const uint16_t m_portnumber;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::io_service::strand m_io_strand;
    std::vector<std::shared_ptr<ApiConnection>> m_sessions;
};

}  // namespace dev
