// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dnssd.h"
#include "dnssdint.h"
#include "global.h"
#include "mdns.h"
#include "utils.h"
struct dnssd_s {
    char *name;
    char mac[6];
    char txt[512];
    int txt_len;
    bool raop_registered;
    bool airplay_registered;
};
dnssd_t *dnssd_init(const char *name, int name_len, const char *mac, int mac_len, int *error)
{
    *error = DNSSD_ERROR_OUTOFMEM;
    if (mac_len != 6) { *error = DNSSD_ERROR_HWADDRLEN; return NULL; }
    dnssd_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->name = strndup(name, name_len);
    if (!d->name) { free(d); return NULL; }
    memcpy(d->mac, mac, 6);
    *error = DNSSD_ERROR_NOERROR;
    return d;
}
int dnssd_register_raop(dnssd_t *d, unsigned short port)
{
    char instance[64];
    int n = utils_hwaddr_raop(instance, sizeof(instance), d->mac, 6);
    if (n < 0 || strlen(instance) + 1 + strlen(d->name) >= sizeof(instance)) return -1;
    strcat(instance, "@");
    strcat(instance, d->name);
    mdns_txt_item_t txt[] = {
        {"ch", RAOP_CH}, {"cn", RAOP_CN}, {"da", RAOP_DA}, {"et", RAOP_ET},
        {"vv", RAOP_VV}, {"ft", RAOP_FT}, {"am", GLOBAL_MODEL}, {"md", RAOP_MD},
        {"rhd", RAOP_RHD}, {"pw", "false"}, {"sr", RAOP_SR}, {"ss", RAOP_SS},
        {"sv", RAOP_SV}, {"tp", RAOP_TP}, {"txtvers", RAOP_TXTVERS},
        {"sf", RAOP_SF}, {"vs", RAOP_VS}, {"vn", RAOP_VN}, {"pk", RAOP_PK},
    };
    if (mdns_service_add(instance, "_raop", "_tcp", port, txt,
                         sizeof(txt) / sizeof(txt[0])) != ESP_OK) return -1;
    d->raop_registered = true;
    return 1;
}
int dnssd_register_airplay(dnssd_t *d, unsigned short port)
{
    char device[18];
    if (utils_hwaddr_airplay(device, sizeof(device), d->mac, 6) < 0) return -1;
    mdns_txt_item_t txt[] = {
        {"deviceid", device}, {"features", AIRPLAY_FEATURES}, {"flags", AIRPLAY_FLAGS},
        {"model", GLOBAL_MODEL}, {"pk", AIRPLAY_PK}, {"pi", AIRPLAY_PI},
        {"srcvers", AIRPLAY_SRCVERS}, {"vv", AIRPLAY_VV},
    };
    d->txt_len = 0;
    for (size_t i = 0; i < sizeof(txt) / sizeof(txt[0]); ++i) {
        size_t len = strlen(txt[i].key) + 1 + strlen(txt[i].value);
        if (len > 255 || (size_t)d->txt_len + 1 + len > sizeof(d->txt)) return -1;
        d->txt[d->txt_len++] = (char)len;
        memcpy(d->txt + d->txt_len, txt[i].key, strlen(txt[i].key));
        d->txt_len += strlen(txt[i].key);
        d->txt[d->txt_len++] = '=';
        memcpy(d->txt + d->txt_len, txt[i].value, strlen(txt[i].value));
        d->txt_len += strlen(txt[i].value);
    }
    if (mdns_service_add(d->name, "_airplay", "_tcp", port, txt,
                         sizeof(txt) / sizeof(txt[0])) != ESP_OK) return -1;
    d->airplay_registered = true;
    return 1;
}
const char *dnssd_get_airplay_txt(dnssd_t *d, int *len) { *len = d->txt_len; return d->txt; }
const char *dnssd_get_name(dnssd_t *d, int *len) { *len = strlen(d->name); return d->name; }
const char *dnssd_get_hw_addr(dnssd_t *d, int *len) { *len = 6; return d->mac; }
void dnssd_unregister_raop(dnssd_t *d)
{
    if (d->raop_registered) { mdns_service_remove("_raop", "_tcp"); d->raop_registered = false; }
}
void dnssd_unregister_airplay(dnssd_t *d)
{
    if (d->airplay_registered) { mdns_service_remove("_airplay", "_tcp"); d->airplay_registered = false; }
}
void dnssd_destroy(dnssd_t *d)
{
    dnssd_unregister_airplay(d);
    dnssd_unregister_raop(d);
    free(d->name);
    free(d);
}
