#include "vhost.h"

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>

// Default ring buffer & mempool configurations
#define NUM_MBUFS 65535
#define MBUF_CACHE_SIZE 250
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

namespace vtb {

VhostController::VhostController() : mbuf_pool(nullptr) {}

VhostController::~VhostController() {
    // Graceful release of DPDK resources
    for (uint16_t port_id : active_ports) {
        std::cout << "Stopping and closing vhost port " << port_id << "..." << std::endl;
        rte_eth_dev_stop(port_id);
        rte_eth_dev_close(port_id);
    }
    // mbuf pools should only be freed once ports utilizing them are completely closed
    if (mbuf_pool != nullptr) {
        rte_mempool_free(mbuf_pool);
    }
}

const std::vector<uint16_t>& VhostController::get_active_ports() const {
    return active_ports;
}

void VhostController::configure_and_start_ports() {
    // 1. Create a unified packet memory pool mapped to the current CPU socket
    mbuf_pool = rte_pktmbuf_pool_create("VHOST_MBUF_POOL", NUM_MBUFS,
                                        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());

    if (mbuf_pool == nullptr) {
        throw std::runtime_error("Failed to allocate mbuf pool. Verify hugepages setup.");
    }

    uint16_t port_id;

    // 2. Iterate through all ethdev instances registered during rte_eal_init()
    RTE_ETH_FOREACH_DEV(port_id) {
        struct rte_eth_dev_info dev_info;
        int ret = rte_eth_dev_info_get(port_id, &dev_info);
        if (ret != 0) {
            std::cerr << "Warning: Failed to retrieve info for port " << port_id << std::endl;
            continue;
        }

        // 3. Filter and parse explicitly for vhost virtual devices (net_vhost PMD)
        if (dev_info.driver_name && std::string(dev_info.driver_name).find("vhost") != std::string::npos) {
            std::cout << "Discovered Vhost Device: port_id=" << port_id 
                      << ", driver=" << dev_info.driver_name 
                      << ", queues_requested=" << dev_info.max_rx_queues << std::endl;
            
            setup_port(port_id);
            active_ports.push_back(port_id);
        }
    }

    if (active_ports.empty()) {
        std::cerr << "Warning: EAL initialized successfully, but no matching vhost PMD devices were discovered. "
                  << "Double check your --vdev parameters." << std::endl;
    }
}

void VhostController::setup_port(uint16_t port_id) {
    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(port_id, &dev_info);

    // Grab queue allocations supplied via 'queues=X' parameter inside --vdev command string
    uint16_t nb_rx_q = dev_info.max_rx_queues;
    uint16_t nb_tx_q = dev_info.max_tx_queues;

    struct rte_eth_conf port_conf;
    std::memset(&port_conf, 0, sizeof(struct rte_eth_conf));
    
    // 1. Configure device infrastructure
    int ret = rte_eth_dev_configure(port_id, nb_rx_q, nb_tx_q, &port_conf);
    if (ret < 0) {
        throw std::runtime_error("rte_eth_dev_configure failed: err=" + std::to_string(ret) + ", port=" + std::to_string(port_id));
    }

    uint16_t rx_rings = RX_RING_SIZE;
    uint16_t tx_rings = TX_RING_SIZE;
    rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &rx_rings, &tx_rings);

    // 2. Setup standard input queues
    for (uint16_t q = 0; q < nb_rx_q; q++) {
        ret = rte_eth_rx_queue_setup(port_id, q, rx_rings,
                                     rte_eth_dev_socket_id(port_id), nullptr, mbuf_pool);
        if (ret < 0) {
            throw std::runtime_error("rte_eth_rx_queue_setup failed for port " + std::to_string(port_id) + " queue " + std::to_string(q));
        }
    }

    // 3. Setup standard output queues
    for (uint16_t q = 0; q < nb_tx_q; q++) {
        ret = rte_eth_tx_queue_setup(port_id, q, tx_rings,
                                     rte_eth_dev_socket_id(port_id), nullptr);
        if (ret < 0) {
            throw std::runtime_error("rte_eth_tx_queue_setup failed for port " + std::to_string(port_id) + " queue " + std::to_string(q));
        }
    }

    // 4. Start the driver data plane channel
    ret = rte_eth_dev_start(port_id);
    if (ret < 0) {
        throw std::runtime_error("rte_eth_dev_start failed for port " + std::to_string(port_id));
    }

    // 5. Allow all frame capture targets (common target behavior for virtual backends)
    rte_eth_promiscuous_enable(port_id);
    std::cout << "Successfully configured and started port " << port_id << std::endl;
}

void VhostController::check_and_print_link_status() const {
    std::cout << "\n============================================\n"
              << "       VHOST INTERFACE HANDSHAKE MONITOR     \n"
              << "=============================================" << std::endl;
    for (uint16_t port_id : active_ports) {
        struct rte_eth_link link;
        rte_eth_link_get_nowait(port_id, &link);

        if (link.link_status == RTE_ETH_LINK_UP) {
            std::cout << " -> Port " << port_id << ": [CONNECTED/UP] at " << link.link_speed << " Mbps" << std::endl;
        } else {
            std::cout << " -> Port " << port_id << ": [WAITING/DOWN] (Awaiting QEMU socket session...)" << std::endl;
        }
    }
    std::cout << "=============================================\n" << std::endl;
}

bool VhostController::is_port_up(uint16_t port_id) const {
    struct rte_eth_link link;
    rte_eth_link_get_nowait(port_id, &link);
    return (link.link_status == RTE_ETH_LINK_UP);
}

}
