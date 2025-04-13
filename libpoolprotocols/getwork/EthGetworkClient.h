#pragma once

#include <atomic>
#include <chrono>
#include <iostream>
#include <queue>
#include <string>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/queue.hpp>

#include <json/json.h>

#include "../PoolClient.h"

namespace dev
{
namespace eth
{

/**
 * @brief Client implementation for Ethereum getwork mining protocol
 *
 * This class implements a client for the Ethereum getwork mining protocol,
 * which periodically requests work from a pool or node, and submits solutions.
 */
class EthGetworkClient : public PoolClient
{
public:
    /**
     * @brief Construct a new EthGetworkClient
     *
     * @param worktimeout Timeout in seconds for considering work stale
     * @param farmRecheckPeriod Period in milliseconds to check for new work
     */
    EthGetworkClient(int worktimeout, unsigned farmRecheckPeriod);

    /**
     * @brief Destructor
     */
    ~EthGetworkClient();

    /**
     * @brief Connect to the getwork server
     */
    void connect() override;

    /**
     * @brief Disconnect from the getwork server
     */
    void disconnect() override;

    /**
     * @brief Submit hashrate to the pool/node
     *
     * @param rate Hashrate to report
     * @param id Worker/miner identifier
     */
    void submitHashrate(uint64_t const& rate, std::string const& id) override;

    /**
     * @brief Submit a solution to the pool/node
     *
     * @param solution Solution found by a miner
     */
    void submitSolution(const Solution& solution) override;

private:
    /**
     * @brief Period to check for new work in milliseconds
     */
    unsigned m_farmRecheckPeriod = 500;

    /**
     * @brief Initiate connection to server
     */
    void begin_connect();

    /**
     * @brief Handle DNS resolution result
     */
    void handle_resolve(
        const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator i);

    /**
     * @brief Handle connection establishment result
     */
    void handle_connect(const boost::system::error_code& ec);

    /**
     * @brief Handle write operation completion
     */
    void handle_write(const boost::system::error_code& ec);

    /**
     * @brief Handle read operation completion
     */
    void handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred);

    /**
     * @brief Process error response
     *
     * @param JRes JSON response containing error
     * @return std::string Error message
     */
    std::string processError(Json::Value& JRes);

    /**
     * @brief Process successful response
     *
     * @param JRes JSON response to process
     */
    void processResponse(Json::Value& JRes);

    /**
     * @brief Send a JSON request to the server
     *
     * @param jReq JSON request to send
     */
    void send(Json::Value const& jReq);

    /**
     * @brief Send a string request to the server
     *
     * @param sReq String request to send
     */
    void send(std::string const& sReq);

    /**
     * @brief Timer handler for periodic getwork requests
     */
    void getwork_timer_elapsed(const boost::system::error_code& ec);

    /**
     * @brief Current work package
     */
    WorkPackage m_current;

    /**
     * @brief Flag indicating if connection is in progress
     */
    std::atomic<bool> m_connecting{false};

    /**
     * @brief Flag indicating if async socket operation is pending
     */
    std::atomic<bool> m_txPending{false};

    /**
     * @brief Queue for outgoing messages
     */
    boost::lockfree::queue<std::string*> m_txQueue;

    /**
     * @brief Strand for serializing IO operations
     */
    boost::asio::io_service::strand m_io_strand;

    /**
     * @brief TCP socket for server communication
     */
    boost::asio::ip::tcp::socket m_socket;

    /**
     * @brief DNS resolver
     */
    boost::asio::ip::tcp::resolver m_resolver;

    /**
     * @brief Queue of resolved endpoints
     */
    std::queue<boost::asio::ip::basic_endpoint<boost::asio::ip::tcp>> m_endpoints;

    /**
     * @brief Buffer for outgoing requests
     */
    boost::asio::streambuf m_request;

    /**
     * @brief Buffer for incoming responses
     */
    boost::asio::streambuf m_response;

    /**
     * @brief JSON writer configuration
     */
    Json::StreamWriterBuilder m_jSwBuilder;

    /**
     * @brief Cached getwork request JSON
     */
    std::string m_jsonGetWork;

    /**
     * @brief Pending JSON request
     */
    Json::Value m_pendingJReq;

    /**
     * @brief Timestamp of pending request
     */
    std::chrono::time_point<std::chrono::steady_clock> m_pending_tstamp;

    /**
     * @brief Timer for scheduling getwork requests
     */
    boost::asio::deadline_timer m_getwork_timer;

    /**
     * @brief Timeout in seconds for considering work stale
     */
    int m_worktimeout;

    /**
     * @brief Timestamp of current work
     */
    std::chrono::time_point<std::chrono::steady_clock> m_current_tstamp;

    /**
     * @brief Maximum JSON ID used for solution submission
     */
    unsigned m_solution_submitted_max_id;
};

}  // namespace eth
}  // namespace dev
