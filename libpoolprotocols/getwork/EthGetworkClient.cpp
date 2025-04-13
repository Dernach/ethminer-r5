#include "EthGetworkClient.h"

#include <chrono>
#include <functional>  // Pour std::bind et std::placeholders

#include <boost/bind/bind.hpp>  // Pour la compatibilité avec boost::bind
#include <ethash/ethash.hpp>

using namespace std;
using namespace dev;
using namespace eth;
using namespace std::placeholders;  // Pour _1, _2, etc.

using boost::asio::ip::tcp;

EthGetworkClient::EthGetworkClient(int worktimeout, unsigned farmRecheckPeriod)
  : PoolClient(),
    m_farmRecheckPeriod(farmRecheckPeriod),
    m_io_strand(g_io_service),
    m_socket(g_io_service),
    m_resolver(g_io_service),
    m_endpoints(),
    m_getwork_timer(g_io_service),
    m_worktimeout(worktimeout),
    m_solution_submitted_max_id(0)
{
    // Configure JSON writer settings
    m_jSwBuilder.settings_["indentation"] = "";

    // Prepare the getwork JSON-RPC request
    Json::Value jGetWork;
    jGetWork["id"] = unsigned(1);
    jGetWork["jsonrpc"] = "2.0";
    jGetWork["method"] = "eth_getWork";
    jGetWork["params"] = Json::Value(Json::arrayValue);
    m_jsonGetWork = Json::writeString(m_jSwBuilder, jGetWork);
}

EthGetworkClient::~EthGetworkClient()
{
    // Do not stop io service - it's global

    // Clean up any pending requests in the queue
    m_txQueue.consume_all([](std::string* str) { delete str; });
}

void EthGetworkClient::connect()
{
    // Prevent unnecessary and potentially dangerous recursion
    bool expected = false;
    if (!m_connecting.compare_exchange_strong(expected, true, std::memory_order_relaxed))
        return;

    // Reset status flags
    m_getwork_timer.cancel();

    // Initialize a new queue of endpoints
    m_endpoints = std::queue<boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>>();
    m_endpoint = boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>();

    if (m_conn->HostNameType() == dev::UriHostNameType::Dns ||
        m_conn->HostNameType() == dev::UriHostNameType::Basic)
    {
        // Begin resolve all IPs associated with hostname
        // Calling the resolver each time is useful as most
        // load balancers will give IPs in different order
        m_resolver = boost::asio::ip::tcp::resolver(g_io_service);
        boost::asio::ip::tcp::resolver::query q(m_conn->Host(), toString(m_conn->Port()));

        // Start resolving async
        m_resolver.async_resolve(q, m_io_strand.wrap(std::bind(&EthGetworkClient::handle_resolve,
                                        this, std::placeholders::_1, std::placeholders::_2)));
    }
    else
    {
        // No need to use the resolver if host is already an IP address
        m_endpoints.push(boost::asio::ip::tcp::endpoint(
            boost::asio::ip::address::from_string(m_conn->Host()), m_conn->Port()));
        send(m_jsonGetWork);
    }
}

void EthGetworkClient::disconnect()
{
    // Release session
    m_connected.store(false, std::memory_order_relaxed);
    if (m_session)
    {
        m_conn->addDuration(m_session->duration());
        m_session = nullptr;
    }

    // Reset state flags
    m_connecting.store(false, std::memory_order_relaxed);
    m_txPending.store(false, std::memory_order_relaxed);
    m_getwork_timer.cancel();

    // Clear all pending operations and buffers
    m_txQueue.consume_all([](std::string* l) { delete l; });
    m_request.consume(m_request.capacity());
    m_response.consume(m_response.capacity());

    // Invoke the disconnection callback if set
    if (m_onDisconnected)
        m_onDisconnected();
}

void EthGetworkClient::begin_connect()
{
    if (!m_endpoints.empty())
    {
        // Pick the first endpoint in list
        // Endpoints get discarded on connection errors
        m_endpoint = m_endpoints.front();
        m_socket.async_connect(m_endpoint,
            m_io_strand.wrap(
                std::bind(&EthGetworkClient::handle_connect, this, std::placeholders::_1)));
    }
    else
    {
        cwarn << "No more IP addresses to try for host: " << m_conn->Host();
        disconnect();
    }
}

