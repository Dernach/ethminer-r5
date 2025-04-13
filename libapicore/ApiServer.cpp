#include "ApiServer.h"

#include <ethminer/buildinfo.h>
#include <libethcore/Farm.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

// Define color constants for HTML output
namespace
{
constexpr const char* HTTP_HDR0_COLOR = "#e8e8e8";
constexpr const char* HTTP_HDR1_COLOR = "#f0f0f0";
constexpr const char* HTTP_ROW0_COLOR = "#f8f8f8";
constexpr const char* HTTP_ROW1_COLOR = "#ffffff";
constexpr const char* HTTP_ROWRED_COLOR = "#f46542";

// Error codes
constexpr int ERROR_INVALID_REQUEST = -32600;
constexpr int ERROR_METHOD_NOT_FOUND = -32601;
constexpr int ERROR_INVALID_PARAMS = -32602;
constexpr int ERROR_UNAUTHORIZED = -401;
constexpr int ERROR_FORBIDDEN = -403;
constexpr int ERROR_INTERNAL = -500;
constexpr int ERROR_PARSING = -32700;

/**
 * @brief Adds an error to the JSON response
 * @param jResponse JSON response object to be modified
 * @param code Error code
 * @param message Error message
 */
void setJsonError(Json::Value& jResponse, int code, const std::string& message)
{
    jResponse["error"]["code"] = code;
    jResponse["error"]["message"] = message;
}

/**
 * @brief Helper template function for getting values from a JSON request
 * @param membername Name of the member to extract
 * @param refValue Reference to store the extracted value
 * @param jRequest JSON request object
 * @param optional Whether the parameter is optional
 * @param jResponse JSON response object to populate in case of error
 * @return True if successful, false otherwise
 */

template <typename T>
bool getRequestValueImpl(
    const char* membername, T& refValue, Json::Value& jRequest, Json::Value& jResponse);

template <typename T>
bool getRequestValue(const char* membername, T& refValue, Json::Value& jRequest, bool optional,
    Json::Value& jResponse)
{
    // Check if the member exists
    if (!jRequest.isMember(membername))
    {
        if (!optional)
        {
            setJsonError(
                jResponse, ERROR_INVALID_PARAMS, std::string("Missing '") + membername + "'");
        }
        return optional;
    }

    // Check for empty values
    if (jRequest[membername].empty())
    {
        setJsonError(jResponse, ERROR_INVALID_PARAMS, std::string("Empty '") + membername + "'");
        return false;
    }

    try
    {
        // Type-specific handling without using if constexpr
        return getRequestValueImpl(membername, refValue, jRequest, jResponse);
    }
    catch (const std::exception&)
    {
        setJsonError(
            jResponse, ERROR_INVALID_PARAMS, std::string("Bad value in '") + membername + "'");
        return false;
    }
}

// Spécialisations pour chaque type
template <>
bool getRequestValueImpl(
    const char* membername, bool& refValue, Json::Value& jRequest, Json::Value& jResponse)
{
    if (!jRequest[membername].isBool())
    {
        setJsonError(jResponse, ERROR_INVALID_PARAMS,
            std::string("Invalid type of value '") + membername + "'");
        return false;
    }
    refValue = jRequest[membername].asBool();
    return true;
}

template <>
bool getRequestValueImpl(
    const char* membername, unsigned& refValue, Json::Value& jRequest, Json::Value& jResponse)
{
    if (!jRequest[membername].isUInt())
    {
        setJsonError(jResponse, ERROR_INVALID_PARAMS,
            std::string("Invalid type of value '") + membername + "'");
        return false;
    }
    refValue = jRequest[membername].asUInt();
    return true;
}

template <>
bool getRequestValueImpl(
    const char* membername, uint64_t& refValue, Json::Value& jRequest, Json::Value& jResponse)
{
    refValue = jRequest[membername].asUInt64();
    return true;
}

template <>
bool getRequestValueImpl(
    const char* membername, std::string& refValue, Json::Value& jRequest, Json::Value& jResponse)
{
    if (!jRequest[membername].isString())
    {
        setJsonError(jResponse, ERROR_INVALID_PARAMS,
            std::string("Invalid type of value '") + membername + "'");
        return false;
    }
    refValue = jRequest[membername].asString();
    return true;
}

template <>
bool getRequestValueImpl(
    const char* membername, Json::Value& refValue, Json::Value& jRequest, Json::Value& jResponse)
{
    if (!jRequest[membername].isObject())
    {
        setJsonError(jResponse, ERROR_INVALID_PARAMS,
            std::string("Invalid type of value '") + membername + "'");
        return false;
    }
    refValue = jRequest[membername];
    return true;
}

// Implantation par défaut qui sera utilisée pour les autres types
template <typename T>
bool getRequestValueImpl(
    const char* membername, T& refValue, Json::Value& jRequest, Json::Value& jResponse)
{
    setJsonError(
        jResponse, ERROR_INVALID_PARAMS, std::string("Unsupported type for '") + membername + "'");
    return false;
}

/**
 * @brief Validates API write access
 * @param is_read_only Whether the API is in read-only mode
 * @param jResponse JSON response object to populate in case of error
 * @return True if write access is allowed, false otherwise
 */
bool checkApiWriteAccess(bool is_read_only, Json::Value& jResponse)
{
    if (is_read_only)
    {
        setJsonError(jResponse, ERROR_METHOD_NOT_FOUND, "Method not available");
    }
    return !is_read_only;
}

/**
 * @brief Parse and validate JSON-RPC request ID
 * @param jRequest JSON request object
 * @param jResponse JSON response object to populate
 * @return True if valid, false otherwise
 */
bool parseRequestId(Json::Value& jRequest, Json::Value& jResponse)
{
    const char* membername = "id";

    // Check if id exists and is not empty
    if (!jRequest.isMember(membername) || jRequest[membername].empty())
    {
        jResponse[membername] = Json::nullValue;
        setJsonError(jResponse, ERROR_INVALID_REQUEST, "Invalid Request (missing or empty id)");
        return false;
    }

    // Parse id as UInt if possible
    if (jRequest[membername].isUInt())
    {
        jResponse[membername] = jRequest[membername].asUInt();
        return true;
    }

    // Otherwise parse as String
    if (jRequest[membername].isString())
    {
        jResponse[membername] = jRequest[membername].asString();
        return true;
    }

    // Invalid id type
    jResponse[membername] = Json::nullValue;
    setJsonError(jResponse, ERROR_INVALID_REQUEST, "Invalid Request (id has invalid type)");
    return false;
}
}  // namespace

