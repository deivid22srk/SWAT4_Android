/* WinSock 2 / WinSock 1.1 stubs (WS2_32.dll + WSOCK32.dll).
 *
 * SWAT 4's m4d/IFC23 shader pipeline and IpDrv import the socket API. Nothing
 * in single-player needs a working network, but every one of these imports
 * must exist: the runtime aborts on an unimplemented import, and m4d calls
 * WSAStartup during D3D device init (build #7 desktop died there, and the
 * phone stalls right before the same call).
 *
 * Policy: WSAStartup succeeds; sockets are fake handles; writes report
 * success, reads report "would block", connects report "would block" (the
 * non-blocking convention) so pollers time out instead of erroring out. */
#include "rt.h"
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

#define SOCKET_ERROR_VAL 0xFFFFFFFFu /* (SOCKET)-1 */
#define WSAEWOULDBLOCK   10035
#define WSAENETDOWN      10050
#define WSAEOPNOTSUPP    10045
#define WSAEINVAL_WSA    10022

static uint32_t g_next_socket = 0x40;
static uint32_t g_wsa_error;

static void wsa_fail(CPU *c, uint32_t err) {
    g_wsa_error = err;
    rt_set_last_error(c, err);
}

/* WSADATA: wVersion@0, wHighVersion@2, szDescription[257]@4,
 * szSystemStatus[129]@261, iMaxSockets@390, iMaxUdpDg@392, lpVendorInfo@396 */
SHIM(WSAStartup) {
    uint32_t data = ARG(1);
    if (data) {
        M16(data) = 0x0202;
        M16(data + 2) = 0x0202;
        snprintf((char *)MEM + data + 4, 257, "WinSock 2.2 (stub)");
        snprintf((char *)MEM + data + 261, 129, "Running");
        M16(data + 390) = 32767;
        M16(data + 392) = 65467;
        M32(data + 396) = 0;
    }
    LOG("WSAStartup -> 2.2 (stub)");
    RET(0, 2);
}
SHIM(WSACleanup) { RET(0, 0); }
SHIM(WSAGetLastError) { RET(g_wsa_error, 0); }
SHIM(WSASetLastError) { g_wsa_error = ARG(0); RETC(0); }
SHIM(__WSAFDIsSet) { RET(0, 2); }
SHIM(WSAAsyncSelect) { RET(0, 4); }
SHIM(WSAEventSelect) { RET(0, 3); }
SHIM(WSACreateEvent) { RET(0x88, 0); }
SHIM(WSACloseEvent) { RET(1, 1); }
SHIM(WSAEnumNetworkEvents) { RET(0, 3); }

