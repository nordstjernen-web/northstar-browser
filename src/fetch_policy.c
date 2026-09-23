/* Northstar — the one policy check every subresource request passes.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "fetch_policy.h"

#include <string.h>

#include "net.h"

struct ns_fetch_policy {
    gatomicrefcount rc;
    char           *document_url;
    ns_csp         *csp;
};

ns_fetch_policy *
ns_fetch_policy_new(const char *document_url, const ns_csp *csp)
{
    ns_fetch_policy *policy = g_new0(ns_fetch_policy, 1);
    g_atomic_ref_count_init(&policy->rc);
    policy->document_url = g_strdup(document_url);
    policy->csp = csp ? ns_csp_copy(csp) : NULL;
    return policy;
}

ns_fetch_policy *
ns_fetch_policy_ref(ns_fetch_policy *policy)
{
    if (policy) g_atomic_ref_count_inc(&policy->rc);
    return policy;
}

void
ns_fetch_policy_unref(ns_fetch_policy *policy)
{
    if (!policy || !g_atomic_ref_count_dec(&policy->rc)) return;
    g_free(policy->document_url);
    ns_csp_free(policy->csp);
    g_free(policy);
}

const char *
ns_fetch_policy_document_url(const ns_fetch_policy *policy)
{
    return policy ? policy->document_url : NULL;
}

const char *
ns_fetch_destination_name(ns_fetch_destination dest)
{
    switch (dest) {
    case NS_FETCH_DEST_SCRIPT:   return "script";
    case NS_FETCH_DEST_STYLE:    return "stylesheet";
    case NS_FETCH_DEST_IMAGE:    return "image";
    case NS_FETCH_DEST_FONT:     return "font";
    case NS_FETCH_DEST_MEDIA:    return "media";
    case NS_FETCH_DEST_FRAME:    return "frame";
    case NS_FETCH_DEST_WORKER:   return "worker";
    case NS_FETCH_DEST_CONNECT:  return "connection";
    case NS_FETCH_DEST_DOCUMENT: return "document";
    default:                     return "resource";
    }
}

static ns_csp_kind
destination_csp_kind(ns_fetch_destination dest)
{
    switch (dest) {
    case NS_FETCH_DEST_IMAGE:   return NS_CSP_IMG;
    case NS_FETCH_DEST_MEDIA:   return NS_CSP_MEDIA;
    case NS_FETCH_DEST_FONT:    return NS_CSP_FONT;
    case NS_FETCH_DEST_CONNECT: return NS_CSP_CONNECT;
    case NS_FETCH_DEST_WORKER:  return NS_CSP_WORKER;
    case NS_FETCH_DEST_FRAME:   return NS_CSP_FRAME;
    default:                    return NS_CSP_KIND_COUNT;
    }
}

static const char *
destination_csp_directive(ns_fetch_destination dest)
{
    switch (dest) {
    case NS_FETCH_DEST_IMAGE:   return "img-src";
    case NS_FETCH_DEST_MEDIA:   return "media-src";
    case NS_FETCH_DEST_FONT:    return "font-src";
    case NS_FETCH_DEST_CONNECT: return "connect-src";
    case NS_FETCH_DEST_WORKER:  return "worker-src";
    case NS_FETCH_DEST_FRAME:   return "frame-src";
    default:                    return "default-src";
    }
}

static gboolean
url_has_scheme(const char *url, const char *scheme)
{
    return url && g_ascii_strncasecmp(url, scheme, strlen(scheme)) == 0;
}

static gboolean
url_is_loopback(const char *url)
{
    char *host = ns_url_host_from(url);
    gboolean loopback = host && ns_url_host_is_loopback(host);
    g_free(host);
    return loopback;
}

static char *
upgrade_to_https(const char *url)
{
    const char *rest = url + strlen("http://");
    gsize authority = strcspn(rest, "/?#");
    if (authority > 3 && strncmp(rest + authority - 3, ":80", 3) == 0) {
        char *host = g_strndup(rest, authority - 3);
        char *out = g_strconcat("https://", host, rest + authority, NULL);
        g_free(host);
        return out;
    }
    return g_strconcat("https://", rest, NULL);
}

ns_fetch_verdict
ns_fetch_policy_check(const ns_fetch_policy *policy, ns_fetch_destination dest,
                      const char *document_url, const char *url,
                      char **out_upgraded_url)
{
    if (out_upgraded_url) *out_upgraded_url = NULL;
    if (!url || dest == NS_FETCH_DEST_DOCUMENT) return NS_FETCH_ALLOWED;
    if (policy && policy->document_url) document_url = policy->document_url;

    if (url_has_scheme(url, "file:") &&
        (!ns_net_file_urls_allowed() ||
         (document_url && *document_url &&
          !url_has_scheme(document_url, "file:"))))
        return NS_FETCH_BLOCKED_SCHEME;

    char *upgraded = NULL;
    if (url_has_scheme(document_url, "https:") &&
        (url_has_scheme(url, "http:") || url_has_scheme(url, "ws:")) &&
        !url_is_loopback(url)) {
        gboolean upgradable = url_has_scheme(url, "http:") &&
                              (dest == NS_FETCH_DEST_IMAGE ||
                               dest == NS_FETCH_DEST_MEDIA);
        if (!upgradable) return NS_FETCH_BLOCKED_MIXED;
        upgraded = upgrade_to_https(url);
    }

    ns_csp_kind kind = destination_csp_kind(dest);
    if (policy && policy->csp && kind != NS_CSP_KIND_COUNT &&
        !ns_csp_allows(policy->csp, kind, upgraded ? upgraded : url,
                       document_url)) {
        g_free(upgraded);
        return NS_FETCH_BLOCKED_CSP;
    }
    if (!upgraded) return NS_FETCH_ALLOWED;
    if (out_upgraded_url) *out_upgraded_url = upgraded;
    else g_free(upgraded);
    return NS_FETCH_UPGRADED;
}

gboolean
ns_fetch_verdict_blocks(ns_fetch_verdict verdict)
{
    return verdict == NS_FETCH_BLOCKED_SCHEME ||
           verdict == NS_FETCH_BLOCKED_MIXED ||
           verdict == NS_FETCH_BLOCKED_CSP;
}

char *
ns_fetch_verdict_message(ns_fetch_verdict verdict, ns_fetch_destination dest,
                         const char *url)
{
    const char *what = ns_fetch_destination_name(dest);
    if (!url) url = "";
    switch (verdict) {
    case NS_FETCH_BLOCKED_SCHEME:
        return g_strdup_printf("Refused to load local %s %s from a page "
                               "that is not a local file", what, url);
    case NS_FETCH_BLOCKED_MIXED:
        return g_strdup_printf("Blocked mixed content: the secure page "
                               "requested the insecure %s %s", what, url);
    case NS_FETCH_BLOCKED_CSP:
        return g_strdup_printf("Refused to load the %s %s because it "
                               "violates the Content-Security-Policy "
                               "directive %s", what, url,
                               destination_csp_directive(dest));
    default:
        return NULL;
    }
}
