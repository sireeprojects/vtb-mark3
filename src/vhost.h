#pragma once

#include <vector>
#include <cstdint>


// Forward declaration of DPDK structure to avoid polluting global namespace
struct rte_mempool;

namespace vtb {

class VhostController {
public:
    VhostController();
    ~VhostController();

    // Prevent copying to avoid multiple resource allocations/deallocations
    VhostController(const VhostController&) = delete;
    VhostController& operator=(const VhostController&) = delete;

    /**
     * @brief Discovers all Vhost virtual devices initialized via EAL commandline (--vdev).
     * Allocates global mempool and configures RX/TX queues dynamically.
     */
    void configure_and_start_ports();

    /**
     * @brief Retrieve the list of successfully initialized and active DPDK port IDs.
     */
    const std::vector<uint16_t>& get_active_ports() const;

    /**
     * @brief Polls all configured ports and logs their link state to stdout.
     * Essential for Client mode to monitor when QEMU establishes a socket connection.
     */
    void check_and_print_link_status() const;

    /**
     * @brief Asynchronously check if a specific vhost port handshake is complete.
     * @param port_id The DPDK assigned port ID.
     * @return true if link is up and ready for RX/TX bursts, false otherwise.
     */
    bool is_port_up(uint16_t port_id) const;

private:
    std::vector<uint16_t> active_ports;
    struct rte_mempool* mbuf_pool;

    /**
     * @brief Sets up configuration, descriptor rings, and queues for a given port.
     */
    void setup_port(uint16_t port_id);
};
}