ApiServer::ApiServer(std::string address, int portnum, std::string password)
  : m_password(std::move(password)),
    m_address(std::move(address)),
    m_portnumber(std::abs(portnum)),
    m_readonly(portnum < 0),
    m_acceptor(g_io_service),
    m_io_strand(g_io_service)
{}

void ApiServer::start()
{
    if (m_portnumber == 0)
        return;

    try
    {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::address::from_string(m_address), m_portnumber);

        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen(64);

        cnote << "API server listening on port " +
                     std::to_string(m_acceptor.local_endpoint().port())
              << (m_password.empty() ? "." : ". Authentication needed.");

        m_running.store(true, std::memory_order_relaxed);
        m_workThread = std::thread([this]() { this->begin_accept(); });
    }
    catch (const std::exception&)
    {
        cwarn << "Could not start API server on port: " + std::to_string(m_portnumber);
        cwarn << "Ensure port is not in use by another service";
    }
}

void ApiServer::stop()
{
    // Exit if not started
    if (!m_running.load(std::memory_order_relaxed))
        return;

    m_acceptor.cancel();
    m_acceptor.close();

    if (m_workThread.joinable())
        m_workThread.join();

    m_running.store(false, std::memory_order_relaxed);

    // Clear all sessions
    m_sessions.clear();
}

void ApiServer::begin_accept()
{
    if (!isRunning())
        return;

    auto session =
        std::make_shared<ApiConnection>(m_io_strand, ++m_lastSessionId, m_readonly, m_password);

    m_acceptor.async_accept(session->socket(),
        m_io_strand.wrap(
            [this, session](boost::system::error_code ec) { this->handle_accept(session, ec); }));
}

void ApiServer::handle_accept(std::shared_ptr<ApiConnection> session, boost::system::error_code ec)
{
    if (!ec)
    {
        session->onDisconnected([this](int id) {
            // Remove session from the collection
            auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
                [id](const std::shared_ptr<ApiConnection>& s) { return s->getId() == id; });

            if (it != m_sessions.end())
                m_sessions.erase(it);
        });

        m_sessions.push_back(session);
        cnote << "New API session from " << session->socket().remote_endpoint();
        session->start();
    }

    // Accept next connection
    begin_accept();
}

ApiConnection::ApiConnection(
    boost::asio::io_service::strand& strand, int id, bool readonly, std::string password)
  : m_sessionId(id),
    m_socket(g_io_service),
    m_io_strand(strand),
    m_readonly(readonly),
    m_password(std::move(password)),
    m_is_authenticated(!m_password.empty() ? false : true)
{
    m_jSwBuilder.settings_["indentation"] = "";
}