void EthGetworkClient::handle_connect(const boost::system::error_code& ec)
{
    if (!ec && m_socket.is_open())
    {
        // If in "connecting" phase raise the proper event
        if (m_connecting.load(std::memory_order_relaxed))
        {
            // Initialize new session
            m_connected.store(true, std::memory_order_relaxed);
            m_session = std::unique_ptr<Session>(new Session());
            m_session->subscribed.store(true, std::memory_order_relaxed);
            m_session->authorized.store(true, std::memory_order_relaxed);

            m_connecting.store(false, std::memory_order_relaxed);

            if (m_onConnected)
                m_onConnected();
            m_current_tstamp = std::chrono::steady_clock::now();
        }

        // Retrieve first line waiting in the queue and submit
        // If other lines are waiting they will be processed
        // at the end of the processed request
        std::string* line = nullptr;
        Json::Reader jRdr;
        std::ostream os(&m_request);

        if (!m_txQueue.empty() && m_txQueue.pop(line))
        {
            if (line->size())
            {
                // Parse the request to track pending operations
                jRdr.parse(*line, m_pendingJReq);
                m_pending_tstamp = std::chrono::steady_clock::now();

                // Make sure path begins with "/"
                std::string path = (m_conn->Path().empty() ? "/" : m_conn->Path());

                // Build HTTP request
                os << "POST " << path << " HTTP/1.0\r\n";
                os << "Host: " << m_conn->Host() << "\r\n";
                os << "Content-Type: application/json\r\n";
                os << "Content-Length: " << line->length() << "\r\n";
                os << "Connection: close\r\n\r\n";  // Double line feed marks beginning of body

                // Add the payload
                os << *line;

                // Log received message for debug purposes
                if (g_logOptions & LOG_JSON)
                    cnote << " >> " << *line;

                delete line;

                // Send the request
                async_write(m_socket, m_request,
                    m_io_strand.wrap(
                        std::bind(&EthGetworkClient::handle_write, this, std::placeholders::_1)));
            }
            else
            {
                delete line;
                // If empty request, check next item in queue
                begin_connect();
            }
        }
        else
        {
            // No pending operations
            m_txPending.store(false, std::memory_order_relaxed);
        }
    }
    else
    {
        if (ec != boost::asio::error::operation_aborted)
        {
            // This endpoint doesn't respond - try next one
            cwarn << "Error connecting to " << m_conn->Host() << ":" << toString(m_conn->Port())
                  << " : " << ec.message();
            m_endpoints.pop();
            begin_connect();
        }
    }
}

void EthGetworkClient::handle_write(const boost::system::error_code& ec)
{
    if (!ec)
    {
        // Transmission successfully sent
        // Read the response asynchronously
        async_read(m_socket, m_response, boost::asio::transfer_all(),
            m_io_strand.wrap(std::bind(&EthGetworkClient::handle_read, this, std::placeholders::_1,
                std::placeholders::_2)));
    }
    else
    {
        if (ec != boost::asio::error::operation_aborted)
        {
            cwarn << "Error writing to " << m_conn->Host() << ":" << toString(m_conn->Port())
                  << " : " << ec.message();
            m_endpoints.pop();
            begin_connect();
        }
    }
}

void EthGetworkClient::handle_read(
    const boost::system::error_code& ec, std::size_t bytes_transferred)
{
    if (!ec || ec == boost::asio::error::eof)
    {
        // Close socket
        if (m_socket.is_open())
            m_socket.close();

        // Get the whole message
        std::string rx_message(
            boost::asio::buffer_cast<const char*>(m_response.data()), bytes_transferred);
        m_response.consume(bytes_transferred);

        // Empty response check
        if (rx_message.empty())
        {
            cwarn << "Invalid response from " << m_conn->Host() << ":" << toString(m_conn->Port());
            disconnect();
            return;
        }

        // Read message by lines.
        // First line is HTTP status
        // Other lines are headers
        // A double "\r\n" identifies beginning of body
        // The rest is body
        const std::string linedelimiter = "\r\n";
        std::size_t delimiteroffset = rx_message.find(linedelimiter);

        unsigned int linenum = 0;
        bool isHeader = true;
        while (!rx_message.empty() && delimiteroffset != std::string::npos)
        {
            linenum++;
            std::string line = rx_message.substr(0, delimiteroffset);
            rx_message.erase(0, delimiteroffset + 2);

            // Empty line identifies beginning of body
            if (line.empty())
            {
                isHeader = false;
                delimiteroffset = rx_message.find(linedelimiter);
                if (delimiteroffset != std::string::npos)
                    continue;

                // Clean up newlines in response body
                boost::replace_all(rx_message, "\n", "");
                line = rx_message;
            }

            // Parse HTTP status line
            if (isHeader && linenum == 1)
            {
                if (line.substr(0, 7) != "HTTP/1.")
                {
                    cwarn << "Invalid response from " << m_conn->Host() << ":"
                          << toString(m_conn->Port());
                    disconnect();
                    return;
                }

                std::size_t spaceoffset = line.find(' ');
                if (spaceoffset == std::string::npos)
                {
                    cwarn << "Invalid response from " << m_conn->Host() << ":"
                          << toString(m_conn->Port());
                    disconnect();
                    return;
                }

                std::string status = line.substr(spaceoffset + 1);
                if (status.substr(0, 3) != "200")
                {
                    cwarn << m_conn->Host() << ":" << toString(m_conn->Port())
                          << " reported status " << status;
                    disconnect();
                    return;
                }
            }

            // Parse response body
            if (!isHeader)
            {
                // Log received message for debug purposes
                if (g_logOptions & LOG_JSON)
                    cnote << " << " << line;

                // Parse and process JSON
                Json::Value jRes;
                Json::Reader jRdr;
                if (jRdr.parse(line, jRes))
                {
                    // Process the response synchronously to avoid overlapping async reads
                    processResponse(jRes);
                }
                else
                {
                    std::string what = jRdr.getFormattedErrorMessages();
                    boost::replace_all(what, "\n", " ");
                    cwarn << "Got invalid JSON message: " << what;
                }
            }

            delimiteroffset = rx_message.find(linedelimiter);
        }

        // Check for more pending operations
        if (!m_txQueue.empty())
        {
            begin_connect();
        }
        else
        {
            // Signal end of async send/receive operations
            m_txPending.store(false, std::memory_order_relaxed);
        }
    }
    else
    {
        if (ec != boost::asio::error::operation_aborted)
        {
            cwarn << "Error reading from " << m_conn->Host() << ":" << toString(m_conn->Port())
                  << ": " << ec.message();
            disconnect();
        }
    }
}

