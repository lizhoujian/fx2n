#include "stdafx.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "user_fx.h"

#include "MyComm.h"
#include "MyCommDoc.h"
#include "MyCommView.h"

#if defined(_WIN32) || defined(_WIN64)
#define __WINDOWS__
#endif

#ifdef __WINDOWS__

// test response for windows
#define __WINDOWS_TEST__

typedef void * xQueueHandle;
typedef void * xSemaphoreHandle;
typedef unsigned int portTickType;

#define UART0 0

#define portTICK_RATE_MS 100
#define portMAX_DELAY 0
#define xQueueCreate(A, B) NULL
#define xQueueReset(A)
#define xQueueSendFromISR(A, B, C)
#define xQueueReceive(A, B, C)
#define xSemaphoreTakeFromISR(A, B)
#define xSemaphoreGiveFromISR(A, B)
#define xSemaphoreTake(A, B)
#define xSemaphoreGive(A)
#define xSemaphoreCreateMutex() NULL
#define vQueueDelete(A)

static void uart_init_for_fx(void)
{

}

static set_tx_cb curr_set_tx_cb;
void uart_set_tx_cb(set_tx_cb cb)
{
    curr_set_tx_cb = cb;
}

static void uart_cb(u8 c);
void uart_on_recv_char(unsigned char c)
{
    uart_cb((u8)c);
}

static void uart_tx_one_char(u8 port, u8 c)
{
    if (curr_set_tx_cb) {
        (*curr_set_tx_cb)((unsigned char)c);
    }
}

typedef void (*cb)(u8 c);
static void uart_set_recv_cb(cb myCb)
{

}
#else
#define TRACE printf
#endif

#include "user_fx.h"

#define __countof(a) (sizeof(a) / sizeof(a[0]))

#define TO_ASCII(half) (((half) < 10) ? ((half) + 0x30) : ((half) + 0x41 - 0xa))
#define SWAP_BYTE(word) ((((word) >> 8) & 0xff) | (((word) << 8) & 0xff00));

#define TO_HEX(c) ((((c) - 0x30) < 10) ? ((c) - 0x30) : ((c) - 0x41 + 10))

typedef struct register_t
{
    u8 type; /* register type */
    u32 byte_base_addr; /* base address for current */
    u32 bit_base_addr; /* base address for current */
    u32 (*addr)(void *r, u16 offset, bool bit); /* calc address */
    u8 addr_len; /* bytes of address */
} register_t;

#define WAIT_RECV_TIMEOUT 2000

typedef struct uart_event_t
{
    u8 event;
} uart_event_t;
enum {
    UART_EVENT_DONE = 200
};

typedef struct uart_buf_t
{
    u16 index;
    u16 len;
    u8 *data;
} uart_buf_t;

static xQueueHandle uart_queue_recv = NULL;
static xSemaphoreHandle recv_buf_mutex = NULL;
static uart_buf_t uart_recv_buf = {0,};

static void post_event(u8 ev)
{
    uart_event_t e = {0,};
    e.event = ev;
    xQueueSendFromISR(uart_queue_recv, (void *)&e, NULL);
}

static void parse_buf(uart_buf_t *p, u16 len)
{
    if (len == p->len || p->data[0] == NAK) {
        post_event(UART_EVENT_DONE);
    }
}

static void uart_cb(u8 c)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTakeFromISR(uart_queue_recv, NULL);

    if (p->data && p->index < p->len) {
        p->data[p->index++] = c;
        parse_buf(p, p->index);
    } else {
        TRACE("insert char overflow or not init c=%c???\n", c);
    }

    xSemaphoreGiveFromISR(uart_queue_recv, NULL);
}

static void uart_send(u8 *data, u16 len)
{
    int i;
    for (i = 0; i < len; i++) {
        uart_tx_one_char(UART0, data[i]);
    }
}

u8 check_sum(u8 *in, u16 inLen)
{
    int i, sum;

    sum = 0;
    for (i = 0; i < inLen; i++) {
        sum += in[i];
    }

    return sum & 0xff;
}

static u8 to_hex(u8 *in)
{
    return ((TO_HEX(in[0]) << 4) & 0xf0) | ((TO_HEX(in[1]) & 0xf));
}

