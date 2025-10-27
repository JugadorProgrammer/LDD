#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h>

static struct nf_hook_ops nfho;
static __be16 target_port = htons(8808);

static unsigned int hook_func(void *priv, struct sk_buff *skb,
                              const struct nf_hook_state *state) {
    struct sk_buff *skb_dup;
    struct iphdr *ip_header;
    struct tcphdr *tcp_header;
    struct udphdr *udp_header;
    __be16 src_port = 0, dst_port = 0;

    printk(KERN_INFO "DUPLICATE: 1: hook_func");
    if (!skb) return NF_ACCEPT;

    ip_header = ip_hdr(skb);
    if (!ip_header) return NF_ACCEPT;

    printk(KERN_INFO "DUPLICATE: 2: !ip_header");
    // Только TCP/UDP пакеты на 127.0.0.1
    if (ip_header->daddr != htonl(INADDR_LOOPBACK) ||
        (ip_header->protocol != IPPROTO_TCP && ip_header->protocol != IPPROTO_UDP)) {
        return NF_ACCEPT;
    }

    printk(KERN_INFO "DUPLICATE: 3: ip_header->protocol == IPPROTO_TCP");
    // Получаем порты из оригинального пакета
    if (ip_header->protocol == IPPROTO_TCP) {
        tcp_header = tcp_hdr(skb);
        if (!tcp_header) {
            return NF_ACCEPT;
        }
        src_port = tcp_header->source;
        dst_port = tcp_header->dest;
    } else if (ip_header->protocol == IPPROTO_UDP) {
        udp_header = udp_hdr(skb);
        if (!udp_header) {
            return NF_ACCEPT;
        }
        src_port = udp_header->source;
        dst_port = udp_header->dest;
    }

    printk(KERN_INFO "DUPLICATE: 4: dst_port = %d", ntohs(dst_port));


    // ФИЛЬТР: обрабатываем только пакеты с портом 8807
    if (dst_port != htons(8807)) {
        return NF_ACCEPT;
    }

    // Создаем копию
    skb_dup = skb_copy(skb, GFP_ATOMIC);
    if (!skb_dup) return NF_ACCEPT;

    printk(KERN_INFO "DUPLICATE: 5: skb_dup");
    // Меняем порт в копии
    ip_header = ip_hdr(skb_dup);
    if (ip_header->protocol == IPPROTO_TCP) {
        tcp_header = tcp_hdr(skb_dup);
        if (tcp_header) {
            tcp_header->dest = target_port;
        }
    } else if (ip_header->protocol == IPPROTO_UDP) {
        udp_header = udp_hdr(skb_dup);
        if (udp_header) {
            udp_header->dest = target_port;
        }
    }

    printk(KERN_INFO "DUPLICATE: 6: netif_rx");
    // Просто повторно вводим пакет в сетевую подсистему
    netif_rx(skb_dup);

    return NF_ACCEPT;
}

static int __init duplicator_init(void) {
    nfho.hook = hook_func;
    nfho.hooknum = NF_INET_PRE_ROUTING;
    nfho.pf = PF_INET;
    nfho.priority = NF_IP_PRI_FIRST;

    nf_register_net_hook(&init_net, &nfho);
    printk(KERN_INFO "Localhost duplicator: active on port 8808\n");
    return 0;
}

static void __exit duplicator_exit(void) {
    nf_unregister_net_hook(&init_net, &nfho);
    printk(KERN_INFO "Localhost duplicator: stopped\n");
}

module_init(duplicator_init);
module_exit(duplicator_exit);

// Информация о модуле
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Илья Хомченков");
MODULE_DESCRIPTION("Span Driver"); // Описание
