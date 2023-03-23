// ~~~~~~~~~~~~~~~~~~~~~~~~~~ INCLUDES
#include "mbt-srv.h"

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ DEFINITIONS
#define STATUS_SLEEP   600    ///< This is the sleep between two check cicles. Nothing is actually done during the main cicle.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ FUNCTIONS
void help(){
  printf( "Command: \n  ./modbus-tester tcp -a <address> -p <port> [options]\n\n" );

  /** @todo  Add missing help */

  printf( "\nSpecific: \n" );
  printf( "  rtu                 Enable RTU\n" );
  printf( "  tcp                 Enable TCP\n" );
  printf( "  -a, --address       Specify IP address to bind modbus TCP server ( default = %s )\n", DEF_TCP_ADDR );
  printf( "  -p, --port          Port used by TCP socket ( default = %s )\n", DEF_TCP_PORT );
  printf( "  -d, --rtu-dev       tty used bu RTU  ( default = %s )\n", DEF_RTU_DEV );
  printf( "  -r, --rtu-addr      RTU Address number ( default = %d )\n", DEF_RTU_ADDR );
  printf( "  -s, --rtu-speed     RTU serial speed ( default = %d )\n", DEF_RTU_SPEED );
  printf( "  -e, --error-rate    Modbus Errors Rate in percent [0.0 - 100.0] ( default = %f )\n", DEF_ERR_RATE );
  printf( "  -i, --init-value    Modbus Errors Rate in percent [0x0 - 0xFF] ( default = %02X )\n", DEF_INIT_VAL );

  printf( "\nCommon:\n" );
  printf( "  -l, --level         Sets verbosity level [ error, warning, info, verbose, debug, none ] ( default = %s )\n", DEF_LEVEL_STR );
  printf( "  -c, --colors        Set colored output on\n" );
  printf( "  -h, --help          Print help\n" );

  printf( "\nlibmodbus %s (compiled) %d.%d.%d (linked)\n", LIBMODBUS_VERSION_STRING, libmodbus_version_major, libmodbus_version_minor, libmodbus_version_micro );
  printf( "\n" );
}