static void ascii_to_hex(u8 *in, u8 *out, u16 inLen)
{
    int i;
    for (i = 0; i < inLen; i += 2) {
        out[i/2] = to_hex(&in[i]);
    }
}

static void to_ascii(u8 in, u8 *out)
{
    out[0] = TO_ASCII((in >> 4) & 0xf);
    out[1] = TO_ASCII(in & 0xf);
}

void hex_to_ascii(u8 *in, u8 *out, u16 inLen)
{
    int i;
    for (i = 0; i < inLen; i++) {
        to_ascii(in[i], out + i * 2);
    }
}

static void free_recv_buff(void)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTake(uart_queue_recv, portMAX_DELAY);    
    if (p->data) {
        p->index = 0;
        p->len = 0;
        free(p->data);
        p->data = NULL;
        //TRACE("free last recv buff.\n");
    }
    xSemaphoreGive(uart_queue_recv);
}

static bool alloc_recv_buff(u16 len)
{
    uart_buf_t *p = &uart_recv_buf;

    xSemaphoreTake(uart_queue_recv, portMAX_DELAY);

    p->len = len;
    p->data = (u8*)malloc(p->len);
    if (p->data) {
        memset(p->data, 0, p->len);
    }
    p->index = 0;

    xSemaphoreGive(uart_queue_recv);

    return p->data != NULL;
}

static bool wait_recv_done(u16 miliseconds)
{
    bool ret = false;
    int i;
    uart_buf_t *p = &uart_recv_buf;
    uart_event_t e = {0,};

    if ((miliseconds % (portTICK_RATE_MS)) > 0) {
        miliseconds += portTICK_RATE_MS;
    }

#ifdef __WINDOWS__
    Sleep(100);
    ret = true;
#else
    xQueueReceive(uart_queue_recv, (void *)&e, (portTickType)(miliseconds / portTICK_RATE_MS));
    if (e.event == UART_EVENT_DONE) {
        ret = true;
    } else {
        TRACE("wait recv error(timeout?), event = %d\n", e.event);
        ret = false;
    }
#endif
    for (i = 0; i < p->len; i++) {
        TRACE("%02x ", p->data[i]);
    }
    TRACE("\r\n");
    return ret;
}

static bool is_ack(void)
{
    uart_buf_t *p = &uart_recv_buf;
    u8 ack;

    xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    ack = p->data[0];
    xSemaphoreGive(uart_queue_recv);

    return ack == ACK;
}

static bool is_stx(void)
{
    uart_buf_t *p = &uart_recv_buf;
    u8 stx;

    xSemaphoreTake(uart_queue_recv, portMAX_DELAY);
    stx = p->data[0];
    xSemaphoreGive(uart_queue_recv);

    return stx == STX;
}

/* init response before request */
static bool create_response(u8 cmd, u16 data_len)
{
    u16 len;

    xQueueReset(uart_queue_recv);

    if (cmd == ACTION_READ) {
        len = 1 + data_len * 2 + 1 + 2;
    } else {
        len = 1;
    }

    return alloc_recv_buff(len);
}

static bool wait_response(u16 miliseconds)
{
    return wait_recv_done(miliseconds);
}

static bool parse_response_data(u8 *out, u16 len)
{
    bool ret = false;
    uart_buf_t *p = &uart_recv_buf;
    u8 sum, recv_sum;

    xSemaphoreTake(uart_queue_recv, portMAX_DELAY);

    if ((p->index == p->len) && p->data[0] == STX && (p->index > 3 && p->data[p->index - 1 - 2] == ETX)) {
        sum = check_sum(&p->data[1], p->len - 3); /* - STX - CHECKSUM */
        recv_sum = to_hex(&p->data[p->index - 1 - 1]);
        if (sum == recv_sum) {
            ascii_to_hex(&p->data[1], out, p->len - 4);
            ret = true;
        } else {
            TRACE("response data check sum error.\n");
        }
    } else {
        TRACE("parse response data invalid.\n");
    }

    xSemaphoreGive(uart_queue_recv);

    return ret;
}

static void free_response(void)
{
    free_recv_buff();
}

