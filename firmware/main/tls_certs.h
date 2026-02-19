/**
 * @file tls_certs.h
 * @brief Pinned CA certificate for app.plexus.company
 *
 * This certificate is used for TLS certificate pinning when connecting
 * to the Plexus ingest endpoint. The mbedTLS certificate bundle provides
 * broad CA coverage; this pin adds defense-in-depth for the primary endpoint.
 *
 * The firmware uses the ESP-IDF mbedTLS certificate bundle by default,
 * which covers standard CAs. This pinned cert is an additional layer
 * for production deployments that want to restrict trust to a specific CA.
 *
 * To update: replace the PEM content when the CA certificate rotates.
 * The mbedTLS bundle will continue to work as fallback.
 */

#ifndef PLEXUS_TLS_CERTS_H
#define PLEXUS_TLS_CERTS_H

/*
 * ISRG Root X1 (Let's Encrypt) — used by app.plexus.company
 *
 * Valid until: June 4 2035
 * SHA-256 Fingerprint:
 *   96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6
 */
static const char PLEXUS_CA_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
    "TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
    "cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
    "WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
    "ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
    "MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
    "h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
    "0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
    "A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
    "T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
    "B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
    "B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
    "KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
    "OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
    "jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
    "qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
    "rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
    "HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
    "hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
    "ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
    "3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
    "NFtY2PwByVS5uCbMiogZiUvsaIF+qb8zguAmCTg3LNBSEII3NaQ+t00L6FEBDhQy\n"
    "voQGFMDI+KkC3JfKZ0CtQBL/JXxPgNMnFpKMoBr6aKo7nGMHh4bJAd/JlqHPjbm\n"
    "AOi/x9VOYL9Vf5FBdN1yBCE0HsPnBMnEM/OHPGXap43eLKFkMvMVTEXifwjkQALn\n"
    "2J4LDy1qYSeCzuBY+bKCkjE7W90ttJd4m0W0DNx1mMB8GA1hR01s7tXFtuyx35GD\n"
    "T4Em4HNqobNaATn+GAGgqAGoEhGDAIRVILjkfj3AX1pR0+GFATU4WNRNA0X7uzJ5\n"
    "-----END CERTIFICATE-----\n";

#endif /* PLEXUS_TLS_CERTS_H */
