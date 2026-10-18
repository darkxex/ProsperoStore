// ps5-native-app-boilerplate - Resolver allocation, numeric input and failure checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <string.h>
int curl_test_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void curl_test_freeaddrinfo(struct addrinfo *);
static int lookups, pools, resolvers, fail;
int sceNetPoolCreate(const char *name, int size, int flags)
{
    assert(name && size == 16384 && flags == 0);
    ++pools;
    return 10;
}
int sceNetPoolDestroy(int pool)
{
    assert(pool == 10);
    --pools;
    return 0;
}
int sceNetResolverCreate(const char *name, int pool, int flags)
{
    assert(name && pool == 10 && flags == 0);
    ++resolvers;
    return 20;
}
int sceNetResolverDestroy(int resolver)
{
    assert(resolver == 20);
    --resolvers;
    return 0;
}
int sceNetResolverStartNtoa(int resolver, const char *name, struct in_addr *out, int timeout,
                            int retries, int flags)
{
    assert(resolver == 20 && !strcmp(name, "prosperostore.net") && timeout == 5000000 &&
           retries == 2 && flags == 0);
    ++lookups;
    assert(inet_pton(AF_INET, "192.0.2.1", out) == 1);
    return fail ? -1 : 0;
}
int main(void)
{
    struct addrinfo hints = {0}, *out = NULL;
    hints.ai_family = AF_INET;
    assert(curl_test_getaddrinfo("192.0.2.1", "443", &hints, &out) == 0);
    assert(ntohs(((struct sockaddr_in *)out->ai_addr)->sin_port) == 443 && lookups == 0);
    curl_test_freeaddrinfo(out);
    assert(curl_test_getaddrinfo("prosperostore.net", "70000", &hints, &out) == EAI_SERVICE &&
           !out);
    assert(curl_test_getaddrinfo("prosperostore.net", "443", &hints, &out) == 0);
    curl_test_freeaddrinfo(out);
    assert(lookups == 1 && pools == 0 && resolvers == 0);
    fail = 1;
    assert(curl_test_getaddrinfo("prosperostore.net", "443", &hints, &out) == EAI_NONAME && !out);
    assert(pools == 0 && resolvers == 0);
    hints.ai_flags = AI_NUMERICHOST;
    assert(curl_test_getaddrinfo("prosperostore.net", "443", &hints, &out) == EAI_NONAME);
    hints.ai_family = AF_INET6;
    assert(curl_test_getaddrinfo("::1", "443", &hints, &out) == EAI_FAMILY);
}