void fx_init(void)
{
    uart_queue_recv = xQueueCreate(1, sizeof(uart_event_t));
    if (uart_queue_recv) {
        recv_buf_mutex = xSemaphoreCreateMutex();
        if (recv_buf_mutex) {
            uart_init_for_fx();
            uart_set_recv_cb(uart_cb);
        } else {
            vQueueDelete(uart_queue_recv);
            TRACE("create recv_buf_mutex failed.\n");
        }
    } else {
        TRACE("create uart_queue_recv failed.\n");
    }
}

static u32 get_base(void *r, bool bit)
{
    register_t *t = (register_t*)r;
    return (bit ? t->bit_base_addr : t->byte_base_addr);
}

static u32 calc_address(void *r, u16 offset, bool bit)
{
     return get_base(r, bit) + offset;
}

static u32 calc_address2(void *r, u16 offset, bool bit)
{
    return get_base(r, bit) + offset * 2;
}

static u32 calc_address3(void *r, u16 offset, bool bit)
{
    return get_base(r, bit) + offset * 3;
}

static u32 swap_address(void *r, u32 addr)
{
    register_t *t = (register_t*)r;
    return addr;
}

static u32 swap_address2(void *r, u32 addr)
{
    register_t *t = (register_t*)r;
    u16 addr16 = (u16)addr;
    if (t->addr_len == 2) {
        return SWAP_BYTE(addr16);
    } else {
        return addr;
    }
}

static register_t registers[] = {
    {REG_D, REG_D_BASE_ADDRESS, REG_D_BIT_BASE_ADDRESS, calc_address2, 2},
    {REG_M, REG_M_BASE_ADDRESS, REG_M_BIT_BASE_ADDRESS, calc_address2, 2},
    {REG_T, REG_T_BASE_ADDRESS, REG_T_BIT_BASE_ADDRESS, calc_address, 2},
    {REG_S, REG_S_BASE_ADDRESS, REG_S_BIT_BASE_ADDRESS, calc_address3, 3},
    {REG_C, REG_C_BASE_ADDRESS, REG_C_BIT_BASE_ADDRESS, calc_address2, 2},
    {REG_X, REG_X_BASE_ADDRESS, REG_X_BIT_BASE_ADDRESS, calc_address, 2},
    {REG_Y, REG_Y_BASE_ADDRESS, REG_Y_BIT_BASE_ADDRESS, calc_address, 2},
};

static register_t *find_registers(u8 addr_type)
{
    int i, c = __countof(registers);

    for (i = 0; i < c; i++) {
        if (addr_type == registers[i].type)
            return &registers[i];
    }
    TRACE("not found register type.");
    return NULL;
}

static bool is_little_endian(void)
{
    u16 d = 0x1234;
    u8 *c = (u8*)&d;
    return *c == 0x34;
}

static u16 unit_addr(register_t *r, u8 cmd, u16 addr)
{
    u16 raddr = 0;
    if (cmd == ACTION_FORCE_ON || cmd == ACTION_FORCE_OFF) {
        raddr = r->addr(r, addr, true);
        if (!is_little_endian()) {
            raddr = SWAP_BYTE(raddr);
        }
    } else if (cmd == ACTION_READ || cmd == ACTION_WRITE) {
        raddr = r->addr(r, addr, false);
        if (is_little_endian()) {
            raddr = SWAP_BYTE(raddr);
        }
    }
    return raddr;
}

static u16 create_request(register_t *r, u8 cmd, u16 addr, u8 *data, u16 len, u8 **req)
{
    u8 *buf;
    u16 rlen;
    u16 raddr;
    u8 sum;

    raddr = unit_addr(r, cmd, addr);

    rlen = 1 + 1 + 4;
    rlen += (len > 0 ? 2 : 0);
    rlen += (data != NULL ? len * 2 : 0);
    rlen += 1 + 2;

    buf = (u8*)malloc(rlen);
    if (buf) {
        memset(buf, 0, rlen);
        buf[0] = STX;
        buf[1] = TO_ASCII(cmd);
        hex_to_ascii((u8*)&raddr, &buf[2], 2); /* 4 bytes */
        if (len > 0) {
            to_ascii((u8)len, &buf[6]); /* 2 bytes */
        }
        if (data != NULL) {
            hex_to_ascii(data, &buf[8], len); /* (2 * len) bytes */
        }
        buf[rlen - 1 - 2] = ETX;
        sum = check_sum(&buf[1], rlen - 3); /* - STX - CHECKSUM */
        to_ascii(sum, &buf[rlen - 1 - 1]);

        *req = buf;
        return rlen;
    }

    return 0;
}

