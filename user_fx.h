#ifndef __USER_FX_H__
#define __USER_FX_H__

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef void (*set_tx_cb)(unsigned char c);
void uart_set_tx_cb(set_tx_cb cb);
void uart_on_recv_char(unsigned char c);

#define STX 0x2
#define ETX 0x3
#define ENQ 0x5
#define ACK 0x6
#define NAK 0x15

#define ACTION_READ      0x0
#define ACTION_WRITE     0x1
#define ACTION_FORCE_ON  0x7
#define ACTION_FORCE_OFF 0x8

// register type
enum {
    REG_D = 0,
    REG_M,
    REG_T,
    REG_S,
    REG_C,
    REG_X,
    REG_Y,
    REG_MS,
    REG_YP,
    REG_TO,
    REG_MP,
    REG_CO,
    REG_TR,
    REG_CR,
    REG_TV,
    REG_CV16,
    REG_CV32,
    REG_DS
};

// register bytes address
#define REG_S_BASE_ADDRESS 0x0
#define REG_X_BASE_ADDRESS 0x80
#define REG_Y_BASE_ADDRESS 0x0a0
#define REG_T_BASE_ADDRESS 0x0c0
#define REG_M_BASE_ADDRESS 0x100
#define REG_C_BASE_ADDRESS 0x1c0
#define REG_MS_BASE_ADDRESS 0x1e0   /* M special register(8000 - 8255) */
#define REG_D_BASE_ADDRESS 0x1000   /* D 16bits value register */

#define REG_YP_BASE_ADDRESS 0x2a0   /* P PLS/PLF register */
#define REG_TO_BASE_ADDRESS 0x2c0   /* T OUT register */
#define REG_MP_BASE_ADDRESS 0x300   /* M PLS/PLF register */
#define REG_CO_BASE_ADDRESS 0x3c0   /* C OUT register */

#define REG_TR_BASE_ADDRESS 0x4c0   /* T RST register */
#define REG_CR_BASE_ADDRESS 0x5c0   /* C RST register */

#define REG_TV16_BASE_ADDRESS 0x800   /* T 16bits value register */
#define REG_CV16_BASE_ADDRESS 0xa00   /* C 16bits value register */
#define REG_CV32_BASE_ADDRESS 0xc00   /* C 32bits value register */

#define REG_DS_BASE_ADDRESS 0xe00   /* D special 16bits value register(8000 - 8255) */

// register bit address
#define REG_S_BIT_BASE_ADDRESS 0x0
#define REG_X_BIT_BASE_ADDRESS 0x400
#define REG_Y_BIT_BASE_ADDRESS 0x500
#define REG_T_BIT_BASE_ADDRESS 0x600
#define REG_M_BIT_BASE_ADDRESS 0x800
#define REG_C_BIT_BASE_ADDRESS 0xe00
#define REG_MS_BIT_BASE_ADDRESS 0xf00
#define REG_D_BIT_BASE_ADDRESS 0x0

#define MAX_DATA_ONETIME 32

bool fx_enquiry(void);
bool fx_force_on(u8 addr_type, u16 addr);
bool fx_force_off(u8 addr_type, u16 addr);
bool fx_read(u8 addr_type, u16 addr, u8 *out, u16 len);
bool fx_write(u8 addr_type, u16 addr, u8 *data, u16 len);

#endif // __USER_FX_H__