void ApiConnection::start()
{
    recvSocketData();
}

void ApiConnection::disconnect()
{
    // Cancel pending operations
    m_socket.cancel();

    if (m_socket.is_open())
    {
        boost::system::error_code ec;
        m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }

    if (m_onDisconnected)
    {
        m_onDisconnected(m_sessionId);
    }
}

void ApiConnection::processRequest(Json::Value& jRequest, Json::Value& jResponse)
{
    jResponse["jsonrpc"] = "2.0";

    // Validate JSON-RPC request
    if (!parseRequestId(jRequest, jResponse))
        return;

    std::string jsonrpc;
    std::string method;
    if (!getRequestValue("jsonrpc", jsonrpc, jRequest, false, jResponse) || jsonrpc != "2.0" ||
        !getRequestValue("method", method, jRequest, false, jResponse))
    {
        setJsonError(jResponse, ERROR_INVALID_REQUEST, "Invalid Request");
        return;
    }

    // Handle authentication
    if (!m_is_authenticated || method == "api_authorize")
    {
        if (method != "api_authorize")
        {
            setJsonError(jResponse, ERROR_FORBIDDEN, "Authorization needed");
            return;
        }

        m_is_authenticated = false;  // Allow re-authentication

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        std::string psw;
        if (!getRequestValue("psw", psw, jRequestParams, false, jResponse))
            return;

        // Constant-time password comparison to prevent timing attacks
        constexpr int max_length = 500;
        char input_copy[max_length] = {0};
        char password_copy[max_length] = {0};

        psw.copy(input_copy, max_length);
        m_password.copy(password_copy, max_length);

        int result = 0;
        for (int i = 0; i < max_length; ++i)
        {
            result |= input_copy[i] ^ password_copy[i];
        }

        if (result == 0)
        {
            m_is_authenticated = true;
        }
        else
        {
            setJsonError(jResponse, ERROR_UNAUTHORIZED, "Invalid password");
            cerr << "API: Invalid password provided.";
        }
        return;
    }

    // Process authenticated methods
    cnote << "API: Method " << method << " requested";

    if (method == "miner_getstat1")
    {
        jResponse["result"] = getMinerStat1();
    }
    else if (method == "miner_getstatdetail")
    {
        jResponse["result"] = getMinerStatDetail();
    }
    else if (method == "miner_shuffle")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;
        jResponse["result"] = true;
        Farm::f().shuffle();
    }
    else if (method == "miner_ping")
    {
        jResponse["result"] = "pong";
    }
    else if (method == "miner_restart")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;
        jResponse["result"] = true;
        Farm::f().restart_async();
    }
    else if (method == "miner_reboot")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;
        jResponse["result"] = Farm::f().reboot({{"api_miner_reboot"}});
    }
    else if (method == "miner_getconnections")
    {
        jResponse["result"] = PoolManager::p().getConnectionsJson();
    }
    else if (method == "miner_addconnection")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        std::string uri;
        if (!getRequestValue("uri", uri, jRequestParams, false, jResponse))
            return;

        try
        {
            PoolManager::p().addConnection(uri);
            jResponse["result"] = true;
        }
        catch (...)
        {
            setJsonError(jResponse, ERROR_INVALID_PARAMS, "Bad URI: " + uri);
        }
    }
    else if (method == "miner_setactiveconnection")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        try
        {
            if (jRequestParams.isMember("index"))
            {
                unsigned index;
                if (getRequestValue("index", index, jRequestParams, false, jResponse))
                {
                    PoolManager::p().setActiveConnection(index);
                    jResponse["result"] = true;
                }
            }
            else
            {
                std::string uri;
                if (getRequestValue("URI", uri, jRequestParams, false, jResponse))
                {
                    PoolManager::p().setActiveConnection(uri);
                    jResponse["result"] = true;
                }
                else
                {
                    setJsonError(jResponse, ERROR_INVALID_PARAMS, "Invalid parameter");
                }
            }
        }
        catch (const std::exception& ex)
        {
            setJsonError(jResponse, ERROR_INVALID_PARAMS, ex.what());
        }
    }
    else if (method == "miner_removeconnection")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned index;
        if (!getRequestValue("index", index, jRequestParams, false, jResponse))
            return;

        try
        {
            PoolManager::p().removeConnection(index);
            jResponse["result"] = true;
        }
        catch (const std::exception& ex)
        {
            setJsonError(jResponse, ERROR_INVALID_PARAMS, ex.what());
        }
    }
    else if (method == "miner_getscramblerinfo")
    {
        jResponse["result"] = Farm::f().get_nonce_scrambler_json();
    }
    else if (method == "miner_setscramblerinfo")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        bool anyValueProvided = false;
        uint64_t nonce = Farm::f().get_nonce_scrambler();
        unsigned exp = Farm::f().get_segment_width();

        if (jRequestParams.isMember("noncescrambler"))
        {
            anyValueProvided = true;
            std::string nonceHex = jRequestParams["noncescrambler"].asString();

            try
            {
                if (nonceHex.substr(0, 2) == "0x")
                {
                    nonce = std::stoull(nonceHex, nullptr, 16);
                }
                else
                {
                    if (!getRequestValue("noncescrambler", nonce, jRequestParams, false, jResponse))
                        return;
                }
            }
            catch (const std::exception&)
            {
                setJsonError(jResponse, ERROR_INVALID_PARAMS, "Invalid nonce");
                return;
            }
        }

        if (jRequestParams.isMember("segmentwidth"))
        {
            anyValueProvided = true;
            if (!getRequestValue("segmentwidth", exp, jRequestParams, false, jResponse))
                return;
        }

        if (!anyValueProvided)
        {
            setJsonError(jResponse, ERROR_INVALID_PARAMS, "Missing parameters");
            return;
        }

        if (exp < 10u)
            exp = 10u;
        else if (exp > 40u)
            exp = 40u;

        Farm::f().set_nonce_scrambler(nonce);
        Farm::f().set_nonce_segment_width(exp);
        jResponse["result"] = true;
    }
    else if (method == "miner_pausegpu")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned index;
        if (!getRequestValue("index", index, jRequestParams, false, jResponse))
            return;

        bool pause;
        if (!getRequestValue("pause", pause, jRequestParams, false, jResponse))
            return;

        auto const& miner = Farm::f().getMiner(index);
        if (miner)
        {
            if (pause)
                miner->pause(MinerPauseEnum::PauseDueToAPIRequest);
            else
                miner->resume(MinerPauseEnum::PauseDueToAPIRequest);

            jResponse["result"] = true;
        }
        else
        {
            setJsonError(jResponse, ERROR_INVALID_PARAMS, "Index out of bounds");
        }
    }
    else if (method == "miner_setverbosity")
    {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned verbosity;
        if (!getRequestValue("verbosity", verbosity, jRequestParams, false, jResponse))
            return;

        if (verbosity >= LOG_NEXT)
        {
            setJsonError(jResponse, ERROR_INVALID_PARAMS,
                "Verbosity out of bounds (0-" + std::to_string(LOG_NEXT - 1) + ")");
            return;
        }

        cnote << "Setting verbosity level to " << verbosity;
        g_logOptions = verbosity;
        jResponse["result"] = true;
    }
    else
    {
        // Method not found
        setJsonError(jResponse, ERROR_METHOD_NOT_FOUND, "Method not found");
    }
}

