// ~~~~~~~~~~~~~~~~~~~~~~~~~~ INCLUDES
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>

#include "mbt-srv.h"

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ DEFINITIONS
#define RESTART_CONTEXT_TO            2                     ///< Sleep time before restarting the context build procedure
#define MBSRV_THREAD_TO              20                     ///< Timeout seconds for modbus server thread
#define LISTEN_BACKLOG               50                     ///< Growth of socket listen queue
#define MBCMD_TYPE_TCP                1                     ///< Identify a TCP modbus command structure
#define MBCMD_TYPE_RTU                2                     ///< Identify a RTU modbus command structure
#define RTU_SEND_DELAY_MSEC          50                     ///< Milliseconds delay after sending a reply to RTU

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ GLOBAL VARS
int srv_socket    = -1;                                     ///< Main Server socket
pthread_t trd_tcp = 0,                                      ///< Starts modbus tcp server
          trd_rtu = 0;                                      ///< Starts modbus rtu slave
int srv_terminate = 0;                                      ///< If set to true server thread should stop// Modbus Context
modbus_t *ctx_rtu = NULL;                                   ///< RTU context, i need it global so I can stop it when shutting down

const char *mdb_proto_strings[] = {
  "TCP",
  "RTU"
};

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ FUNCTIONS
int mb_query( const uint8_t *query, const uint16_t qlen, enum mdb_proto_type mproto, modbus_mapping_t *mb_mapping ){
  if( !mb_mapping ){ return MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE; }

  int mdb_proto_offset = 0; ///< Used to "shift" ther effective mdb query data read, regardless of RTU/TCP variant

  switch( mproto ){
    case MDB_PROTO_TCP:
      if( qlen < 12 ){
        log_war( "Wrong query length (%d) for TCP command", qlen );
        return MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
      }
      mdb_proto_offset = 7;
      break;

    case MDB_PROTO_RTU:
      if( qlen < 5 ){
        log_war( "Wrong query length (%d) for TCP command", qlen );
        return MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
      }
      mdb_proto_offset = 1;
      break;

    default:
      log_err( "Unsupported MDB protocol: %d", mproto );
      return MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
  }


  uint8_t   mcmd = query[ 0 + mdb_proto_offset ];                                    ///< Modbus Command code
  uint16_t  mreg = (((uint16_t)query[ 1 + mdb_proto_offset ]) << 8) +                ///< Modbus Registry address
                     (uint16_t)query[ 2 + mdb_proto_offset ];
  uint16_t  mlen = (((uint16_t)query[ 3 + mdb_proto_offset ]) << 8) +                ///< Modbus Length (for reading or multi write)
                     (uint16_t)query[ 4 + mdb_proto_offset ];
  // uint16_t  mval = mlen;                                                             ///< Modbus Value for single reg write (actually in the same position of len)
  // uint16_t  max_reg = 0;                                                             ///< Maximu reg address, used to check for read/write bounds
  log_dbg( "%s l=%d QRY: %02X %04X %04X", mdb_proto_strings[mproto], qlen, mcmd, mreg, mlen );

  // checking command and address bounds
  // switch(  mcmd ){
  //   case MODBUS_FC_READ_COILS:
  //     mval = 0;
  //     max_reg = MB_BITS_MAX;
  //     break;

  //   case MODBUS_FC_READ_HOLDING_REGISTERS:
  //     mval = 0;
  //     max_reg = MB_REGS_MAX;
  //     break;

  //   case MODBUS_FC_WRITE_SINGLE_REGISTER:
  //     mlen = 1;
  //     max_reg = MB_REGS_MAX;
  //     break;

  //   default:
  //     log_war( "Unsupported modbus command: %02X", mcmd );
  //     return MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
  // }

  // if( !(mreg + mlen < max_reg) ){
  //   log_war( "Out of regs bound: 0x%04X ( bound 0x%04X )", (mreg + mlen), max_reg -1 );
  //   return MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
  // }

  // // Writing values
  // for( int i = 0; i < mlen; i++ ){
  //   switch( mcmd ){
  //     case MODBUS_FC_WRITE_SINGLE_REGISTER:
  //       mb_mapping->tab_registers[ mreg + i ] = *( &mval + i );
  //       break;
  //   }
  // }

  return 0;
}