SHIM(socket) {
    RET(g_next_socket++, 3);
}
SHIM(closesocket) { RET(0, 1); }
SHIM(shutdown) { RET(0, 2); }
SHIM(ioctlsocket) { RET(0, 3); }
SHIM(bind) { RET(0, 3); }
SHIM(listen) { RET(0, 2); }
SHIM(accept) { wsa_fail(c, WSAEWOULDBLOCK); RET(SOCKET_ERROR_VAL, 3); }
SHIM(connect) { wsa_fail(c, WSAEWOULDBLOCK); RET(SOCKET_ERROR_VAL, 3); }
SHIM(select) { RET(0, 5); }
SHIM(recv) { wsa_fail(c, WSAEWOULDBLOCK); RET(SOCKET_ERROR_VAL, 4); }
SHIM(recvfrom) { wsa_fail(c, WSAEWOULDBLOCK); RET(SOCKET_ERROR_VAL, 6); }
SHIM(send) { RET(ARG(2), 4); }          /* pretend the whole buffer went out */
SHIM(sendto) { RET(ARG(2), 6); }
SHIM(setsockopt) { RET(0, 5); }
SHIM(getsockopt) {
    if (ARG(3)) M32(ARG(3)) = 0; /* int options -> 0 */
    RET(0, 5);
}
SHIM(getpeername) {
    if (ARG(1) && ARG(2)) { memset(MEM + ARG(1), 0, 16); M16(ARG(1)) = 2; }
    RET(0, 3);
}
SHIM(getsockname) {
    if (ARG(1) && ARG(2)) { memset(MEM + ARG(1), 0, 16); M16(ARG(1)) = 2; }
    RET(0, 3);
}
SHIM(gethostname) {
    const char *n = "localhost";
    uint32_t cap = ARG(1);
    if (ARG(0) && cap > strlen(n)) snprintf((char *)MEM + ARG(0), cap, "%s", n);
    RET(0, 2);
}
SHIM(gethostbyname) { RET(0, 1); }
SHIM(gethostbyaddr) { RET(0, 4); }
SHIM(htonl) { RET(htonl(ARG(0)), 1); }
SHIM(htons) { RET(htons(ARG(0) & 0xFFFF), 1); }
SHIM(ntohl) { RET(ntohl(ARG(0)), 1); }
SHIM(ntohs) { RET(ntohs(ARG(0) & 0xFFFF), 1); }
SHIM(inet_addr) {
    struct in_addr a;
    if (inet_pton(AF_INET, GSTR(ARG(0)), &a) == 1) RET(a.s_addr, 1);
    wsa_fail(c, WSAEINVAL_WSA);
    RET(0xFFFFFFFFu, 1); /* INADDR_NONE */
}
/* inet_ntoa returns a host-static buffer; guests read it as a guest pointer,
 * so park the result in a small rotating pool of guest strings. */
SHIM(inet_ntoa) {
    static uint32_t bufs[8];
    static int idx;
    struct in_addr a = { .s_addr = ARG(0) };
    if (!bufs[0]) for (int i = 0; i < 8; i++) bufs[i] = gmalloc(32);
    uint32_t b = bufs[idx++ & 7];
    snprintf((char *)MEM + b, 32, "%u.%u.%u.%u", a.s_addr & 255,
             (a.s_addr >> 8) & 255, (a.s_addr >> 16) & 255, a.s_addr >> 24);
    RET(b, 1);
}
SHIM(WSAStringToAddress) { wsa_fail(c, WSAEINVAL_WSA); RET(SOCKET_ERROR_VAL, 5); }
SHIM(WSAAddressToString) { RET(SOCKET_ERROR_VAL, 5); }
SHIM(WSAIoctl) { RET(SOCKET_ERROR_VAL, 9); }

/* ---- registration: same functions under both DLL names, by name and ordinal ---- */
static const ShimDef by_name[] = {
    {"WS2_32.dll", "WSAStartup", shim_WSAStartup},
    {"WS2_32.dll", "WSACleanup", shim_WSACleanup},
    {"WS2_32.dll", "WSAGetLastError", shim_WSAGetLastError},
    {"WS2_32.dll", "WSASetLastError", shim_WSASetLastError},
    {"WS2_32.dll", "__WSAFDIsSet", shim___WSAFDIsSet},
    {"WS2_32.dll", "WSAAsyncSelect", shim_WSAAsyncSelect},
    {"WS2_32.dll", "WSAEventSelect", shim_WSAEventSelect},
    {"WS2_32.dll", "WSACreateEvent", shim_WSACreateEvent},
    {"WS2_32.dll", "WSACloseEvent", shim_WSACloseEvent},
    {"WS2_32.dll", "WSAEnumNetworkEvents", shim_WSAEnumNetworkEvents},
    {"WS2_32.dll", "WSAStringToAddress", shim_WSAStringToAddress},
    {"WS2_32.dll", "WSAAddressToString", shim_WSAAddressToString},
    {"WS2_32.dll", "WSAIoctl", shim_WSAIoctl},
    {"WS2_32.dll", "socket", shim_socket},
    {"WS2_32.dll", "closesocket", shim_closesocket},
    {"WS2_32.dll", "shutdown", shim_shutdown},
    {"WS2_32.dll", "ioctlsocket", shim_ioctlsocket},
    {"WS2_32.dll", "bind", shim_bind},
    {"WS2_32.dll", "listen", shim_listen},
    {"WS2_32.dll", "accept", shim_accept},
    {"WS2_32.dll", "connect", shim_connect},
    {"WS2_32.dll", "select", shim_select},
    {"WS2_32.dll", "recv", shim_recv},
    {"WS2_32.dll", "recvfrom", shim_recvfrom},
    {"WS2_32.dll", "send", shim_send},
    {"WS2_32.dll", "sendto", shim_sendto},
    {"WS2_32.dll", "setsockopt", shim_setsockopt},
    {"WS2_32.dll", "getsockopt", shim_getsockopt},
    {"WS2_32.dll", "getpeername", shim_getpeername},
    {"WS2_32.dll", "getsockname", shim_getsockname},
    {"WS2_32.dll", "gethostname", shim_gethostname},
    {"WS2_32.dll", "gethostbyname", shim_gethostbyname},
    {"WS2_32.dll", "gethostbyaddr", shim_gethostbyaddr},
    {"WS2_32.dll", "htonl", shim_htonl},
    {"WS2_32.dll", "htons", shim_htons},
    {"WS2_32.dll", "ntohl", shim_ntohl},
    {"WS2_32.dll", "ntohs", shim_ntohs},
    {"WS2_32.dll", "inet_addr", shim_inet_addr},
    {"WS2_32.dll", "inet_ntoa", shim_inet_ntoa},
    {NULL, NULL, NULL},
};

