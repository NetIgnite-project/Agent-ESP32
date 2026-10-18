#ifndef ICMP_H
#define ICMP_H

#include <Arduino.h>
#include <IPAddress.h>
#include <IPv6Address.h>
#include <ping/ping_sock.h>
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <net/if.h>
#include <lwip/ip4.h>
#include <lwip/ip6.h>
#include <esp_netif_net_stack.h>
#include <atomic>
#include <new>

#define PING_TIMEOUT_MS 5000

// State shared between the caller and the ping task. It lives on the heap and is freed by
// whichever side releases it last, so a ping that outlives PING_TIMEOUT_MS can't write to memory
// that is gone.
struct ping_context_t {
    std::atomic<bool> success{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> released{false};
};

class ICMP {
  public:

    static bool ping(IPAddress dest, size_t count = 1) {

        ip_addr_t target_addr;
        target_addr.type = IPADDR_TYPE_V4;
        target_addr.u_addr.ip4.addr = static_cast<const uint32_t>(dest);

        return do_ping(target_addr, count);
    }

    static bool ping(IPv6Address dest, size_t count = 1) {
        
        ip_addr_t target_addr;
        target_addr.type = IPADDR_TYPE_V6;
        // target_addr.u_addr.ip6.zone = get_netif_index();

        memcpy(target_addr.u_addr.ip6.addr, static_cast<const uint32_t*>(dest), sizeof(target_addr.u_addr.ip6.addr));

        return do_ping(target_addr, count);
    }

  private:
    static bool do_ping(ip_addr_t target_addr, size_t count) {

        esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
        ping_config.target_addr = target_addr;
        ping_config.count = count;
        // ping_config.interface = get_netif_index();

        ping_context_t *context = new (std::nothrow) ping_context_t();
        if (!context) return false;

        esp_ping_callbacks_t cbs;
        cbs.on_ping_success = on_ping_success;
        cbs.on_ping_timeout = on_ping_timeout;
        cbs.on_ping_end = on_ping_end;
        cbs.cb_args = context;

        esp_ping_handle_t ping = nullptr;
        if (esp_ping_new_session(&ping_config, &cbs, &ping) != ESP_OK) {
            delete context;
            return false;
        }
        if (esp_ping_start(ping) != ESP_OK) {
            esp_ping_delete_session(ping);
            delete context;
            return false;
        }

        unsigned long start_time = millis();
        while (!context->finished && (millis() - start_time < PING_TIMEOUT_MS)) {
            delay(10);
        }

        bool success = context->success;
        release(context);
        return success;
    }

    static void release(ping_context_t *context) {
        // The second of the two releases (caller and on_ping_end) frees the context.
        if (context->released.exchange(true)) {
            delete context;
        }
    }

    static void on_ping_success(esp_ping_handle_t hdl, void *args) {
        ping_context_t *context = static_cast<ping_context_t*>(args);

        context->success = true;
    }

    static void on_ping_timeout(esp_ping_handle_t hdl, void *args) {}

    static void on_ping_end(esp_ping_handle_t hdl, void *args) {
        ping_context_t *context = static_cast<ping_context_t*>(args);
        context->finished = true;
        release(context);

        esp_ping_delete_session(hdl);
    }

    static int get_netif_index()
    {
        struct netif *netif = netif_list;
        while (netif) {
            if (netif->name[0] == 's' && netif->name[1] == 't') { // "st" usually stands for STA
                return netif_get_index(netif); // this gives the scope ID
            }
            netif = netif->next;
        }
        // Serial.println("No suitable network interface found for ping.");
        return 0;
    }


};

#endif // ICMP_H