// ========================================
// Modbus TCP server
// ========================================
void mbtcp_runner( struct tcp_args_t *args ){
  pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

  int master_socket,                // Socket for current Modbus master
      fdmax;                        // Maximum file descriptor number
  fd_set refset, rdset;

  modbus_t *ctx_tcp = NULL;
  uint8_t query[ MODBUS_TCP_MAX_ADU_LENGTH ];
  modbus_mapping_t *mb_mapping = modbus_mapping_new( MB_BITS_MAX, MB_BITS_IN_MAX, MB_REGS_MAX, MB_REGS_IN_MAX );
  if( !mb_mapping ){
    log_err( "Failed to allocate mapping with err( %d ): %s", errno, modbus_strerror(errno) );
    pthread_exit( NULL );
  }

  while( !srv_terminate ){
    // Setting up context
    ctx_tcp = modbus_new_tcp_pi( args->addr, args->port );
    if( !ctx_tcp ){
      log_err( "Failed setting up modbus tcp_pi context. Retry in %d seconds", RESTART_CONTEXT_TO );
      sleep( RESTART_CONTEXT_TO );
      continue;
    }
    // Starting Server
    srv_socket = modbus_tcp_pi_listen( ctx_tcp, NB_CONNECTION );
    if( srv_socket == -1 ){
      log_err( "Failed socket listening err( %d ): %s", errno, modbus_strerror(errno) );
      modbus_close( ctx_tcp );
      modbus_free( ctx_tcp );
      sleep( RESTART_CONTEXT_TO );
      continue;
    }
    log_inf( "TCP server runner thread started: %s:%s", args->addr, args->port );

    FD_ZERO( &refset );
    FD_SET( srv_socket, &refset );
    fdmax = srv_socket;

    // Socket infos
    char s_addr[INET6_ADDRSTRLEN] = { 0 };
    struct sockaddr_in cli_addr   = { 0 };
    socklen_t cli_addrlen         = 0;

    while( !srv_terminate ){
      rdset = refset;

      if( select( fdmax+1, &rdset, NULL, NULL, NULL ) == -1 ){
        log_err( "Server select() failure" );
        continue;
      }

      // Run through the existing connections looking for data to be read
      for( master_socket = 0; master_socket <= fdmax; master_socket++ ){
        if( !FD_ISSET( master_socket, &rdset ) ){ continue; }

        // Client asking a new connection
        if( master_socket == srv_socket ){
          int newfd = accept( srv_socket, (struct sockaddr *)&cli_addr, &cli_addrlen );
          if( newfd == -1 ){
            log_ver( "Server accept() error: %s", strerror( errno ) );
          }
          else{
            FD_SET( newfd, &refset );
            // Keep track of the maximum
            if( newfd > fdmax ){ fdmax = newfd; }
            log_ver( "New connection on socket %d", newfd );
          }

          continue;
        }

        // Receiving request on existent connection
        modbus_set_socket( ctx_tcp, master_socket );
        int rc = modbus_receive( ctx_tcp, query );

        if( getpeername( master_socket, (struct sockaddr *)&cli_addr, &cli_addrlen ) == 0 ){
          inet_ntop( cli_addr.sin_family,  &(cli_addr.sin_addr), s_addr, INET6_ADDRSTRLEN );
        }

        // Connection error or terminated
        if( rc < 0 ){
          if( errno != ECONNRESET ){
            log_ver( "Failed receive from %s:%d closed on socket %d - for: %s", s_addr, cli_addr.sin_port, master_socket, modbus_strerror( errno ) );
          }

          // Close socket e remove from reference set
          close( master_socket );
          FD_CLR( master_socket, &refset );
          if( master_socket == fdmax ){ fdmax--; }

          continue;
        }

        int err = mb_query( query, rc, MDB_PROTO_TCP, mb_mapping );

        // Sending response
        if( err == 0 ){
          modbus_reply( ctx_tcp, query, rc, mb_mapping );
          log_dbg( "[%s:%d] Reply sent successfully", s_addr, cli_addr.sin_port );
        }
        else{
          log_war( "[%s:%d] Query failed. Modbus exception %d (%s)", s_addr, cli_addr, err, modbus_strerror(err) );
          modbus_reply_exception( ctx_tcp, query, err );
        }
      }
    }

    // Server Terminated
    log_inf( "srv terminated: %s", modbus_strerror(errno) );
    if( srv_socket != -1 ){ close( srv_socket ); }

    // Cleaning up
    modbus_close( ctx_tcp );
    modbus_free( ctx_tcp );
    ctx_tcp    = NULL;
    sleep( RESTART_CONTEXT_TO );
  }

  // Cleaning up
  modbus_mapping_free( mb_mapping );
  mb_mapping = NULL;

  pthread_exit( NULL );
}