// ~~~~~~~~~~~~~~~~~~~~~~~~~~ MAIN
int main( const int argc, const char** argv ){
  const char *tcp_addr = NULL,
             *port     = NULL,    // tcp_pi uses string port/service
             *rtu_dev  = NULL;    // tty path of RTU
  int rtu_addr    = 0,
      rtu_speed   = 0,
      rtu_enabled = 0,
      tcp_enabled = 0;

  uint8_t init_value = DEF_INIT_VAL;
  float error_rate = DEF_ERR_RATE;

  struct tcp_args_t tcp_args = { 0 };
  struct rtu_args_t rtu_args = { 0 };

  // Setting default debug level
  set_debug( DEF_LEVEL );

  // BEGIN: Command line args
  for( int i = 1; i < argc; i++ ){
    if(      strcmp( argv[i], "-h"  ) == 0 || strcmp( argv[i], "--help"        ) == 0 ){ help(); return 0; }
    else if( strcmp( argv[i], "-c"  ) == 0 || strcmp( argv[i], "--colors"      ) == 0 ){ set_msg_colors( 1 ); }
    else if( strcmp( argv[i], "rtu" ) == 0 ){ rtu_enabled = 1; }
    else if( strcmp( argv[i], "tcp" ) == 0 ){ tcp_enabled = 1; }
    else if( (strcmp( argv[i], "-a" ) == 0 || strcmp( argv[i], "--address"    ) == 0 ) && (i+1)<argc ){
      i++;
      tcp_addr = argv[i];
    }
    else if( (strcmp( argv[i], "-p" ) == 0 || strcmp( argv[i], "--port"       ) == 0 ) && (i+1)<argc ){
      i++;
      port = argv[i]; // tcp_pi uses string port/service
    }
    else if( (strcmp( argv[i], "-d" ) == 0 || strcmp( argv[i], "--rtu-dev"    ) == 0 ) && (i+1)<argc ){
      i++;
      rtu_dev = argv[i];
    }
    else if( (strcmp( argv[i], "-r" ) == 0 || strcmp( argv[i], "--rtu-addr"   ) == 0 ) && (i+1)<argc && atoi(argv[i+1]) > 0 ){
      i++;
      rtu_addr = atoi( argv[i] ); // tcp_pi uses string port/service
    }
    else if( (strcmp( argv[i], "-s" ) == 0 || strcmp( argv[i], "--rtu-speed"  ) == 0 ) && (i+1)<argc && atoi(argv[i+1]) > 0 ){
      i++;
      rtu_speed = atoi( argv[i] );
    }
    else if( (strcmp( argv[i], "-e" ) == 0 || strcmp( argv[i], "--error-rate" ) == 0 ) && (i+1)<argc && atof(argv[i+1]) >= 0.0 ){
      i++;
      error_rate = atof( argv[i] );
      if (error_rate < 0.0 || error_rate > 100.0) { error_rate = DEF_ERR_RATE; }
    }
    else if( (strcmp( argv[i], "-i" ) == 0 || strcmp( argv[i], "--init-value" ) == 0 ) && (i+1)<argc && strtol(argv[i+1], NULL, 0) >= 0 ){
      i++;
      long tmp_val = strtol(argv[i], NULL, 0);
      if (tmp_val >= 0x0 && tmp_val <= 0xFF) { init_value = tmp_val & 0xFF; }
    }
    else if( (strcmp( argv[i], "-l" ) == 0 || strcmp( argv[i], "--level"      ) == 0 ) && (i+1)<argc ){
      i++;
      if(       strcmp( argv[i], "error"   ) == 0 ){ set_debug( DBG_ERR   ); }
      else if(  strcmp( argv[i], "warning" ) == 0 ){ set_debug( DBG_WAR   ); }
      else if(  strcmp( argv[i], "info"    ) == 0 ){ set_debug( DBG_INF   ); }
      else if(  strcmp( argv[i], "verbose" ) == 0 ){ set_debug( DBG_VER   ); }
      else if(  strcmp( argv[i], "debug"   ) == 0 ){ set_debug( DBG_DBG   ); }
      else if(  strcmp( argv[i], "none"    ) == 0 ){ set_debug( DBG_NONE  ); }
    }
    else{ log_war( "Unknown or invalid argument: '%s'", argv[i] ); }
  }

  // GWC std debug compatibility: overrides setted debug level
  if( getenv( "VERBOSE" ) ){ set_debug( DBG_DBG ); }

  // Check params
  if( !tcp_addr ){
    tcp_addr = DEF_TCP_ADDR;
  }
  if( !port || atoi(port) < 1 || atoi(port) > 65535 ){
    port = DEF_TCP_PORT;
  }
  if( !rtu_dev ){
    rtu_dev = DEF_RTU_DEV;
  }
  if( rtu_addr <= 0 ){
    rtu_addr = DEF_RTU_ADDR;
  }
  if( rtu_speed <= 0 ){
    rtu_speed = DEF_RTU_SPEED;
  }
  // END: Command line args

  snprintf( tcp_args.port, sizeof(tcp_args.port), "%s", port );
  tcp_args.enabled = tcp_enabled;
  rtu_args.enabled = rtu_enabled;
  rtu_args.addr    = rtu_addr;
  rtu_args.speed   = rtu_speed;
  rtu_args.dev     = (char *)rtu_dev;
  tcp_args.addr    = (char *)tcp_addr;

  tcp_args.init_value = init_value;
  rtu_args.init_value = init_value;
  tcp_args.error_rate = error_rate;
  rtu_args.error_rate = error_rate;

  // Debug printing used vars
  if( get_debug() <= DBG_DBG ){
    log_dbg( "┌───── PARAMS" );
    if( is_msg_colors() ){
      log_dbg( "├─ rtu enabled:         %s%s%s", rtu_args.enabled ? COL_BRIGHT_GREEN : COL_BRIGHT_RED, rtu_args.enabled ? "on" : "off", COL_RESET );
      log_dbg( "├─ tcp enabled:         %s%s%s", tcp_args.enabled ? COL_BRIGHT_GREEN : COL_BRIGHT_RED, tcp_args.enabled ? "on" : "off", COL_RESET );
    }
    else{
      log_dbg( "├─ rtu enabled:         %s", rtu_args.enabled ? "on" : "off" );
      log_dbg( "├─ tcp enabled:         %s", tcp_args.enabled ? "on" : "off" );
    }
    log_dbg( "├─ tcp_args.addr:       %s", tcp_args.addr      );
    log_dbg( "├─ tcp_args.port:       %s", tcp_args.port      );
    log_dbg( "├─ tcp_args.init_value: %d", tcp_args.init_value);
    log_dbg( "├─ tcp_args.error_rate: %f", tcp_args.error_rate);
    log_dbg( "├─ rtu_args.dev:        %s", rtu_args.dev       );
    log_dbg( "├─ rtu_args.addr:       %d", rtu_args.addr      );
    log_dbg( "├─ rtu_args.speed:      %d", rtu_args.speed     );
    log_dbg( "├─ rtu_args.init_value: %d", rtu_args.init_value);
    log_dbg( "├─ rtu_args.error_rate: %f", rtu_args.error_rate);
    log_dbg( "├─────" );
    log_dbg( "├─ dbgl:                %d", get_debug() );
    log_dbg( "├─ colors:              %s", is_msg_colors() ? ( COL_BRIGHT_GREEN "on" COL_RESET ) : "off" );
    log_dbg( "└───── PARAMS" );
  }
  /** @todo  Add here the paramenters check! Very important!! */

  const int start_status = mbsrv_start( &tcp_args, &rtu_args );
  if( start_status != 0 ){
    log_err( "Server failed to start with error: %d", start_status );
    return -1;
  }

  // ========================================
  // Main cicle starts
  // ========================================
  while( 1 ){ /** @todo Add support for signals */
    usleep( STATUS_SLEEP * 1000000 );

    /** @todo add some logic here. Implemented this way to give the possibility to do something while the server is running */
  }

  // Here I have to stop server and clean conf to start from 0
  if( mbsrv_stop() != 0 ){
    log_err( "Failed stopping modbus server runner. I must commit suicide to be sure to kill it." );
    return -1;
  }

  return EXIT_SUCCESS;
}