void ApiConnection::recvSocketData()
{
    boost::asio::async_read(m_socket, m_recvBuffer, boost::asio::transfer_at_least(1),
        m_io_strand.wrap([this](boost::system::error_code ec, std::size_t bytes_transferred) {
            this->onRecvSocketDataCompleted(ec, bytes_transferred);
        }));
}

void ApiConnection::onRecvSocketDataCompleted(
    const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    // HTTP request pattern
    static std::regex http_pattern("^([A-Z]{1,6}) (\\/[\\S]*) (HTTP\\/1\\.[0-9]{1})");
    std::smatch http_matches;

    if (!ec && bytes_transferred > 0)
    {
        // Extract received message and free the buffer
        std::string rx_message(
            boost::asio::buffer_cast<const char*>(m_recvBuffer.data()), bytes_transferred);
        m_recvBuffer.consume(bytes_transferred);
        m_message.append(rx_message);

        // Wait for more data if message is too small
        if (m_message.size() < 4)
        {
            recvSocketData();
            return;
        }

        // Check if this is an HTTP request
        if (std::regex_search(m_message, http_matches, http_pattern))
        {
            processHttpRequest(http_matches);
        }
        else
        {
            // Process as JSON-RPC
            processJsonRpcRequest();
        }
    }
    else
    {
        disconnect();
    }
}