// ========================================
// Modbus RTU slave
// ========================================
void mbrtu_runner( struct rtu_args_t *args ){
  pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

  // Modbus mapping, contains all regs values e structures
  modbus_mapping_t *mb_mapping = modbus_mapping_new( MB_BITS_MAX, MB_BITS_IN_MAX, MB_REGS_MAX, MB_REGS_IN_MAX );
  if( !mb_mapping ){
    log_err( "Failed to allocate mapping with err( %d ): %s", errno, modbus_strerror(errno) );
    pthread_exit( NULL );
  }

  // Query variables
  uint8_t query[ MODBUS_RTU_MAX_ADU_LENGTH ];

  while( !srv_terminate ){
    // Setting up modbus rtu
    ctx_rtu = modbus_new_rtu( args->dev, args->speed, 'N', 8, 1 );
    if( !ctx_rtu ){
      log_err( "Failed setting up modbus rtu context. Restarting in %d seconds", RESTART_CONTEXT_TO );
      ctx_rtu = NULL;
      sleep( RESTART_CONTEXT_TO );
      continue;
    }
    if( modbus_set_slave( ctx_rtu, args->addr ) != 0 ){
      log_war( "Invalid rtu address: %d", args->addr );
      modbus_close( ctx_rtu );
      modbus_free( ctx_rtu );
      sleep( RESTART_CONTEXT_TO );
      continue;
    }
    if( modbus_connect( ctx_rtu ) == -1) {
      log_err( "Unable to connect RTU context: %s", modbus_strerror(errno) );
      modbus_close( ctx_rtu );
      modbus_free( ctx_rtu );
      sleep( RESTART_CONTEXT_TO );
      continue;
    }

    log_inf( "RTU slave runner thread started: %s %d", args->dev, args->addr );

    while( !srv_terminate ){
      int rc = 0;
      do{ rc = modbus_receive( ctx_rtu, query ); } while( rc == 0 );

      modbus_flush( ctx_rtu );

      // Connection error or terminated
      if( rc == -1 ){
        int mb_error = errno;
        log_ver( "modbus_receive error(%d): %s", mb_error, modbus_strerror( mb_error ) );
        continue;
      }

      // Skipping query for some other slave
      if( query[0] != args->addr ){
        log_dbg( "Skipping query for different slave: %d", query[0] );
        continue;
      }

      int err = mb_query( query, rc, MDB_PROTO_RTU, mb_mapping );

      // Sending response
      if( err == 0 ){
        modbus_reply( ctx_rtu, query, rc, mb_mapping );
        log_dbg( "Reply sent" );
      }
      else{
        log_war( "Query failed. Modbus exception %d (%s)", err, modbus_strerror(err) );
        modbus_reply_exception( ctx_rtu, query, err );
      }
    }

    // Server Terminated
    log_inf( "srv terminated" );

    // Cleaning up
    modbus_close( ctx_rtu );
    modbus_free( ctx_rtu );
    ctx_rtu = NULL;
    sleep( RESTART_CONTEXT_TO );
  }

  // Cleaning up
  modbus_mapping_free( mb_mapping );
  mb_mapping = NULL;

  pthread_exit( NULL );
}


// ========================================
// Functions to interface with
// server thread
// ========================================

int mbsrv_start( struct tcp_args_t *tcp_args, struct rtu_args_t *rtu_args ){
  if( !( tcp_args && rtu_args ) ||
      ( tcp_args->enabled && !( tcp_args->addr     && tcp_args->addr ) ) ||
      ( rtu_args->enabled && !( rtu_args->addr > 0 && rtu_args->dev  ) )
    ){
    log_err( "failed! tcp: %s tcp_addr=%s tcp_port=%s - rtu: %s rtu_addr=%d rtu_dev=%s",
         tcp_args->enabled ? "on" : "off",
         tcp_args->addr,
         tcp_args->port,
         rtu_args->enabled ? "on" : "off",
         rtu_args->addr,
         rtu_args->dev
        );
    return -1;
  }


  // Set the termination status to false
  srv_terminate = 0;

  // Running modbus tcp srv dedicated thread
  if( !tcp_args->enabled ){ log_inf( "Modbus TCP disabled. Skipping" ); }
  else{
    if( pthread_create( &trd_tcp, NULL, (void *)&mbtcp_runner, (void *)tcp_args ) ){
      log_err( "FAILED CREATING mbtcp runner thread" );
      return -1;
    }
    pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );
  }

  // Running modbus rtu slave dedicated thread
  if( !rtu_args->enabled ){ log_inf( "Modbus RTU disabled. Skipping" ); }
  else{
    if( pthread_create( &trd_rtu, NULL, (void *)&mbrtu_runner, (void *)rtu_args ) ){
      log_err( "FAILED CREATING mbrtu runner thread" );
      return -1;
    }
  }

  return 0;
}