static void send_request(u8 *s, u16 len)
{
    uart_send(s, len);
}

static void free_request(u8 *data)
{
    if (data) {
        free(data);
    }
}

#ifdef __WINDOWS_TEST__
static UINT thread_ack(LPVOID param)
{
    uart_on_recv_char(ACK);
    return 0;
}
static UINT thread_read(LPVOID param)
{
    u16 rlen, len = (u16)param;
    u8 *buf, sum;
    int i, r;

    srand((unsigned)time(NULL));

    rlen = 1 + len * 2 + 1 + 2;
    buf = (u8*)malloc(rlen);
    if (buf) {
        memset(buf, 0, rlen);
        buf[0] = STX;
        for (i = 0; i < len * 2; i++) {
            r = -1;
            while (!(r >= 0 && r <= 15)) {
                r = rand() % 16;
            }
            buf[i + 1] = TO_ASCII((u8)r);
        }
        buf[rlen - 1 - 2] = ETX;
        sum = check_sum(&buf[1], rlen - 3); /* - STX - CHECKSUM */
        to_ascii(sum, &buf[rlen - 1 - 1]);

        for (i = 0; i < rlen; i++) {
            uart_on_recv_char(buf[i]);
        }

        free(buf);
    }
    return 0;
}
#endif

bool fx_enquiry(void)
{
    bool ret = false;
    u8 cmd = ENQ;

    create_response(cmd, 0);
    uart_send(&cmd, 1);
#ifdef __WINDOWS_TEST__
    AfxBeginThread(thread_ack, NULL);
#endif
    ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
    free_response();

    return ret;
}

static bool fx_force_onoff(u8 addr_type, u16 addr, u8 cmd)
{
    register_t *r;
    u8 *req;
    u16 rlen;
    bool ret = false;

    if (fx_enquiry()) {
        r = find_registers(addr_type);
        if (r) {
            if ((rlen = create_request(r, cmd, addr, NULL, 0, &req)) > 0) {
                create_response(cmd, 0);
                send_request(req, rlen);
#ifdef __WINDOWS_TEST__
                AfxBeginThread(thread_ack, NULL);
#endif
                free_request(req);
                ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
                free_response();
            }
        }
    }

    return ret;
}

bool fx_force_on(u8 addr_type, u16 addr)
{
    return fx_force_onoff(addr_type, addr, ACTION_FORCE_ON);
}

bool fx_force_off(u8 addr_type, u16 addr)
{
    return fx_force_onoff(addr_type, addr, ACTION_FORCE_OFF);
}

bool fx_read(u8 addr_type, u16 addr, u8 *out, u16 len)
{
    register_t *r;
    u8 *req;
    u16 rlen;
    bool ret = false;

    if (fx_enquiry()) {
        r = find_registers(addr_type);
        if (r) {
            if ((rlen = create_request(r, ACTION_READ, addr, NULL, len, &req)) > 0) {
                create_response(ACTION_READ, len);
                send_request(req, rlen);
#ifdef __WINDOWS_TEST__
                AfxBeginThread(thread_read, (LPVOID)len);
#endif
                free_request(req);
                ret = wait_response(WAIT_RECV_TIMEOUT) && is_stx();
                if (ret) {
                    ret = parse_response_data(out, len);
                }
                free_response();
            }
        }
    }

    return ret;
}

bool fx_write(u8 addr_type, u16 addr, u8 *data, u16 len)
{
    register_t *r;
    u8 *req;
    u16 rlen;
    bool ret = false;

    if (fx_enquiry()) {
        r = find_registers(addr_type);
        if (r) {
            if ((rlen = create_request(r, ACTION_WRITE, addr, data, len, &req)) > 0) {
                create_response(ACTION_WRITE, 0);
                send_request(req, rlen);
#ifdef __WINDOWS_TEST__
                AfxBeginThread(thread_ack, NULL);
#endif
                free_request(req);
                ret = wait_response(WAIT_RECV_TIMEOUT) && is_ack();
                free_response();
            }
        }
    }

    return ret;
}