void ApiConnection::processHttpRequest(const std::smatch& http_matches)
{
    std::string http_method = http_matches[1].str();
    std::string http_path = http_matches[2].str();
    std::string http_ver = http_matches[3].str();

    // Currently only supporting GET method
    if (http_method != "GET")
    {
        std::string response = buildHttpResponse(
            http_ver, "405 Method not allowed", "Method " + http_method + " not allowed");
        sendSocketData(response, true);
        m_message.clear();
        return;
    }

    // Only supporting root and getstat1 paths
    if (http_path != "/" && http_path != "/getstat1")
    {
        std::string response = buildHttpResponse(http_ver, "404 Not Found",
            "The requested resource " + http_path + " not found on this server");
        sendSocketData(response, true);
        m_message.clear();
        return;
    }

    // Process GET request for stats
    try
    {
        std::string body = getHttpMinerStatDetail();
        std::string response =
            buildHttpResponse(http_ver, "200 Ok", body, "text/html; charset=utf-8");
        sendSocketData(response, true);
    }
    catch (const std::exception& ex)
    {
        std::string response = buildHttpResponse(
            http_ver, "500 Internal Server Error", "Internal error: " + std::string(ex.what()));
        sendSocketData(response, true);
    }

    m_message.clear();
}

std::string ApiConnection::buildHttpResponse(const std::string& http_ver, const std::string& status,
    const std::string& body, const std::string& content_type)
{
    std::stringstream ss;
    ss << http_ver << " " << status << "\r\n"
       << "Server: " << ethminer_get_buildinfo()->project_name_with_version << "\r\n"
       << "Content-Type: " << (content_type.empty() ? "text/plain" : content_type) << "\r\n"
       << "Content-Length: " << body.size() << "\r\n\r\n"
       << body << "\r\n";
    return ss.str();
}

void ApiConnection::processJsonRpcRequest()
{
    // Process each line in the transmission
    std::string linedelimiter = "\n";
    std::size_t linedelimiteroffset = m_message.find(linedelimiter);

    while (linedelimiteroffset != std::string::npos)
    {
        if (linedelimiteroffset > 0)
        {
            std::string line = m_message.substr(0, linedelimiteroffset);
            boost::trim(line);

            if (!line.empty())
            {
                // Parse and process JSON-RPC request
                Json::Value jMsg;
                Json::Value jRes;
                Json::Reader jRdr;

                if (jRdr.parse(line, jMsg))
                {
                    try
                    {
                        processRequest(jMsg, jRes);
                    }
                    catch (const std::exception& ex)
                    {
                        jRes = Json::Value();
                        jRes["jsonrpc"] = "2.0";
                        jRes["id"] = Json::Value::null;
                        setJsonError(jRes, ERROR_INTERNAL, ex.what());
                    }
                }
                else
                {
                    jRes = Json::Value();
                    jRes["jsonrpc"] = "2.0";
                    jRes["id"] = Json::Value::null;

                    std::string error = jRdr.getFormattedErrorMessages();
                    boost::replace_all(error, "\n", " ");
                    cwarn << "API: Got invalid JSON message " << error;

                    setJsonError(jRes, ERROR_PARSING, "JSON parse error: " + error);
                }

                // Send response
                sendSocketData(jRes);
            }
        }

        // Process next line
        m_message.erase(0, linedelimiteroffset + 1);
        linedelimiteroffset = m_message.find(linedelimiter);
    }

    // Continue reading from socket if still open
    if (m_socket.is_open())
        recvSocketData();
}

void ApiConnection::sendSocketData(Json::Value const& jReq, bool disconnect)
{
    if (!m_socket.is_open())
        return;

    std::stringstream ss;
    ss << Json::writeString(m_jSwBuilder, jReq) << std::endl;
    sendSocketData(ss.str(), disconnect);
}

void ApiConnection::sendSocketData(std::string const& data, bool disconnect)
{
    if (!m_socket.is_open())
        return;

    std::ostream os(&m_sendBuffer);
    os << data;

    boost::asio::async_write(m_socket, m_sendBuffer,
        m_io_strand.wrap([this, disconnect](boost::system::error_code ec, std::size_t) {
            this->onSendSocketDataCompleted(ec, disconnect);
        }));
}

void ApiConnection::onSendSocketDataCompleted(const boost::system::error_code& ec, bool disconnect)
{
    if (ec || disconnect)
        this->disconnect();
}