void ws2_init(void) {
    rt_register_shims(by_name);
    /* WSOCK32 exports the same entry points (1.1-era names). */
    rt_register_shims_as("WSOCK32.dll", by_name);
    /* Ordinal forms: ws2_32 and wsock32 share the BSD socket ordinals and the
     * classic WSA ordinals (111=WSAGetLastError, 115=WSAStartup, 116=WSACleanup). */
    static const struct { const char *ord; ShimFn fn; } ordmap[] = {
        {"#1", shim_accept}, {"#2", shim_bind}, {"#3", shim_closesocket},
        {"#4", shim_connect}, {"#5", shim_getpeername}, {"#6", shim_getsockname},
        {"#7", shim_getsockopt}, {"#8", shim_htonl}, {"#9", shim_htons},
        {"#10", shim_inet_addr}, {"#11", shim_inet_ntoa}, {"#12", shim_ioctlsocket},
        {"#13", shim_listen}, {"#14", shim_ntohl}, {"#15", shim_ntohs},
        {"#16", shim_recv}, {"#17", shim_recvfrom}, {"#18", shim_select},
        {"#19", shim_send}, {"#20", shim_sendto}, {"#21", shim_setsockopt},
        {"#22", shim_shutdown}, {"#23", shim_socket}, {"#24", shim_gethostbyaddr},
        {"#25", shim_gethostbyname}, {"#51", shim_gethostbyaddr}, {"#52", shim___WSAFDIsSet},
        {"#53", shim_gethostname}, {"#55", shim_gethostname}, {"#57", shim_gethostname},
        {"#111", shim_WSAGetLastError}, {"#112", shim_WSASetLastError},
        {"#115", shim_WSAStartup}, {"#116", shim_WSACleanup},
        {"#151", shim_WSAAsyncSelect},
        {NULL, NULL},
    };
    static ShimDef ord_defs[2 * (sizeof ordmap / sizeof ordmap[0])];
    int n = 0;
    for (int i = 0; ordmap[i].ord; i++) {
        ord_defs[n].dll = "WS2_32.dll"; ord_defs[n].name = ordmap[i].ord; ord_defs[n].fn = ordmap[i].fn; n++;
        ord_defs[n].dll = "WSOCK32.dll"; ord_defs[n].name = ordmap[i].ord; ord_defs[n].fn = ordmap[i].fn; n++;
    }
    ord_defs[n].dll = ord_defs[n].name = NULL;
    ord_defs[n].fn = NULL;
    rt_register_shims(ord_defs);
}
