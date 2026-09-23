/* Northstar — WHATWG Encoding Standard: labels, decoders and encoders.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "encoding.h"

#include <string.h>

typedef enum {
    NS_KIND_UTF8,
    NS_KIND_SINGLE_BYTE,
    NS_KIND_GB18030,
    NS_KIND_BIG5,
    NS_KIND_EUC_JP,
    NS_KIND_ISO_2022_JP,
    NS_KIND_SHIFT_JIS,
    NS_KIND_EUC_KR,
    NS_KIND_REPLACEMENT,
    NS_KIND_UTF16BE,
    NS_KIND_UTF16LE,
    NS_KIND_X_USER_DEFINED,
} ns_encoding_kind;

struct ns_encoding {
    const char *name;
    ns_encoding_kind kind;
    const char *charset;
};

enum {
    NS_ENC_UTF_8,
    NS_ENC_IBM866,
    NS_ENC_ISO_8859_2,
    NS_ENC_ISO_8859_3,
    NS_ENC_ISO_8859_4,
    NS_ENC_ISO_8859_5,
    NS_ENC_ISO_8859_6,
    NS_ENC_ISO_8859_7,
    NS_ENC_ISO_8859_8,
    NS_ENC_ISO_8859_8_I,
    NS_ENC_ISO_8859_10,
    NS_ENC_ISO_8859_13,
    NS_ENC_ISO_8859_14,
    NS_ENC_ISO_8859_15,
    NS_ENC_ISO_8859_16,
    NS_ENC_KOI8_R,
    NS_ENC_KOI8_U,
    NS_ENC_MACINTOSH,
    NS_ENC_WINDOWS_874,
    NS_ENC_WINDOWS_1250,
    NS_ENC_WINDOWS_1251,
    NS_ENC_WINDOWS_1252,
    NS_ENC_WINDOWS_1253,
    NS_ENC_WINDOWS_1254,
    NS_ENC_WINDOWS_1255,
    NS_ENC_WINDOWS_1256,
    NS_ENC_WINDOWS_1257,
    NS_ENC_WINDOWS_1258,
    NS_ENC_X_MAC_CYRILLIC,
    NS_ENC_GBK,
    NS_ENC_GB18030,
    NS_ENC_BIG5,
    NS_ENC_EUC_JP,
    NS_ENC_ISO_2022_JP,
    NS_ENC_SHIFT_JIS,
    NS_ENC_EUC_KR,
    NS_ENC_REPLACEMENT,
    NS_ENC_UTF_16BE,
    NS_ENC_UTF_16LE,
    NS_ENC_X_USER_DEFINED,
    NS_ENC_COUNT
};

static const ns_encoding ns_encodings[NS_ENC_COUNT] = {
    [NS_ENC_UTF_8]          = { "UTF-8", NS_KIND_UTF8, NULL },
    [NS_ENC_IBM866]         = { "IBM866", NS_KIND_SINGLE_BYTE, "IBM866" },
    [NS_ENC_ISO_8859_2]     = { "ISO-8859-2", NS_KIND_SINGLE_BYTE, "ISO-8859-2" },
    [NS_ENC_ISO_8859_3]     = { "ISO-8859-3", NS_KIND_SINGLE_BYTE, "ISO-8859-3" },
    [NS_ENC_ISO_8859_4]     = { "ISO-8859-4", NS_KIND_SINGLE_BYTE, "ISO-8859-4" },
    [NS_ENC_ISO_8859_5]     = { "ISO-8859-5", NS_KIND_SINGLE_BYTE, "ISO-8859-5" },
    [NS_ENC_ISO_8859_6]     = { "ISO-8859-6", NS_KIND_SINGLE_BYTE, "ISO-8859-6" },
    [NS_ENC_ISO_8859_7]     = { "ISO-8859-7", NS_KIND_SINGLE_BYTE, "ISO-8859-7" },
    [NS_ENC_ISO_8859_8]     = { "ISO-8859-8", NS_KIND_SINGLE_BYTE, "ISO-8859-8" },
    [NS_ENC_ISO_8859_8_I]   = { "ISO-8859-8-I", NS_KIND_SINGLE_BYTE, "ISO-8859-8" },
    [NS_ENC_ISO_8859_10]    = { "ISO-8859-10", NS_KIND_SINGLE_BYTE, "ISO-8859-10" },
    [NS_ENC_ISO_8859_13]    = { "ISO-8859-13", NS_KIND_SINGLE_BYTE, "ISO-8859-13" },
    [NS_ENC_ISO_8859_14]    = { "ISO-8859-14", NS_KIND_SINGLE_BYTE, "ISO-8859-14" },
    [NS_ENC_ISO_8859_15]    = { "ISO-8859-15", NS_KIND_SINGLE_BYTE, "ISO-8859-15" },
    [NS_ENC_ISO_8859_16]    = { "ISO-8859-16", NS_KIND_SINGLE_BYTE, "ISO-8859-16" },
    [NS_ENC_KOI8_R]         = { "KOI8-R", NS_KIND_SINGLE_BYTE, "KOI8-R" },
    [NS_ENC_KOI8_U]         = { "KOI8-U", NS_KIND_SINGLE_BYTE, "KOI8-U" },
    [NS_ENC_MACINTOSH]      = { "macintosh", NS_KIND_SINGLE_BYTE, "MACINTOSH" },
    [NS_ENC_WINDOWS_874]    = { "windows-874", NS_KIND_SINGLE_BYTE, "CP874" },
    [NS_ENC_WINDOWS_1250]   = { "windows-1250", NS_KIND_SINGLE_BYTE, "CP1250" },
    [NS_ENC_WINDOWS_1251]   = { "windows-1251", NS_KIND_SINGLE_BYTE, "CP1251" },
    [NS_ENC_WINDOWS_1252]   = { "windows-1252", NS_KIND_SINGLE_BYTE, "CP1252" },
    [NS_ENC_WINDOWS_1253]   = { "windows-1253", NS_KIND_SINGLE_BYTE, "CP1253" },
    [NS_ENC_WINDOWS_1254]   = { "windows-1254", NS_KIND_SINGLE_BYTE, "CP1254" },
    [NS_ENC_WINDOWS_1255]   = { "windows-1255", NS_KIND_SINGLE_BYTE, "CP1255" },
    [NS_ENC_WINDOWS_1256]   = { "windows-1256", NS_KIND_SINGLE_BYTE, "CP1256" },
    [NS_ENC_WINDOWS_1257]   = { "windows-1257", NS_KIND_SINGLE_BYTE, "CP1257" },
    [NS_ENC_WINDOWS_1258]   = { "windows-1258", NS_KIND_SINGLE_BYTE, "CP1258" },
    [NS_ENC_X_MAC_CYRILLIC] = { "x-mac-cyrillic", NS_KIND_SINGLE_BYTE, "MACCYRILLIC" },
    [NS_ENC_GBK]            = { "GBK", NS_KIND_GB18030, NULL },
    [NS_ENC_GB18030]        = { "gb18030", NS_KIND_GB18030, NULL },
    [NS_ENC_BIG5]           = { "Big5", NS_KIND_BIG5, NULL },
    [NS_ENC_EUC_JP]         = { "EUC-JP", NS_KIND_EUC_JP, NULL },
    [NS_ENC_ISO_2022_JP]    = { "ISO-2022-JP", NS_KIND_ISO_2022_JP, NULL },
    [NS_ENC_SHIFT_JIS]      = { "Shift_JIS", NS_KIND_SHIFT_JIS, NULL },
    [NS_ENC_EUC_KR]         = { "EUC-KR", NS_KIND_EUC_KR, NULL },
    [NS_ENC_REPLACEMENT]    = { "replacement", NS_KIND_REPLACEMENT, NULL },
    [NS_ENC_UTF_16BE]       = { "UTF-16BE", NS_KIND_UTF16BE, NULL },
    [NS_ENC_UTF_16LE]       = { "UTF-16LE", NS_KIND_UTF16LE, NULL },
    [NS_ENC_X_USER_DEFINED] = { "x-user-defined", NS_KIND_X_USER_DEFINED, NULL },
};

static const struct {
    const char *label;
    guint8 id;
} ns_encoding_labels[] = {
    { "866", NS_ENC_IBM866 },
    { "ansi_x3.4-1968", NS_ENC_WINDOWS_1252 },
    { "arabic", NS_ENC_ISO_8859_6 },
    { "ascii", NS_ENC_WINDOWS_1252 },
    { "asmo-708", NS_ENC_ISO_8859_6 },
    { "big5", NS_ENC_BIG5 },
    { "big5-hkscs", NS_ENC_BIG5 },
    { "chinese", NS_ENC_GBK },
    { "cn-big5", NS_ENC_BIG5 },
    { "cp1250", NS_ENC_WINDOWS_1250 },
    { "cp1251", NS_ENC_WINDOWS_1251 },
    { "cp1252", NS_ENC_WINDOWS_1252 },
    { "cp1253", NS_ENC_WINDOWS_1253 },
    { "cp1254", NS_ENC_WINDOWS_1254 },
    { "cp1255", NS_ENC_WINDOWS_1255 },
    { "cp1256", NS_ENC_WINDOWS_1256 },
    { "cp1257", NS_ENC_WINDOWS_1257 },
    { "cp1258", NS_ENC_WINDOWS_1258 },
    { "cp819", NS_ENC_WINDOWS_1252 },
    { "cp866", NS_ENC_IBM866 },
    { "csbig5", NS_ENC_BIG5 },
    { "cseuckr", NS_ENC_EUC_KR },
    { "cseucpkdfmtjapanese", NS_ENC_EUC_JP },
    { "csgb2312", NS_ENC_GBK },
    { "csibm866", NS_ENC_IBM866 },
    { "csiso2022jp", NS_ENC_ISO_2022_JP },
    { "csiso2022kr", NS_ENC_REPLACEMENT },
    { "csiso58gb231280", NS_ENC_GBK },
    { "csiso88596e", NS_ENC_ISO_8859_6 },
    { "csiso88596i", NS_ENC_ISO_8859_6 },
    { "csiso88598e", NS_ENC_ISO_8859_8 },
    { "csiso88598i", NS_ENC_ISO_8859_8_I },
    { "csisolatin1", NS_ENC_WINDOWS_1252 },
    { "csisolatin2", NS_ENC_ISO_8859_2 },
    { "csisolatin3", NS_ENC_ISO_8859_3 },
    { "csisolatin4", NS_ENC_ISO_8859_4 },
    { "csisolatin5", NS_ENC_WINDOWS_1254 },
    { "csisolatin6", NS_ENC_ISO_8859_10 },
    { "csisolatin9", NS_ENC_ISO_8859_15 },
    { "csisolatinarabic", NS_ENC_ISO_8859_6 },
    { "csisolatincyrillic", NS_ENC_ISO_8859_5 },
    { "csisolatingreek", NS_ENC_ISO_8859_7 },
    { "csisolatinhebrew", NS_ENC_ISO_8859_8 },
    { "cskoi8r", NS_ENC_KOI8_R },
    { "csksc56011987", NS_ENC_EUC_KR },
    { "csmacintosh", NS_ENC_MACINTOSH },
    { "csshiftjis", NS_ENC_SHIFT_JIS },
    { "csunicode", NS_ENC_UTF_16LE },
    { "cyrillic", NS_ENC_ISO_8859_5 },
    { "dos-874", NS_ENC_WINDOWS_874 },
    { "ecma-114", NS_ENC_ISO_8859_6 },
    { "ecma-118", NS_ENC_ISO_8859_7 },
    { "elot_928", NS_ENC_ISO_8859_7 },
    { "euc-jp", NS_ENC_EUC_JP },
    { "euc-kr", NS_ENC_EUC_KR },
    { "gb18030", NS_ENC_GB18030 },
    { "gb2312", NS_ENC_GBK },
    { "gb_2312", NS_ENC_GBK },
    { "gb_2312-80", NS_ENC_GBK },
    { "gbk", NS_ENC_GBK },
    { "greek", NS_ENC_ISO_8859_7 },
    { "greek8", NS_ENC_ISO_8859_7 },
    { "hebrew", NS_ENC_ISO_8859_8 },
    { "hz-gb-2312", NS_ENC_REPLACEMENT },
    { "ibm819", NS_ENC_WINDOWS_1252 },
    { "ibm866", NS_ENC_IBM866 },
    { "iso-10646-ucs-2", NS_ENC_UTF_16LE },
    { "iso-2022-cn", NS_ENC_REPLACEMENT },
    { "iso-2022-cn-ext", NS_ENC_REPLACEMENT },
    { "iso-2022-jp", NS_ENC_ISO_2022_JP },
    { "iso-2022-kr", NS_ENC_REPLACEMENT },
    { "iso-8859-1", NS_ENC_WINDOWS_1252 },
    { "iso-8859-10", NS_ENC_ISO_8859_10 },
    { "iso-8859-11", NS_ENC_WINDOWS_874 },
    { "iso-8859-13", NS_ENC_ISO_8859_13 },
    { "iso-8859-14", NS_ENC_ISO_8859_14 },
    { "iso-8859-15", NS_ENC_ISO_8859_15 },
    { "iso-8859-16", NS_ENC_ISO_8859_16 },
    { "iso-8859-2", NS_ENC_ISO_8859_2 },
    { "iso-8859-3", NS_ENC_ISO_8859_3 },
    { "iso-8859-4", NS_ENC_ISO_8859_4 },
    { "iso-8859-5", NS_ENC_ISO_8859_5 },
    { "iso-8859-6", NS_ENC_ISO_8859_6 },
    { "iso-8859-6-e", NS_ENC_ISO_8859_6 },
    { "iso-8859-6-i", NS_ENC_ISO_8859_6 },
    { "iso-8859-7", NS_ENC_ISO_8859_7 },
    { "iso-8859-8", NS_ENC_ISO_8859_8 },
    { "iso-8859-8-e", NS_ENC_ISO_8859_8 },
    { "iso-8859-8-i", NS_ENC_ISO_8859_8_I },
    { "iso-8859-9", NS_ENC_WINDOWS_1254 },
    { "iso-ir-100", NS_ENC_WINDOWS_1252 },
    { "iso-ir-101", NS_ENC_ISO_8859_2 },
    { "iso-ir-109", NS_ENC_ISO_8859_3 },
    { "iso-ir-110", NS_ENC_ISO_8859_4 },
    { "iso-ir-126", NS_ENC_ISO_8859_7 },
    { "iso-ir-127", NS_ENC_ISO_8859_6 },
    { "iso-ir-138", NS_ENC_ISO_8859_8 },
    { "iso-ir-144", NS_ENC_ISO_8859_5 },
    { "iso-ir-148", NS_ENC_WINDOWS_1254 },
    { "iso-ir-149", NS_ENC_EUC_KR },
    { "iso-ir-157", NS_ENC_ISO_8859_10 },
    { "iso-ir-58", NS_ENC_GBK },
    { "iso8859-1", NS_ENC_WINDOWS_1252 },
    { "iso8859-10", NS_ENC_ISO_8859_10 },
    { "iso8859-11", NS_ENC_WINDOWS_874 },
    { "iso8859-13", NS_ENC_ISO_8859_13 },
    { "iso8859-14", NS_ENC_ISO_8859_14 },
    { "iso8859-15", NS_ENC_ISO_8859_15 },
    { "iso8859-2", NS_ENC_ISO_8859_2 },
    { "iso8859-3", NS_ENC_ISO_8859_3 },
    { "iso8859-4", NS_ENC_ISO_8859_4 },
    { "iso8859-5", NS_ENC_ISO_8859_5 },
    { "iso8859-6", NS_ENC_ISO_8859_6 },
    { "iso8859-7", NS_ENC_ISO_8859_7 },
    { "iso8859-8", NS_ENC_ISO_8859_8 },
    { "iso8859-9", NS_ENC_WINDOWS_1254 },
    { "iso88591", NS_ENC_WINDOWS_1252 },
    { "iso885910", NS_ENC_ISO_8859_10 },
    { "iso885911", NS_ENC_WINDOWS_874 },
    { "iso885913", NS_ENC_ISO_8859_13 },
    { "iso885914", NS_ENC_ISO_8859_14 },
    { "iso885915", NS_ENC_ISO_8859_15 },
    { "iso88592", NS_ENC_ISO_8859_2 },
    { "iso88593", NS_ENC_ISO_8859_3 },
    { "iso88594", NS_ENC_ISO_8859_4 },
    { "iso88595", NS_ENC_ISO_8859_5 },
    { "iso88596", NS_ENC_ISO_8859_6 },
    { "iso88597", NS_ENC_ISO_8859_7 },
    { "iso88598", NS_ENC_ISO_8859_8 },
    { "iso88599", NS_ENC_WINDOWS_1254 },
    { "iso_8859-1", NS_ENC_WINDOWS_1252 },
    { "iso_8859-15", NS_ENC_ISO_8859_15 },
    { "iso_8859-1:1987", NS_ENC_WINDOWS_1252 },
    { "iso_8859-2", NS_ENC_ISO_8859_2 },
    { "iso_8859-2:1987", NS_ENC_ISO_8859_2 },
    { "iso_8859-3", NS_ENC_ISO_8859_3 },
    { "iso_8859-3:1988", NS_ENC_ISO_8859_3 },
    { "iso_8859-4", NS_ENC_ISO_8859_4 },
    { "iso_8859-4:1988", NS_ENC_ISO_8859_4 },
    { "iso_8859-5", NS_ENC_ISO_8859_5 },
    { "iso_8859-5:1988", NS_ENC_ISO_8859_5 },
    { "iso_8859-6", NS_ENC_ISO_8859_6 },
    { "iso_8859-6:1987", NS_ENC_ISO_8859_6 },
    { "iso_8859-7", NS_ENC_ISO_8859_7 },
    { "iso_8859-7:1987", NS_ENC_ISO_8859_7 },
    { "iso_8859-8", NS_ENC_ISO_8859_8 },
    { "iso_8859-8:1988", NS_ENC_ISO_8859_8 },
    { "iso_8859-9", NS_ENC_WINDOWS_1254 },
    { "iso_8859-9:1989", NS_ENC_WINDOWS_1254 },
    { "koi", NS_ENC_KOI8_R },
    { "koi8", NS_ENC_KOI8_R },
    { "koi8-r", NS_ENC_KOI8_R },
    { "koi8-ru", NS_ENC_KOI8_U },
    { "koi8-u", NS_ENC_KOI8_U },
    { "koi8_r", NS_ENC_KOI8_R },
    { "korean", NS_ENC_EUC_KR },
    { "ks_c_5601-1987", NS_ENC_EUC_KR },
    { "ks_c_5601-1989", NS_ENC_EUC_KR },
    { "ksc5601", NS_ENC_EUC_KR },
    { "ksc_5601", NS_ENC_EUC_KR },
    { "l1", NS_ENC_WINDOWS_1252 },
    { "l2", NS_ENC_ISO_8859_2 },
    { "l3", NS_ENC_ISO_8859_3 },
    { "l4", NS_ENC_ISO_8859_4 },
    { "l5", NS_ENC_WINDOWS_1254 },
    { "l6", NS_ENC_ISO_8859_10 },
    { "l9", NS_ENC_ISO_8859_15 },
    { "latin1", NS_ENC_WINDOWS_1252 },
    { "latin2", NS_ENC_ISO_8859_2 },
    { "latin3", NS_ENC_ISO_8859_3 },
    { "latin4", NS_ENC_ISO_8859_4 },
    { "latin5", NS_ENC_WINDOWS_1254 },
    { "latin6", NS_ENC_ISO_8859_10 },
    { "logical", NS_ENC_ISO_8859_8_I },
    { "mac", NS_ENC_MACINTOSH },
    { "macintosh", NS_ENC_MACINTOSH },
    { "ms932", NS_ENC_SHIFT_JIS },
    { "ms_kanji", NS_ENC_SHIFT_JIS },
    { "replacement", NS_ENC_REPLACEMENT },
    { "shift-jis", NS_ENC_SHIFT_JIS },
    { "shift_jis", NS_ENC_SHIFT_JIS },
    { "sjis", NS_ENC_SHIFT_JIS },
    { "sun_eu_greek", NS_ENC_ISO_8859_7 },
    { "tis-620", NS_ENC_WINDOWS_874 },
    { "ucs-2", NS_ENC_UTF_16LE },
    { "unicode", NS_ENC_UTF_16LE },
    { "unicode-1-1-utf-8", NS_ENC_UTF_8 },
    { "unicode11utf8", NS_ENC_UTF_8 },
    { "unicode20utf8", NS_ENC_UTF_8 },
    { "unicodefeff", NS_ENC_UTF_16LE },
    { "unicodefffe", NS_ENC_UTF_16BE },
    { "us-ascii", NS_ENC_WINDOWS_1252 },
    { "utf-16", NS_ENC_UTF_16LE },
    { "utf-16be", NS_ENC_UTF_16BE },
    { "utf-16le", NS_ENC_UTF_16LE },
    { "utf-8", NS_ENC_UTF_8 },
    { "utf8", NS_ENC_UTF_8 },
    { "visual", NS_ENC_ISO_8859_8 },
    { "windows-1250", NS_ENC_WINDOWS_1250 },
    { "windows-1251", NS_ENC_WINDOWS_1251 },
    { "windows-1252", NS_ENC_WINDOWS_1252 },
    { "windows-1253", NS_ENC_WINDOWS_1253 },
    { "windows-1254", NS_ENC_WINDOWS_1254 },
    { "windows-1255", NS_ENC_WINDOWS_1255 },
    { "windows-1256", NS_ENC_WINDOWS_1256 },
    { "windows-1257", NS_ENC_WINDOWS_1257 },
    { "windows-1258", NS_ENC_WINDOWS_1258 },
    { "windows-31j", NS_ENC_SHIFT_JIS },
    { "windows-874", NS_ENC_WINDOWS_874 },
    { "windows-949", NS_ENC_EUC_KR },
    { "x-cp1250", NS_ENC_WINDOWS_1250 },
    { "x-cp1251", NS_ENC_WINDOWS_1251 },
    { "x-cp1252", NS_ENC_WINDOWS_1252 },
    { "x-cp1253", NS_ENC_WINDOWS_1253 },
    { "x-cp1254", NS_ENC_WINDOWS_1254 },
    { "x-cp1255", NS_ENC_WINDOWS_1255 },
    { "x-cp1256", NS_ENC_WINDOWS_1256 },
    { "x-cp1257", NS_ENC_WINDOWS_1257 },
    { "x-cp1258", NS_ENC_WINDOWS_1258 },
    { "x-euc-jp", NS_ENC_EUC_JP },
    { "x-gbk", NS_ENC_GBK },
    { "x-mac-cyrillic", NS_ENC_X_MAC_CYRILLIC },
    { "x-mac-roman", NS_ENC_MACINTOSH },
    { "x-mac-ukrainian", NS_ENC_X_MAC_CYRILLIC },
    { "x-sjis", NS_ENC_SHIFT_JIS },
    { "x-unicode20utf8", NS_ENC_UTF_8 },
    { "x-user-defined", NS_ENC_X_USER_DEFINED },
    { "x-x-big5", NS_ENC_BIG5 },
};

static const struct {
    guint8 id;
    guint8 byte;
    guint16 code_point;
} ns_single_byte_fixups[] = {
    { NS_ENC_KOI8_U, 0xAE, 0x045E },
    { NS_ENC_KOI8_U, 0xBE, 0x040E },
    { NS_ENC_MACINTOSH, 0xC6, 0x2206 },
    { NS_ENC_MACINTOSH, 0xF0, 0xF8FF },
    { NS_ENC_WINDOWS_1255, 0xCA, 0x05BA },
    { NS_ENC_X_MAC_CYRILLIC, 0xFF, 0x20AC },
};

typedef enum {
    NS_INDEX_JIS0208,
    NS_INDEX_JIS0212,
    NS_INDEX_EUC_KR,
    NS_INDEX_GB18030,
    NS_INDEX_BIG5,
    NS_INDEX_COUNT
} ns_index_id;

static const struct {
    const char *charset;
    guint size;
} ns_index_sources[NS_INDEX_COUNT] = {
    [NS_INDEX_JIS0208] = { "CP932", 11280 },
    [NS_INDEX_JIS0212] = { "EUC-JP", 8836 },
    [NS_INDEX_EUC_KR]  = { "CP949", 23940 },
    [NS_INDEX_GB18030] = { "GB18030", 23940 },
    [NS_INDEX_BIG5]    = { "BIG5-HKSCS", 19782 },
};

static const struct {
    guint16 pointer;
    guint16 code_point;
    guint8 count;
} ns_big5_fixups[] = {
    { 2082, 0x7BB8, 1 }, { 2088, 0x7C06, 1 }, { 2103, 0x7CCE, 1 },
    { 2114, 0x7DD2, 1 }, { 2123, 0x7E1D, 1 }, { 2148, 0x8005, 1 },
    { 2151, 0x8028, 1 }, { 2221, 0x83C1, 1 }, { 2239, 0x84A8, 1 },
    { 2244, 0x840F, 1 }, { 2303, 0x89A6, 1 }, { 2304, 0x89A9, 1 },
    { 2354, 0x8D77, 1 }, { 2400, 0x90FD, 1 }, { 2413, 0x92B9, 1 },
    { 2477, 0x975C, 1 }, { 2498, 0x97FF, 1 }, { 2605, 0x9F16, 1 },
    { 2673, 0x8503, 1 }, { 2746, 0x5159, 1 }, { 2747, 0x515B, 1 },
    { 2748, 0x515D, 2 }, { 2771, 0x936E, 1 }, { 2780, 0x7479, 1 },
    { 2990, 0x6D67, 1 }, { 3087, 0x799B, 1 }, { 3259, 0x9097, 1 },
    { 3301, 0x975D, 1 }, { 3436, 0x701E, 1 }, { 3451, 0x5B28, 1 },
    { 4136, 0x7201, 1 }, { 4138, 0x77D7, 1 }, { 4141, 0x7E87, 1 },
    { 4182, 0x99D6, 1 }, { 4206, 0x91D4, 1 }, { 4220, 0x60DE, 1 },
    { 4230, 0x6FB6, 1 }, { 4241, 0x8F36, 1 }, { 4258, 0x4FBB, 1 },
    { 4273, 0x71DF, 1 }, { 4279, 0x9104, 1 }, { 4282, 0x9DF0, 1 },
    { 4294, 0x83CF, 1 }, { 4329, 0x5C10, 1 }, { 4330, 0x79E3, 1 },
    { 4349, 0x5A67, 1 }, { 4419, 0x8F0B, 1 }, { 4422, 0x7B51, 1 },
    { 4494, 0x62D0, 1 }, { 4624, 0x6062, 1 }, { 4694, 0x75F9, 1 },
    { 4708, 0x6C4A, 1 }, { 4742, 0x9B2E, 1 }, { 4748, 0x9F17, 1 },
    { 4815, 0x50ED, 1 }, { 4828, 0x5F0C, 1 }, { 4902, 0x880F, 1 },
    { 4922, 0x62CE, 1 }, { 4982, 0x7468, 1 }, { 4992, 0x7162, 1 },
    { 4997, 0x7250, 1 }, { 5029, 0x2027, 1 }, { 5038, 0xFE51, 1 },
    { 5050, 0x2574, 1 }, { 5120, 0x00AF, 1 }, { 5121, 0xFFE3, 1 },
    { 5123, 0x02CD, 1 }, { 5153, 0xFF5E, 1 }, { 5168, 0x2295, 1 },
    { 5169, 0x2299, 1 }, { 5180, 0xFF0F, 1 }, { 5181, 0xFF3C, 1 },
    { 5182, 0x2215, 1 }, { 5183, 0xFE68, 1 }, { 5185, 0xFFE5, 1 },
    { 5187, 0xFFE0, 2 }, { 5287, 0x5341, 1 }, { 5289, 0x5345, 1 },
    { 5432, 0x2400, 32 }, { 5464, 0x2421, 1 }, { 5465, 0x20AC, 1 },
    { 10942, 0x5EF4, 1 }, { 10946, 0x65E0, 1 }, { 10948, 0x7676, 1 },
    { 10950, 0x96B6, 1 }, { 10957, 0x3003, 1 }, { 10958, 0x4EDD, 1 },
    { 19028, 0x5029, 1 }, { 19035, 0x507D, 1 }, { 19088, 0x5305, 1 },
    { 19096, 0x5344, 1 }, { 19112, 0x537F, 1 }, { 19162, 0x5605, 1 },
    { 19240, 0x5A77, 1 }, { 19299, 0x5E75, 1 }, { 19305, 0x5ED0, 1 },
    { 19326, 0x5F58, 1 }, { 19355, 0x60A4, 1 }, { 19398, 0x6490, 1 },
    { 19439, 0x6674, 1 }, { 19454, 0x675E, 1 }, { 19553, 0x6C9C, 1 },
    { 19554, 0x6E1D, 1 }, { 19557, 0x6E2F, 1 }, { 19611, 0x716E, 1 },
    { 19643, 0x732A, 1 }, { 19672, 0x745C, 1 }, { 19697, 0x74E9, 1 },
    { 19748, 0x7809, 1 },
};

static const struct {
    guint32 pointer;
    guint32 code_point;
} ns_gb18030_ranges[] = {
    { 0, 0x0080 }, { 36, 0x00A5 }, { 38, 0x00A9 }, { 45, 0x00B2 },
    { 50, 0x00B8 }, { 81, 0x00D8 }, { 89, 0x00E2 }, { 95, 0x00EB },
    { 96, 0x00EE }, { 100, 0x00F4 }, { 103, 0x00F8 }, { 104, 0x00FB },
    { 105, 0x00FD }, { 109, 0x0102 }, { 126, 0x0114 }, { 133, 0x011C },
    { 148, 0x012C }, { 172, 0x0145 }, { 175, 0x0149 }, { 179, 0x014E },
    { 208, 0x016C }, { 306, 0x01CF }, { 307, 0x01D1 }, { 308, 0x01D3 },
    { 309, 0x01D5 }, { 310, 0x01D7 }, { 311, 0x01D9 }, { 312, 0x01DB },
    { 313, 0x01DD }, { 341, 0x01FA }, { 428, 0x0252 }, { 443, 0x0262 },
    { 544, 0x02C8 }, { 545, 0x02CC }, { 558, 0x02DA }, { 741, 0x03A2 },
    { 742, 0x03AA }, { 749, 0x03C2 }, { 750, 0x03CA }, { 805, 0x0402 },
    { 819, 0x0450 }, { 820, 0x0452 }, { 7922, 0x2011 }, { 7924, 0x2017 },
    { 7925, 0x201A }, { 7927, 0x201E }, { 7934, 0x2027 }, { 7943, 0x2031 },
    { 7944, 0x2034 }, { 7945, 0x2036 }, { 7950, 0x203C }, { 8062, 0x20AD },
    { 8148, 0x2104 }, { 8149, 0x2106 }, { 8152, 0x210A }, { 8164, 0x2117 },
    { 8174, 0x2122 }, { 8236, 0x216C }, { 8240, 0x217A }, { 8262, 0x2194 },
    { 8264, 0x219A }, { 8374, 0x2209 }, { 8380, 0x2210 }, { 8381, 0x2212 },
    { 8384, 0x2216 }, { 8388, 0x221B }, { 8390, 0x2221 }, { 8392, 0x2224 },
    { 8393, 0x2226 }, { 8394, 0x222C }, { 8396, 0x222F }, { 8401, 0x2238 },
    { 8406, 0x223E }, { 8416, 0x2249 }, { 8419, 0x224D }, { 8424, 0x2253 },
    { 8437, 0x2262 }, { 8439, 0x2268 }, { 8445, 0x2270 }, { 8482, 0x2296 },
    { 8485, 0x229A }, { 8496, 0x22A6 }, { 8521, 0x22C0 }, { 8603, 0x2313 },
    { 8936, 0x246A }, { 8946, 0x249C }, { 9046, 0x254C }, { 9050, 0x2574 },
    { 9063, 0x2590 }, { 9066, 0x2596 }, { 9076, 0x25A2 }, { 9092, 0x25B4 },
    { 9100, 0x25BE }, { 9108, 0x25C8 }, { 9111, 0x25CC }, { 9113, 0x25D0 },
    { 9131, 0x25E6 }, { 9162, 0x2607 }, { 9164, 0x260A }, { 9218, 0x2641 },
    { 9219, 0x2643 }, { 11329, 0x2E82 }, { 11331, 0x2E85 }, { 11334, 0x2E89 },
    { 11336, 0x2E8D }, { 11346, 0x2E98 }, { 11361, 0x2EA8 },
    { 11363, 0x2EAB }, { 11366, 0x2EAF }, { 11370, 0x2EB4 },
    { 11372, 0x2EB8 }, { 11375, 0x2EBC }, { 11389, 0x2ECB },
    { 11682, 0x2FFC }, { 11686, 0x3004 }, { 11687, 0x3018 },
    { 11692, 0x301F }, { 11694, 0x302A }, { 11714, 0x303F },
    { 11716, 0x3094 }, { 11723, 0x309F }, { 11725, 0x30F7 },
    { 11730, 0x30FF }, { 11736, 0x312A }, { 11982, 0x322A },
    { 11989, 0x3232 }, { 12102, 0x32A4 }, { 12336, 0x3390 },
    { 12348, 0x339F }, { 12350, 0x33A2 }, { 12384, 0x33C5 },
    { 12393, 0x33CF }, { 12395, 0x33D3 }, { 12397, 0x33D6 },
    { 12510, 0x3448 }, { 12553, 0x3474 }, { 12851, 0x359F },
    { 12962, 0x360F }, { 12973, 0x361B }, { 13738, 0x3919 },
    { 13823, 0x396F }, { 13919, 0x39D1 }, { 13933, 0x39E0 },
    { 14080, 0x3A74 }, { 14298, 0x3B4F }, { 14585, 0x3C6F },
    { 14698, 0x3CE1 }, { 15583, 0x4057 }, { 15847, 0x4160 },
    { 16318, 0x4338 }, { 16434, 0x43AD }, { 16438, 0x43B2 },
    { 16481, 0x43DE }, { 16729, 0x44D7 }, { 17102, 0x464D },
    { 17122, 0x4662 }, { 17315, 0x4724 }, { 17320, 0x472A },
    { 17402, 0x477D }, { 17418, 0x478E }, { 17859, 0x4948 },
    { 17909, 0x497B }, { 17911, 0x497E }, { 17915, 0x4984 },
    { 17916, 0x4987 }, { 17936, 0x499C }, { 17939, 0x49A0 },
    { 17961, 0x49B8 }, { 18664, 0x4C78 }, { 18703, 0x4CA4 },
    { 18814, 0x4D1A }, { 18962, 0x4DAF }, { 19043, 0x9FA6 },
    { 33469, 0xE76C }, { 33470, 0xE7C8 }, { 33471, 0xE7E7 },
    { 33484, 0xE815 }, { 33485, 0xE819 }, { 33490, 0xE81F },
    { 33497, 0xE827 }, { 33501, 0xE82D }, { 33505, 0xE833 },
    { 33513, 0xE83C }, { 33520, 0xE844 }, { 33536, 0xE856 },
    { 33550, 0xE865 }, { 37845, 0xF92D }, { 37921, 0xF97A },
    { 37948, 0xF996 }, { 38029, 0xF9E8 }, { 38038, 0xF9F2 },
    { 38064, 0xFA10 }, { 38065, 0xFA12 }, { 38066, 0xFA15 },
    { 38069, 0xFA19 }, { 38075, 0xFA22 }, { 38076, 0xFA25 },
    { 38078, 0xFA2A }, { 39108, 0xFE32 }, { 39109, 0xFE45 },
    { 39113, 0xFE53 }, { 39114, 0xFE58 }, { 39115, 0xFE67 },
    { 39116, 0xFE6C }, { 39265, 0xFF5F }, { 39394, 0xFFE6 },
    { 189000, 0x10000 },
};

static guint16 ns_single_byte_tables[NS_ENC_COUNT][128];
static gsize ns_single_byte_ready[NS_ENC_COUNT];
static guint32 *ns_index_tables[NS_INDEX_COUNT];
static gsize ns_index_ready[NS_INDEX_COUNT];

#define NS_EOS (-1)

enum {
    NS_STEP_CONTINUE = 0,
    NS_STEP_ERROR = -1,
    NS_STEP_FINISHED = -2,
};

enum {
    NS_ISO_ASCII,
    NS_ISO_ROMAN,
    NS_ISO_KATAKANA,
    NS_ISO_LEAD,
    NS_ISO_TRAIL,
    NS_ISO_ESCAPE_START,
    NS_ISO_ESCAPE,
};

struct ns_decoder {
    const ns_encoding *enc;
    gboolean fatal;
    guint8 pending[8];
    guint npending;
    guint32 code_point;
    guint needed;
    guint seen;
    guint8 lower;
    guint8 upper;
    guint8 lead;
    guint8 second;
    guint8 third;
    int lead_byte;
    guint32 lead_surrogate;
    gboolean jis0212;
    gboolean output;
    gboolean replaced;
    guint8 state;
    guint8 output_state;
};

static guint32
iconv_code_point(GIConv cd, const guint8 *bytes, gsize n)
{
    guint8 buf[8];
    gchar *in = (gchar *)bytes, *out = (gchar *)buf;
    gsize in_left = n, out_left = sizeof buf;
    g_iconv(cd, NULL, NULL, NULL, NULL);
    if (g_iconv(cd, &in, &in_left, &out, &out_left) == (gsize)-1 ||
        in_left != 0)
        return 0;
    if (g_iconv(cd, NULL, NULL, &out, &out_left) == (gsize)-1)
        return 0;
    if (sizeof buf - out_left != 4) return 0;
    return (guint32)buf[0] | (guint32)buf[1] << 8 |
           (guint32)buf[2] << 16 | (guint32)buf[3] << 24;
}

static const guint16 *
single_byte_table(guint id)
{
    if (g_once_init_enter(&ns_single_byte_ready[id])) {
        guint16 *table = ns_single_byte_tables[id];
        const ns_encoding *enc = &ns_encodings[id];
        GIConv cd = g_iconv_open("UTF-32LE", enc->charset);
        gboolean windows = g_str_has_prefix(enc->name, "windows-");
        for (guint b = 0x80; b <= 0xFF; b++) {
            guint8 byte = (guint8)b;
            guint32 cp = cd != (GIConv)-1 ? iconv_code_point(cd, &byte, 1) : 0;
            if (cp == 0 && windows && b < 0xA0) cp = b;
            table[b - 0x80] = cp <= 0xFFFF ? (guint16)cp : 0;
        }
        if (cd != (GIConv)-1) g_iconv_close(cd);
        for (gsize i = 0; i < G_N_ELEMENTS(ns_single_byte_fixups); i++)
            if (ns_single_byte_fixups[i].id == id)
                table[ns_single_byte_fixups[i].byte - 0x80] =
                    ns_single_byte_fixups[i].code_point;
        g_once_init_leave(&ns_single_byte_ready[id], 1);
    }
    return ns_single_byte_tables[id];
}

static gsize
index_pointer_bytes(ns_index_id id, guint pointer, guint8 *b)
{
    guint trail;
    switch (id) {
    case NS_INDEX_JIS0208:
        b[0] = (guint8)(pointer / 188 < 0x1F ? pointer / 188 + 0x81
                                               : pointer / 188 + 0xC1);
        trail = pointer % 188;
        b[1] = (guint8)(trail < 0x3F ? trail + 0x40 : trail + 0x41);
        return 2;
    case NS_INDEX_JIS0212:
        b[0] = 0x8F;
        b[1] = (guint8)(pointer / 94 + 0xA1);
        b[2] = (guint8)(pointer % 94 + 0xA1);
        return 3;
    case NS_INDEX_EUC_KR:
        b[0] = (guint8)(pointer / 190 + 0x81);
        b[1] = (guint8)(pointer % 190 + 0x41);
        return 2;
    case NS_INDEX_GB18030:
        b[0] = (guint8)(pointer / 190 + 0x81);
        trail = pointer % 190;
        b[1] = (guint8)(trail < 0x3F ? trail + 0x40 : trail + 0x41);
        return 2;
    default:
        b[0] = (guint8)(pointer / 157 + 0x81);
        trail = pointer % 157;
        b[1] = (guint8)(trail < 0x3F ? trail + 0x40 : trail + 0x62);
        return 2;
    }
}

static void
index_apply_fixups(ns_index_id id, guint32 *table)
{
    if (id == NS_INDEX_JIS0208) {
        for (guint p = 8836; p <= 10715; p++) table[p] = 0;
    } else if (id == NS_INDEX_GB18030) {
        table[6555] = 0x3000;
    } else if (id == NS_INDEX_BIG5) {
        for (gsize i = 0; i < G_N_ELEMENTS(ns_big5_fixups); i++)
            for (guint k = 0; k < ns_big5_fixups[i].count; k++)
                table[ns_big5_fixups[i].pointer + k] =
                    ns_big5_fixups[i].code_point + k;
    }
}

static const guint32 *
index_table(ns_index_id id)
{
    if (g_once_init_enter(&ns_index_ready[id])) {
        guint size = ns_index_sources[id].size;
        guint32 *table = g_new0(guint32, size);
        GIConv cd = g_iconv_open("UTF-32LE", ns_index_sources[id].charset);
        if (cd != (GIConv)-1) {
            for (guint p = 0; p < size; p++) {
                guint8 bytes[3];
                gsize n = index_pointer_bytes(id, p, bytes);
                table[p] = iconv_code_point(cd, bytes, n);
            }
            g_iconv_close(cd);
        }
        index_apply_fixups(id, table);
        ns_index_tables[id] = table;
        g_once_init_leave(&ns_index_ready[id], 1);
    }
    return ns_index_tables[id];
}

static guint32
index_code_point(ns_index_id id, guint pointer)
{
    if (pointer >= ns_index_sources[id].size) return 0;
    return index_table(id)[pointer];
}

static guint32
gb18030_ranges_code_point(guint32 pointer)
{
    if ((pointer > 39419 && pointer < 189000) || pointer > 1237575) return 0;
    if (pointer == 7457) return 0xE7C7;
    gsize lo = 0, hi = G_N_ELEMENTS(ns_gb18030_ranges);
    while (hi - lo > 1) {
        gsize mid = lo + (hi - lo) / 2;
        if (ns_gb18030_ranges[mid].pointer <= pointer) lo = mid;
        else hi = mid;
    }
    return ns_gb18030_ranges[lo].code_point + pointer -
           ns_gb18030_ranges[lo].pointer;
}

const ns_encoding *
ns_encoding_for_label(const char *label)
{
    if (!label) return NULL;
    while (*label == ' ' || *label == '\t' || *label == '\n' ||
           *label == '\f' || *label == '\r')
        label++;
    gsize len = strlen(label);
    while (len > 0 && (label[len - 1] == ' ' || label[len - 1] == '\t' ||
                       label[len - 1] == '\n' || label[len - 1] == '\f' ||
                       label[len - 1] == '\r'))
        len--;
    for (gsize i = 0; i < G_N_ELEMENTS(ns_encoding_labels); i++) {
        if (strlen(ns_encoding_labels[i].label) == len &&
            g_ascii_strncasecmp(ns_encoding_labels[i].label, label, len) == 0)
            return &ns_encodings[ns_encoding_labels[i].id];
    }
    return NULL;
}

const ns_encoding *
ns_encoding_for_name(const char *name)
{
    if (!name) return NULL;
    for (guint i = 0; i < NS_ENC_COUNT; i++)
        if (g_ascii_strcasecmp(ns_encodings[i].name, name) == 0)
            return &ns_encodings[i];
    return ns_encoding_for_label(name);
}

const ns_encoding *
ns_encoding_utf8(void)
{
    return &ns_encodings[NS_ENC_UTF_8];
}

const char *
ns_encoding_name(const ns_encoding *enc)
{
    return enc ? enc->name : "UTF-8";
}

gboolean
ns_encoding_is_utf8(const ns_encoding *enc)
{
    return enc && enc->kind == NS_KIND_UTF8;
}

gboolean
ns_encoding_is_utf16(const ns_encoding *enc)
{
    return enc && (enc->kind == NS_KIND_UTF16BE ||
                   enc->kind == NS_KIND_UTF16LE);
}

gboolean
ns_encoding_is_replacement(const ns_encoding *enc)
{
    return enc && enc->kind == NS_KIND_REPLACEMENT;
}

static void
prepend(ns_decoder *d, const guint8 *bytes, guint n)
{
    if (d->npending + n > sizeof d->pending) return;
    memmove(d->pending + n, d->pending, d->npending);
    memcpy(d->pending, bytes, n);
    d->npending += n;
}

static void
prepend_byte(ns_decoder *d, int byte)
{
    if (byte == NS_EOS) return;
    guint8 b = (guint8)byte;
    prepend(d, &b, 1);
}

static int
utf8_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) {
        if (d->needed == 0) return NS_STEP_FINISHED;
        ns_decoder_reset(d);
        return NS_STEP_ERROR;
    }
    if (d->needed == 0) {
        if (byte <= 0x7F) {
            out[0] = (guint32)byte;
            return 1;
        }
        if (byte >= 0xC2 && byte <= 0xDF) {
            d->needed = 1;
            d->code_point = byte & 0x1F;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            if (byte == 0xE0) d->lower = 0xA0;
            if (byte == 0xED) d->upper = 0x9F;
            d->needed = 2;
            d->code_point = byte & 0xF;
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            if (byte == 0xF0) d->lower = 0x90;
            if (byte == 0xF4) d->upper = 0x8F;
            d->needed = 3;
            d->code_point = byte & 0x7;
        } else {
            return NS_STEP_ERROR;
        }
        return NS_STEP_CONTINUE;
    }
    if (byte < d->lower || byte > d->upper) {
        d->code_point = 0;
        d->needed = d->seen = 0;
        d->lower = 0x80;
        d->upper = 0xBF;
        prepend_byte(d, byte);
        return NS_STEP_ERROR;
    }
    d->lower = 0x80;
    d->upper = 0xBF;
    d->code_point = (d->code_point << 6) | (guint32)(byte & 0x3F);
    if (++d->seen != d->needed) return NS_STEP_CONTINUE;
    out[0] = d->code_point;
    d->code_point = 0;
    d->needed = d->seen = 0;
    return 1;
}

static int
utf16_step(ns_decoder *d, int byte, gboolean be, guint32 *out)
{
    if (byte == NS_EOS) {
        if (d->lead_byte < 0 && !d->lead_surrogate) return NS_STEP_FINISHED;
        d->lead_byte = -1;
        d->lead_surrogate = 0;
        return NS_STEP_ERROR;
    }
    if (d->lead_byte < 0) {
        d->lead_byte = byte;
        return NS_STEP_CONTINUE;
    }
    guint32 unit = be ? ((guint32)d->lead_byte << 8) + (guint32)byte
                      : ((guint32)byte << 8) + (guint32)d->lead_byte;
    d->lead_byte = -1;
    if (d->lead_surrogate) {
        guint32 lead = d->lead_surrogate;
        d->lead_surrogate = 0;
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            out[0] = 0x10000 + ((lead - 0xD800) << 10) + (unit - 0xDC00);
            return 1;
        }
        guint8 bytes[2] = { (guint8)(be ? unit >> 8 : unit & 0xFF),
                            (guint8)(be ? unit & 0xFF : unit >> 8) };
        prepend(d, bytes, 2);
        return NS_STEP_ERROR;
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
        d->lead_surrogate = unit;
        return NS_STEP_CONTINUE;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF) return NS_STEP_ERROR;
    out[0] = unit;
    return 1;
}

static int
single_byte_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) return NS_STEP_FINISHED;
    if (byte < 0x80) {
        out[0] = (guint32)byte;
        return 1;
    }
    guint16 cp = single_byte_table((guint)(d->enc - ns_encodings))[byte - 0x80];
    if (!cp) return NS_STEP_ERROR;
    out[0] = cp;
    return 1;
}

static int
gb18030_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) {
        if (!d->lead && !d->second && !d->third) return NS_STEP_FINISHED;
        d->lead = d->second = d->third = 0;
        return NS_STEP_ERROR;
    }
    if (d->third) {
        if (byte < 0x30 || byte > 0x39) {
            guint8 bytes[3] = { d->second, d->third, (guint8)byte };
            prepend(d, bytes, 3);
            d->lead = d->second = d->third = 0;
            return NS_STEP_ERROR;
        }
        guint32 pointer = (((guint32)(d->lead - 0x81) * 10 + d->second - 0x30)
                           * 126 + d->third - 0x81) * 10 + (guint32)byte - 0x30;
        d->lead = d->second = d->third = 0;
        guint32 cp = gb18030_ranges_code_point(pointer);
        if (!cp) return NS_STEP_ERROR;
        out[0] = cp;
        return 1;
    }
    if (d->second) {
        if (byte >= 0x81 && byte <= 0xFE) {
            d->third = (guint8)byte;
            return NS_STEP_CONTINUE;
        }
        guint8 bytes[2] = { d->second, (guint8)byte };
        prepend(d, bytes, 2);
        d->lead = d->second = 0;
        return NS_STEP_ERROR;
    }
    if (d->lead) {
        if (byte >= 0x30 && byte <= 0x39) {
            d->second = (guint8)byte;
            return NS_STEP_CONTINUE;
        }
        guint lead = d->lead;
        d->lead = 0;
        guint offset = byte < 0x7F ? 0x40 : 0x41;
        guint32 cp = 0;
        if ((byte >= 0x40 && byte <= 0x7E) || (byte >= 0x80 && byte <= 0xFE))
            cp = index_code_point(NS_INDEX_GB18030,
                                  (lead - 0x81) * 190 + (guint)byte - offset);
        if (cp) {
            out[0] = cp;
            return 1;
        }
        if (byte < 0x80) prepend_byte(d, byte);
        return NS_STEP_ERROR;
    }
    if (byte < 0x80) {
        out[0] = (guint32)byte;
        return 1;
    }
    if (byte == 0x80) {
        out[0] = 0x20AC;
        return 1;
    }
    if (byte <= 0xFE) {
        d->lead = (guint8)byte;
        return NS_STEP_CONTINUE;
    }
    return NS_STEP_ERROR;
}

static int
big5_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) {
        if (!d->lead) return NS_STEP_FINISHED;
        d->lead = 0;
        return NS_STEP_ERROR;
    }
    if (d->lead) {
        guint lead = d->lead;
        d->lead = 0;
        if ((byte >= 0x40 && byte <= 0x7E) || (byte >= 0xA1 && byte <= 0xFE)) {
            guint offset = byte < 0x7F ? 0x40 : 0x62;
            guint pointer = (lead - 0x81) * 157 + (guint)byte - offset;
            static const guint32 pairs[][3] = {
                { 1133, 0x00CA, 0x0304 }, { 1135, 0x00CA, 0x030C },
                { 1164, 0x00EA, 0x0304 }, { 1166, 0x00EA, 0x030C },
            };
            for (gsize i = 0; i < G_N_ELEMENTS(pairs); i++) {
                if (pairs[i][0] == pointer) {
                    out[0] = pairs[i][1];
                    out[1] = pairs[i][2];
                    return 2;
                }
            }
            guint32 cp = index_code_point(NS_INDEX_BIG5, pointer);
            if (cp) {
                out[0] = cp;
                return 1;
            }
        }
        if (byte < 0x80) prepend_byte(d, byte);
        return NS_STEP_ERROR;
    }
    if (byte < 0x80) {
        out[0] = (guint32)byte;
        return 1;
    }
    if (byte >= 0x81 && byte <= 0xFE) {
        d->lead = (guint8)byte;
        return NS_STEP_CONTINUE;
    }
    return NS_STEP_ERROR;
}

static int
euc_jp_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) {
        if (!d->lead) return NS_STEP_FINISHED;
        d->lead = 0;
        d->jis0212 = FALSE;
        return NS_STEP_ERROR;
    }
    if (d->lead == 0x8E && byte >= 0xA1 && byte <= 0xDF) {
        d->lead = 0;
        out[0] = 0xFF61 - 0xA1 + (guint32)byte;
        return 1;
    }
    if (d->lead == 0x8F && byte >= 0xA1 && byte <= 0xFE) {
        d->jis0212 = TRUE;
        d->lead = (guint8)byte;
        return NS_STEP_CONTINUE;
    }
    if (d->lead) {
        guint lead = d->lead;
        d->lead = 0;
        guint32 cp = 0;
        if (lead >= 0xA1 && lead <= 0xFE && byte >= 0xA1 && byte <= 0xFE)
            cp = index_code_point(d->jis0212 ? NS_INDEX_JIS0212
                                             : NS_INDEX_JIS0208,
                                  (lead - 0xA1) * 94 + (guint)byte - 0xA1);
        d->jis0212 = FALSE;
        if (cp) {
            out[0] = cp;
            return 1;
        }
        if (byte < 0x80) prepend_byte(d, byte);
        return NS_STEP_ERROR;
    }
    if (byte < 0x80) {
        out[0] = (guint32)byte;
        return 1;
    }
    if (byte == 0x8E || byte == 0x8F || (byte >= 0xA1 && byte <= 0xFE)) {
        d->lead = (guint8)byte;
        return NS_STEP_CONTINUE;
    }
    return NS_STEP_ERROR;
}

static int
iso_2022_jp_escape(ns_decoder *d, int byte)
{
    guint8 lead = d->lead;
    int state = -1;
    d->lead = 0;
    if (lead == 0x28 && byte == 0x42) state = NS_ISO_ASCII;
    else if (lead == 0x28 && byte == 0x4A) state = NS_ISO_ROMAN;
    else if (lead == 0x28 && byte == 0x49) state = NS_ISO_KATAKANA;
    else if (lead == 0x24 && (byte == 0x40 || byte == 0x42)) state = NS_ISO_LEAD;
    if (state >= 0) {
        gboolean was_output = d->output;
        d->state = d->output_state = (guint8)state;
        d->output = TRUE;
        return was_output ? NS_STEP_ERROR : NS_STEP_CONTINUE;
    }
    guint8 bytes[2] = { lead, (guint8)byte };
    prepend(d, bytes, byte == NS_EOS ? 1 : 2);
    d->output = FALSE;
    d->state = d->output_state;
    return NS_STEP_ERROR;
}

static int
iso_2022_jp_step(ns_decoder *d, int byte, guint32 *out)
{
    if (d->state == NS_ISO_ESCAPE_START) {
        if (byte == 0x24 || byte == 0x28) {
            d->lead = (guint8)byte;
            d->state = NS_ISO_ESCAPE;
            return NS_STEP_CONTINUE;
        }
        prepend_byte(d, byte);
        d->output = FALSE;
        d->state = d->output_state;
        return NS_STEP_ERROR;
    }
    if (d->state == NS_ISO_ESCAPE) return iso_2022_jp_escape(d, byte);
    if (d->state == NS_ISO_TRAIL) {
        if (byte == 0x1B) {
            d->state = NS_ISO_ESCAPE_START;
            return NS_STEP_ERROR;
        }
        d->state = NS_ISO_LEAD;
        if (byte < 0x21 || byte > 0x7E) return NS_STEP_ERROR;
        guint32 cp = index_code_point(NS_INDEX_JIS0208,
                                      (d->lead - 0x21) * 94u + (guint)byte - 0x21);
        if (!cp) return NS_STEP_ERROR;
        out[0] = cp;
        return 1;
    }
    if (byte == 0x1B) {
        d->state = NS_ISO_ESCAPE_START;
        return NS_STEP_CONTINUE;
    }
    if (byte == NS_EOS) return NS_STEP_FINISHED;
    d->output = FALSE;
    switch (d->state) {
    case NS_ISO_ROMAN:
        if (byte == 0x5C) out[0] = 0xA5;
        else if (byte == 0x7E) out[0] = 0x203E;
        else if (byte <= 0x7F && byte != 0x0E && byte != 0x0F) out[0] = (guint32)byte;
        else return NS_STEP_ERROR;
        return 1;
    case NS_ISO_KATAKANA:
        if (byte < 0x21 || byte > 0x5F) return NS_STEP_ERROR;
        out[0] = 0xFF61 - 0x21 + (guint32)byte;
        return 1;
    case NS_ISO_LEAD:
        if (byte < 0x21 || byte > 0x7E) return NS_STEP_ERROR;
        d->lead = (guint8)byte;
        d->state = NS_ISO_TRAIL;
        return NS_STEP_CONTINUE;
    default:
        if (byte > 0x7F || byte == 0x0E || byte == 0x0F) return NS_STEP_ERROR;
        out[0] = (guint32)byte;
        return 1;
    }
}

static int
shift_jis_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) {
        if (!d->lead) return NS_STEP_FINISHED;
        d->lead = 0;
        return NS_STEP_ERROR;
    }
    if (d->lead) {
        guint lead = d->lead;
        d->lead = 0;
        if ((byte >= 0x40 && byte <= 0x7E) || (byte >= 0x80 && byte <= 0xFC)) {
            guint offset = byte < 0x7F ? 0x40 : 0x41;
            guint lead_offset = lead < 0xA0 ? 0x81 : 0xC1;
            guint pointer = (lead - lead_offset) * 188 + (guint)byte - offset;
            if (pointer >= 8836 && pointer <= 10715) {
                out[0] = 0xE000 - 8836 + pointer;
                return 1;
            }
            guint32 cp = index_code_point(NS_INDEX_JIS0208, pointer);
            if (cp) {
                out[0] = cp;
                return 1;
            }
        }
        if (byte < 0x80) prepend_byte(d, byte);
        return NS_STEP_ERROR;
    }
    if (byte <= 0x80) {
        out[0] = (guint32)byte;
        return 1;
    }
    if (byte >= 0xA1 && byte <= 0xDF) {
        out[0] = 0xFF61 - 0xA1 + (guint32)byte;
        return 1;
    }
    if ((byte >= 0x81 && byte <= 0x9F) || (byte >= 0xE0 && byte <= 0xFC)) {
        d->lead = (guint8)byte;
        return NS_STEP_CONTINUE;
    }
    return NS_STEP_ERROR;
}

static int
euc_kr_step(ns_decoder *d, int byte, guint32 *out)
{
    if (byte == NS_EOS) {
        if (!d->lead) return NS_STEP_FINISHED;
        d->lead = 0;
        return NS_STEP_ERROR;
    }
    if (d->lead) {
        guint lead = d->lead;
        d->lead = 0;
        if (byte >= 0x41 && byte <= 0xFE) {
            guint32 cp = index_code_point(NS_INDEX_EUC_KR,
                                          (lead - 0x81) * 190 + (guint)byte - 0x41);
            if (cp) {
                out[0] = cp;
                return 1;
            }
        }
        if (byte < 0x80) prepend_byte(d, byte);
        return NS_STEP_ERROR;
    }
    if (byte < 0x80) {
        out[0] = (guint32)byte;
        return 1;
    }
    if (byte >= 0x81 && byte <= 0xFE) {
        d->lead = (guint8)byte;
        return NS_STEP_CONTINUE;
    }
    return NS_STEP_ERROR;
}

static int
decoder_step(ns_decoder *d, int byte, guint32 *out)
{
    switch (d->enc->kind) {
    case NS_KIND_UTF8:        return utf8_step(d, byte, out);
    case NS_KIND_SINGLE_BYTE: return single_byte_step(d, byte, out);
    case NS_KIND_GB18030:     return gb18030_step(d, byte, out);
    case NS_KIND_BIG5:        return big5_step(d, byte, out);
    case NS_KIND_EUC_JP:      return euc_jp_step(d, byte, out);
    case NS_KIND_ISO_2022_JP: return iso_2022_jp_step(d, byte, out);
    case NS_KIND_SHIFT_JIS:   return shift_jis_step(d, byte, out);
    case NS_KIND_EUC_KR:      return euc_kr_step(d, byte, out);
    case NS_KIND_UTF16BE:     return utf16_step(d, byte, TRUE, out);
    case NS_KIND_UTF16LE:     return utf16_step(d, byte, FALSE, out);
    case NS_KIND_REPLACEMENT:
        if (byte == NS_EOS || d->replaced) return NS_STEP_FINISHED;
        d->replaced = TRUE;
        return NS_STEP_ERROR;
    case NS_KIND_X_USER_DEFINED:
        if (byte == NS_EOS) return NS_STEP_FINISHED;
        out[0] = byte < 0x80 ? (guint32)byte : 0xF780 + (guint32)byte - 0x80;
        return 1;
    }
    return NS_STEP_ERROR;
}

ns_decoder *
ns_decoder_new(const ns_encoding *enc, gboolean fatal)
{
    ns_decoder *d = g_new0(ns_decoder, 1);
    d->enc = enc ? enc : ns_encoding_utf8();
    d->fatal = fatal;
    ns_decoder_reset(d);
    return d;
}

void
ns_decoder_reset(ns_decoder *d)
{
    const ns_encoding *enc = d->enc;
    gboolean fatal = d->fatal;
    memset(d, 0, sizeof *d);
    d->enc = enc;
    d->fatal = fatal;
    d->lower = 0x80;
    d->upper = 0xBF;
    d->lead_byte = -1;
    d->state = d->output_state = NS_ISO_ASCII;
}

void
ns_decoder_free(ns_decoder *d)
{
    g_free(d);
}

static gsize
utf8_valid_run(const guint8 *data, gsize len)
{
    const gchar *end = NULL;
    g_utf8_validate_len((const gchar *)data, len, &end);
    return (gsize)((const guint8 *)end - data);
}

gboolean
ns_decoder_decode(ns_decoder *d, const guint8 *data, gsize len,
                  gboolean flush, GString *out)
{
    gsize i = 0;
    for (;;) {
        if (d->enc->kind == NS_KIND_UTF8 && d->needed == 0 &&
            d->npending == 0 && i < len) {
            gsize run = utf8_valid_run(data + i, len - i);
            g_string_append_len(out, (const gchar *)data + i, (gssize)run);
            i += run;
        }
        int byte;
        if (d->npending) {
            byte = d->pending[0];
            d->npending--;
            memmove(d->pending, d->pending + 1, d->npending);
        } else if (i < len) {
            byte = data[i++];
        } else if (flush) {
            byte = NS_EOS;
        } else {
            return TRUE;
        }
        guint32 cps[2];
        int r = decoder_step(d, byte, cps);
        if (r > 0) {
            for (int k = 0; k < r; k++) g_string_append_unichar(out, cps[k]);
        } else if (r == NS_STEP_ERROR) {
            if (d->fatal) return FALSE;
            g_string_append_unichar(out, 0xFFFD);
        } else if (r == NS_STEP_FINISHED) {
            return TRUE;
        }
    }
}

char *
ns_encoding_decode(const ns_encoding *enc, const char *data, gsize len,
                   gsize *out_len)
{
    ns_decoder *d = ns_decoder_new(enc, FALSE);
    GString *out = g_string_sized_new(len + 1);
    ns_decoder_decode(d, (const guint8 *)data, len, TRUE, out);
    ns_decoder_free(d);
    if (out_len) *out_len = out->len;
    return g_string_free(out, FALSE);
}

char *
ns_encoding_decode_sniffed(const ns_encoding *enc, const char *data, gsize len,
                           gsize *out_len)
{
    if (len >= 3 && memcmp(data, "\xEF\xBB\xBF", 3) == 0)
        return ns_encoding_decode(&ns_encodings[NS_ENC_UTF_8], data + 3,
                                  len - 3, out_len);
    if (len >= 2 && memcmp(data, "\xFE\xFF", 2) == 0)
        return ns_encoding_decode(&ns_encodings[NS_ENC_UTF_16BE], data + 2,
                                  len - 2, out_len);
    if (len >= 2 && memcmp(data, "\xFF\xFE", 2) == 0)
        return ns_encoding_decode(&ns_encodings[NS_ENC_UTF_16LE], data + 2,
                                  len - 2, out_len);
    return ns_encoding_decode(enc, data, len, out_len);
}

static gboolean
mime_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char *
ns_encoding_mime_charset(const char *mime)
{
    const char *p = mime ? strchr(mime, ';') : NULL;
    while (p) {
        p++;
        while (mime_is_space(*p)) p++;
        const char *name = p;
        while (*p && *p != '=' && *p != ';') p++;
        gsize name_len = (gsize)(p - name);
        if (*p != '=') {
            p = *p ? p : NULL;
            continue;
        }
        p++;
        GString *value = g_string_new(NULL);
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                g_string_append_c(value, *p++);
            }
            p = strchr(p, ';');
        } else {
            const char *start = p;
            while (*p && *p != ';') p++;
            const char *end = p;
            while (end > start && mime_is_space(end[-1])) end--;
            g_string_append_len(value, start, end - start);
            p = *p ? p : NULL;
        }
        if (name_len == 7 && g_ascii_strncasecmp(name, "charset", 7) == 0 &&
            value->len > 0)
            return g_string_free(value, FALSE);
        g_string_free(value, TRUE);
    }
    return NULL;
}