Json::Value ApiConnection::getMinerStat1()
{
    auto connection = PoolManager::p().getActiveConnection();
    TelemetryType t = Farm::f().Telemetry();
    auto runningTime = std::chrono::duration_cast<std::chrono::minutes>(
        std::chrono::steady_clock::now() - t.start);

    std::stringstream totalMhEth;
    std::stringstream totalMhDcr;
    std::stringstream detailedMhEth;
    std::stringstream detailedMhDcr;
    std::stringstream tempAndFans;
    std::stringstream poolAddresses;
    std::stringstream invalidStats;

    // Format outputs with required precision
    totalMhEth << std::fixed << std::setprecision(0) << t.farm.hashrate / 1000.0f << ";"
               << t.farm.solutions.accepted << ";" << t.farm.solutions.rejected;

    totalMhDcr << "0;0;0";                            // DualMining not supported
    invalidStats << t.farm.solutions.failed << ";0";  // Invalid + Pool switches
    poolAddresses << connection->Host() << ':' << connection->Port();
    invalidStats << ";0;0";  // DualMining not supported

    int numGpus = static_cast<int>(t.miners.size());

    // Generate per-GPU stats
    for (int gpuIndex = 0; gpuIndex < numGpus; gpuIndex++)
    {
        detailedMhEth << std::fixed << std::setprecision(0)
                      << t.miners.at(gpuIndex).hashrate / 1000.0f
                      << (gpuIndex < numGpus - 1 ? ";" : "");

        detailedMhDcr << "off" << (gpuIndex < numGpus - 1 ? ";" : "");
    }

    // Generate temperature and fan data
    for (int gpuIndex = 0; gpuIndex < numGpus; gpuIndex++)
    {
        tempAndFans << t.miners.at(gpuIndex).sensors.tempC << ";"
                    << t.miners.at(gpuIndex).sensors.fanP << (gpuIndex < numGpus - 1 ? ";" : "");
    }

    Json::Value jRes;
    jRes[0] = ethminer_get_buildinfo()->project_name_with_version;  // miner version
    jRes[1] = std::to_string(runningTime.count());                  // running time in minutes
    jRes[2] = totalMhEth.str();     // total ETH hashrate, accepted shares, rejected shares
    jRes[3] = detailedMhEth.str();  // detailed ETH hashrate for all GPUs
    jRes[4] = totalMhDcr.str();     // total DCR hashrate (not supported)
    jRes[5] = detailedMhDcr.str();  // detailed DCR hashrate (not supported)
    jRes[6] = tempAndFans.str();    // Temperature and Fan speed(%) pairs for all GPUs
    jRes[7] = poolAddresses.str();  // current mining pool
    jRes[8] = invalidStats.str();   // invalid shares stats

    return jRes;
}

Json::Value ApiConnection::getMinerStatDetailPerMiner(
    const TelemetryType& telemetry, std::shared_ptr<Miner> miner)
{
    unsigned index = miner->Index();
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    Json::Value jRes;
    DeviceDescriptor minerDescriptor = miner->getDescriptor();

    jRes["_index"] = index;
    jRes["_mode"] =
        (minerDescriptor.subscriptionType == DeviceSubscriptionTypeEnum::Cuda ? "CUDA" : "OpenCL");

    // Hardware Info section
    Json::Value hwinfo;
    hwinfo["pci"] = minerDescriptor.uniqueId;
    hwinfo["type"] = [&]() {
        switch (minerDescriptor.type)
        {
        case DeviceTypeEnum::Gpu:
            return "GPU";
        case DeviceTypeEnum::Accelerator:
            return "ACCELERATOR";
        default:
            return "CPU";
        }
    }();

    std::ostringstream ss;
    ss << (minerDescriptor.clDetected ? minerDescriptor.clName : minerDescriptor.cuName) << " "
       << dev::getFormattedMemory((double)minerDescriptor.totalMemory);
    hwinfo["name"] = ss.str();

    // Hardware Sensors section
    Json::Value sensors = Json::Value(Json::arrayValue);
    sensors.append(telemetry.miners.at(index).sensors.tempC);
    sensors.append(telemetry.miners.at(index).sensors.fanP);
    sensors.append(telemetry.miners.at(index).sensors.powerW);
    hwinfo["sensors"] = sensors;

    // Mining Info section
    Json::Value mininginfo;

    // Shares data
    Json::Value jshares = Json::Value(Json::arrayValue);
    jshares.append(telemetry.miners.at(index).solutions.accepted);
    jshares.append(telemetry.miners.at(index).solutions.rejected);
    jshares.append(telemetry.miners.at(index).solutions.failed);

    auto solution_lastupdated = std::chrono::duration_cast<std::chrono::seconds>(
        now - telemetry.miners.at(index).solutions.tstamp);
    jshares.append(static_cast<uint64_t>(solution_lastupdated.count()));

    mininginfo["shares"] = jshares;
    mininginfo["paused"] = miner->paused();
    mininginfo["pause_reason"] = miner->paused() ? miner->pausedString() : Json::Value::null;

    // Nonce segment info
    Json::Value jsegment = Json::Value(Json::arrayValue);
    auto segment_width = Farm::f().get_segment_width();
    uint64_t gpustartnonce =
        Farm::f().get_nonce_scrambler() + (static_cast<uint64_t>(index) << segment_width);

    jsegment.append(toHex(gpustartnonce, HexPrefix::Add));
    jsegment.append(
        toHex(static_cast<uint64_t>(gpustartnonce + (1ULL << segment_width)), HexPrefix::Add));
    mininginfo["segment"] = jsegment;

    // Hashrate info
    mininginfo["hashrate"] =
        toHex(static_cast<uint32_t>(telemetry.miners.at(index).hashrate), HexPrefix::Add);

    // Combine all sections
    jRes["hardware"] = hwinfo;
    jRes["mining"] = mininginfo;

    return jRes;
}