void EthGetworkClient::handle_resolve(
    const boost::system::error_code& ec, tcp::resolver::iterator i)
{
    if (!ec)
    {
        // Add all resolved endpoints to our queue
        while (i != tcp::resolver::iterator())
        {
            m_endpoints.push(i->endpoint());
            ++i;
        }

        m_resolver.cancel();

        // Resolver has finished so invoke connection asynchronously
        send(m_jsonGetWork);
    }
    else
    {
        cwarn << "Could not resolve host " << m_conn->Host() << ": " << ec.message();
        disconnect();
    }
}

void EthGetworkClient::processResponse(Json::Value& JRes)
{
    // Initialize default response values
    unsigned _id = 0;         // Should match the request id
    bool _isSuccess = false;  // Whether response indicates success
    std::string _errReason;   // Error message if any

    // Check for required id field
    if (!JRes.isMember("id"))
    {
        cwarn << "Missing id member in response from " << m_conn->Host() << ":"
              << toString(m_conn->Port());
        return;
    }

    // Get id from pending request
    // Note: Some pools (like Dwarfpool) always respond with "id":0
    _id = m_pendingJReq.get("id", 0).asUInt();
    _isSuccess = JRes.get("error", Json::Value::null).empty();
    _errReason = (_isSuccess ? "" : processError(JRes));

    // Handle different response types based on id
    if (_id == 0 || _id == 1)
    {
        // Handle getwork response
        if (!_isSuccess)
        {
            // Got an error - delay next request
            cwarn << "Got " << _errReason << " from " << m_conn->Host() << ":"
                  << toString(m_conn->Port());
            m_getwork_timer.expires_from_now(boost::posix_time::seconds(30));
            m_getwork_timer.async_wait(m_io_strand.wrap(
                std::bind(&EthGetworkClient::getwork_timer_elapsed, this, std::placeholders::_1)));
        }
        else
        {
            // Handle successful getwork response
            if (!JRes.isMember("result"))
            {
                cwarn << "Missing data for eth_getWork request from " << m_conn->Host() << ":"
                      << toString(m_conn->Port());
            }
            else
            {
                // Parse work package
                Json::Value JPrm = JRes.get("result", Json::Value::null);
                WorkPackage newWp;

                newWp.header = h256(JPrm.get(Json::Value::ArrayIndex(0), "").asString());
                newWp.seed = h256(JPrm.get(Json::Value::ArrayIndex(1), "").asString());
                newWp.boundary = h256(JPrm.get(Json::Value::ArrayIndex(2), "").asString());
                newWp.job = newWp.header.hex();

                // Only process if new work is different
                if (m_current.header != newWp.header)
                {
                    m_current = newWp;
                    m_current_tstamp = std::chrono::steady_clock::now();

                    if (m_onWorkReceived)
                        m_onWorkReceived(m_current);
                }

                // Schedule next getwork request
                m_getwork_timer.expires_from_now(
                    boost::posix_time::milliseconds(m_farmRecheckPeriod));
                m_getwork_timer.async_wait(m_io_strand.wrap(std::bind(
                    &EthGetworkClient::getwork_timer_elapsed, this, std::placeholders::_1)));
            }
        }
    }
    else if (_id == 9)
    {
        // Response to hashrate submission - no action needed
    }
    else if (_id >= 40 && _id <= m_solution_submitted_max_id)
    {
        // Handle solution submission response
        if (_isSuccess && JRes["result"].isConvertibleTo(Json::ValueType::booleanValue))
            _isSuccess = JRes["result"].asBool();

        // Calculate response time
        std::chrono::milliseconds _delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_pending_tstamp);

        // Determine which miner submitted this solution
        const unsigned miner_index = _id - 40;

        // Trigger appropriate callback
        if (_isSuccess)
        {
            if (m_onSolutionAccepted)
                m_onSolutionAccepted(_delay, miner_index, false);
        }
        else
        {
            if (m_onSolutionRejected)
                m_onSolutionRejected(_delay, miner_index);
        }
    }
}