int mbsrv_stop(){
  // Asks srv runner thread to terminate;
  srv_terminate = 1;

  int ret;
  struct timespec killtime;

  // Wait for tcp thread to self terminate
  if( !trd_tcp ){ log_inf( "Thread mbsrv seems not started" ); }
  else{
    if( srv_socket != -1 ){ shutdown( srv_socket, SHUT_RDWR ); }

    if( clock_gettime( CLOCK_REALTIME, &killtime ) != -1 ){ killtime.tv_sec += MBSRV_THREAD_TO; }
    else{
      log_ver( "TCP Failed getting CLOCK_REALTIME" );
      killtime.tv_nsec = 0;
      killtime.tv_sec  = 0;
    }

    ret = pthread_timedjoin_np( trd_tcp, NULL, &killtime );
    if( ret != 0 ){
      log_war( "Failed to wait for tcp runner thread. Killing it" );
      pthread_cancel( trd_tcp );
    }
  }

  // Wait for rtu thread to self terminate
  if( !trd_rtu ){ log_inf( "Thread mbsrv seems not started" ); }
  else{
    if( ctx_rtu ){ modbus_close( ctx_rtu ); }
    if( clock_gettime( CLOCK_REALTIME, &killtime ) != -1 ){ killtime.tv_sec += MBSRV_THREAD_TO; }
    else{
      log_ver( "RTU Failed getting CLOCK_REALTIME" );
      killtime.tv_nsec = 0;
      killtime.tv_sec  = 0;
    }

    ret = pthread_timedjoin_np( trd_rtu, NULL, &killtime );
    if( ret != 0 ){
      log_war( "Failed to wait for rtu runner thread. Killing it" );
      pthread_cancel( trd_rtu );
    }
  }

  return 0;
}

// ~~~~~~~~~~~~~~~~~~~~~ LOG FUNCTIONS
int msg_colors = 0;         ///< Global variable to set colored msg output
int msg_dbgl   = DBG_INF;   ///< Sets debug level, higher means more informations

void set_debug( int lvl ){
  msg_dbgl = lvl;
}
void set_msg_colors( int on ){
  msg_colors = on == 1 ? 1 : 0;
}

int is_msg_colors(){
  return msg_colors;
}
int get_debug(){
  return msg_dbgl;
}

void msg( int lvl, const char* pfx, const char *logstr , ... ){
   if( msg_dbgl < lvl ){ return; }

  char    buff[20];
  struct  tm* tm_info;
  time_t  timer;
  int     multiline_pad_len = 16;

  // Printing timestamp
  time( &timer );
  tm_info = localtime( &timer );
  if( strftime( buff, sizeof buff, "%Y%m%d %H%M%S", tm_info ) ){
    printf( "%s%s%s ", ( msg_colors ? COL_BRIGHT_BLACK : "" ), buff, ( msg_colors ? COL_RESET : "" ) );
  }

  // Printing dbg level
  switch( lvl ){
    case DBG_ERR: printf( "%sERR%s ", ( msg_colors ? COL_BRIGHT_RED    : "" ), ( msg_colors ? COL_RESET : "" ) ); break;
    case DBG_WAR: printf( "%sWAR%s ", ( msg_colors ? COL_BRIGHT_YELLOW : "" ), ( msg_colors ? COL_RESET : "" ) ); break;
    case DBG_INF: printf( "%sINF%s ", ( msg_colors ? COL_BRIGHT_CYAN   : "" ), ( msg_colors ? COL_RESET : "" ) ); break;
    case DBG_VER: printf( "%sVER%s ", ( msg_colors ? COL_BRIGHT_PURPLE : "" ), ( msg_colors ? COL_RESET : "" ) ); break;
    case DBG_DBG: printf( "%sDBG%s ", ( msg_colors ? COL_BRIGHT_GREEN  : "" ), ( msg_colors ? COL_RESET : "" ) ); break;
    default:      printf( "%sUNK%s ", ( msg_colors ? COL_BRIGHT_BLACK  : "" ), ( msg_colors ? COL_RESET : "" ) ); break;
  }

  // Printing pfx
  if( pfx ){
    printf( "[%s%s%s] ", ( msg_colors ? COL_BRIGHT_BLACK : "" ), pfx, ( msg_colors ? COL_RESET : "" ) );
    multiline_pad_len += strlen( pfx ) + 3 ;
  }

  // Printing message
  va_list arglist;
  va_start( arglist, logstr );
  vprintf( logstr, arglist );
  va_end( arglist );

  // Forcing newline
  printf( "\n" );
}