std::string ApiConnection::getHttpMinerStatDetail()
{
    Json::Value jStat = getMinerStatDetail();
    uint64_t durationSeconds = jStat["host"]["runtime"].asUInt64();

    int hours = static_cast<int>(durationSeconds / 3600);
    durationSeconds -= (hours * 3600);
    int minutes = static_cast<int>(durationSeconds / 60);
    int hoursSize = (hours > 9 ? (hours > 99 ? 3 : 2) : 1);

    // Build HTML response
    std::stringstream html;
    html << "<!doctype html>"
         << "<html lang=en>"
         << "<head>"
         << "<meta charset=utf-8>"
         << "<meta http-equiv=\"refresh\" content=\"30\">"
         << "<title>" << jStat["host"]["name"].asString() << "</title>"
         << "<style>"
         << "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,"
         << "\"Helvetica Neue\",Helvetica,Arial,sans-serif;font-size:16px;line-height:1.5;"
         << "text-align:center;}"
         << "table,td,th{border:1px inset #000;}"
         << "table{border-spacing:0;}"
         << "td,th{padding:3px;}"
         << "tbody tr:nth-child(even){background-color:" << HTTP_ROW0_COLOR << ";}"
         << "tbody tr:nth-child(odd){background-color:" << HTTP_ROW1_COLOR << ";}"
         << ".mx-auto{margin-left:auto;margin-right:auto;}"
         << ".bg-header1{background-color:" << HTTP_HDR1_COLOR << ";}"
         << ".bg-header0{background-color:" << HTTP_HDR0_COLOR << ";}"
         << ".bg-red{color:" << HTTP_ROWRED_COLOR << ";}"
         << ".right{text-align: right;}"
         << "</style>"
         << "</head>"
         << "<body>"
         << "<table class=mx-auto>"
         << "<thead>"
         << "<tr class=bg-header1>"
         << "<th colspan=9>" << jStat["host"]["version"].asString() << " - " << std::setw(hoursSize)
         << hours << ":" << std::setw(2) << std::setfill('0') << std::fixed << minutes
         << "<br>Pool: " << jStat["connection"]["uri"].asString() << "</th>"
         << "</tr>"
         << "<tr class=bg-header0>"
         << "<th>PCI</th>"
         << "<th>Device</th>"
         << "<th>Mode</th>"
         << "<th>Paused</th>"
         << "<th class=right>Hash Rate</th>"
         << "<th class=right>Solutions</th>"
         << "<th class=right>Temp.</th>"
         << "<th class=right>Fan %</th>"
         << "<th class=right>Power</th>"
         << "</tr>"
         << "</thead><tbody>";

    // Add miner data rows
    double total_hashrate = 0;
    double total_power = 0;
    unsigned int total_solutions = 0;

    for (Json::Value::ArrayIndex i = 0; i != jStat["devices"].size(); i++)
    {
        Json::Value device = jStat["devices"][i];
        double hashrate = std::stoul(device["mining"]["hashrate"].asString(), nullptr, 16);
        double power = device["hardware"]["sensors"][2].asDouble();
        unsigned int solutions = device["mining"]["shares"][0].asUInt();

        total_hashrate += hashrate;
        total_power += power;
        total_solutions += solutions;

        // Add row with optional red color for paused miners
        html << "<tr" << (device["mining"]["paused"].asBool() ? " class=\"bg-red\"" : "") << ">";

        // Device info
        html << "<td>" << device["hardware"]["pci"].asString() << "</td>"
             << "<td>" << device["hardware"]["name"].asString() << "</td>"
             << "<td>" << device["_mode"].asString() << "</td>"
             << "<td>"
             << (device["mining"]["paused"].asBool() ? device["mining"]["pause_reason"].asString() :
                                                       "No")
             << "</td>";

        // Performance data
        html << "<td class=right>" << dev::getFormattedHashes(hashrate) << "</td>";

        // Solutions data
        std::string solString = "A" + device["mining"]["shares"][0].asString() + ":R" +
                                device["mining"]["shares"][1].asString() + ":F" +
                                device["mining"]["shares"][2].asString();

        html << "<td class=right>" << solString << "</td>";

        // Sensor data
        html << "<td class=right>" << device["hardware"]["sensors"][0].asString() << "</td>"
             << "<td class=right>" << device["hardware"]["sensors"][1].asString() << "</td>";

        // Power data (rounded to 2 decimals)
        std::stringstream powerStream;
        powerStream << std::fixed << std::setprecision(2) << power;
        html << "<td class=right>" << powerStream.str() << "</td>";

        html << "</tr>";
    }

    html << "</tbody>";

    // Add summary footer
    html << "<tfoot><tr class=bg-header0>"
         << "<td colspan=4 class=right>Total</td>"
         << "<td class=right>" << dev::getFormattedHashes(total_hashrate) << "</td>"
         << "<td class=right>" << total_solutions << "</td>"
         << "<td colspan=3 class=right>" << std::setprecision(2) << total_power << "</td>"
         << "</tr></tfoot>";

    html << "</table></body></html>";
    return html.str();
}

