#ifndef _MBT_SRV_H_
#define _MBT_SRV_H_

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <modbus/modbus.h>

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ DEFINITIONS
#define NB_CONNECTION           15                 ///< Max num of simultaneous connections

#define MB_BITS_MAX             0xFFFF
#define MB_BITS_IN_MAX          0xFFFF
#define MB_REGS_MAX             0xFFFF
#define MB_REGS_IN_MAX          0xFFFF

#define DEF_TCP_ADDR            "*"
#define DEF_TCP_PORT            "502"
#define DEF_RTU_SPEED           9600
#define DEF_RTU_DEV             "/dev/ttyUSB0"
#define DEF_RTU_ADDR            1
#define DEF_SRST_INT            15*60
#define DEF_QCOUNT              50
#define DEF_LEVEL               DBG_NONE           ///< Default debug level
#define DEF_LEVEL_STR           "none"             ///< Default debug level in string form
#define DEF_ERR_RATE            0.0                ///< Default modbus error rate
#define DEF_INIT_VAL            0                  ///< Default init value for the modbus registries

// Term colors
#define COL_RESET               "\e[0m"
#define COL_BRIGHT_RED          "\e[0;91m"
#define COL_BRIGHT_CYAN         "\e[0;96m"
#define COL_BRIGHT_GREEN        "\e[0;92m"
#define COL_BRIGHT_YELLOW       "\e[0;93m"
#define COL_BRIGHT_BLACK        "\e[0;90m"
#define COL_BRIGHT_BLUE         "\e[0;94m"
#define COL_BRIGHT_PURPLE       "\e[0;95m"
#define COL_BRIGHT_WHITE        "\e[0;97m"

// Debug levels
#define DBG_NONE  0
#define DBG_ERR   1
#define DBG_WAR   2
#define DBG_INF   3
#define DBG_VER   4
#define DBG_DBG   5

// Debug shortcut
#define log_dbg(...)  msg( DBG_DBG, __FUNCTION__, __VA_ARGS__ )
#define log_ver(...)  msg( DBG_VER, __FUNCTION__, __VA_ARGS__ )
#define log_inf(...)  msg( DBG_INF, __FUNCTION__, __VA_ARGS__ )
#define log_war(...)  msg( DBG_WAR, __FUNCTION__, __VA_ARGS__ )
#define log_err(...)  msg( DBG_ERR, __FUNCTION__, __VA_ARGS__ )

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ TYPES
struct rtu_args_t{
  uint8_t enabled;
  float   error_rate;
  uint8_t init_value;
  char    *dev;
  int     addr;
  int     speed;
};
struct tcp_args_t{
  uint8_t enabled;
  float   error_rate;
  uint8_t init_value;
  char    *addr;
  char    port[6];
};

extern const char *mdb_proto_strings[];

enum mdb_proto_type {
  MDB_PROTO_TCP,
  MDB_PROTO_RTU
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ FUNCTIONS

/**
 * @brief      Starts the modbus server
 *
 * @param      tcp_args  The tcp arguments
 * @param      rtu_args  The rtu arguments
 *
 * @return     0, in case of success. ERROR code otherwise
 */
int mbsrv_start( struct tcp_args_t *tcp_args, struct rtu_args_t *rtu_args );

/**
 * @brief      Stops the modbus server
 *
 * @return     0
 */
int mbsrv_stop();

/**
 * @brief      Sets the debug level.
 *
 * @param[in]  lvl   The new value
 */
void set_debug( int lvl );

/**
 * @brief      Sets the colored output for debug messages.
 *
 * @param[in]  on    The new value
 */
void set_msg_colors( int on );

/**
 * @brief      Determines if messages are colored.
 *
 * @return     1 if message are colored, 0 otherwise.
 */
int is_msg_colors();

/**
 * @brief      Gets the debug level.
 *
 * @return     The debug.
 */
int get_debug();


/**
 * @brief      { function_description }
 *
 * @param[in]  lvl        The Message Level
 * @param[in]  fname      The Message Prefix
 * @param[in]  logstr     The String to be logged (as for printf)
 * @param[in]  <unnamed>  printf arguments
 */
void msg( int lvl, const char* pfx, const char *logstr , ... );

#endif // _MBT_SRV_H_