std::string EthGetworkClient::processError(Json::Value& JRes)
{
    std::string retVar;

    if (JRes.isMember("error") && !JRes.get("error", Json::Value::null).isNull())
    {
        // Extract error based on JSON type
        if (JRes["error"].isConvertibleTo(Json::ValueType::stringValue))
        {
            retVar = JRes.get("error", "Unknown error").asString();
        }
        else if (JRes["error"].isConvertibleTo(Json::ValueType::arrayValue))
        {
            for (const auto& i : JRes["error"])
            {
                retVar += i.asString() + " ";
            }
        }
        else if (JRes["error"].isConvertibleTo(Json::ValueType::objectValue))
        {
            for (Json::Value::iterator i = JRes["error"].begin(); i != JRes["error"].end(); ++i)
            {
                Json::Value k = i.key();
                Json::Value v = (*i);
                retVar += static_cast<std::string>(i.name()) + ":" + v.asString() + " ";
            }
        }
    }
    else
    {
        retVar = "Unknown error";
    }

    return retVar;
}

void EthGetworkClient::send(Json::Value const& jReq)
{
    send(Json::writeString(m_jSwBuilder, jReq));
}

void EthGetworkClient::send(std::string const& sReq)
{
    // Add request to queue
    std::string* line = new std::string(sReq);
    m_txQueue.push(line);

    // If no operations pending, start connection process
    bool expected = false;
    if (m_txPending.compare_exchange_strong(expected, true, std::memory_order_relaxed))
        begin_connect();
}

void EthGetworkClient::submitHashrate(uint64_t const& rate, string const& id)
{
    // Submit hashrate if session exists
    if (m_session)
    {
        Json::Value jReq;
        jReq["id"] = unsigned(9);
        jReq["jsonrpc"] = "2.0";
        jReq["method"] = "eth_submitHashrate";
        jReq["params"] = Json::Value(Json::arrayValue);
        jReq["params"].append(toHex(rate, HexPrefix::Add));  // Already expressed as hex
        jReq["params"].append(id);                           // Already prefixed by 0x
        send(jReq);
    }
}

void EthGetworkClient::submitSolution(const Solution& solution)
{
    // Submit solution if session exists
    if (m_session)
    {
        // Create the JSON-RPC request
        Json::Value jReq;
        std::string nonceHex = toHex(solution.nonce);

        // Generate a unique ID for this submission
        unsigned id = 40 + solution.midx;
        jReq["id"] = id;
        jReq["jsonrpc"] = "2.0";

        // Track highest solution ID for response handling
        m_solution_submitted_max_id = std::max(m_solution_submitted_max_id, id);

        // Set method and parameters
        jReq["method"] = "eth_submitWork";
        jReq["params"] = Json::Value(Json::arrayValue);
        jReq["params"].append("0x" + nonceHex);
        jReq["params"].append("0x" + solution.work.header.hex());
        jReq["params"].append("0x" + solution.mixHash.hex());

        // Send the request
        send(jReq);
    }
}

void EthGetworkClient::getwork_timer_elapsed(const boost::system::error_code& ec)
{
    // Handle getwork timer expiration
    if (!ec)
    {
        // Check if last work is older than timeout
        std::chrono::seconds _delay = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_current_tstamp);

        if (_delay.count() > m_worktimeout)
        {
            // Work is stale - disconnect and try next endpoint
            cwarn << "No new work received in " << m_worktimeout << " seconds.";
            m_endpoints.pop();
            disconnect();
        }
        else
        {
            // Request new work
            send(m_jsonGetWork);
        }
    }
}