Json::Value ApiConnection::getMinerStatDetail()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    TelemetryType telemetry = Farm::f().Telemetry();

    auto runningTime = std::chrono::duration_cast<std::chrono::seconds>(now - telemetry.start);

    Json::Value jRes;
    Json::Value devices = Json::Value(Json::arrayValue);

    // Host Info section
    Json::Value hostinfo;
    hostinfo["version"] = ethminer_get_buildinfo()->project_name_with_version;
    hostinfo["runtime"] = static_cast<uint64_t>(runningTime.count());

    // Get hostname if available
    char hostName[HOST_NAME_MAX + 1]{};
    if (gethostname(hostName, HOST_NAME_MAX + 1) == 0)
        hostinfo["name"] = hostName;
    else
        hostinfo["name"] = Json::Value::null;

    // Connection info section
    Json::Value connectioninfo;
    auto connection = PoolManager::p().getActiveConnection();
    connectioninfo["uri"] = connection->str();
    connectioninfo["connected"] = PoolManager::p().isConnected();
    connectioninfo["switches"] = PoolManager::p().getConnectionSwitches();

    // Mining Info section
    Json::Value mininginfo;
    mininginfo["hashrate"] = toHex(static_cast<uint32_t>(telemetry.farm.hashrate), HexPrefix::Add);
    mininginfo["epoch"] = PoolManager::p().getCurrentEpoch();
    mininginfo["epoch_changes"] = PoolManager::p().getEpochChanges();
    mininginfo["difficulty"] = PoolManager::p().getCurrentDifficulty();

    // Share statistics
    Json::Value sharesinfo = Json::Value(Json::arrayValue);
    sharesinfo.append(telemetry.farm.solutions.accepted);
    sharesinfo.append(telemetry.farm.solutions.rejected);
    sharesinfo.append(telemetry.farm.solutions.failed);

    auto solution_lastupdated =
        std::chrono::duration_cast<std::chrono::seconds>(now - telemetry.farm.solutions.tstamp);
    sharesinfo.append(static_cast<uint64_t>(solution_lastupdated.count()));

    mininginfo["shares"] = sharesinfo;

    // Monitors Info section
    Json::Value monitorinfo;
    auto tstop = Farm::f().get_tstop();
    if (tstop)
    {
        Json::Value tempsinfo = Json::Value(Json::arrayValue);
        tempsinfo.append(Farm::f().get_tstart());
        tempsinfo.append(tstop);
        monitorinfo["temperatures"] = tempsinfo;
    }

    // Devices Info section - collect data for each miner
    for (std::shared_ptr<Miner> miner : Farm::f().getMiners())
        devices.append(getMinerStatDetailPerMiner(telemetry, miner));

    // Combine all sections into result
    jRes["devices"] = devices;
    jRes["monitors"] = monitorinfo;
    jRes["connection"] = connectioninfo;
    jRes["host"] = hostinfo;
    jRes["mining"] = mininginfo;

    return jRes;
}
