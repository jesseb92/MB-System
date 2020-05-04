/*--------------------------------------------------------------------
 *    The MB-system:  mbtrnpp.c  2/19/2018
 *
 *    Copyright (c) 2018-2019 by
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, CA 95039
 *    and Dale N. Chayes (dale@ldeo.columbia.edu)
 *      Lamont-Doherty Earth Observatory
 *      Palisades, NY 10964
 *
 *    See README file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * mbtrnpp - originally mbtrnpreprocess
 *
 * Author:  D. W. Caress
 * Date:  February 18, 2018
 */

#if defined(__CYGWIN__)
#include <Windows.h>
#endif

#include <arpa/inet.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mb_status.h"
#include "mb_format.h"
#include "mb_define.h"
#include "mb_io.h"
#include "mbsys_ldeoih.h"
#include "mbsys_kmbes.h"

#include "merror.h"
#include "mconfig.h"
#include "r7kc.h"
#include "msocket.h"
#include "mtime.h"
#include "mlist.h"
#include "mlog.h"
#include "medebug.h"
#include "r7k-reader.h"
#include "mstats.h"
#include "mkvconf.h"
#ifdef WITH_MBTNAV
#include "trnw.h"
#include "netif.h"
#include "trnif_proto.h"
#include "trn_msg.h"
#endif // WITH_MBTNAV

/* ping structure definition */
struct mbtrnpp_ping_struct {
  int count;
  int time_i[7];
  double time_d;
  double navlon;
  double navlat;
  double speed;
  double heading;
  double distance;
  double altitude;
  double sonardepth;
  double roll;
  double pitch;
  double heave;
  int beams_bath;
  int beams_amp;
  int pixels_ss;
  char *beamflag;
  char *beamflag_filter;
  double *bath;
  double *bathacrosstrack;
  double *bathalongtrack;
  double *amp;
  double *ss;
  double *ssacrosstrack;
  double *ssalongtrack;
};

typedef enum {
    INPUT_MODE_SOCKET = 1,
    INPUT_MODE_FILE = 2
} input_mode_t;

typedef enum{
    OUTPUT_NONE        =0x000, OUTPUT_MB1_FILE_EN =0x001, OUTPUT_MB1_SVR_EN  =0x002,
    OUTPUT_TRN_SVR_EN  =0x004, OUTPUT_TRNU_SVR_EN =0x008, OUTPUT_MB1_BIN     =0x010,
    OUTPUT_RESON_BIN   =0x020, OUTPUT_TRNU_ASC    =0x040, OUTPUT_TRNU_SOUT   =0x080,
    OUTPUT_TRNU_SERR   =0x100, OUTPUT_TRNU_DEBUG  =0x200, OUTPUT_MBTRNPP_MSG =0x400,
    OUTPUT_MBSYS_STDOUT = 0x800,
    OUTPUT_ALL         =0xFFF
}output_mode_t;


// mbtrnpp_opts_s only encapsulates options.
// Options representing numeric primatives and booleans are parsed.
// Options with aggregate types are stored as strings.
// Parsing (into configuration variables) is done separately,
// enabling layered overrides clearer parsing logic (cmdline->config file->compilation default)
typedef struct mbtrnpp_opts_s{
    // opt "verbose"
    int verbose;
    // opt "input"
    char *input;
    // opt "format"
    int format;
    // opt "platform-file"
    char *platform_file;
    // opt "platform-target-sensor"
    int platform_target_sensor;
    // opt "log-directory"
    char *log_directory;
    // opt "output"
    char *output;
    // opt "projection
    int projection;
    // opt "swath-width"
    double swath_width;
    // opt "soundings"
    int soundings;
    // opt "median-filter"
    char *median_filter;
    // opt "mbhbn"
    int mbhbn;
    // opt "mbhbt"
    double mbhbt;
    // opt "trnhbt"
    double trnhbt;
    // opt "trnuhbt"
    double trnuhbt;
    // opt "delay"
    int64_t delay;
    // opt "statsec"
    double statsec;
    // opt "statflags"
    char *statflags_str;
    mstats_flags statflags;
    // opt "trn-en"
    bool trn_en;
    // opt "trn-utm"
    long int trn_utm;
    // opt "trn-map"
    char *trn_map;
    // opt "trn-cfg"
    char *trn_cfg;
    // opt "trn-par"
    char *trn_par;
    // opt "trn-mid"
    char *trn_mid;
    // opt "trn-mtype"
    int trn_mtype;
    // opt "trn-ftype"
    int trn_ftype;
    // opt "trn-ncov"
    double trn_ncov;
    // opt "trn-nerr"
    double trn_nerr;
    // opt "trn-ecov"
    double trn_ecov;
    // opt "trn-eerr"
    double trn_eerr;
    // opt "mb-out"
    char *mb_out;
    // opt "trn-out"
    char *trn_out;
    // opt "trn-decn"
    unsigned int trn_decn;
    // opt "trn-decs"
    double trn_decs;
    // opt "trn-nombgain"
    bool trn_nombgain;
    // opt "help"
    bool help;

}mbtrnpp_opts_t;

typedef struct mbtrnpp_cfg_s{
    // verbose output
    // <0 special trn module-specific debug channels
    // >0 mb-system debug (>2 very verbose)
    int verbose;
    // input mode selector
    input_mode_t input_mode;
    // socket input specifier
    mb_path socket_definition;
    // output file name
    mb_path output_file;
    // input specifier
    mb_path input;
    // data format
    int format;
    // platform file name
    mb_path platform_file;
    // platform file enable
    bool use_platform_file;
    // target sensor ID
    int target_sensor;
    // log directory
    mb_path log_directory;
    // log enable
    bool make_logs;
    // trn_log_directory
    // uses log_directory, defaults to . on failure
    char *trn_log_dir;
    // swath width (dec deg)
    double swath_width;
    // sonar output soundings
    int n_output_soundings;
    // median filter threshold
    double median_filter_threshold;
    // median filter cross-track
    int median_filter_n_across;
    // median filter along-track
    int median_filter_n_along;
    // median filter enable
    bool median_filter_en;
    // median filter buffer depth
    int n_buffer_max;
    // MB1 server hostname
    char *mb1svr_host;
    // MB1 server port
    int mb1svr_port;
    // TRN server hostname
    char *trnsvr_host;
    // TRN server port
    int trnsvr_port;
    // TRN UDP server hostname
    char *trnusvr_host;//TRNU_HOST_DFL
    // TRN UDP server port
    int trnusvr_port;
    // TRN output flags
    output_mode_t output_flags;
    // MB1 server heartbeat counts
    int mbsvr_hbtok;
    // MB1 server heartbeat timeout (s)
    double mbsvr_hbto;
    // TRN server heartbeat timeout (s)
    double trnsvr_hbto;
    // TRN UDP server heartbeat timeout
    double trnusvr_hbto;
    // TRN processing loop delay (msec)
    int64_t mbtrnpp_loop_delay_msec;
    // profiling interval (s)
    double trn_status_interval_sec;
    // profiling configuration flags
    mstats_flags mbtrnpp_stat_flags;
    // TRN processing enable
    bool trn_enable;
    // TRN UTM zone
    long int trn_utm_zone;
    // TRN map type
    int trn_mtype;
    // TRN filter type
    int trn_ftype;
    // TRN convergence northing covariance limit
    double trn_max_ncov;
    // TRN convergence northing error limit
    double trn_max_nerr;
    // TRN convergence easing covariance limit
    double trn_max_ecov;
    // TRN convergence easting error limit
    double trn_max_eerr;
    // TRN map file
    char *trn_map_file;
    // TRN config file
    char *trn_cfg_file;
    // TRN particles file
    char *trn_particles_file;
    // TRN mission ID
    char *trn_mission_id;
    // TRN process gating modulus
    unsigned int trn_decn;
    // TRN process gating timeout
    double trn_decs;
    // TRN mbgain reset disable
    // if true, disable sonar gain TRN resets
    bool trn_nombgain;
}mbtrnpp_cfg_t;

// ping buffer size default
#define MBTRNPREPROCESS_BUFFER_DEFAULT 20
#define MBTRNPREPROCESS_OUTPUT_STDOUT 0
#define MBTRNPREPROCESS_OUTPUT_TRN 1
#define MBTRNPREPROCESS_OUTPUT_FILE 2

#define MBTRNPREPROCESS_MB1_HEADER_SIZE 56
#define MBTRNPREPROCESS_MB1_SOUNDING_SIZE 28
#define MBTRNPREPROCESS_MB1_CHECKSUM_SIZE 4

#define MBTRNPREPROCESS_LOGFILE_TIMELENGTH 900.0

#define CHK_STRDUP(s) ( NULL!=s ? strdup(s) : NULL )
#define MEM_CHKFREE(s) if(NULL!=s)free(s)
#define MEM_CHKINVALIDATE(s) do{\
if(NULL!=s)free(s);\
s=NULL;\
}while(0)

#define BOOL2YNC(v) ( v ? 'Y' : 'N' )
#define BOOL2YNS(v) ( v ? "Y" : "N" )
#define BOOL2TF(v) ( v ? "true" : "false" )
#define BOOL2IC(v) ( v ? '1' : '0' )
#define BOOL2II(v) ( v ? 1 : 0 )

#define MBTRNPP_CONF_DEL "="
#define OPT_VERBOSE_DFL                 0
#define OPT_INPUT_DFL                   "socket:localhost:7000:0"
#define OPT_FORMAT_DFL                  88
#define OPT_PLATFORM_FILE_DFL           NULL
#define OPT_PLATFORM_TARGET_SENSOR_DFL  0
#define OPT_LOG_DIRECTORY_DFL           "."
#define OPT_OUTPUT_DFL                  NULL
#define OPT_PROJECTION_DFL              0
#define OPT_SWATH_WIDTH_DFL             90
#define OPT_SOUNDINGS_DFL               11
#define OPT_MEDIAN_FILTER_DFL           NULL
#define OPT_MBHBN_DFL                   MB1SVR_HBTOK_DFL
#define OPT_MBHBT_DFL                   MB1SVR_HBTO_DFL
#define OPT_TRNHBT_DFL                  TRNSVR_HBTO_DFL
#define OPT_TRNUHBT_DFL                 TRNUSVR_HBTO_DFL
#define OPT_DELAY_DFL                   0
#define OPT_STATSEC_DFL                 MBTRNPP_STAT_PERIOD_SEC
#define OPT_STATFLAGS_DFL               MBTRNPP_STAT_FLAGS_DFL
#define OPT_STATFLAG_STR_DFL            "MSF_STATUS|MSF_EVENT|MSF_ASTAT|MSF_PSTAT"
#define OPT_TRN_EN_DFL                  true
#define OPT_TRN_UTM_DFL                 TRN_UTM_DFL
#define OPT_MAP_DFL                     NULL
#define OPT_CFG_DFL                     NULL
#define OPT_PAR_DFL                     NULL
#define OPT_TRN_MDIR_DFL                "mb"
#define OPT_TRN_MTYPE_DFL               TRN_MTYPE_DFL
#define OPT_TRN_FTYPE_DFL               TRN_FTYPE_DFL
#define OPT_TRN_NCOV_DFL                TRN_MAX_NCOV_DFL
#define OPT_TRN_NERR_DFL                TRN_MAX_NERR_DFL
#define OPT_TRN_ECOV_DFL                TRN_MAX_ECOV_DFL
#define OPT_TRN_EERR_DFL                TRN_MAX_EERR_DFL
#define OPT_MB_OUT_DFL                  NULL
#define OPT_TRN_OUT_DFL                 NULL
#define OPT_TRN_DECN_DFL                0
#define OPT_TRN_DECS_DFL                0.0
#define OPT_TRN_NOMBGAIN_DFL            false
#define OPT_HELP_DFL                    false

#define CFG_INPUT_DFL                    "datalist.mb-1"

#define CFG_MNEM_SESSION       "SESSION"
#define CFG_MNEM_RHOST         "RESON_HOST"
#define CFG_MNEM_MBTRN_HOST    "MBTRN_HOST"
#define CFG_MNEM_TRN_SESSION   "TRN_SESSION"
#define CFG_MNEM_TRN_LOGFILES  "TRN_LOGFILES"
#define CFG_MNEM_TRN_MAPFILES  "TRN_MAPFILES"
#define CFG_MNEM_TRN_DATAFILES "TRN_DATAFILES"
#define CFG_MNEM_TRN_CFGFILES  "TRN_CFGFILES"
#define CFG_TRN_LOG_DIR_DFL    "."

#define MNEM_MAX_LEN 64
#define HOSTNAME_BUF_LEN 256
#define MB_PATH_SIZE 1024
#define MBOUT_OPT_N 16
#define MBSYSOUT_OPT_N 8
#define TRNOUT_OPT_N 16
#define SONAR_READER_CAPACITY_DFL (256 * 1024)
#define SESSION_BUF_LEN 16
#define TRNSESSION_BUF_LEN 9


//mb_path socket_definition;

#define SONAR_SIM_HOST "localhost"

#define MBTPP 1
//input_mode_t input_mode;
#define OUTPUT_FLAG_SET(m)  ((m&mbtrn_cfg->output_flags)==0 ? false : true)
#define OUTPUT_FLAG_CLR(m)  ((m&mbtrn_cfg->output_flags)==0 ? true : false)
#define OUTPUT_FLAGS_ZERO() ((mbtrn_cfg->output_flags==0) ? true : false)
//output_mode_t output_flags = OUTPUT_MBTRNPP_MSG;

//int64_t mbtrnpp_loop_delay_msec = 0;

#define MBTRN_CFG_NAME    "mbtrn.cfg"
#define MBTRN_CFG_PATH    "."

#define MB1_BLOG_NAME     "mb1"
#define MB1_BLOG_DESC     "mb1 binary data"
#define MBTRNPP_MLOG_NAME "mbtrnpp"
#define MBTRNPP_MLOG_DESC "mbtrnpp message log"
#define RESON_BLOG_NAME   "r7kbin"
#define RESON_BLOG_DESC   "reson 7k frame log"
#define TRN_ULOG_NAME     "trnu"
#define TRN_ULOG_DESC     "trn update log"
#define MBTRNPP_LOG_EXT   ".log"
#ifdef WITH_MBTNAV
#define UTM_MONTEREY_BAY 10L
#define UTM_AXIAL        12L
#define TRN_UTM_DFL      UTM_MONTEREY_BAY
#define TRN_MTYPE_DFL    TRN_MAP_BO
#define TRN_FTYPE_DFL    TRN_FILT_PARTICLE
#define TRN_OUT_DFL      (TRNW_ODEBUG|TRNW_OLOG)
#define TRNU_HOST_DFL    "localhost"
#define TRNU_PORT_DFL    8000
#define TRNSVR_HOST_DFL  "localhost"
#define TRNSVR_PORT_DFL  28000
#define TRN_XMIT_GAIN_RESON7K_DFL 200.0
#define TRN_XMIT_GAIN_KMALL_DFL -20.0

#endif //WITH_MBTNAV
#define SZ_1M (1024 * 1024)
#define SZ_1G (1024 * 1024 * 1024)
#define MBTRNPP_CMD_LINE_BYTES 2048

// MB1 socket output configuration
#define MB1SVR_HOST_DFL "localhost"
#define MB1SVR_PORT_DFL 27000
#define MB1SVR_MSG_CON_LEN 4
#define MB1SVR_HBTOK_DFL 50
#define MB1SVR_HBTO_DFL  0.0
#define TRNSVR_HBTO_DFL  0.0
#define TRNUSVR_HBTO_DFL 0.0

// MSF_STAT_FLAGS define stats processing options
// may include
// MSF_STATUS : status counters
// MSF_EVENT  : event/error counters
// MSF_ASTAT  : aggregate stats
// MSF_PSTAT  : periodic stats
// MSF_READER : r7kr reader stats
#define MBTRNPP_STAT_FLAGS_DFL (MSF_STATUS | MSF_EVENT | MSF_ASTAT | MSF_PSTAT)

mbtrnpp_opts_t mbtrn_opts_s, *mbtrn_opts=&mbtrn_opts_s;
mbtrnpp_cfg_t mbtrn_cfg_s, *mbtrn_cfg=&mbtrn_cfg_s;

static char program_name[] = "mbtrnpp";

char *mbtrn_cfg_path=NULL;

mlog_id_t mb1_blog_id = MLOG_ID_INVALID;
mlog_id_t mbtrnpp_mlog_id = MLOG_ID_INVALID;
mlog_id_t reson_blog_id = MLOG_ID_INVALID;
mlog_id_t trn_ulog_id = MLOG_ID_INVALID;

mlog_config_t mb1_blog_conf = {100 * SZ_1M, ML_NOLIMIT, ML_NOLIMIT, ML_OSEG | ML_LIMLEN, ML_FILE, ML_TFMT_ISO1806};
mlog_config_t mbtrnpp_mlog_conf = {ML_NOLIMIT, ML_NOLIMIT, ML_NOLIMIT, ML_MONO, ML_FILE, ML_TFMT_ISO1806};
mlog_config_t reson_blog_conf = {ML_NOLIMIT, ML_NOLIMIT, ML_NOLIMIT, ML_MONO, ML_FILE, ML_TFMT_ISO1806};
mlog_config_t trn_ulog_conf = {ML_NOLIMIT, ML_NOLIMIT, ML_NOLIMIT, ML_MONO, ML_FILE, ML_TFMT_ISO1806};

char *mb1_blog_path = NULL;
char *mbtrnpp_mlog_path = NULL;
char *reson_blog_path = NULL;
char *trn_ulog_path = NULL;

mfile_flags_t flags = MFILE_RDWR | MFILE_APPEND | MFILE_CREATE;
mfile_mode_t mode = MFILE_RU | MFILE_WU | MFILE_RG | MFILE_WG;

netif_t *mb1svr=NULL;
//int mb1svr_port=MB1SVR_PORT_DFL;
//char *mb1svr_host=MB1SVR_HOST_DFL;
//int mbsvr_hbtok = MB1SVR_HBTOK_DFL;
//double mbsvr_hbto = MB1SVR_HBTO_DFL;
//double trnsvr_hbto = TRNSVR_HBTO_DFL;
//double trnusvr_hbto = TRNUSVR_HBTO_DFL;

#ifdef WITH_MBTNAV
trn_config_t *trn_cfg = NULL;
//bool trn_enable = false;
//long int trn_utm_zone = TRN_UTM_DFL;
//int trn_mtype = TRN_MTYPE_DFL;
//int trn_ftype = TRN_FTYPE_DFL;
//char *trn_map_file = NULL;
//char *trn_cfg_file = NULL;
//char *trn_particles_file = NULL;
//char *trn_log_dir = NULL;
//unsigned int trn_decn=0;
//double trn_decs=0.0;
unsigned int trn_dec_cycles=0;
double trn_dec_time=0.0;
wtnav_t *trn_instance = NULL;
trnw_oflags_t trn_oflags=TRN_OUT_DFL;
//double trn_max_ncov=TRN_MAX_NCOV_DFL;
//double trn_max_nerr=TRN_MAX_NERR_DFL;
//double trn_max_ecov=TRN_MAX_ECOV_DFL;
//double trn_max_eerr=TRN_MAX_EERR_DFL;

netif_t *trnsvr=NULL;
//int trnsvr_port=TRNSVR_PORT_DFL;
//char *trnsvr_host=TRNSVR_HOST_DFL;

netif_t *trnusvr=NULL;
//int trnusvr_port=TRNU_PORT_DFL;
//char *trnusvr_host=TRNU_HOST_DFL;

#endif // WITH_MBTNAV

//char g_cmd_line[MBTRNPP_CMD_LINE_BYTES] = {0};

//char *trn_log_dir = NULL;

typedef enum{RF_NONE=0,RF_FORCE_UPDATE=0x1,RF_RELEASE=0x2}mb_resource_flag_t;

// profiling - event channels
typedef enum {
  MBTPP_EV_MB_CYCLES = 0,
  MBTPP_EV_MB_CONN,
  MBTPP_EV_MB_DISN,
  MBTPP_EV_MB_PUBN,
    MBTPP_EV_MB_TRN_REINIT,
    MBTPP_EV_MB_GAIN_LO,
    MBTPP_EV_EMBGETALL,
    MBTPP_EV_EMBFAILURE,
    MBTPP_EV_EMBFRAMERD,
    MBTPP_EV_EMBLOGWR,
    MBTPP_EV_EMBSOCKET,
    MBTPP_EV_EMBCON,
#ifdef WITH_MBTNAV
    MBTPP_EV_TRN_PROCN,
    MBTPP_EV_TRNU_PUBN,
#endif
    MBTPP_EV_COUNT
} mbtrnpp_stevent_id;

// profiling - status channels
typedef enum {
    MBTPP_STA_MB_FWRITE_BYTES=0,
    MBTPP_STA_MB_SYNC_BYTES,
    MBTPP_STA_COUNT
} mbtrnpp_ststatus_id;

// profiling - measurement channels
typedef enum {
  MBTPP_CH_MB_GETALL_XT = 0,
  MBTPP_CH_MB_PING_XT,
  MBTPP_CH_MB_LOG_XT,
  MBTPP_CH_MB_DTIME_XT,
  MBTPP_CH_MB_GETFAIL_XT,
  MBTPP_CH_MB_POST_XT,
  MBTPP_CH_MB_STATS_XT,
  MBTPP_CH_MB_CYCLE_XT,
  MBTPP_CH_MB_FWRITE_XT,
  MBTPP_CH_MB_PROC_MB1_XT,
#ifdef WITH_MBTNAV
    MBTPP_CH_TRN_UPDATE_XT,
    MBTPP_CH_TRN_BIASEST_XT,
    MBTPP_CH_TRN_NREINITS_XT,
    MBTPP_CH_TRN_TRNU_PUB_XT,
    MBTPP_CH_TRN_TRNU_LOG_XT,
    MBTPP_CH_TRN_PROC_XT,
    MBTPP_CH_TRN_TRNSVR_XT,
    MBTPP_CH_TRN_TRNUSVR_XT,
    MBTPP_CH_TRN_PROC_TRN_XT,
#endif
    MBTPP_CH_COUNT
} mbtrnpp_stchan_id;

// profiling - event channel labels
const char *mbtrnpp_stevent_labels[] = {
    "mb_cycles", "mb_con", "mb_dis", "mb_pub_n", "mb_trn_reinit", "mb_gain_lo",
    "e_mbgetall", "e_mbfailure", "e_mb_frame_rd", "e_mb_log_wr", "e_mbsocket",
    "e_mbcon"
#ifdef WITH_MBTNAV
    ,"trn_proc_n","trnu_pub_n"
#endif
};

// profiling - status channel labels
const char *mbtrnpp_ststatus_labels[] = {
    "mb_fwrite_bytes",
    "mb_sync_bytes"
};

// profiling - measurement channel labels
const char *mbtrnpp_stchan_labels[] = {
    "mb_getall_xt",  "mb_ping_xt", "mb_log_xt", "mb_dtime_xt",
    "mb_getfail_xt", "mb_post_xt", "mb_stats_xt", "mb_cycle_xt", "mb_fwrite_xt",
    "mb_proc_mb1_xt"
#ifdef WITH_MBTNAV
    , "trn_update_xt", "trn_biasest_xt", "trn_nreinits_xt",
    "trn_trnu_pub_xt", "trn_trnu_log_xt", "trn_proc_xt",
    "trn_trnsvr_xt", "trn_trnusvr_xt", "trn_proc_trn_xt"
#endif
};

const char **mbtrnpp_stats_labels[MSLABEL_COUNT] = {mbtrnpp_stevent_labels, mbtrnpp_ststatus_labels, mbtrnpp_stchan_labels};
mstats_profile_t *app_stats = NULL;
mstats_t *reader_stats = NULL;
//double trn_status_interval_sec = MBTRNPP_STAT_PERIOD_SEC;
// stats interval end
static double stats_prev_end = 0.0;
// stats interval start
static double stats_prev_start = 0.0;
// system clock resolution logging enable/disable
static bool log_clock_res = true;

#ifdef MST_STATS_EN
#define MBTRNPP_UPDATE_STATS(p, l, f) (mbtrnpp_update_stats(p, l, f))
#else
#define MBTRNPP_UPDATE_STATS(p, l, f)
#endif // MST_STATS_EN

//mstats_flags mbtrnpp_stat_flags = MBTRNPP_STAT_FLAGS_DFL;

int mbtrnpp_openlog(int verbose, mb_path log_directory, FILE **logfp, int *error);
int mbtrnpp_closelog(int verbose, FILE **logfp, int *error);
int mbtrnpp_postlog(int verbose, FILE *logfp, char *message, int *error);
int mbtrnpp_logparameters(int verbose, FILE *logfp, char *input, int format, char *output, double swath_width,
                          int n_output_soundings, bool median_filter, int median_filter_n_across, int median_filter_n_along,
                          double median_filter_threshold, int n_buffer_max, int *error);
int mbtrnpp_logstatistics(int verbose, FILE *logfp, int n_pings_read, int n_soundings_read, int n_soundings_valid_read,
                          int n_soundings_flagged_read, int n_soundings_null_read, int n_soundings_trimmed,
                          int n_soundings_decimated, int n_soundings_flagged, int n_soundings_written, int *error);
int mbtrnpp_init_debug(int verbose);

int mbtrnpp_reson7kr_input_open(int verbose, void *mbio_ptr, char *definition, int *error);
int mbtrnpp_reson7kr_input_read(int verbose, void *mbio_ptr, size_t *size, char *buffer, int *error);
int mbtrnpp_reson7kr_input_close(int verbose, void *mbio_ptr, int *error);
int mbtrnpp_kemkmall_input_open(int verbose, void *mbio_ptr, char *definition, int *error);
int mbtrnpp_kemkmall_input_read(int verbose, void *mbio_ptr, size_t *size, char *buffer, int *error);
int mbtrnpp_kemkmall_input_close(int verbose, void *mbio_ptr, int *error);

// Configuration helper functions

// get TRN session string (YYYY-DDD used in TRN log directories)
static char *s_mbtrnpp_trnsession_str(char **pdest, size_t len, mb_resource_flag_t flags);
// get mbtrnpp session string (YYYYMMDD-hhmmss used in log names)
static char *s_mbtrnpp_session_str(char **pdest, size_t len, mb_resource_flag_t flags);
// get mbtrnpp command line string (used in logs, debugging)
static char *s_mbtrnpp_cmdline_str(char **pdest, size_t len, int argc, char **argv, mb_resource_flag_t flags);
// show mbtrnpp config struct contents
static int s_mbtrnpp_show_cfg(mbtrnpp_cfg_t *self, bool verbose, int indent);
// show mbtrnpp option struct contents
static int s_mbtrnpp_show_opts(mbtrnpp_opts_t *opts, bool verbose, int indent);
// get config mnemonic value
char *s_mnem_value(char **pdest, size_t len, const char *key);
// substitute mnemonic value into string
// (dest must be dynamically allocated, caller must free)
char *s_sub_mnem(char **pdest, size_t len, char *src,const char *pkey,const char *pval);
// test mnemonic substitution
static int s_test_mnem();
// initialize mbtrnpp config struct
static int s_mbtrnpp_init_cfg(mbtrnpp_cfg_t *cfg);
// initialize mbtrnpp options struct
static int s_mbtrnpp_init_opts(mbtrnpp_opts_t *opts);
// release dynamically allocated resources in options struct
static void s_mbtrnpp_free_opts(mbtrnpp_opts_t **pself);
// release dynamically allocated resources in config struct
static void s_mbtrnpp_free_cfg(mbtrnpp_cfg_t **pself);
// parse option: --output
static int s_parse_opt_output(mbtrnpp_cfg_t *cfg, char *opt_str);
// parse option: --mbout
static int s_parse_opt_mbout(mbtrnpp_cfg_t *cfg, char *opt_str);
// parse option: --trnout
static int s_parse_opt_trnout(mbtrnpp_cfg_t *cfg, char *opt_str);
// parse option: --logdir
static int s_parse_opt_logdir(mbtrnpp_cfg_t *cfg, char *opt_str);
// get --config option from cmdline, if provided
static char *s_mbtrnpp_peek_opt_cfg(int argc, char **argv, char **buf, size_t len);

// Primary configuration functions

// key/value parsing function (mkvc_parser_fn)
static int s_mbtrnpp_kvparse_fn(char *key, char *val, void *opts);
// load configuration file (override run-time defaults)
static int s_mbtrnpp_load_config(char *config_path, mbtrnpp_opts_t *opts);
// load command line options (override run-time/config file defaults)
static int s_mbtrnpp_process_cmdline(int argc, char **argv, mbtrnpp_opts_t *opts);
// parse options to configuration values
static int s_mbtrnpp_configure(mbtrnpp_cfg_t *cfg, mbtrnpp_opts_t *opts);
// validate configuration
static int s_mbtrnpp_validate_config(mbtrnpp_cfg_t *cfg);

int mbtrnpp_update_stats(mstats_profile_t *stats, mlog_id_t log_id, mstats_flags flags);
int mbtrnpp_process_mb1(char *mb1, size_t len, trn_config_t *cfg);

#ifdef WITH_MBTNAV
int mbtrnpp_init_trn(wtnav_t **pdest, int verbose, trn_config_t *cfg);
int mbtrnpp_init_trnsvr(netif_t **psvr, wtnav_t *trn, char *host, int port, bool verbose);
int mbtrnpp_init_mb1svr(netif_t **psvr, char *host, int port, bool verbose);
int mbtrnpp_init_trnusvr(netif_t **psvr, char *host, int port, bool verbose);
int mbtrnpp_trn_process_mb1(wtnav_t *tnav, mb1_t *mb1, trn_config_t *cfg);
int mbtrnpp_trn_update(wtnav_t *self, mb1_t *src, wposet_t **pt_out, wmeast_t **mt_out, trn_config_t *cfg);
int mbtrnpp_trn_get_bias_estimates(wtnav_t *self, wposet_t *pt, trn_update_t *pstate);
int mbtrnpp_trn_publish(trn_update_t *pstate, trn_config_t *cfg);

int mbtrnpp_trn_pub_ostream(trn_update_t *update, FILE *stream);
int mbtrnpp_trn_pub_odebug(trn_update_t *update);
int mbtrnpp_trn_pub_olog(trn_update_t *update, mlog_id_t log_id);
int mbtrnpp_trn_pub_osocket(trn_update_t *update, msock_socket_t *pub_sock);
char *mbtrnpp_trn_updatestr(char *dest, int len, trn_update_t *update, int indent);
#endif // WITH_MBTNAV

// arm the TRN reinit flag
// reinit TRN when sonar transmit gain above threshold
// or if trn_nombgain is true
bool trn_reinit_flag=true;
//bool trn_nombgain=false;

char mRecordBuf[MBSYS_KMBES_MAX_NUM_MRZ_DGMS][64*1024];
/*--------------------------------------------------------------------*/

static char *s_mbtrnpp_trnsession_str(char **pdest, size_t len, mb_resource_flag_t flags)
{
    static bool initialized=false;
    static char session_date[TRNSESSION_BUF_LEN] = {0};
    char *retval=session_date;

    // lazy initialize session time string to use
    // in log file names
    time_t rawtime;
    struct tm *gmt;

    time(&rawtime);
    // Get GMT time
    gmt = gmtime(&rawtime);

    if(!initialized || ((flags&RF_FORCE_UPDATE)!=0)){
        initialized=true;
        // format YYYY.DDD
        strftime(session_date,TRNSESSION_BUF_LEN,"%Y.%j",gmt);
    }

    if(NULL!=pdest){
        // return requested
        if(NULL==*pdest){
            *pdest=CHK_STRDUP(session_date);
            retval=*pdest;
        }else {
            if(len>=TRNSESSION_BUF_LEN){
                sprintf(*pdest,"%s",session_date);
                retval=*pdest;
            }else{
                fprintf(stderr,"ERR - dest buffer too small");
            }
        }
    }
    return retval;
}
static char *s_mbtrnpp_session_str(char **pdest, size_t len, mb_resource_flag_t flags)
{
    static bool initialized=false;
    static char session_date[SESSION_BUF_LEN] = {0};
    char *retval=session_date;

    // lazy initialize session time string to use
    // in log file names
    time_t rawtime;
    struct tm *gmt;

    time(&rawtime);
    // Get GMT time
    gmt = gmtime(&rawtime);

    if(!initialized || ((flags&RF_FORCE_UPDATE)!=0)){
        initialized=true;
        // format YYYYMMDD-HHMMSS
        sprintf(session_date, "%04d%02d%02d-%02d%02d%02d", (gmt->tm_year + 1900), gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour,
                gmt->tm_min, gmt->tm_sec);
    }

    if(NULL!=pdest){
        // return requested
        if(NULL==*pdest){
            *pdest=CHK_STRDUP(session_date);
            retval=*pdest;
        }else {
            if(len>=SESSION_BUF_LEN){
                sprintf(*pdest,"%s",session_date);
                retval=*pdest;
            }else{
                fprintf(stderr,"ERR - dest buffer too small");
            }
        }
    }
    return retval;
}

static char *s_mbtrnpp_cmdline_str(char **pdest, size_t len, int argc, char **argv, mb_resource_flag_t flags)
{
    static bool initialized=false;
    static char *cmd_line=NULL;
    static size_t slen=0;
    char *retval=cmd_line;

    if(argc>0 && NULL!=argv){
        // lazy initialize command line string
        if(!initialized || ((flags&RF_FORCE_UPDATE)!=0)){
            initialized=true;
            MEM_CHKINVALIDATE(cmd_line);
            // calculate buffer len (+1 for NULL)
            len=1;
            int i=0;
            for(i=0;i<argc;i++){
                // arg len + space
                slen+=strlen(argv[i])+1;
            }
            // allocate buffer
            cmd_line=(char *)malloc(slen*sizeof(char));
            if(NULL!=cmd_line){
                memset(cmd_line,0,len);
                char *ip=cmd_line;
                for(i=0;i<argc;i++){
                    sprintf(ip,"%s%s",(i==0?"":" "),argv[i]);
                    ip=cmd_line+strlen(cmd_line);
                }
            }else{
                len=0;
                initialized=false;
            }
        }
        if((flags&RF_RELEASE)!=0){
            MEM_CHKINVALIDATE(cmd_line);
        }
        if(NULL!=pdest){
            // return requested
            if(NULL==*pdest){
                *pdest=CHK_STRDUP(cmd_line);
                retval=*pdest;
            }else {
                if(len>=slen){
                    sprintf(*pdest,"%s",cmd_line);
                    retval=*pdest;
                }else{
                    fprintf(stderr,"ERR - dest buffer too small");
                }
            }
        }
    }
    return retval;
}

char *s_mnem_value(char **pdest, size_t len, const char *key)
{
    char *retval=NULL;
    if( NULL!=key){

        char *val=NULL;
        char *alt=NULL;

        if(strcmp(key,CFG_MNEM_RHOST)==0){
            val=CHK_STRDUP(getenv(key));
            if(NULL==val){
                // if unset, use local IP
                char host[HOSTNAME_BUF_LEN]={0};
                struct hostent *host_entry;
                if(gethostname(host, HOSTNAME_BUF_LEN)==0 && strlen(host)>0){

                    if( (host_entry = gethostbyname(host))!=NULL){
                        //Convert into IP string
                        char *s =inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));
                        val = CHK_STRDUP(s);
                    } //find host information
                }
                if(NULL==val){
                    val=CHK_STRDUP("localhost");
                }
            }
        }else if(strcmp(key,CFG_MNEM_SESSION)==0){

            val=CHK_STRDUP(s_mbtrnpp_session_str(NULL,0,RF_NONE));

        }else if(strcmp(key,CFG_MNEM_TRN_SESSION)==0){

            val=CHK_STRDUP(s_mbtrnpp_trnsession_str(NULL,0,RF_NONE));

        }else if(strcmp(key,CFG_MNEM_MBTRN_HOST)==0){

            // try env
            val=CHK_STRDUP(getenv(key));

            if(NULL==val){
                // if unset, use local IP
                char host[HOSTNAME_BUF_LEN]={0};
                struct hostent *host_entry;
                if(gethostname(host, HOSTNAME_BUF_LEN)==0 && strlen(host)>0){

                    if( (host_entry = gethostbyname(host))!=NULL){
                        //Convert into IP string
                        char *s =inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));
                        val = CHK_STRDUP(s);
                    } //find host information
                }
                if(NULL==val){
                    val=CHK_STRDUP("localhost");
                }
            }
        }else if(strcmp(key,CFG_MNEM_TRN_LOGFILES)==0 ||
                 strcmp(key,CFG_MNEM_TRN_MAPFILES)==0 ||
                 strcmp(key,CFG_MNEM_TRN_CFGFILES)==0 ||
                 strcmp(key,CFG_MNEM_TRN_DATAFILES)==0){
            // try env
            val=CHK_STRDUP(getenv(key));
            alt=".";
       }// else unsupported option

        if(NULL!=val || NULL!=alt){
            char *dest=NULL;
            size_t dlen = (NULL!=val ? strlen(val)+1 : strlen(alt)+1);
            if(NULL==pdest){
                dest=(char *)malloc(dlen);
                memset(dest,0,dlen);
                retval=dest;
            }else{
                if(NULL!=*pdest){
                    if(len>0){
                        // must fit
                        if(len<=dlen){
                            // OK
                            dest=*pdest;
                        }else{
                            fprintf(stderr,"%s - dest buffer too small\n",__func__);
                        }
                    }else{
                        // OK to realloc it
                        *pdest = (char *)realloc(*pdest,dlen);
                        dest=*pdest;
                        retval=*pdest;
                    }
                }else{
                    *pdest=(char *)malloc(dlen);
                    memset(*pdest,0,dlen);
                    retval=*pdest;
                    dest=*pdest;
                }
            }
            if(NULL!=dest){
                sprintf(dest,"%s",(NULL!=val ? val : alt));
            }else{PTRACE();}
//            fprintf(stderr,"%s:%d - dest[%p/%s] pdest[%p/%s] retval[%s]\n",__func__,__LINE__,dest,dest,*pdest,*pdest,retval);

        }else{PTRACE();}

        MEM_CHKINVALIDATE(val);
    }// else invalid arg
    return retval;
}

char *s_sub_mnem(char **pdest, size_t len, char *src,const char *pkey,const char *pval)
{

    char *retval=NULL;
    if(NULL!=src && NULL!=pkey && strlen(pkey)>0 && NULL!=pval){
        char *result;
        int i, cnt = 0;
        int vlen = strlen(pval);
        int klen = strlen(pkey);

        // Counting the number of times old word
        // occur in the string
        for (i = 0; src[i] != '\0'; i++){
            if (strstr(&src[i], pkey) == &src[i]){
                cnt++;
                // Jumping to index after the old word.
                i += klen - 1;
            }
        }

        if(cnt>0){
           size_t new_size=(i + cnt * (vlen - klen) + 1);
            // Making new string of enough length
            result = (char *)malloc(new_size);

            i = 0;
            char *cur=src;
            while (*cur){
                // compare the substring with the result
                if (strstr(cur, pkey) == cur){
                    strcpy(&result[i], pval);
                    i += vlen;
                    cur += klen;
                }else{
                    result[i++] = *cur++;
                }
            }

            result[i] = '\0';

            if(NULL==pdest){
                *pdest=result;
            }else if(NULL==*pdest){
                *pdest=result;
            }else if(*pdest==src){
                if(len==0){
                    // OK to realloc
                    *pdest=(char *)realloc(*pdest,new_size);
                    sprintf(*pdest,"%s",result);
                }else if(len>0 && strlen(result)<=len){
                    sprintf(*pdest,"%s",result);
                }else{
                    fprintf(stderr,"ERR - dest buffer too small [%zu/%zu]\n",len,new_size);
                }
                MEM_CHKFREE(result);
            }else{
                if(len>0 && strlen(result)<=len){
                    sprintf(*pdest,"%s",result);
                }else{
                    fprintf(stderr,"ERR - dest buffer too small [%zu/%zu]\n",len,new_size);
                }
                // not returning result, free
                MEM_CHKFREE(result);
            }
            retval=*pdest;
        }
    }
//    fprintf(stderr,"%d - ret dest[%s]\n",__LINE__,*pdest);
    return retval;
}

static int s_test_mnem()
{
    char *opt_session = strdup("test_session-SESSION--");
    char *opt_rhost=strdup("test_rhost-RESON_HOST--");
    char *opt_mbtrnhost=strdup("test_mbtrnhost-MBTRN_HOST--");
    char *opt_trnsession = strdup("test_trnsession-TRN_SESSION--");
    char *opt_trnlog = strdup("test_trnlog-TRN_LOGFILES--");
    char *opt_trnmap = strdup("test_trnmap-TRN_MAPFILES--");
    char *opt_trndata = strdup("test_trndata-TRN_DATAFILES--");
    char *opt_trncfg = strdup("test_trncfg-TRN_CFGFILES--");

    char *val=NULL;
    s_sub_mnem(&opt_session,0,opt_session,CFG_MNEM_SESSION,s_mnem_value(&val,0,CFG_MNEM_SESSION));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_rhost,0,opt_rhost,CFG_MNEM_RHOST,s_mnem_value(&val,0,CFG_MNEM_RHOST));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_mbtrnhost,0,opt_mbtrnhost,CFG_MNEM_MBTRN_HOST,s_mnem_value(&val,0,CFG_MNEM_MBTRN_HOST));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_trnsession,0,opt_trnsession,CFG_MNEM_TRN_SESSION,s_mnem_value(&val,0,CFG_MNEM_TRN_SESSION));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_trnlog,0,opt_trnlog,CFG_MNEM_TRN_LOGFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_LOGFILES));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_trnmap,0,opt_trnmap,CFG_MNEM_TRN_MAPFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_MAPFILES));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_trndata,0,opt_trndata,CFG_MNEM_TRN_DATAFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_DATAFILES));
    MEM_CHKINVALIDATE(val);
    s_sub_mnem(&opt_trncfg,0,opt_trncfg,CFG_MNEM_TRN_CFGFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_CFGFILES));
    MEM_CHKINVALIDATE(val);

    fprintf(stderr,"%s:%d - opt_session    [%s]\n",__func__,__LINE__,opt_session);
    fprintf(stderr,"%s:%d - opt_rhost      [%s]\n",__func__,__LINE__,opt_rhost);
    fprintf(stderr,"%s:%d - opt_mbtrnhost  [%s]\n",__func__,__LINE__,opt_mbtrnhost);
    fprintf(stderr,"%s:%d - opt_trnsession [%s]\n",__func__,__LINE__,opt_trnsession);
    fprintf(stderr,"%s:%d - opt_trnlog     [%s]\n",__func__,__LINE__,opt_trnlog);
    fprintf(stderr,"%s:%d - opt_trnmap     [%s]\n",__func__,__LINE__,opt_trnmap);
    fprintf(stderr,"%s:%d - opt_trndata    [%s]\n",__func__,__LINE__,opt_trndata);
    fprintf(stderr,"%s:%d - opt_trncfg     [%s]\n",__func__,__LINE__,opt_trncfg);

    MEM_CHKFREE(opt_session);
    MEM_CHKFREE(opt_rhost);
    MEM_CHKFREE(opt_mbtrnhost);
    MEM_CHKFREE(opt_trnsession);
    MEM_CHKFREE(opt_trnlog);
    MEM_CHKFREE(opt_trnmap);
    MEM_CHKFREE(opt_trncfg);
    MEM_CHKFREE(opt_trndata);

    return 0;
}

static int s_mbtrnpp_init_cfg(mbtrnpp_cfg_t *cfg)
{
    int retval =-1;
    if(NULL!=cfg){
        cfg->verbose=0;
        cfg->input_mode=INPUT_MODE_FILE;
        memset(cfg->socket_definition,0,MB_PATH_SIZE);
        memset(cfg->output_file,0,MB_PATH_SIZE);
        memset(cfg->input,0,MB_PATH_SIZE);
        sprintf(cfg->input,"%s",CFG_INPUT_DFL);
        cfg->format=0;
        memset(cfg->platform_file,0,MB_PATH_SIZE);
        cfg->use_platform_file=false;
        cfg->target_sensor=-1;
        memset(cfg->log_directory,0,MB_PATH_SIZE);
        cfg->make_logs=false;
        cfg->trn_log_dir=CHK_STRDUP(CFG_TRN_LOG_DIR_DFL);
        cfg->swath_width=150;
        cfg->n_output_soundings=101;
        cfg->median_filter_threshold=0.5;
        cfg->median_filter_n_across=1;
        cfg->median_filter_n_along=1;
        cfg->median_filter_en=false;
        cfg->n_buffer_max=1;

        cfg->mb1svr_host=CHK_STRDUP(MB1SVR_HOST_DFL);
        cfg->mb1svr_port=MB1SVR_PORT_DFL;
        cfg->trnsvr_port=TRNSVR_PORT_DFL;
        cfg->trnsvr_host=CHK_STRDUP(TRNSVR_HOST_DFL);
        cfg->trnusvr_port=TRNU_PORT_DFL;
        cfg->trnusvr_host=CHK_STRDUP(TRNU_HOST_DFL);
        cfg->output_flags=OUTPUT_MBTRNPP_MSG;
        cfg->mbsvr_hbtok=MB1SVR_HBTOK_DFL;
        cfg->mbsvr_hbto=MB1SVR_HBTO_DFL;
        cfg->trnsvr_hbto=TRNSVR_HBTO_DFL;
        cfg->trnusvr_hbto=TRNUSVR_HBTO_DFL;
        cfg->mbtrnpp_loop_delay_msec=0;
        cfg->trn_status_interval_sec=MBTRNPP_STAT_PERIOD_SEC;
        cfg->mbtrnpp_stat_flags=MBTRNPP_STAT_FLAGS_DFL;
        cfg->trn_enable=false;
        cfg->trn_utm_zone=TRN_UTM_DFL;
        cfg->trn_mtype=TRN_MTYPE_DFL;
        cfg->trn_ftype=TRN_FTYPE_DFL;
        cfg->trn_max_ncov=TRN_MAX_NCOV_DFL;
        cfg->trn_max_nerr=TRN_MAX_NERR_DFL;
        cfg->trn_max_ecov=TRN_MAX_ECOV_DFL;
        cfg->trn_max_eerr=TRN_MAX_EERR_DFL;
        char *trn_map_file=NULL;
        char *trn_cfg_file=NULL;
        char *trn_particles_file=NULL;
        char *trn_mission_id=NULL;
        cfg->trn_decn=0;
        cfg->trn_decs=0.0;
        cfg->trn_nombgain=false;
        retval=0;
    }
    return retval;
}
static int s_mbtrnpp_init_opts(mbtrnpp_opts_t *opts)
{
    int retval =-1;
    if(NULL!=opts){
        opts->verbose=OPT_VERBOSE_DFL;
        opts->input=CHK_STRDUP(OPT_INPUT_DFL);
        opts->format=OPT_FORMAT_DFL;
        opts->platform_file=CHK_STRDUP(OPT_PLATFORM_FILE_DFL);
        opts->platform_target_sensor=OPT_PLATFORM_TARGET_SENSOR_DFL;
        opts->log_directory=CHK_STRDUP(OPT_LOG_DIRECTORY_DFL);
        opts->output=CHK_STRDUP(OPT_OUTPUT_DFL);
        opts->projection=OPT_PROJECTION_DFL;
        opts->swath_width=OPT_SWATH_WIDTH_DFL;
        opts->soundings=OPT_SOUNDINGS_DFL;
        opts->median_filter=CHK_STRDUP(OPT_MEDIAN_FILTER_DFL);
        opts->mbhbn=OPT_MBHBN_DFL;
        opts->mbhbt=OPT_MBHBT_DFL;
        opts->trnhbt=OPT_TRNHBT_DFL;
        opts->trnuhbt=OPT_TRNUHBT_DFL;
        opts->delay=OPT_DELAY_DFL;
        opts->statsec=OPT_STATSEC_DFL;
        opts->statflags_str=CHK_STRDUP(OPT_STATFLAG_STR_DFL);
        opts->statflags=OPT_STATFLAGS_DFL;
        opts->trn_en=OPT_TRN_EN_DFL;
        opts->trn_utm=OPT_TRN_UTM_DFL;
        opts->trn_map=CHK_STRDUP(OPT_MAP_DFL);
        opts->trn_cfg=CHK_STRDUP(OPT_CFG_DFL);
        opts->trn_par=CHK_STRDUP(OPT_PAR_DFL);
        opts->trn_mid=CHK_STRDUP(OPT_TRN_MDIR_DFL);
        opts->trn_mtype=OPT_TRN_MTYPE_DFL;
        opts->trn_ftype=OPT_TRN_FTYPE_DFL;
        opts->trn_ncov=OPT_TRN_NCOV_DFL;
        opts->trn_nerr=OPT_TRN_NERR_DFL;
        opts->trn_ecov=OPT_TRN_ECOV_DFL;
        opts->trn_eerr=OPT_TRN_EERR_DFL;
        opts->mb_out=CHK_STRDUP(OPT_MB_OUT_DFL);
        opts->trn_out=CHK_STRDUP(OPT_TRN_OUT_DFL);
        opts->trn_decn=OPT_TRN_DECN_DFL;
        opts->trn_decs=OPT_TRN_DECS_DFL;
        opts->trn_nombgain=OPT_TRN_NOMBGAIN_DFL;
        opts->help=OPT_HELP_DFL;
        retval=0;
    }
    return retval;
}

static void s_mbtrnpp_free_opts(mbtrnpp_opts_t **pself)
{
    if(NULL!=pself && NULL!=*pself){
        mbtrnpp_opts_t *self=*pself;
        MEM_CHKFREE(self->input);
        MEM_CHKFREE(self->platform_file);
        MEM_CHKFREE(self->log_directory);
        MEM_CHKFREE(self->output);
        MEM_CHKFREE(self->median_filter);
        MEM_CHKFREE(self->statflags_str);
        MEM_CHKFREE(self->trn_map);
        MEM_CHKFREE(self->trn_cfg);
        MEM_CHKFREE(self->trn_par);
        MEM_CHKFREE(self->trn_mid);
        MEM_CHKFREE(self->mb_out);
        MEM_CHKFREE(self->trn_out);
    }
}

static void s_mbtrnpp_free_cfg(mbtrnpp_cfg_t **pself)
{
    if(NULL!=pself && NULL!=*pself){
        mbtrnpp_cfg_t *self=*pself;
        MEM_CHKFREE(self->trn_log_dir);
        MEM_CHKFREE(self->mb1svr_host);
        MEM_CHKFREE(self->trnsvr_host);
        MEM_CHKFREE(self->trnusvr_host);
        MEM_CHKFREE(self->trn_map_file);
        MEM_CHKFREE(self->trn_cfg_file);
        MEM_CHKFREE(self->trn_particles_file);
        MEM_CHKFREE(self->trn_mission_id);
    }
}

static int s_mbtrnpp_show_cfg(mbtrnpp_cfg_t *self, bool verbose, int indent)
{
    int retval=0;

    if(NULL!=self){
        int wkey=25;
        int wval=30;
        retval+=fprintf(stderr,"%*s %*s  %*p\n",indent,(indent>0?" ":""), wkey,"self",wval,self);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"verbose",wval,self->verbose);
        retval+=fprintf(stderr,"%*s %*s  %*u\n",indent,(indent>0?" ":""), wkey,"input_mode",wval,self->input_mode);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"input",wval,self->input);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"socket_definition",wval,self->socket_definition);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"output_file",wval,self->output_file);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"format",wval,self->format);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"platform-file",wval,self->platform_file);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"use_platform_file",wval,BOOL2YNC(self->use_platform_file));
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"platform-target-sensor",wval,self->target_sensor);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"log-directory",wval,self->log_directory);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn_log_dir",wval,self->trn_log_dir);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"make_logs",wval,BOOL2YNC(self->make_logs));
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"platform-file",wval,BOOL2YNC(self->make_logs));
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"swath-width",wval,self->swath_width);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"n_output_soundings",wval,self->n_output_soundings);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"median_filter_threshold",wval,self->median_filter_threshold);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"median_filter_n_across",wval,self->median_filter_n_across);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"median_filter_n_along",wval,self->median_filter_n_along);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"median_filter_en",wval,BOOL2YNC(self->median_filter_en));
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"n_buffer_max",wval,self->n_buffer_max);

        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"mb1svr_host",wval,self->mb1svr_host);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"mb1svr_port",wval,self->mb1svr_port);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trnsvr_host",wval,self->trnsvr_host);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"trnsvr_port",wval,self->trnsvr_port);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trnusvr_host",wval,self->trnsvr_host);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"trnusvr_port",wval,self->trnusvr_port);
        retval+=fprintf(stderr,"%*s %*s  %*X\n",indent,(indent>0?" ":""), wkey,"output_flags",wval,self->output_flags);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"mbsvr_hbtok",wval,self->mbsvr_hbtok);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"mbsvr_hbto",wval,self->mbsvr_hbto);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trnsvr_hbto",wval,self->trnsvr_hbto);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trnusvr_hbto",wval,self->trnusvr_hbto);
        retval+=fprintf(stderr,"%*s %*s  %*"PRId64"\n",indent,(indent>0?" ":""), wkey,"mbtrnpp_loop_delay_msec",wval,self->mbtrnpp_loop_delay_msec);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn_status_interval_sec",wval,self->trn_status_interval_sec);
        retval+=fprintf(stderr,"%*s %*s  %*X\n",indent,(indent>0?" ":""), wkey,"mbtrnpp_stat_flags",wval,self->mbtrnpp_stat_flags);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"trn_enable",wval,BOOL2YNC(self->trn_enable));
        retval+=fprintf(stderr,"%*s %*s  %*ld\n",indent,(indent>0?" ":""), wkey,"trn_utm_zone",wval,self->trn_utm_zone);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"trn_mtype",wval,self->trn_mtype);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"trn_ftype",wval,self->trn_ftype);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn_max_ncov",wval,self->trn_max_ncov);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn_max_nerr",wval,self->trn_max_nerr);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn_max_ecov",wval,self->trn_max_ecov);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn_max_eerr",wval,self->trn_max_eerr);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn_map_file",wval,self->trn_map_file);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn_cfg_file",wval,self->trn_cfg_file);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn_particles_file",wval,self->trn_particles_file);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn_mission_dir",wval,self->trn_mission_id);
        retval+=fprintf(stderr,"%*s %*s  %*u\n",indent,(indent>0?" ":""), wkey,"trn_decn",wval,self->trn_decn);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn_decs",wval,self->trn_decs);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"trn_nombgain",wval,BOOL2YNC(self->trn_nombgain));
    }

    return retval;
}

static int s_mbtrnpp_show_opts(mbtrnpp_opts_t *self, bool verbose, int indent){
    int retval=0;

    if(NULL!=self){
        int wkey=25;
        int wval=30;
        retval+=fprintf(stderr,"%*s %*s  %*p\n",indent,(indent>0?" ":""), wkey,"self",wval,self);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"verbose",wval,self->verbose);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"input",wval,self->input);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"format",wval,self->format);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"platform-file",wval,self->platform_file);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"platform-target-sensor",wval,self->platform_target_sensor);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"log-directory",wval,self->log_directory);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"output",wval,self->output);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"projection",wval,self->projection);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"swath-width",wval,self->swath_width);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"soundings",wval,self->soundings);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"median-filter",wval,self->median_filter);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"mbhbn",wval,self->mbhbn);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"mbhbt",wval,self->mbhbt);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trnhbt",wval,self->trnhbt);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trnuhbt",wval,self->trnuhbt);
        retval+=fprintf(stderr,"%*s %*s  %*"PRId64"\n",indent,(indent>0?" ":""), wkey,"delay",wval,self->delay);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"statsec",wval,self->statsec);
        retval+=fprintf(stderr,"%*s %*s  %*X/%s\n",indent,(indent>0?" ":""), wkey,"statflags",wval,self->statflags,self->statflags_str);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"trn-en",wval,BOOL2YNC(self->trn_en));
        retval+=fprintf(stderr,"%*s %*s  %*ld\n",indent,(indent>0?" ":""), wkey,"trn-utm",wval,self->trn_utm);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn-map",wval,self->trn_map);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn-cfg",wval,self->trn_cfg);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn-par",wval,self->trn_par);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn-mid",wval,self->trn_mid);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"trn-mtype",wval,self->trn_mtype);
        retval+=fprintf(stderr,"%*s %*s  %*d\n",indent,(indent>0?" ":""), wkey,"trn-ftype",wval,self->trn_ftype);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn-ncov",wval,self->trn_ncov);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn-nerr",wval,self->trn_nerr);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn-ecov",wval,self->trn_ecov);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn-eerr",wval,self->trn_eerr);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"mb-out",wval,self->mb_out);
        retval+=fprintf(stderr,"%*s %*s  %*s\n",indent,(indent>0?" ":""), wkey,"trn-out",wval,self->trn_out);
        retval+=fprintf(stderr,"%*s %*s  %*u\n",indent,(indent>0?" ":""), wkey,"trn-decn",wval,self->trn_decn);
        retval+=fprintf(stderr,"%*s %*s  %*.2lf\n",indent,(indent>0?" ":""), wkey,"trn-decs",wval,self->trn_decs);
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"trn-nombgain",wval,BOOL2YNC(self->trn_nombgain));
        retval+=fprintf(stderr,"%*s %*s  %*c\n",indent,(indent>0?" ":""), wkey,"help",wval,BOOL2YNC(self->help));
    }

    return retval;
}

static int s_parse_opt_output(mbtrnpp_cfg_t *cfg, char *opt_str)
{
    int retval=0;

    if(NULL!=cfg && NULL!=opt_str){

        // tokenize optarg
        char *ocopy = CHK_STRDUP(opt_str);
        int i=0;
        char *tok[MBSYSOUT_OPT_N]={0};
        for(i=0;i<MBSYSOUT_OPT_N;i++){
            tok[i] = (i==0  ? strtok(ocopy,",") : strtok(NULL,","));
            if(tok[i]==NULL)
                break;
        }
        // parse tokens
        for(i=0;i<MBSYSOUT_OPT_N;i++){
            if(NULL==tok[i])
                break;
            if (NULL!=strstr(tok[i], "socket:")) {
                // enable mb1 socket (use specified IP)
                char *acpy = strdup(tok[i]);
                char *atok = strtok(acpy,":");
                if(NULL!=atok){
                    // uses defaults if NULL
                    char *shost = strtok(NULL,":");
                    char *sport = strtok(NULL,":");
                    //                    fprintf(stderr,"shost[%s] sport[%s]\n",shost,sport);

                    if(NULL!=shost){
                        MEM_CHKINVALIDATE(cfg->mb1svr_host);
                        cfg->mb1svr_host = strdup(shost);
                        retval++;
                    }
                    if(NULL!=sport){
                        sscanf(sport,"%d",&cfg->mb1svr_port);
                        retval++;
                    }
                }
                //                fprintf(stderr,"mb1svr[%s:%d]\n",cfg->mb1svr_host,cfg->mb1svr_port);
                free(acpy);
                cfg->output_flags |= OUTPUT_MB1_SVR_EN;
            }
            if (strcmp(tok[i], "socket") == 0) {
                // enable mb1 socket (use default IP)
                cfg->output_flags |= OUTPUT_MB1_SVR_EN;
            }

            if(NULL!=strstr(tok[i],"file:")){
                char *acpy = strdup(tok[i]);
                char *atok = strtok(acpy,":");
                atok = strtok(NULL,":");
                //                fprintf(stderr,"output_file[%s]\n",atok);
                if(strlen(atok)>0){
                    strcpy(cfg->output_file,atok);
                    // enable mb1 data log (use specified name)
                    cfg->output_flags |= OUTPUT_MB1_FILE_EN;
                    retval++;
                }
                free(acpy);
            }
            if (strcmp(tok[i], "file") == 0) {
                // enable mb1 data log (use default MB-System name)
                cfg->output_flags |= OUTPUT_MB1_FILE_EN;
            }
        }
        free(ocopy);
    }// err - invalid arg
    return retval;
}

static int s_parse_opt_mbout(mbtrnpp_cfg_t *cfg, char *opt_str)
{
    int retval=0;
    if(NULL!=cfg && NULL!=opt_str){

        // tokenize optarg
        char *ocopy = strdup(opt_str);
        int i=0;
        char *tok[MBOUT_OPT_N]={0};
        for(i=0;i<MBOUT_OPT_N;i++){
            tok[i] = (i==0  ? strtok(ocopy,",") : strtok(NULL,","));
            if(tok[i]==NULL)
                break;
        }
        // parse tokens
        for(i=0;i<MBOUT_OPT_N;i++){
            if(NULL==tok[i])
                break;
            if(strstr(tok[i],"mb1svr")!=NULL){
                // enable mb1 socket output (optionally, specify host:port)
                char *acpy = strdup(tok[i]);
                char *atok = strtok(acpy,":");
                if(NULL!=atok){
                    // uses defaults if NULL
                    char *shost = strtok(NULL,":");
                    char *sport = strtok(NULL,":");
                    //                    fprintf(stderr,"shost[%s] sport[%s]\n",shost,sport);

                    if(NULL!=shost){
                        MEM_CHKINVALIDATE(cfg->mb1svr_host);
                        cfg->mb1svr_host = strdup(shost);
                        retval++;
                    }
                    if(NULL!=sport){
                        sscanf(sport,"%d",&cfg->mb1svr_port);
                        retval++;
                    }
                }
                //                fprintf(stderr,"mb1svr[%s:%d]\n",mbtrn_cfg->mb1svr_host,mbtrn_cfg->mb1svr_port);
                cfg->output_flags |= OUTPUT_MB1_SVR_EN;
                free(acpy);
            }
            if(strcmp(tok[i],"mb1")==0){
                // enable mb1 data log
                cfg->output_flags |= OUTPUT_MB1_BIN;
            }
            if(NULL!=strstr(tok[i],"file:")){
                char *acpy = strdup(tok[i]);
                char *atok = strtok(acpy,":");
                atok = strtok(NULL,":");
                //                fprintf(stderr,"output_file[%s]\n",atok);
                if(strlen(atok)>0){
                    strcpy(cfg->output_file,atok);
                    // enable mb1 data log (use specified name)
                    cfg->output_flags |= OUTPUT_MB1_FILE_EN;
                }
                free(acpy);
            }
            if (strcmp(tok[i], "file") == 0) {
                // enable mb1 data log (use default MB-System name)
                cfg->output_flags |= OUTPUT_MB1_FILE_EN;
            }
            if(strcmp(tok[i],"reson")==0){
                // enable reson frame data log
                cfg->output_flags |= OUTPUT_RESON_BIN;
            }
            if(strcmp(tok[i],"nomb1")==0){
                // disable mb1 data log
                cfg->output_flags &= ~OUTPUT_MB1_BIN;
            }
            if(strcmp(tok[i],"noreson")==0){
                // disable reson frame data log
                cfg->output_flags &= ~OUTPUT_RESON_BIN;
            }
            if(strcmp(tok[i],"nombsvr")==0){
                // disable mb1svr
                cfg->output_flags &= ~OUTPUT_MB1_SVR_EN;
                MEM_CHKINVALIDATE(cfg->mb1svr_host);
            }
            if(strcmp(tok[i],"nombtrnpp")==0){
                // disable mbtrnpp message log (not recommended)
                cfg->output_flags &= ~OUTPUT_MBTRNPP_MSG;
            }
        }
        free(ocopy);
    }// err - invalid arg

    return retval;
}

static int s_parse_opt_trnout(mbtrnpp_cfg_t *cfg, char *opt_str)
{
    int retval=0;
    if(NULL!=cfg && NULL!=opt_str){
        // tokenize optarg
        char *ocopy = strdup(opt_str);
        int i=0;
        char *tok[TRNOUT_OPT_N]={0};
        for(i=0;i<TRNOUT_OPT_N;i++){
            tok[i] = (i==0  ? strtok(ocopy,",") : strtok(NULL,","));
            //                fprintf(stderr,"tok[%d][%s]\n",i,tok[i]);
            if(tok[i]==NULL)
                break;
        }
        // parse tokens
        for(i=0;i<TRNOUT_OPT_N;i++){
            if(NULL==tok[i])
                break;
            if(strstr(tok[i],"trnsvr")!=NULL){
                // enable trnsvr (mbsvr:host:port)
                char *acpy = strdup(tok[i]);
                char *atok = strtok(acpy,":");
                if(NULL!=atok){
                    char *shost = strtok(NULL,":");
                    char *sport = strtok(NULL,":");


                    if(NULL!=shost){
                        MEM_CHKINVALIDATE(cfg->trnsvr_host);
                        cfg->trnsvr_host = strdup(shost);
                    }
                    if(NULL!=sport){
                        sscanf(sport,"%d",&cfg->trnsvr_port);
                    }
                }
                cfg->output_flags |= OUTPUT_TRN_SVR_EN;
                free(acpy);
            }
            if(strstr(tok[i],"trnusvr")!=NULL){
                // enable trnsvr (mbsvr:host:port)
                char *acpy = strdup(tok[i]);
                char *tok = strtok(acpy,":");
                if(NULL!=tok){
                    char *shost = strtok(NULL,":");
                    char *sport = strtok(NULL,":");


                    if(NULL!=shost){
                        MEM_CHKINVALIDATE(cfg->trnusvr_host);
                        cfg->trnusvr_host = strdup(shost);
                        retval++;
                    }
                    if(NULL!=sport){
                        sscanf(sport,"%u",&cfg->trnusvr_port);
                        retval++;
                    }
                }
                //                    fprintf(stderr,"trnusvr[%s:%d]\n",cfg->trnusvr_host,cfg->trnusvr_port);
                cfg->output_flags |= OUTPUT_TRNU_SVR_EN;
                free(acpy);
            }
            if(strcmp(tok[i],"trnu")==0){
                // enable trn update data log
                cfg->output_flags |= OUTPUT_TRNU_ASC;
            }
            if(strcmp(tok[i],"sout")==0){
                // enable trn update to stdout
                cfg->output_flags |= OUTPUT_TRNU_SOUT;
            }
            if(strcmp(tok[i],"serr")==0){
                // enable trn updatetp stderr
                cfg->output_flags |= OUTPUT_TRNU_SERR;
            }
            if(strcmp(tok[i],"debug")==0){
                // enable trn update per debug settings
                cfg->output_flags |= OUTPUT_TRNU_DEBUG;
            }
            if(strcmp(tok[i],"notrnsvr")==0){
                // disable trnsvr
                cfg->output_flags &= ~OUTPUT_TRN_SVR_EN;
                MEM_CHKINVALIDATE(cfg->trnsvr_host);
            }
            if(strcmp(tok[i],"notrnusvr")==0){
                // disable trnsvr
                cfg->output_flags &= ~OUTPUT_TRNU_SVR_EN;
                MEM_CHKINVALIDATE(cfg->trnusvr_host);
            }
        }
        free(ocopy);
    }// err - invalid arg

    return retval;
}

static int s_parse_opt_logdir(mbtrnpp_cfg_t *cfg, char *opt_str)
{
    int retval=-1;
    if(NULL!=cfg && NULL!=opt_str){
        strcpy(cfg->log_directory, opt_str);
        struct stat logd_stat;
        int logd_status=0;
        logd_status = stat(cfg->log_directory, &logd_stat);

        if (logd_status != 0) {
            fprintf(stderr, "\nSpecified log file directory %s does not exist...\n", cfg->log_directory);
            cfg->make_logs = false;
            int status=0;
            char *ps = CHK_STRDUP(cfg->log_directory);
            if(NULL!=ps){
                if( (status=mkdir(ps,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH))==0){
                    cfg->make_logs = true;
                    MEM_CHKINVALIDATE(cfg->trn_log_dir);
                   cfg->trn_log_dir=CHK_STRDUP(ps);
                   fprintf(stderr, "\ncreated/using log directory %s...\n", cfg->trn_log_dir);
                }else{
                    fprintf(stderr, "\nCreate log directory %s failed [%d/%s]\n", ps,errno,strerror(errno));
                }
                free(ps);
            }
        }else if((logd_stat.st_mode & S_IFMT) != S_IFDIR) {
            fprintf(stderr, "\nSpecified log file directory %s is not a directory...\n", cfg->log_directory);
            cfg->make_logs = false;
        }else{
            cfg->make_logs = true;
            MEM_CHKINVALIDATE(cfg->trn_log_dir);
            cfg->trn_log_dir = CHK_STRDUP(cfg->log_directory);
            fprintf(stderr, "\nusing log directory %s...\n", cfg->trn_log_dir);
        }
        if(NULL==cfg->trn_log_dir){
            MEM_CHKINVALIDATE(cfg->trn_log_dir);
            cfg->trn_log_dir=CHK_STRDUP(CFG_TRN_LOG_DIR_DFL);
        }
        retval=0;
    }
    return retval;
}

static char *s_mbtrnpp_peek_opt_cfg(int argc, char **argv, char **pbuf, size_t len)
{
    char *retval =NULL;
    int i=0;

    for(i=0;i<argc;i++){
        char *val=NULL;
        if( (val=strstr(argv[i],"config="))!=NULL){
            char *dest=NULL;
            val+=strlen("config=");
            size_t vlen=strlen(val)+1;
            if(NULL==pbuf){
                dest=(char *)malloc(vlen);
            }else{
                char *buf=*pbuf;
                if(NULL==buf){
                    dest=(char *)malloc(vlen);
                    *pbuf=dest;
                }else{
                    if(len>=vlen){
                        dest=buf;
                    }else{
                        fprintf(stderr,"ERR - config path buffer too small\n");
                    }
                }
            }

            if(NULL!=dest){
                memcpy(dest,val,vlen);
                retval=dest;
            }
        }
    }
    return retval;
}

static int s_mbtrnpp_kvparse_fn(char *key, char *val, void *cfg)
{
    int retval =-1;

    if(NULL!=key &&  NULL!=cfg){
        mbtrnpp_opts_t *opts=(mbtrnpp_opts_t *)cfg;
        fprintf(stderr, ">>>> PARSING key/val [%13s / %s]\n", key,val);
        if(NULL!=val){
            // process opts w/ required args

            if(strcmp(key,"verbose")==0 ){
                if(sscanf(val,"%d",&opts->verbose)==1){
                    retval=0;
                }
            }else if(strcmp(key,"input")==0 ){
                MEM_CHKFREE(opts->input);
                if( (opts->input=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"format")==0 ){
                if(sscanf(val,"%d",&opts->format)==1){
                    retval=0;
                }
            }else if(strcmp(key,"platform-file")==0 ){
                MEM_CHKFREE(opts->platform_file);
                if( (opts->platform_file=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"log-directory")==0 ){
                MEM_CHKFREE(opts->log_directory);
                if( (opts->log_directory=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"output")==0 ){
                MEM_CHKFREE(opts->output);
               if( (opts->output=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"projection")==0 ){
                if(sscanf(val,"%d",&opts->projection)==1){
                    retval=0;
                }
            }else if(strcmp(key,"swath-width")==0 || strcmp(key,"swath")==0 ){
                if(sscanf(val,"%lf",&opts->swath_width)==1){
                    retval=0;
                }
            }else if(strcmp(key,"soundings")==0 ){
                if(sscanf(val,"%d",&opts->soundings)==1){
                    retval=0;
                }
            }else if(strcmp(key,"median-filter")==0 ){
                MEM_CHKFREE(opts->median_filter);
                if( (opts->median_filter=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"mbhbn")==0 ){
                if(sscanf(val,"%d",&opts->mbhbn)==1){
                    retval=0;
                }
            }else if(strcmp(key,"mbhbt")==0 ){
                if(sscanf(val,"%lf",&opts->mbhbt)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trnhbt")==0 ){
                if(sscanf(val,"%lf",&opts->trnhbt)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trnuhbt")==0 ){
                if(sscanf(val,"%lf",&opts->trnuhbt)==1){
                    retval=0;
                }
            }else if(strcmp(key,"delay")==0 ){
                if(sscanf(val,"%"PRId64"",&opts->delay)==1){
                    retval=0;
                }
            }else if(strcmp(key,"statsec")==0 ){
                if(sscanf(val,"%lf",&opts->statsec)==1){
                    retval=0;
                }
            }else if(strcmp(key,"statflags")==0 ){
                MEM_CHKFREE(opts->statflags_str);
                if( (opts->statflags_str=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }

                if(sscanf(val,"%u",&opts->statflags)==1){
                    retval=0;
                }
                if(NULL!=strstr(val,"MSF_STATUS") || NULL!=strstr(val,"msf_status")){
                    opts->statflags |= MSF_STATUS;
                    retval=0;
                }
                if(NULL!=strstr(val,"MSF_EVENT") || NULL!=strstr(val,"msf_event")){
                    opts->statflags |= MSF_EVENT;
                    retval=0;
                }
                if(NULL!=strstr(val,"MSF_ASTAT") || NULL!=strstr(val,"msf_astat")){
                    opts->statflags |= MSF_ASTAT;
                    retval=0;
                }
                if(NULL!=strstr(val,"MSF_PSTAT") || NULL!=strstr(val,"msf_pstat")){
                    opts->statflags |= MSF_PSTAT;
                    retval=0;
                }
                if(NULL!=strstr(val,"MSF_READER") || NULL!=strstr(val,"msf_reader")){
                    opts->statflags |= MSF_READER;
                    retval=0;
                }
            }else if(strcmp(key,"trn-utm")==0 ){
                if(sscanf(val,"%ld",&opts->trn_utm)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-map")==0 ){
                MEM_CHKFREE(opts->trn_map);
                if( (opts->trn_map=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"trn-cfg")==0 ){
                MEM_CHKFREE(opts->trn_cfg);
                if( (opts->trn_cfg=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"trn-par")==0 ){
                MEM_CHKFREE(opts->trn_par);
               if( (opts->trn_par=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"trn-mid")==0 ){
                MEM_CHKFREE(opts->trn_mid);
               if( (opts->trn_mid=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"trn-mtype")==0 ){
                if(sscanf(val,"%d",&opts->trn_mtype)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-ftype")==0 ){
                if(sscanf(val,"%d",&opts->trn_ftype)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-ncov")==0 ){
                if(sscanf(val,"%lf",&opts->trn_ncov)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-nerr")==0 ){
                if(sscanf(val,"%lf",&opts->trn_nerr)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-ecov")==0 ){
                if(sscanf(val,"%lf",&opts->trn_ecov)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-eerr")==0 ){
                if(sscanf(val,"%lf",&opts->trn_eerr)==1){
                    retval=0;
                }
            }else if(strcmp(key,"mb-out")==0 ){
                MEM_CHKFREE(opts->mb_out);
                if( (opts->mb_out=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"trn-out")==0 ){
                MEM_CHKFREE(opts->trn_out);
                if( (opts->trn_out=CHK_STRDUP(val)) != NULL){
                    retval=0;
                }
            }else if(strcmp(key,"trn-decn")==0 ){
                if(sscanf(val,"%u",&opts->trn_decn)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-decd")==0 ){
                if(sscanf(val,"%lf",&opts->trn_decs)==1){
                    retval=0;
                }
            }else if(strcmp(key,"trn-nombgain")==0 ){
                if( mkvc_parse_bool(val,&opts->trn_nombgain)==0){
                    retval=0;
                }
            }else if(strcmp(key,"trn-en")==0 ){
                if( mkvc_parse_bool(val,&opts->trn_en)==0){
                    retval=0;
                }else{
                    opts->trn_en=true;
                    retval=0;
                }
            }else if(strcmp(key,"config")==0 ){
                retval=0;
            }
        }else{
            // process args w/o required arguments
             if(strcmp(key,"trn-en")==0 ){
                if( mkvc_parse_bool(val,&opts->trn_en)==0){
                    retval=0;
                }else{
                    opts->trn_en=true;
                    retval=0;
                }
            }else if(strcmp(key,"config")==0 ){
                retval=0;
            }else if(strcmp(key,"help")==0 ){
                opts->help=true;
                retval=0;
            }else {
                fprintf(stderr, "WARN - unsupported key/val [%s/%s]\n", key,val);
            }
        }

        // perform mnemonic substitutions
        char *val=NULL;
        s_sub_mnem(&opts->input,0,opts->input,CFG_MNEM_RHOST,s_mnem_value(&val,0,CFG_MNEM_RHOST));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->output,0,opts->output,CFG_MNEM_SESSION,s_mnem_value(&val,0,CFG_MNEM_SESSION));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->mb_out,0,opts->mb_out,CFG_MNEM_MBTRN_HOST,s_mnem_value(&val,0,CFG_MNEM_MBTRN_HOST));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->trn_out,0,opts->trn_out,CFG_MNEM_MBTRN_HOST,s_mnem_value(&val,0,CFG_MNEM_MBTRN_HOST));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->trn_mid,0,opts->trn_mid,CFG_MNEM_TRN_SESSION,s_mnem_value(&val,0,CFG_MNEM_TRN_SESSION));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->log_directory,0,opts->log_directory,CFG_MNEM_TRN_LOGFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_LOGFILES));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->trn_map,0,opts->trn_map,CFG_MNEM_TRN_MAPFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_MAPFILES));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->trn_par,0,opts->trn_par,CFG_MNEM_TRN_DATAFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_DATAFILES));
        MEM_CHKINVALIDATE(val);
        s_sub_mnem(&opts->trn_cfg,0,opts->trn_cfg,CFG_MNEM_TRN_DATAFILES,s_mnem_value(&val,0,CFG_MNEM_TRN_DATAFILES));
        MEM_CHKINVALIDATE(val);
    }else{
        fprintf(stderr, "ERR - NULL key/val [%s / %s]\n", key,val);
    }
    return retval;
}

static int s_mbtrnpp_load_config(char *config_path, mbtrnpp_opts_t *opts)
{
    int retval=-1;
    int err=0;
    int par=0;
    int inv=0;
    if(NULL!=config_path && NULL!=opts){
        int test=0;
        mkvc_reader_t *cfg_reader = mkvc_new(config_path,MBTRNPP_CONF_DEL,s_mbtrnpp_kvparse_fn);

        if( (test=mkvc_load_config(cfg_reader, opts, &par, &inv, &err))==0){
            retval=0;
        }else{
            fprintf(stderr,"ERR - mkvc_load_config ret[%d] par[%d] inv[%d] err[%d]\n",test,par,inv,err);
		}
        mkvc_destroy(&cfg_reader);
    }
    return retval;
}

static int s_mbtrnpp_process_cmdline(int argc, char **argv, mbtrnpp_opts_t *opts)
{
    int retval =-1;
    int i=0;
    int err_count=0;
    for(i=1;i<argc;i++){
        char *key=NULL;
        char *val=NULL;
        char *opt=strstr(argv[i],"--");
        if(NULL!=opt && mkvc_parse_kx(opt+2,MBTRNPP_CONF_DEL,&key,&val,false)==0 &&
           NULL!=key ){
            // parse key/value into configuration
            if(s_mbtrnpp_kvparse_fn(key,val,opts)!=0){
                fprintf(stderr, "ERR - invalid key/value [%s/%s]\n",key,val);
                err_count++;
            }
        }else{
            fprintf(stderr, "ERR - parse error in [%s]\n", argv[i]);
            err_count++;
        }
        // release key/value strings
        MEM_CHKINVALIDATE(key);
        MEM_CHKINVALIDATE(val);
    }

    retval = err_count;
    return retval;
}

static int s_mbtrnpp_configure(mbtrnpp_cfg_t *cfg, mbtrnpp_opts_t *opts)
{
    int retval =-1;

    if(NULL!=opts && NULL!=cfg){
        // verbose
        cfg->verbose = opts->verbose;
        // input
        char *psdef=NULL;
        if (NULL!=opts->input && (psdef=strstr(opts->input, "socket:"))!=NULL) {
            mbtrn_cfg->input_mode = INPUT_MODE_SOCKET;
            if(NULL!=psdef){
                if(strlen(psdef)<MB_PATH_SIZE){
                    psdef+=strlen("socket:");
                    sprintf(cfg->socket_definition,"%s",psdef);
                }else{
                    fprintf(stderr,"socket definition length exceeds MB_PATH_SIZE [%s/%zu/%zu]\n",psdef,strlen(psdef),(size_t)MB_PATH_SIZE);
                }
                //            fprintf(stderr, "socket_definition|%s\n", cfg->socket_definition);
            }
        }else {
            mbtrn_cfg->input_mode = INPUT_MODE_FILE;
        }
        // output
        s_parse_opt_output(cfg,opts->output);
        // mb-out
        s_parse_opt_mbout(cfg,opts->mb_out);
        // trn-out
        s_parse_opt_trnout(cfg,opts->trn_out);
        // mbhbn
        cfg->mbsvr_hbtok = opts->mbhbn;
        // mbhbt
        cfg->mbsvr_hbto = opts->mbhbt;
        // trnhbt
        cfg->trnsvr_hbto = opts->trnhbt;
        // trnuhbt
        cfg->trnusvr_hbto = opts->trnuhbt;
        // delay
        cfg->mbtrnpp_loop_delay_msec = opts->delay;
        // statsec
        cfg->trn_status_interval_sec = opts->statsec;
        // statflags
        cfg->mbtrnpp_stat_flags = opts->statflags;
        // trn-en
        cfg->trn_enable = opts->trn_en;
        // trn-utm
        cfg->trn_utm_zone = opts->trn_utm;
        // trn-mtype
        cfg->trn_mtype = opts->trn_mtype;
        // trn-ftype
        cfg->trn_ftype = opts->trn_ftype;
        // trn-ncov
        cfg->trn_max_ncov = opts->trn_ncov;
        // trn-nerr
        cfg->trn_max_nerr = opts->trn_nerr;
        // trn-ecov
        cfg->trn_max_ecov = opts->trn_ecov;
        // trn-eerr
        cfg->trn_max_eerr = opts->trn_eerr;
        // trn-map
        MEM_CHKFREE(cfg->trn_map_file);
        cfg->trn_map_file = CHK_STRDUP(opts->trn_map);
        // trn-cfg
        MEM_CHKFREE(cfg->trn_cfg_file);
        cfg->trn_cfg_file = CHK_STRDUP(opts->trn_cfg);
        // trn-par
        MEM_CHKFREE(cfg->trn_particles_file);
        cfg->trn_particles_file = CHK_STRDUP(opts->trn_par);
        // trn-mid
        MEM_CHKFREE(cfg->trn_mission_id);
        cfg->trn_mission_id = CHK_STRDUP(opts->trn_mid);
        // trn-decn
        cfg->trn_decn = opts->trn_decn;
        // trn-decs
        cfg->trn_decs = opts->trn_decs;
        // trn-nombgain
        cfg->trn_nombgain = opts->trn_nombgain;

        // format
        cfg->format = opts->format;
        // platform-file
       if(NULL!=opts->platform_file){
            strcpy(cfg->platform_file, opts->platform_file);
            cfg->use_platform_file=true;
        }
        // platform-target-sensor
        cfg->target_sensor = opts->platform_target_sensor;
        // log-directory
        s_parse_opt_logdir(cfg, opts->log_directory);
        // swath-width
        cfg->swath_width=opts->swath_width;
        // soundings
        cfg->n_output_soundings=opts->soundings;
        // median-filter
        if(NULL!=opts->median_filter){
            int n = sscanf(opts->median_filter, "%lf/%d/%d", &cfg->median_filter_threshold,
                       &cfg->median_filter_n_across, &cfg->median_filter_n_along);
            if (n == 3) {
                cfg->median_filter_en = true;
                cfg->n_buffer_max = cfg->median_filter_n_along;
            }
        }else{
            cfg->median_filter_en = false;
        }
        retval=0;
    }else{
        fprintf(stderr, "ERR - invalid argument (NULL opts)\n");
    }

    return retval;
}

// validate configuration
static int s_mbtrnpp_validate_config(mbtrnpp_cfg_t *cfg)
{
    int retval=-1;
    if(NULL!=cfg){
        int err_count=0;

        if(cfg->median_filter_en){
            if(cfg->median_filter_n_across<0){
                err_count++;
                fprintf(stderr,"ERR - invalid median_filter_n_across [%d] valid range >0\n",cfg->median_filter_n_across);
            }
            if(cfg->median_filter_n_along<0){
                err_count++;
                fprintf(stderr,"ERR - invalid median_filter_n_along [%d] valid range >0\n",cfg->median_filter_n_along);
            }
            if(cfg->median_filter_threshold<0.0){
                err_count++;
                fprintf(stderr,"ERR - invalid median_filter_threshold [%lf] valid range >00\n",cfg->median_filter_threshold);
            }
            if(cfg->n_buffer_max<0){
                err_count++;
                fprintf(stderr,"ERR - invalid n_buffer_max [%d] valid range >0\n",cfg->n_buffer_max);
            }
        }

        if(cfg->swath_width<0.0){
            err_count++;
            fprintf(stderr,"ERR - invalid swath_width [%lf] valid range >0\n",cfg->swath_width);
        }

        switch (cfg->input_mode) {
            case INPUT_MODE_FILE:
                if(strlen(cfg->input)<=0){
                    err_count++;
                    fprintf(stderr,"ERR - input path not set\n");
                }
                break;
            case INPUT_MODE_SOCKET:
                if(strlen(cfg->socket_definition)<=0){
                    err_count++;
                    fprintf(stderr,"ERR - socket_definition not set\n");
                }
                break;
            default:
                err_count++;
                fprintf(stderr,"ERR - invalid input mode [%d]\n",cfg->input_mode);
                break;
        }

        if( (cfg->output_flags&OUTPUT_MB1_FILE_EN)!=0){
            if(strlen(cfg->output_file)<=0){
                err_count++;
                fprintf(stderr,"ERR - output_file not set\n");
            }
        }

        if(cfg->trn_enable){
            // validate required TRN options
            if(NULL==cfg->trn_map_file){
                err_count++;
                fprintf(stderr,"ERR - trn_map_file not set\n");
            }
            if(NULL==cfg->trn_cfg_file){
                err_count++;
                fprintf(stderr,"ERR - trn_cfg_file not set\n");
            }
            if(cfg->trn_utm_zone<1 || cfg->trn_utm_zone>60){
                err_count++;
                fprintf(stderr,"ERR - invalid trn_utm_zone [%ld] valid range 1-60\n",cfg->trn_utm_zone);
            }
            if(cfg->trn_mtype<1 || cfg->trn_mtype>2){
                err_count++;
                fprintf(stderr,"ERR - invalid trn_mtype [%d] valid range 1-2\n",cfg->trn_mtype);
            }
            if(cfg->trn_ftype<0 || cfg->trn_ftype>4){
                err_count++;
                fprintf(stderr,"ERR - invalid trn_mtype [%d] valid range 0-4\n",cfg->trn_ftype);
            }

            if((cfg->output_flags&OUTPUT_MB1_SVR_EN)){
                if(NULL!=cfg->mb1svr_host ){
                    err_count++;
                    fprintf(stderr,"ERR - mb1svr_host not set [%s]\n",cfg->mb1svr_host);
                }
                if((cfg->mb1svr_port<1 || cfg->mb1svr_port>255)){
                    err_count++;
                    fprintf(stderr,"ERR - invalid mb1svr_port [%d] valid range 1-255\n",cfg->mb1svr_port);
                }
        	}
            if((cfg->output_flags&OUTPUT_TRN_SVR_EN)){
                if(NULL!=cfg->trnsvr_host ){
                    err_count++;
                    fprintf(stderr,"ERR - trnsvr_host not set [%s]\n",cfg->trnsvr_host);
                }
                if((cfg->trnsvr_port<1 || cfg->trnsvr_port>255)){
                    err_count++;
                    fprintf(stderr,"ERR - invalid trnsvr_port [%d] valid range 1-255\n",cfg->trnsvr_port);
                }
            }
            if((cfg->output_flags&OUTPUT_TRNU_SVR_EN)){
                if(NULL!=cfg->trnusvr_host ){
                    err_count++;
                    fprintf(stderr,"ERR - trnusvr_host not set [%s]\n",cfg->trnusvr_host);
                }
                if((cfg->trnusvr_port<1 || cfg->trnusvr_port>255)){
                    err_count++;
                    fprintf(stderr,"ERR - invalid trnusvr_port [%d] valid range 1-255\n",cfg->trnusvr_port);
                }
            }
        }

        retval=(err_count==0?0:-1);
    }
    return retval;
}

/*--------------------------------------------------------------------*/

int main(int argc, char **argv) {
  char help_message[] = "mbtrnpp reads raw multibeam data, applies automated cleaning\n\t"
                        "and downsampling, and then passes the bathymetry on to a terrain relative navigation (TRN) process.\n";
  char usage_message[] = "mbtrnpp [\n"
                         "\t--verbose\n"
                         "\t--help\n"
                         "\t--log-directory=path\n"
                         "\t--input=datalist|file|socket_definition\n"
                         "\t--output=file|'socket'\n"
                         "\t--swathwidth=value\n"
                         "\t--soundings=value\n"
                         "\t--median-filter=threshold/nx/ny\n"
                         "\t--format=format\n"
                         "\t--platform-file\n"
                         "\t--platform-target-sensor\n"
                         "\t--projection=projection_id\n"
                         "\t--statsec=d.d\n"
                         "\t--statflags=<MSF_STATUS:MSF_EVENT:MSF_ASTAT:MSF_PSTAT:MSF_READER>\n"
                         "\t--hbeat=n\n"
                         "\t--mbhbn=n\n"
                         "\t--mbhbt=d.d\n"
                         "\t--trnhbt=n\n"
                         "\t--trnuhbt=n\n"
                         "\t--delay=n\n"
                         "\t--trn-en\n"
                         "\t--trn-dis\n"
                         "\t--trn-utm\n"
                         "\t--trn-map\n"
                         "\t--trn-par\n"
                         "\t--trn-mid\n"
                         "\t--trn-cfg\n"
                         "\t--trn-mtype\n"
                         "\t--trn-ftype\n"
                         "\t--trn-ncov\n"
                         "\t--trn-nerr\n"
                         "\t--trn-ecov\n"
                         "\t--trn-eerr\n"
                         "\t--mb-out=mb1svr[:host:port]/mb1/reson\n"
                         "\t--trn-out=trnsvr[:host:port]/trnusvr[:host:port]/trnu/sout/serr/debug\n"
                         "\t--trn-decn\n"
                         "\t--trn-decs\n"
                         "\t--trn-nombgain\n";
  extern char WIN_DECLSPEC *optarg;
  int option_index;
  int errflg = 0;
  int c;
  int help = 0;

  /* MBIO status variables */
  int status;
  //int verbose = OPT_VERBOSE_DFL;
  int error = MB_ERROR_NO_ERROR;
  char *message;

  /* command line option definitions */
  /* mbtrnpp
   *     --verbose
   *     --help
   *     --input=datalist [or file or socket id]
   *     --format=format
   *     --platform-file=file
   *     --platform-target-sensor
   *     --log-directory=path
   *     --output=file [or socket id]
   *     --projection=projection_id
   *     --swath-width=value
   *     --soundings=value
   *     --median-filter=threshold/nacrosstrack/nalongtrack
   */
//  static struct option options[] = {{"help", no_argument, NULL, 0},
//                                    {"verbose", required_argument, NULL, 0},
//                                    {"input", required_argument, NULL, 0},
//                                    {"mbhbn", required_argument, NULL, 0},
//                                    {"mbhbt", required_argument, NULL, 0},
//                                    {"trnhbt", required_argument, NULL, 0},
//                                    {"trnuhbt", required_argument, NULL, 0},
//                                    {"delay", required_argument, NULL, 0},
//                                    {"statsec", required_argument, NULL, 0},
//                                    {"statflags", required_argument, NULL, 0},
//                                    {"format", required_argument, NULL, 0},
//                                    {"platform-file", required_argument, NULL, 0},
//                                    {"platform-target-sensor", required_argument, NULL, 0},
//                                    {"log-directory", required_argument, NULL, 0},
//                                    {"output", required_argument, NULL, 0},
//                                    {"projection", required_argument, NULL, 0},
//                                    {"swath-width", required_argument, NULL, 0},
//                                    {"soundings", required_argument, NULL, 0},
//                                    {"median-filter", required_argument, NULL, 0},
//                                    {"trn-en", no_argument, NULL, 0},
//                                    {"trn-dis", no_argument, NULL, 0},
//                                    {"trn-utm", required_argument, NULL, 0},
//                                    {"trn-map", required_argument, NULL, 0},
//                                    {"trn-cfg", required_argument, NULL, 0},
//                                    {"trn-par", required_argument, NULL, 0},
//                                    {"trn-mid", required_argument, NULL, 0},
//                                    {"trn-mtype", required_argument, NULL, 0},
//                                    {"trn-ftype", required_argument, NULL, 0},
//                                    {"trn-ncov", required_argument, NULL, 0},
//                                    {"trn-nerr", required_argument, NULL, 0},
//                                    {"trn-ecov", required_argument, NULL, 0},
//                                    {"trn-eerr", required_argument, NULL, 0},
//                                    {"mb-out", required_argument, NULL, 0},
//                                    {"trn-out", required_argument, NULL, 0},
//                                    {"trn-decn", required_argument, NULL, 0},
//                                    {"trn-decs", required_argument, NULL, 0},
//                                    {"trn-nombgain", no_argument, NULL, 0},
//                                    {NULL, 0, NULL, 0}};

  /* MBIO read control parameters */
  int read_datalist = false;
  int read_data = false;
  int read_socket = false;
//  mb_path input;
  void *datalist;
  int look_processed = MB_DATALIST_LOOK_UNSET;
  double file_weight;
//  int format;
  int system;
  int pings;
  int lonflip;
  double bounds[4];
  int btime_i[7];
  int etime_i[7];
  double btime_d;
  double etime_d;
  double speedmin;
  double timegap;
  int beams_bath;
  int beams_amp;
  int pixels_ss;
  int obeams_bath;
  int obeams_amp;
  int opixels_ss;
  mb_path ifile;
  mb_path dfile;
  void *imbio_ptr = NULL;
  unsigned int ping_number = 0;

  /* mbio read and write values */
  void *store_ptr;
  int kind;
  int ndata = 0;
  char comment[MB_COMMENT_MAXLINE];

  /* platform definition file */
//  mb_path platform_file;
//  int use_platform_file = false;
  struct mb_platform_struct *platform = NULL;
  struct mb_sensor_struct *sensor_bathymetry = NULL;
  struct mb_sensor_struct *sensor_backscatter = NULL;
  struct mb_sensor_struct *sensor_position = NULL;
  struct mb_sensor_struct *sensor_depth = NULL;
  struct mb_sensor_struct *sensor_heading = NULL;
  struct mb_sensor_struct *sensor_rollpitch = NULL;
  struct mb_sensor_struct *sensor_heave = NULL;
  struct mb_sensor_struct *sensor_target = NULL;
//  int target_sensor = -1;

  /* buffer handling parameters */
//  int n_buffer_max = 1;
  struct mbtrnpp_ping_struct ping[MBTRNPREPROCESS_BUFFER_DEFAULT];

  /* counting parameters */
  int n_pings_read = 0;
  int n_soundings_read = 0;
  int n_soundings_valid_read = 0;
  int n_soundings_flagged_read = 0;
  int n_soundings_null_read = 0;
  int n_soundings_trimmed = 0;
  int n_soundings_decimated = 0;
  int n_soundings_flagged = 0;
  int n_soundings_written = 0;
  int n_tot_pings_read = 0;
  int n_tot_soundings_read = 0;
  int n_tot_soundings_valid_read = 0;
  int n_tot_soundings_flagged_read = 0;
  int n_tot_soundings_null_read = 0;
  int n_tot_soundings_trimmed = 0;
  int n_tot_soundings_decimated = 0;
  int n_tot_soundings_flagged = 0;
  int n_tot_soundings_written = 0;

  /* processing control variables */
//  double swath_width = 150.0;
  double tangent, threshold_tangent;
//  int n_output_soundings = 101;
//  int median_filter_en = false;
//  int median_filter_n_across = 1;
//  int median_filter_n_along = 1;
  int median_filter_n_total = 1;
  int median_filter_n_min = 1;
//  double median_filter_threshold = 0.05;
  double *median_filter_soundings = NULL;
  int n_median_filter_soundings = 0;
  double median;
  int n_output;

  /* output write control parameters */
//  mb_path output_file;
  FILE *output_fp = NULL;
  char *output_buffer = NULL;
  int n_output_buffer_alloc = 0;
  size_t mb1_size, index;
  unsigned int checksum;

  /* log file parameters */
//  int make_logs = false;
//  mb_path log_directory;
  FILE *logfp = NULL;
  mb_path log_message;
  double now_time_d;
  double log_file_open_time_d = 0.0;
  char date[32];
  struct stat logd_stat;
  int logd_status;

  /* function pointers for reading realtime sonar data using a socket */
  int (*mbtrnpp_input_open)(int verbose, void *mbio_ptr, char *definition, int *error);
  int (*mbtrnpp_input_read)(int verbose, void *mbio_ptr, size_t *size, char *buffer, int *error);
  int (*mbtrnpp_input_close)(int verbose, void *mbio_ptr, int *error);

  int idataread, n_ping_process, i_ping_process;
  int beam_start, beam_end, beam_decimation;
  int i, ii, j, jj, n;
  int jj0, jj1, dj;
  int ii0, ii1, di;

  struct timeval timeofday;
  struct timezone timezone;
  double time_d;

  /* set default values */
  mbtrn_cfg->format = 0;
  pings = 1;
  bounds[0] = -360.;
  bounds[1] = 360.;
  bounds[2] = -90.;
  bounds[3] = 90.;
  btime_i[0] = 1962;
  btime_i[1] = 2;
  btime_i[2] = 21;
  btime_i[3] = 10;
  btime_i[4] = 30;
  btime_i[5] = 0;
  btime_i[6] = 0;
  etime_i[0] = 2062;
  etime_i[1] = 2;
  etime_i[2] = 21;
  etime_i[3] = 10;
  etime_i[4] = 30;
  etime_i[5] = 0;
  etime_i[6] = 0;
  speedmin = 0.0;
  timegap = 1000000000.0;

  /* set default input and output */
//  memset(input, 0, sizeof(mb_path));
  memset(mbtrn_cfg->output_file, 0, sizeof(mb_path));
  memset(mbtrn_cfg->log_directory, 0, sizeof(mb_path));
//  strcpy(input, "datalist.mb-1");
  strcpy(mbtrn_cfg->output_file, "stdout");

  // initialize session time string
    s_mbtrnpp_session_str(NULL,0,RF_NONE);
    s_mbtrnpp_trnsession_str(NULL,0,RF_NONE);
    // initialize command line string
    s_mbtrnpp_cmdline_str(NULL, 0, argc, argv, RF_NONE);

    fprintf(stderr,">>> CMDLINE [%s]\n",s_mbtrnpp_cmdline_str(NULL, 0, 0, NULL, RF_NONE));
//    MEM_CHKINVALIDATE(mbtrn_cfg->trn_log_dir);
//  mbtrn_cfg->trn_log_dir = strdup(CFG_TRN_LOG_DIR_DFL);

    // set run-time config defaults
    s_mbtrnpp_init_cfg(mbtrn_cfg);
    // set run-time option defaults
    s_mbtrnpp_init_opts(mbtrn_opts);

    // load option overrrides from config file, if specified
    char *cfg_path=NULL;
    if(s_mbtrnpp_peek_opt_cfg(argc,argv,&cfg_path,0)!=NULL){
        fprintf(stderr,"loading config file [%s]\n",cfg_path);
       if(s_mbtrnpp_load_config(cfg_path,mbtrn_opts)!=0){
           PTRACE();
            fprintf(stderr,"ERR - error(s) in config file [%s]\n",cfg_path);
            errflg++;
        }
    }
    MEM_CHKINVALIDATE(cfg_path);
    fprintf(stderr,"opts: config:\n");
    s_mbtrnpp_show_opts(mbtrn_opts,true,5);

    // load option overrrides from command line, if specified
    if(s_mbtrnpp_process_cmdline(argc,argv,mbtrn_opts)!=0){
        PTRACE();
        fprintf(stderr,"ERR - error(s) in cmdline\n");
        errflg++;
    };

    fprintf(stderr,"opts: cmdline:\n");
    s_mbtrnpp_show_opts(mbtrn_opts,true,5);

    // configure using selected options
    if(s_mbtrnpp_configure(mbtrn_cfg, mbtrn_opts)!=0){
        errflg++;
    };

    // check configuration
    if(s_mbtrnpp_validate_config(mbtrn_cfg)!=0){
        errflg++;
    };

    fprintf(stderr,"opts: final\n");
    s_mbtrnpp_show_opts(mbtrn_opts,true,5);
    fprintf(stderr,"\nconfiguration:\n");
    s_mbtrnpp_show_cfg(mbtrn_cfg,true,5);

//    fprintf(stderr, "%s:%d - TODO - REMOVE MNEM-SUB TEST\n",__func__,__LINE__);
//    s_test_mnem();

//    fprintf(stderr, "%s:%d - TODO - REMOVE VALGRIND LEAK TEST\n",__func__,__LINE__);
//    char *g_leak=(char *)malloc(12345);
//    memset(g_leak,0,12345);
//    printf("%s - g_leak[%p]\n",__func__,g_leak);

//    fprintf(stderr, "%s:%d - TODO - REMOVE TEST EXIT\n",__func__,__LINE__);
//    s_mbtrnpp_free_opts(&mbtrn_opts);
//    s_mbtrnpp_free_cfg(&mbtrn_cfg);
//    s_mbtrnpp_session_str(NULL, 0, RF_RELEASE);
//    s_mbtrnpp_trnsession_str(NULL, 0, RF_RELEASE);
//    s_mbtrnpp_cmdline_str(NULL, 0, 0, NULL, RF_RELEASE);
//    return -1;

  /* process argument list */
//  while ((c = getopt_long(argc, argv, "", options, &option_index)) != -1)
//    switch (c) {
//    /* long options all return c=0 */
//    case 0:
//      /* verbose */
//      if (strcmp("verbose", options[option_index].name) == 0) {
//        sscanf(optarg, "%d", &verbose); // verbose++;
//      }
//
//      /* help */
//      else if (strcmp("help", options[option_index].name) == 0) {
//        help = true;
//      }
//
//      /*-------------------------------------------------------
//       * Define input file and format */
//
//      /* input */
//      else if (strcmp("input", options[option_index].name) == 0) {
//        strcpy(input, optarg);
//        if (strstr(input, "socket:")) {
//          input_mode = INPUT_MODE_SOCKET;
//          sscanf(input, "socket:%s", socket_definition);
//            fprintf(stderr, "socket_definition|%s\n", socket_definition);
//        }
//        else {
//          input_mode = INPUT_MODE_FILE;
//        }
//      }
//      /* output */
//      else if ((strcmp("output", options[option_index].name) == 0)) {
//#define MBSYSOUT_OPT_N 8
//          // tokenize optarg
//          char *ocopy = strdup(optarg);
//          int i=0;
//          char *tok[MBSYSOUT_OPT_N]={0};
//          for(i=0;i<MBSYSOUT_OPT_N;i++){
//              tok[i] = (i==0  ? strtok(ocopy,",") : strtok(NULL,","));
//              fprintf(stderr,"tok[%d][%s]\n",i,tok[i]);
//              if(tok[i]==NULL)
//                  break;
//          }
//          // parse tokens
//          for(i=0;i<MBSYSOUT_OPT_N;i++){
//              if(NULL==tok[i])
//                  break;
//              if (NULL!=strstr(tok[i], "socket:")) {
//                  // enable mb1 socket (use specified IP)
//               char *acpy = strdup(tok[i]);
//                  char *atok = strtok(acpy,":");
//                  if(NULL!=atok){
//                      // uses defaults if NULL
//                      char *shost = strtok(NULL,":");
//                      char *sport = strtok(NULL,":");
//                      fprintf(stderr,"shost[%s] sport[%s]\n",shost,sport);
//
//                      if(NULL!=shost){
//                          mb1svr_host = strdup(shost);
//                      }
//                      if(NULL!=sport){
//                          sscanf(sport,"%d",&mb1svr_port);
//                      }
//                  }
//                  fprintf(stderr,"mb1svr[%s:%d]\n",mb1svr_host,mb1svr_port);
//                  free(acpy);
//                  output_flags |= OUTPUT_MB1_SVR_EN;
//             }
//              if (strcmp(tok[i], "socket") == 0) {
//                  // enable mb1 socket (use default IP)
//                  output_flags |= OUTPUT_MB1_SVR_EN;
//              }
//
//              if(NULL!=strstr(tok[i],"file:")){
//                  char *acpy = strdup(tok[i]);
//                  char *atok = strtok(acpy,":");
//                  atok = strtok(NULL,":");
//                  fprintf(stderr,"output_file[%s]\n",atok);
//                  if(strlen(atok)>0){
//                      strcpy(output_file,atok);
//                      // enable mb1 data log (use specified name)
//                      output_flags |= OUTPUT_MB1_FILE_EN;
//                  }
//                  free(acpy);
//              }
//              if (strcmp(tok[i], "file") == 0) {
//                  // enable mb1 data log (use default MB-System name)
//                  output_flags |= OUTPUT_MB1_FILE_EN;
//              }
//          }
//          free(ocopy);
//      }
//
//        // MB1 output options
//      else if (strcmp("mb-out", options[option_index].name) == 0) {
//#define MBOUT_OPT_N 16
//          // tokenize optarg
//          char *ocopy = strdup(optarg);
//          int i=0;
//          char *tok[MBOUT_OPT_N]={0};
//          for(i=0;i<MBOUT_OPT_N;i++){
//              tok[i] = (i==0  ? strtok(ocopy,",") : strtok(NULL,","));
//              fprintf(stderr,"tok[%d][%s]\n",i,tok[i]);
//              if(tok[i]==NULL)
//                  break;
//          }
//          // parse tokens
//          for(i=0;i<MBOUT_OPT_N;i++){
//              if(NULL==tok[i])
//                  break;
//              if(strstr(tok[i],"mb1svr")!=NULL){
//                  // enable mb1 socket output (optionally, specify host:port)
//                  fprintf(stderr,"tok[%d][%s]\n",i,tok[i]);
//                  char *acpy = strdup(tok[i]);
//                  char *atok = strtok(acpy,":");
//                  fprintf(stderr,"args[%s]\n",atok);
//                  if(NULL!=atok){
//                      // uses defaults if NULL
//                      char *shost = strtok(NULL,":");
//                      char *sport = strtok(NULL,":");
//                      fprintf(stderr,"shost[%s] sport[%s]\n",shost,sport);
//
//                      if(NULL!=shost){
//                          mb1svr_host = strdup(shost);
//                      }
//                      if(NULL!=sport){
//                          sscanf(sport,"%d",&mb1svr_port);
//                      }
//                  }
//                  fprintf(stderr,"mb1svr[%s:%d]\n",mb1svr_host,mb1svr_port);
//                  output_flags |= OUTPUT_MB1_SVR_EN;
//                  free(acpy);
//              }
//              if(strcmp(tok[i],"mb1")==0){
//                  // enable mb1 data log
//                  output_flags |= OUTPUT_MB1_BIN;
//              }
//              if(NULL!=strstr(tok[i],"file:")){
//                  char *acpy = strdup(tok[i]);
//                  char *atok = strtok(acpy,":");
//                  atok = strtok(NULL,":");
//                  fprintf(stderr,"output_file[%s]\n",atok);
//                  if(strlen(atok)>0){
//                      strcpy(output_file,atok);
//                      // enable mb1 data log (use specified name)
//                      output_flags |= OUTPUT_MB1_FILE_EN;
//                  }
//                  free(acpy);
//              }
//              if (strcmp(tok[i], "file") == 0) {
//                  // enable mb1 data log (use default MB-System name)
//                  output_flags |= OUTPUT_MB1_FILE_EN;
//              }
//              if(strcmp(tok[i],"reson")==0){
//                  // enable reson frame data log
//                  output_flags |= OUTPUT_RESON_BIN;
//              }
//              if(strcmp(tok[i],"nomb1")==0){
//                  // disable mb1 data log
//                  output_flags &= ~OUTPUT_MB1_BIN;
//              }
//              if(strcmp(tok[i],"noreson")==0){
//                  // disable reson frame data log
//                  output_flags &= ~OUTPUT_RESON_BIN;
//              }
//              if(strcmp(tok[i],"nombsvr")==0){
//                  // disable mb1svr
//                  output_flags &= ~OUTPUT_MB1_SVR_EN;
//                  if(NULL!=mb1svr_host)free(mb1svr_host);
//                  mb1svr_host=NULL;
//              }
//              if(strcmp(tok[i],"nombtrnpp")==0){
//                  // disable mbtrnpp message log (not recommended)
//                  output_flags &= ~OUTPUT_MBTRNPP_MSG;
//              }
//          }
//          free(ocopy);
//      }
//
//        // TRN output options
//      else if (strcmp("trn-out", options[option_index].name) == 0) {
//#define TRNOUT_OPT_N 16
//          // tokenize optarg
//          char *ocopy = strdup(optarg);
//          int i=0;
//          char *tok[TRNOUT_OPT_N]={0};
//          for(i=0;i<TRNOUT_OPT_N;i++){
//              tok[i] = (i==0  ? strtok(ocopy,",") : strtok(NULL,","));
//              fprintf(stderr,"tok[%d][%s]\n",i,tok[i]);
//              if(tok[i]==NULL)
//                  break;
//          }
//          // parse tokens
//          for(i=0;i<TRNOUT_OPT_N;i++){
//              if(NULL==tok[i])
//                  break;
//              if(strstr(tok[i],"trnsvr")!=NULL){
//                  // enable trnsvr (mbsvr:host:port)
//                  char *acpy = strdup(tok[i]);
//                  char *atok = strtok(acpy,":");
//                  if(NULL!=atok){
//                      char *shost = strtok(NULL,":");
//                      char *sport = strtok(NULL,":");
//
//
//                      if(NULL!=shost){
//                          trnsvr_host = strdup(shost);
//                      }
//                      if(NULL!=sport){
//                          sscanf(sport,"%d",&trnsvr_port);
//                      }
//                  }
//                  output_flags |= OUTPUT_TRN_SVR_EN;
//                  free(acpy);
//              }
//              if(strstr(tok[i],"trnusvr")!=NULL){
//                  // enable trnsvr (mbsvr:host:port)
//                  char *acpy = strdup(tok[i]);
//                  char *tok = strtok(acpy,":");
//                  if(NULL!=tok){
//                      char *shost = strtok(NULL,":");
//                      char *sport = strtok(NULL,":");
//
//
//                      if(NULL!=shost){
//                          trnusvr_host = strdup(shost);
//                      }
//                      if(NULL!=sport){
//                          sscanf(sport,"%u",&trnusvr_port);
//                      }
//                  }
//                  fprintf(stderr,"trnusvr[%s:%d]\n",trnusvr_host,trnusvr_port);
//                  output_flags |= OUTPUT_TRNU_SVR_EN;
//                  free(acpy);
//              }
//              if(strcmp(tok[i],"trnu")==0){
//                  // enable trn update data log
//                  output_flags |= OUTPUT_TRNU_ASC;
//              }
//              if(strcmp(tok[i],"sout")==0){
//                  // enable trn update to stdout
//                  output_flags |= OUTPUT_TRNU_SOUT;
//              }
//              if(strcmp(tok[i],"serr")==0){
//                  // enable trn updatetp stderr
//                  output_flags |= OUTPUT_TRNU_SERR;
//              }
//              if(strcmp(tok[i],"debug")==0){
//                  // enable trn update per debug settings
//                  output_flags |= OUTPUT_TRNU_DEBUG;
//              }
//              if(strcmp(tok[i],"notrnsvr")==0){
//                  // disable trnsvr
//                  output_flags &= ~OUTPUT_TRN_SVR_EN;
//                  trnsvr_host=NULL;
//              }
//              if(strcmp(tok[i],"notrnusvr")==0){
//                  // disable trnsvr
//                  output_flags &= ~OUTPUT_TRNU_SVR_EN;
//                  if(NULL!=trnusvr_host)free(trnusvr_host);
//                 trnusvr_host=NULL;
//              }
//          }
//          free(ocopy);
//      }
//
//     // heartbeat (pings)
//      else if (strcmp("mbhbn", options[option_index].name) == 0) {
//          sscanf(optarg, "%d", &mbsvr_hbtok);
//      }
//      else if (strcmp("mbhbt", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &mbsvr_hbto);
//      }
//      else if (strcmp("trnhbt", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &trnsvr_hbto);
//      }
//      else if (strcmp("trnuhbt", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &trnusvr_hbto);
//      }
//      // ping delay
//      else if (strcmp("delay", options[option_index].name) == 0) {
//        sscanf(optarg, "%lld", &mbtrnpp_loop_delay_msec);
//      }
//      /* status log interval (decimal s) */
//      else if (strcmp("statsec", options[option_index].name) == 0) {
//        sscanf(optarg, "%lf", &trn_status_interval_sec);
//      }
//            /* status flags */
//      else if (strcmp("statflags", options[option_index].name) == 0) {
//          mbtrnpp_stat_flags=0;
//          if(NULL!=strstr(optarg,"MSF_STATUS") || NULL!=strstr(optarg,"msf_status")){
//              mbtrnpp_stat_flags |= MSF_STATUS;
//          }
//          if(NULL!=strstr(optarg,"MSF_EVENT") || NULL!=strstr(optarg,"msf_event")){
//              mbtrnpp_stat_flags |= MSF_EVENT;
//          }
//          if(NULL!=strstr(optarg,"MSF_ASTAT") || NULL!=strstr(optarg,"msf_astat")){
//              mbtrnpp_stat_flags |= MSF_ASTAT;
//          }
//          if(NULL!=strstr(optarg,"MSF_PSTAT") || NULL!=strstr(optarg,"msf_pstat")){
//              mbtrnpp_stat_flags |= MSF_PSTAT;
//          }
//          if(NULL!=strstr(optarg,"MSF_READER") || NULL!=strstr(optarg,"msf_reader")){
//              mbtrnpp_stat_flags |= MSF_READER;
//          }
//      }
//#ifdef WITH_MBTNAV
//        /* TRN enable */
//        else if (strcmp("trn-en", options[option_index].name) == 0) {
//            trn_enable = true;
//        }
//        /* TRN disable */
//        else if (strcmp("trn-dis", options[option_index].name) == 0) {
//            trn_enable = false;
//        }
//      /* TRN UTM zone */
//      else if (strcmp("trn-utm", options[option_index].name) == 0) {
//        sscanf(optarg, "%ld", &trn_utm_zone);
//      }
//      /* TRN map type */
//      else if (strcmp("trn-mtype", options[option_index].name) == 0) {
//        sscanf(optarg, "%d", &trn_mtype);
//      }
//      /* TRN filter type */
//      else if (strcmp("trn-ftype", options[option_index].name) == 0) {
//        sscanf(optarg, "%d", &trn_ftype);
//      }
//      /* TRN valid northing covariance limit (m^2) */
//      else if (strcmp("trn-ncov", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &trn_max_ncov);
//      }
//      /* TRN valid northing error limit (m) */
//      else if (strcmp("trn-nerr", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &trn_max_nerr);
//      }
//      /* TRN valid easting covariance limit (m^2) */
//      else if (strcmp("trn-ecov", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &trn_max_ecov);
//      }
//      /* TRN valid easting error limit (m) */
//      else if (strcmp("trn-eerr", options[option_index].name) == 0) {
//          sscanf(optarg, "%lf", &trn_max_eerr);
//      }
//      /* TRN map file */
//      else if (strcmp("trn-map", options[option_index].name) == 0) {
//        if (NULL != trn_map_file)
//          free(trn_map_file);
//        trn_map_file = strdup(optarg);
//      }
//      /* TRN config file */
//      else if (strcmp("trn-cfg", options[option_index].name) == 0) {
//        if (NULL != trn_cfg_file)
//          free(trn_cfg_file);
//        trn_cfg_file = strdup(optarg);
//      }
//      /* TRN particles file */
//      else if (strcmp("trn-par", options[option_index].name) == 0) {
//        if (NULL != trn_particles_file)
//          free(trn_particles_file);
//        trn_particles_file = strdup(optarg);
//      }
//      /* TRN log directory */
//      else if (strcmp("trn-log", options[option_index].name) == 0) {
//        if (NULL != trn_log_dir)
//          free(trn_log_dir);
//        trn_log_dir = strdup(optarg);
//      }
//            /* TRN processing decimation (modulus, update every decn'th sounding) */
//      else if (strcmp("trn-decn", options[option_index].name) == 0) {
//          sscanf(optarg,"%u",&trn_decn);
//      }
//            /* TRN processing decimation (period, update every decms milliseconds) */
//      else if (strcmp("trn-decs", options[option_index].name) == 0) {
//          sscanf(optarg,"%lf",&trn_decs);
//          // start decimation timer
//          if(trn_decs>0.0)
//              trn_dec_time=mtime_dtime();
//
//      }
//      /* Ignore TRN transmit gain threshold checks */
//      else if (strcmp("trn-nombgain", options[option_index].name) == 0) {
//          trn_nombgain=true;
//      }
//
//#endif // WITH_MBTNAV
//
//      /* format */
//      else if (strcmp("format", options[option_index].name) == 0) {
//        n = sscanf(optarg, "%d", &format);
//      }
//
//      /*-------------------------------------------------------
//       * Set platform file */
//
//      /* platform-file */
//      else if (strcmp("platform-file", options[option_index].name) == 0) {
//        n = sscanf(optarg, "%s", platform_file);
//        if (n == 1)
//          use_platform_file = true;
//      }
//
//      /* platform-target-sensor */
//      else if (strcmp("platform-target-sensor", options[option_index].name) == 0) {
//          n = sscanf(optarg, "%d", &target_sensor);
//      }
//
//      /*-------------------------------------------------------
//       * Define processing parameters */
//
//      /* output */
//      else if ((strcmp("output", options[option_index].name) == 0)) {
//        strcpy(output_file, optarg);
//        if (strstr(output_file, "socket") != NULL) {
//            output_flags|=OUTPUT_MB1_SVR_EN;
//        }
//        if (strstr(output_file, "file") != NULL) {
//            output_flags|=OUTPUT_MB1_FILE_EN;
//        }
//      }
//
//      /* log-directory */
//      else if ((strcmp("log-directory", options[option_index].name) == 0)) {
//        strcpy(log_directory, optarg);
//        logd_status = stat(log_directory, &logd_stat);
//        if (logd_status != 0) {
//          fprintf(stderr, "\nSpecified log file directory %s does not exist...\n", log_directory);
//          make_logs = false;
//            int status=0;
//            char *ps = strdup(log_directory);
//           if( (status=mkdir(ps,S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH))==0){
//                make_logs = true;
//               free(trn_log_dir);
//               trn_log_dir=strdup(ps);
//                 fprintf(stderr, "\ncreated/using log directory %s...\n", trn_log_dir);
//            }else{
//                fprintf(stderr, "\nCreate log directory %s failed [%d/%s]\n", ps,errno,strerror(errno));
//            }
//            free(ps);
//        }
//        else if ((logd_stat.st_mode & S_IFMT) != S_IFDIR) {
//          fprintf(stderr, "\nSpecified log file directory %s is not a directory...\n", log_directory);
//          make_logs = false;
//        }
//        else {
//          make_logs = true;
//          free(trn_log_dir);
//          trn_log_dir = strdup(log_directory);
//          fprintf(stderr, "\nusing log directory %s...\n", trn_log_dir);
//        }
//      }
//
//      /* swathwidth */
//      else if ((strcmp("swath-width", options[option_index].name) == 0)) {
//        n = sscanf(optarg, "%lf", &swath_width);
//      }
//
//      /* soundings */
//      else if ((strcmp("soundings", options[option_index].name) == 0)) {
//        n = sscanf(optarg, "%d", &n_output_soundings);
//      }
//
//      /* median-filter */
//      else if ((strcmp("median-filter", options[option_index].name) == 0)) {
//        n = sscanf(optarg, "%lf/%d/%d", &median_filter_threshold,
//                            &median_filter_n_across, &median_filter_n_along);
//        if (n == 3) {
//          median_filter_en = true;
//          n_buffer_max = median_filter_n_along;
//        }
//      }
//
//      /*-------------------------------------------------------*/
//
//      break;
//    case '?':
//      errflg++;
//    }

  /* if error flagged then print it and exit */
  if (errflg) {
    fprintf(stderr, "usage: %s\n", usage_message);
    fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
    error = MB_ERROR_BAD_USAGE;
    exit(error);
  }

  /* print starting message */
  if (mbtrn_cfg->verbose == 1 || mbtrn_cfg->verbose <= -2 || mbtrn_opts->help) {
    fprintf(stderr, "\nProgram %s\n", program_name);
    fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
  }

  /* print starting debug statements */
  if (mbtrn_cfg->verbose >= 2) {
    fprintf(stderr, "\ndbg2  Program <%s>\n", program_name);
    fprintf(stderr, "dbg2  MB-system Version %s\n", MB_VERSION);
    fprintf(stderr, "dbg2  Control Parameters:\n");
    fprintf(stderr, "dbg2       verbose:                  %d\n", mbtrn_cfg->verbose);
    fprintf(stderr, "dbg2       help:                     %d\n", mbtrn_opts->help);
    fprintf(stderr, "dbg2       pings:                    %d\n", pings);
    fprintf(stderr, "dbg2       lonflip:                  %d\n", lonflip);
    fprintf(stderr, "dbg2       bounds[0]:                %f\n", bounds[0]);
    fprintf(stderr, "dbg2       bounds[1]:                %f\n", bounds[1]);
    fprintf(stderr, "dbg2       bounds[2]:                %f\n", bounds[2]);
    fprintf(stderr, "dbg2       bounds[3]:                %f\n", bounds[3]);
    fprintf(stderr, "dbg2       btime_i[0]:               %d\n", btime_i[0]);
    fprintf(stderr, "dbg2       btime_i[1]:               %d\n", btime_i[1]);
    fprintf(stderr, "dbg2       btime_i[2]:               %d\n", btime_i[2]);
    fprintf(stderr, "dbg2       btime_i[3]:               %d\n", btime_i[3]);
    fprintf(stderr, "dbg2       btime_i[4]:               %d\n", btime_i[4]);
    fprintf(stderr, "dbg2       btime_i[5]:               %d\n", btime_i[5]);
    fprintf(stderr, "dbg2       btime_i[6]:               %d\n", btime_i[6]);
    fprintf(stderr, "dbg2       etime_i[0]:               %d\n", etime_i[0]);
    fprintf(stderr, "dbg2       etime_i[1]:               %d\n", etime_i[1]);
    fprintf(stderr, "dbg2       etime_i[2]:               %d\n", etime_i[2]);
    fprintf(stderr, "dbg2       etime_i[3]:               %d\n", etime_i[3]);
    fprintf(stderr, "dbg2       etime_i[4]:               %d\n", etime_i[4]);
    fprintf(stderr, "dbg2       etime_i[5]:               %d\n", etime_i[5]);
    fprintf(stderr, "dbg2       etime_i[6]:               %d\n", etime_i[6]);
    fprintf(stderr, "dbg2       speedmin:                 %f\n", speedmin);
    fprintf(stderr, "dbg2       timegap:                  %f\n", timegap);
    fprintf(stderr, "dbg2       input:                    %s\n", mbtrn_cfg->input);
    fprintf(stderr, "dbg2       format:                   %d\n", mbtrn_cfg->format);
    fprintf(stderr, "dbg2       output:                   %s\n", mbtrn_cfg->output_file);
    fprintf(stderr, "dbg2       swath_width:              %f\n", mbtrn_cfg->swath_width);
    fprintf(stderr, "dbg2       n_output_soundings:       %d\n", mbtrn_cfg->n_output_soundings);
    fprintf(stderr, "dbg2       median_filter_en:         %d\n", mbtrn_cfg->median_filter_en);
    fprintf(stderr, "dbg2       median_filter_n_across:   %d\n", mbtrn_cfg->median_filter_n_across);
    fprintf(stderr, "dbg2       median_filter_n_along:    %d\n", mbtrn_cfg->median_filter_n_along);
    fprintf(stderr, "dbg2       median_filter_threshold:  %f\n", mbtrn_cfg->median_filter_threshold);
    fprintf(stderr, "dbg2       n_buffer_max:             %d\n", mbtrn_cfg->n_buffer_max);
    fprintf(stderr, "dbg2       socket_definition:        %s\n", mbtrn_cfg->socket_definition);
    fprintf(stderr, "dbg2       mb1svr_host:              %s\n", mbtrn_cfg->mb1svr_host);
    fprintf(stderr, "dbg2       mb1svr_port:              %d\n", mbtrn_cfg->mb1svr_port);
  }

  /* if help desired then print it and exit */
  if (mbtrn_opts->help) {
    fprintf(stderr, "\n%s\n", help_message);
    fprintf(stderr, "\nusage: %s\n", usage_message);
    exit(error);
  }

#ifdef SOCKET_TIMING
  // print time message
  struct timeval stv = {0};
  gettimeofday(&stv, NULL);
  double start_sys_time = (double)stv.tv_sec + ((double)stv.tv_usec / 1000000.0) + (7 * 3600);
  fprintf(stderr, "%11.5lf systime %.4lf\n", mtime_dtime(), start_sys_time);
#endif

  mbtrnpp_init_debug(mbtrn_cfg->verbose);

#ifdef WITH_MBTNAV

    trn_cfg = trncfg_new(NULL, -1, mbtrn_cfg->trn_utm_zone, mbtrn_cfg->trn_mtype, mbtrn_cfg->trn_ftype, mbtrn_cfg->trn_map_file, mbtrn_cfg->trn_cfg_file, mbtrn_cfg->trn_particles_file, mbtrn_cfg->trn_mission_id,trn_oflags,mbtrn_cfg->trn_max_ncov,mbtrn_cfg->trn_max_nerr, mbtrn_cfg->trn_max_ecov, mbtrn_cfg->trn_max_eerr);

    if (mbtrn_cfg->trn_enable &&  NULL!=trn_cfg ) {
        mbtrnpp_init_trn(&trn_instance,mbtrn_cfg->verbose, trn_cfg);

        // temporarily enable module debug
        mmd_en_mask_t olvl=0;

        if (mbtrn_cfg->verbose!=0) {
            olvl = mmd_get_enmask(MOD_MBTRNPP, NULL);
            mmd_channel_en(MOD_MBTRNPP,MM_DEBUG);
        }

        // initialize socket outputs
        int test=-1;
        if( (test=mbtrnpp_init_trnsvr(&trnsvr, trn_instance, mbtrn_cfg-> trnsvr_host,mbtrn_cfg->trnsvr_port,true))==0){
//            PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"TRN server netif OK [%s:%d]\n",mbtrn_cfg-> trnsvr_host,mbtrn_cfg->trnsvr_port));
            fprintf(stderr,"TRN server netif OK [%s:%d]\n",mbtrn_cfg-> trnsvr_host,mbtrn_cfg->trnsvr_port);

        }else{
            fprintf(stderr, "\nTRN server netif init failed [%d] [%d %s]\n",test,errno,strerror(errno));
        }

        test = -1;
        if( (test=mbtrnpp_init_trnusvr(&trnusvr, mbtrn_cfg->trnusvr_host,mbtrn_cfg->trnusvr_port,true))==0){
//            PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"TRNU server netif OK [%s:%d]\n",mbtrn_cfg->trnusvr_host,mbtrn_cfg-> trnusvr_port));
            fprintf(stderr,"TRNU server netif OK [%s:%d]\n",mbtrn_cfg->trnusvr_host,mbtrn_cfg-> trnusvr_port);
        }else{
            fprintf(stderr, "TRNU server netif init failed [%d] [%d %s]\n",test,errno,strerror(errno));
        }

        if (mbtrn_cfg->verbose != 0) {
       // restore module debug
        mmd_channel_set(MOD_MBTRNPP,olvl);
       }
    }else{
        fprintf(stderr,"WARN: skipping TRN init trn_enable[%c] trn_cfg[%p]\n",(mbtrn_cfg->trn_enable?'Y':'N'),trn_cfg);
    }

    // release the config strings
    MEM_CHKINVALIDATE(mbtrn_cfg->trn_map_file);
    MEM_CHKINVALIDATE(mbtrn_cfg->trn_cfg_file);
    MEM_CHKINVALIDATE(mbtrn_cfg->trn_particles_file);
    MEM_CHKINVALIDATE(mbtrn_cfg->trn_mission_id);

    trncfg_show(trn_cfg, true, 5);

#endif // WITH_MBTNAV

  /* load platform definition if specified */
  if (mbtrn_cfg->use_platform_file == true) {
    status = mb_platform_read(mbtrn_cfg->verbose, mbtrn_cfg->platform_file, (void **)&platform, &error);
    if (status == MB_FAILURE) {
      error = MB_ERROR_OPEN_FAIL;
      fprintf(stderr, "\nUnable to open and parse platform file: %s\n", mbtrn_cfg->platform_file);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      exit(error);
    }

    /* get sensor structures */
    if (platform->source_bathymetry >= 0)
      sensor_bathymetry = &(platform->sensors[platform->source_bathymetry]);
    if (platform->source_backscatter >= 0)
      sensor_backscatter = &(platform->sensors[platform->source_backscatter]);
    if (platform->source_position >= 0)
      sensor_position = &(platform->sensors[platform->source_position]);
    if (platform->source_depth >= 0)
      sensor_depth = &(platform->sensors[platform->source_depth]);
    if (platform->source_heading >= 0)
      sensor_heading = &(platform->sensors[platform->source_heading]);
    if (platform->source_rollpitch >= 0)
      sensor_rollpitch = &(platform->sensors[platform->source_rollpitch]);
    if (platform->source_heave >= 0)
      sensor_heave = &(platform->sensors[platform->source_heave]);
    if (mbtrn_cfg->target_sensor < 0)
      mbtrn_cfg->target_sensor = platform->source_bathymetry;
    if (mbtrn_cfg->target_sensor >= 0)
      sensor_target = &(platform->sensors[mbtrn_cfg->target_sensor]);
  }

  /* initialize output */
    if ( OUTPUT_FLAG_SET(OUTPUT_MBSYS_STDOUT)) {
    }
  /* insert option to recognize and initialize ipc with TRN */
  /* else open ipc to TRN */

 if ( OUTPUT_FLAG_SET(OUTPUT_MB1_SVR_EN) ) {
    mmd_en_mask_t olvl= 0;
    if (mbtrn_cfg->verbose != 0) {
      olvl = mmd_get_enmask(MOD_MBTRNPP, NULL);
      mmd_channel_en(MOD_MBTRNPP, MM_DEBUG);
    }

    int test = -1;
     if( (test=mbtrnpp_init_mb1svr(&mb1svr, mbtrn_cfg->mb1svr_host,mbtrn_cfg->mb1svr_port,true))==0){
         PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"MB1 server netif OK [%s:%d]\n",mbtrn_cfg->mb1svr_host,mbtrn_cfg->mb1svr_port));
         fprintf(stderr,"MB1 server netif OK [%s:%d]\n",mbtrn_cfg->mb1svr_host,mbtrn_cfg->mb1svr_port);
      }else{
          fprintf(stderr, "MB1 server netif init failed [%d] [%d %s]\n",test,errno,strerror(errno));
      }

    if (mbtrn_cfg->verbose != 0) {
      mmd_channel_set(MOD_MBTRNPP, olvl);
    }
  }

    /* else open output file in which the binary data otherwise communicated
   * to TRN will be saved */
 if ( OUTPUT_FLAG_SET(OUTPUT_MB1_FILE_EN)) {
     if(NULL!=mbtrn_cfg->trn_log_dir){
         if(mbtrn_cfg->output_file[0]!='/' && mbtrn_cfg->output_file[0]!='.'){
             char *ocopy = strdup(mbtrn_cfg->output_file);
             if(NULL!=ocopy){
             sprintf(mbtrn_cfg->output_file,"%s/%s",mbtrn_cfg->trn_log_dir,ocopy);
             free(ocopy);
             }
         }
     }
    output_fp = fopen(mbtrn_cfg->output_file, "w");
  }

  /* get number of ping records to hold */
  if (mbtrn_cfg->median_filter_en == true) {
    median_filter_n_total = mbtrn_cfg->median_filter_n_across * mbtrn_cfg->median_filter_n_along;
    median_filter_n_min = median_filter_n_total / 2;

    /* allocate memory for median filter */
    if (error == MB_ERROR_NO_ERROR) {
      status = mb_mallocd(mbtrn_cfg->verbose, __FILE__, __LINE__, median_filter_n_total * sizeof(double),
                          (void **)&median_filter_soundings, &error);
      if (error != MB_ERROR_NO_ERROR) {
        mb_error(mbtrn_cfg->verbose, error, &message);
        fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
        fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
        exit(error);
      }
    }
  }

  /* get format if required */
  if (mbtrn_cfg->format == 0)
    mb_get_format(mbtrn_cfg->verbose, mbtrn_cfg->input, NULL, &mbtrn_cfg->format, &error);

  /* determine whether to read one file or a list of files */
  if (mbtrn_cfg->format < 0)
    read_datalist = true;

  /* open file list */
  if (read_datalist == true) {
    if ((status = mb_datalist_open(mbtrn_cfg->verbose, &datalist, mbtrn_cfg->input, look_processed, &error)) != MB_SUCCESS) {
      error = MB_ERROR_OPEN_FAIL;
      fprintf(stderr, "\nUnable to open data list file: %s\n", mbtrn_cfg->input);
      fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
      exit(error);
    }
    if ((status = mb_datalist_read(mbtrn_cfg->verbose, datalist, ifile, dfile, &mbtrn_cfg->format, &file_weight, &error)) == MB_SUCCESS)
      read_data = true;
    else
      read_data = false;
  }
  /* else copy single filename to be read */
  else {
    strcpy(ifile, mbtrn_cfg->input);
    read_data = true;
  }

    /* set transmit_gain threshold according to format */
    double transmit_gain_threshold = 0.0;
    if (mbtrn_cfg->format == MBF_RESON7KR) {
        transmit_gain_threshold = TRN_XMIT_GAIN_RESON7K_DFL;
    }
    else if (mbtrn_cfg->format == MBF_KEMKMALL) {
        transmit_gain_threshold = TRN_XMIT_GAIN_KMALL_DFL;
    }
    mlog_tprintf(mbtrnpp_mlog_id,"mbtrnpp: transmit gain threshold[%.2lf] nombgain[%c]\n",transmit_gain_threshold,(mbtrn_cfg->trn_nombgain?'Y':'N'));

  // kick off the first cycle here
  // future cycles start and end in the stats update
  MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_CYCLE_XT], mtime_dtime());
  MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_STATS_XT], mtime_dtime());
  /* loop over all files to be read */
  while (read_data == true) {

    /* open log file if specified */
    if (mbtrn_cfg->make_logs == true) {
      gettimeofday(&timeofday, &timezone);
      now_time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
      // fprintf(stderr,"CHECKING AT TOP OF LOOP: logfp:%p log_file_open_time_d:%.6ff now_time_d:%.6f\n", logfp,
      // log_file_open_time_d, now_time_d);
      if (logfp == NULL || (now_time_d - log_file_open_time_d) > MBTRNPREPROCESS_LOGFILE_TIMELENGTH) {
        if (logfp != NULL) {
          status = mbtrnpp_logstatistics(mbtrn_cfg->verbose, logfp, n_pings_read, n_soundings_read, n_soundings_valid_read,
                                         n_soundings_flagged_read, n_soundings_null_read, n_soundings_trimmed,
                                         n_soundings_decimated, n_soundings_flagged, n_soundings_written, &error);
          n_tot_pings_read += n_pings_read;
          n_tot_soundings_read += n_soundings_read;
          n_tot_soundings_valid_read += n_soundings_valid_read;
          n_tot_soundings_flagged_read += n_soundings_flagged_read;
          n_tot_soundings_null_read += n_soundings_null_read;
          n_tot_soundings_trimmed += n_soundings_trimmed;
          n_tot_soundings_decimated += n_soundings_decimated;
          n_tot_soundings_flagged += n_soundings_flagged;
          n_tot_soundings_written += n_soundings_written;
          n_pings_read = 0;
          n_soundings_read = 0;
          n_soundings_valid_read = 0;
          n_soundings_flagged_read = 0;
          n_soundings_null_read = 0;
          n_soundings_trimmed = 0;
          n_soundings_decimated = 0;
          n_soundings_flagged = 0;
          n_soundings_written = 0;

          status = mbtrnpp_closelog(mbtrn_cfg->verbose, &logfp, &error);
        }

        status = mbtrnpp_openlog(mbtrn_cfg->verbose, mbtrn_cfg->log_directory, &logfp, &error);
        if (status == MB_SUCCESS) {
          gettimeofday(&timeofday, &timezone);
          log_file_open_time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
          status = mbtrnpp_logparameters(mbtrn_cfg->verbose, logfp, mbtrn_cfg->input, mbtrn_cfg->format, mbtrn_cfg->output_file, mbtrn_cfg->swath_width, mbtrn_cfg->n_output_soundings,
                                         mbtrn_cfg->median_filter_en, mbtrn_cfg->median_filter_n_across, mbtrn_cfg->median_filter_n_along,
                                         mbtrn_cfg->median_filter_threshold, mbtrn_cfg->n_buffer_max, &error);
        }
        else {
          fprintf(stderr, "\nLog file could not be opened in directory %s...\n", mbtrn_cfg->log_directory);
          fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
          exit(error);
        }
      }
    }

      fprintf(stderr, "\nmbtrn_cfg->input[%s]\n", mbtrn_cfg->input);
    /* check for format with amplitude or sidescan data */
    status = mb_format_system(mbtrn_cfg->verbose, &mbtrn_cfg->format, &system, &error);
    status = mb_format_dimensions(mbtrn_cfg->verbose, &mbtrn_cfg->format, &beams_bath, &beams_amp, &pixels_ss, &error);

    /* initialize reading the input swath data over a socket interface
     * using functions defined in this code block and passed into the
     * init function as function pointers */
    if (strncmp(mbtrn_cfg->input, "socket", 6) == 0) {
      if (mbtrn_cfg->format == MBF_RESON7KR) {
        mbtrnpp_input_open = &mbtrnpp_reson7kr_input_open;
        mbtrnpp_input_read = &mbtrnpp_reson7kr_input_read;
        mbtrnpp_input_close = &mbtrnpp_reson7kr_input_close;
      }
      else if (mbtrn_cfg->format == MBF_KEMKMALL) {
        mbtrnpp_input_open = &mbtrnpp_kemkmall_input_open;
        mbtrnpp_input_read = &mbtrnpp_kemkmall_input_read;
        mbtrnpp_input_close = &mbtrnpp_kemkmall_input_close;
      }else{
          fprintf(stderr,"ERR - Invalid output format [%d]\n",mbtrn_cfg->format);
      }
      if ((status = mb_input_init(mbtrn_cfg->verbose, mbtrn_cfg->socket_definition, mbtrn_cfg->format, pings, lonflip, bounds,
                                  btime_i, etime_i, speedmin, timegap,
                                  &imbio_ptr, &btime_d, &etime_d,
                                  &beams_bath, &beams_amp, &pixels_ss,
                                  mbtrnpp_input_open, mbtrnpp_input_read, mbtrnpp_input_close,
                                  &error) != MB_SUCCESS)) {

        sprintf(log_message, "MBIO Error returned from function <mb_input_init>");
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        fprintf(stderr, "\n%s\n", log_message);

        mb_error(mbtrn_cfg->verbose, error, &message);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, message, &error);
        fprintf(stderr, "%s\n", message);

        sprintf(log_message, "Sonar data socket <%s> not initialized for reading", ifile);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        fprintf(stderr, "\n%s\n", log_message);

        sprintf(log_message, "Program <%s> Terminated", program_name);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        fprintf(stderr, "\n%s\n", log_message);

        exit(error);
      }
      else {

        sprintf(log_message, "Sonar data socket <%s> initialized for reading", ifile);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        if (mbtrn_cfg->verbose > 0)
          fprintf(stderr, "\n%s\n", log_message);

        sprintf(log_message, "MBIO format id: %d", mbtrn_cfg->format);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        if (mbtrn_cfg->verbose > 0)
          fprintf(stderr, "%s\n", log_message);
      }
    }

    /* otherwised open swath data files as is normal for MB-System programs */
    else {

      if ((status = mb_read_init(mbtrn_cfg->verbose, ifile, mbtrn_cfg->format, pings, lonflip, bounds, btime_i, etime_i, speedmin, timegap,
                                 &imbio_ptr, &btime_d, &etime_d, &beams_bath, &beams_amp, &pixels_ss, &error)) !=
          MB_SUCCESS) {

        sprintf(log_message, "MBIO Error returned from function <mb_read_init>");
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        fprintf(stderr, "\n%s\n", log_message);

        mb_error(mbtrn_cfg->verbose, error, &message);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, message, &error);
        fprintf(stderr, "%s\n", message);

        sprintf(log_message, "Sonar File <%s> not initialized for reading", ifile);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        fprintf(stderr, "\n%s\n", log_message);

        sprintf(log_message, "Program <%s> Terminated", program_name);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        fprintf(stderr, "\n%s\n", log_message);

        exit(error);
      }
      else {
        sprintf(log_message, "Sonar File <%s> initialized for reading", ifile);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        if (mbtrn_cfg->verbose > 0)
          fprintf(stderr, "\n%s\n", log_message);

        sprintf(log_message, "MBIO format id: %d", mbtrn_cfg->format);
        if (logfp != NULL)
          mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
        if (mbtrn_cfg->verbose > 0)
          fprintf(stderr, "%s\n", log_message);
      }
    }

    /* allocate memory for data arrays */
    memset(ping, 0, MBTRNPREPROCESS_BUFFER_DEFAULT * sizeof(struct mbtrnpp_ping_struct));
    for (i = 0; i < mbtrn_cfg->n_buffer_max; i++) {
      if (error == MB_ERROR_NO_ERROR)
        status = mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char), (void **)&ping[i].beamflag,
                                   &error);
      if (error == MB_ERROR_NO_ERROR)
        status = mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(char),
                                   (void **)&ping[i].beamflag_filter, &error);
      if (error == MB_ERROR_NO_ERROR)
        status =
            mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double), (void **)&ping[i].bath, &error);
      if (error == MB_ERROR_NO_ERROR)
        status =
            mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_AMPLITUDE, sizeof(double), (void **)&ping[i].amp, &error);
      if (error == MB_ERROR_NO_ERROR)
        status = mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double),
                                   (void **)&ping[i].bathacrosstrack, &error);
      if (error == MB_ERROR_NO_ERROR)
        status = mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_BATHYMETRY, sizeof(double),
                                   (void **)&ping[i].bathalongtrack, &error);
      if (error == MB_ERROR_NO_ERROR)
        status =
            mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double), (void **)&ping[i].ss, &error);
      if (error == MB_ERROR_NO_ERROR)
        status = mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double),
                                   (void **)&ping[i].ssacrosstrack, &error);
      if (error == MB_ERROR_NO_ERROR)
        status = mb_register_array(mbtrn_cfg->verbose, imbio_ptr, MB_MEM_TYPE_SIDESCAN, sizeof(double),
                                   (void **)&ping[i].ssalongtrack, &error);
    }

    /* plan on storing enough pings for median filter */
    mbtrn_cfg->n_buffer_max = mbtrn_cfg->median_filter_n_along;
    n_ping_process = mbtrn_cfg->n_buffer_max / 2;

    /* loop over reading data */
    bool done = false;
    idataread = 0;

    while (!done) {
      /* open new log file if it is time */
      if (mbtrn_cfg->make_logs == true) {

        gettimeofday(&timeofday, &timezone);
        now_time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
        // fprintf(stderr,"CHECKING AT MIDDLE OF LOOP: logfp:%p log_file_open_time_d:%.6f now_time_d:%.6f\n", logfp,
        // log_file_open_time_d, now_time_d);
        if (logfp == NULL || (now_time_d - log_file_open_time_d) > MBTRNPREPROCESS_LOGFILE_TIMELENGTH) {
          if (logfp != NULL) {
            status = mbtrnpp_logstatistics(mbtrn_cfg->verbose, logfp, n_pings_read, n_soundings_read, n_soundings_valid_read,
                                           n_soundings_flagged_read, n_soundings_null_read, n_soundings_trimmed,
                                           n_soundings_decimated, n_soundings_flagged, n_soundings_written, &error);
            n_tot_pings_read += n_pings_read;
            n_tot_soundings_read += n_soundings_read;
            n_tot_soundings_valid_read += n_soundings_valid_read;
            n_tot_soundings_flagged_read += n_soundings_flagged_read;
            n_tot_soundings_null_read += n_soundings_null_read;
            n_tot_soundings_trimmed += n_soundings_trimmed;
            n_tot_soundings_decimated += n_soundings_decimated;
            n_tot_soundings_flagged += n_soundings_flagged;
            n_tot_soundings_written += n_soundings_written;
            n_pings_read = 0;
            n_soundings_read = 0;
            n_soundings_valid_read = 0;
            n_soundings_flagged_read = 0;
            n_soundings_null_read = 0;
            n_soundings_trimmed = 0;
            n_soundings_decimated = 0;
            n_soundings_flagged = 0;
            n_soundings_written = 0;

            status = mbtrnpp_closelog(mbtrn_cfg->verbose, &logfp, &error);
          }

          status = mbtrnpp_openlog(mbtrn_cfg->verbose, mbtrn_cfg->log_directory, &logfp, &error);
          if (status == MB_SUCCESS) {
            gettimeofday(&timeofday, &timezone);
            log_file_open_time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
            status = mbtrnpp_logparameters(mbtrn_cfg->verbose, logfp, mbtrn_cfg->input, mbtrn_cfg->format, mbtrn_cfg->output_file, mbtrn_cfg->swath_width, mbtrn_cfg->n_output_soundings,
                                           mbtrn_cfg->median_filter_en, mbtrn_cfg->median_filter_n_across, mbtrn_cfg->median_filter_n_along,
                                           mbtrn_cfg->median_filter_threshold, mbtrn_cfg->n_buffer_max, &error);
          }
          else {
            fprintf(stderr, "\nLog file could not be opened in directory %s...\n", mbtrn_cfg->log_directory);
            fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
            exit(error);
          }
        }
      }

      /* read the next data */
      error = MB_ERROR_NO_ERROR;

      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_GETALL_XT], mtime_dtime());
      status = mb_get_all(mbtrn_cfg->verbose, imbio_ptr, &store_ptr, &kind, ping[idataread].time_i, &ping[idataread].time_d,
                          &ping[idataread].navlon, &ping[idataread].navlat, &ping[idataread].speed,
                          &ping[idataread].heading, &ping[idataread].distance, &ping[idataread].altitude,
                          &ping[idataread].sonardepth, &ping[idataread].beams_bath, &ping[idataread].beams_amp,
                          &ping[idataread].pixels_ss, ping[idataread].beamflag, ping[idataread].bath, ping[idataread].amp,
                          ping[idataread].bathacrosstrack, ping[idataread].bathalongtrack, ping[idataread].ss,
                          ping[idataread].ssacrosstrack, ping[idataread].ssalongtrack, comment, &error);

      //            PMPRINT(MOD_MBTRNPP,MBTRNPP_V4,(stderr,"mb_get_all - status[%d] kind[%d] err[%d]\n",status, kind,
      //            error));
      MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_GETALL_XT], mtime_dtime());
      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_PING_XT], mtime_dtime());

      if (status == MB_SUCCESS && kind == MB_DATA_DATA) {
        ping[idataread].count = ndata;
        ndata++;
        n_pings_read++;
        n_soundings_read += ping[idataread].beams_bath;


          // apply transmit gain thresholding
          double transmit_gain;
          double pulse_length;
          double receive_gain;

          status = mb_gains(mbtrn_cfg->verbose, imbio_ptr, store_ptr, &kind, &transmit_gain, &pulse_length, &receive_gain, &error);
          if (transmit_gain < transmit_gain_threshold) {
              for (i = 0; i < ping[idataread].beams_bath; i++) {
                  if (mb_beam_ok(ping[idataread].beamflag[i])) {
                      ping[idataread].beamflag[i] = (char)(MB_FLAG_SONAR | MB_FLAG_FLAG);
                  }
              }
          }

          // count soundings
        for (i = 0; i < ping[idataread].beams_bath; i++) {
          ping[idataread].beamflag_filter[i] = ping[idataread].beamflag[i];
          if (mb_beam_ok(ping[idataread].beamflag[i])) {
            n_soundings_valid_read++;
          }
          else if (ping[idataread].beamflag[i] == MB_FLAG_NULL) {
            n_soundings_null_read++;
          }
          else {
            n_soundings_flagged_read++;
          }
        }

        status = mb_extract_nav(mbtrn_cfg->verbose, imbio_ptr, store_ptr, &kind, ping[idataread].time_i, &ping[idataread].time_d,
                                &ping[idataread].navlon, &ping[idataread].navlat, &ping[idataread].speed,
                                &ping[idataread].heading, &ping[idataread].sonardepth, &ping[idataread].roll,
                                &ping[idataread].pitch, &ping[idataread].heave, &error);
        status = mb_extract_altitude(mbtrn_cfg->verbose, imbio_ptr, store_ptr, &kind, &ping[idataread].sonardepth,
                                     &ping[idataread].altitude, &error);

        /* only process and output if enough data have been read */
        if (ndata == mbtrn_cfg->n_buffer_max) {
          for (i = 0; i < mbtrn_cfg->n_buffer_max; i++) {
            if (ping[i].count == n_ping_process)
              i_ping_process = i;
          }
          // fprintf(stdout, "\nProcess some data: ndata:%d counts: ", ndata);
          // for (i = 0; i < mbtrn_cfg->n_buffer_max; i++) {
          //    fprintf(stdout,"%d ", ping[i].count);
          //}
          // fprintf(stdout," : process %d\n", i_ping_process);

          /* apply swath width */
          threshold_tangent = tan(DTR * 0.5 * mbtrn_cfg->swath_width);
          beam_start = ping[i_ping_process].beams_bath - 1;
          beam_end = 0;
          for (j = 0; j < ping[i_ping_process].beams_bath; j++) {
            if (mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {
              tangent = ping[i_ping_process].bathacrosstrack[j] /
                        (ping[i_ping_process].bath[j] - ping[i_ping_process].sonardepth);
              if (fabs(tangent) > threshold_tangent && mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {
                ping[i_ping_process].beamflag_filter[j] = MB_FLAG_FLAG + MB_FLAG_FILTER;
                n_soundings_trimmed++;
              }
              else {
                beam_start = MIN(beam_start, j);
                beam_end = MAX(beam_end, j);
              }
            }
          }

          /* apply decimation - only consider outputting decimated soundings */
          beam_decimation = ((beam_end - beam_start + 1) / mbtrn_cfg->n_output_soundings) + 1;
          dj = mbtrn_cfg->median_filter_n_across / 2;
          di = mbtrn_cfg->median_filter_n_along / 2;
          n_output = 0;
          for (j = beam_start; j <= beam_end; j++) {
            if ((j - beam_start) % beam_decimation == 0) {
              if (mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {
                /* apply median filtering to this sounding */
                if (median_filter_n_total > 1) {
                  /* accumulate soundings for median filter */
                  n_median_filter_soundings = 0;
                  jj0 = MAX(beam_start, j - dj);
                  jj1 = MIN(beam_end, j + dj);
                  for (ii = 0; ii < mbtrn_cfg->n_buffer_max; ii++) {
                    for (jj = jj0; jj <= jj1; jj++) {
                      if (mb_beam_ok(ping[ii].beamflag[jj])) {
                        median_filter_soundings[n_median_filter_soundings] = ping[ii].bath[jj];
                        n_median_filter_soundings++;
                      }
                    }
                  }

                  /* run qsort */
                  qsort((char *)median_filter_soundings, n_median_filter_soundings, sizeof(double),
                        (void *)mb_double_compare);
                  median = median_filter_soundings[n_median_filter_soundings / 2];
                  // fprintf(stdout, "Beam %3d of %d:%d bath:%.3f n:%3d:%3d median:%.3f ", j, beam_start,
                  // beam_end, ping[i_ping_process].bath[j], n_median_filter_soundings, median_filter_n_min,
                  // median);

                  /* apply median filter - also flag soundings that don't have enough neighbors to filter */
                  if (n_median_filter_soundings < median_filter_n_min ||
                      fabs(ping[i_ping_process].bath[j] - median) > mbtrn_cfg->median_filter_threshold * median) {
                    ping[i_ping_process].beamflag_filter[j] = MB_FLAG_FLAG + MB_FLAG_FILTER;
                    n_soundings_flagged++;

                    // fprintf(stdout, "**filtered**");
                  }
                  // fprintf(stdout, "\n");
                }
                if (mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {
                  n_output++;
                }
              }
            }
            else if (mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {
              ping[i_ping_process].beamflag_filter[j] = MB_FLAG_FLAG + MB_FLAG_FILTER;
              n_soundings_decimated++;
            }
          }

          /* write out results to stdout as text */
            if ( OUTPUT_FLAG_SET(OUTPUT_MBSYS_STDOUT) ) {
            fprintf(stdout, "Ping: %.9f %.7f %.7f %.3f %.3f %4d\n", ping[i_ping_process].time_d,
                    ping[i_ping_process].navlat, ping[i_ping_process].navlon, ping[i_ping_process].sonardepth,
                    (double)(DTR * ping[i_ping_process].heading), n_output);
            for (j = 0; j < ping[i_ping_process].beams_bath; j++) {
              if (mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {
                fprintf(stdout, "%3.3d starboard:%.3f forward:%.3f down:%.3f\n", j,
                        ping[i_ping_process].bathacrosstrack[j], ping[i_ping_process].bathalongtrack[j],
                        ping[i_ping_process].bath[j] - ping[i_ping_process].sonardepth);
                n_soundings_written++;
              }
            }
          }

          /* pack the data into a TRN MB1 packet and either send it to TRN or write it to a file */
        if(!OUTPUT_FLAGS_ZERO()){
            n_soundings_written++;

            /* make sure buffer is large enough to hold the packet */
            mb1_size = MBTRNPREPROCESS_MB1_HEADER_SIZE + n_output * MBTRNPREPROCESS_MB1_SOUNDING_SIZE +
                       MBTRNPREPROCESS_MB1_CHECKSUM_SIZE;
            if (n_output_buffer_alloc < mb1_size) {
              if ((status = mb_reallocd(mbtrn_cfg->verbose, __FILE__, __LINE__, mb1_size, (void **)&output_buffer, &error)) ==
                  MB_SUCCESS) {
                n_output_buffer_alloc = mb1_size;
              }
              else {
                mb_error(mbtrn_cfg->verbose, error, &message);
                fprintf(stderr, "\nMBIO Error allocating data arrays:\n%s\n", message);
                fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
                exit(error);
              }
            }

            // get ping number
            mb_pingnumber(mbtrn_cfg->verbose, imbio_ptr, &ping_number, &error);

            /* now pack the data into the packet buffer */
            index = 0;
            output_buffer[index] = 'M';
            index++;
            output_buffer[index] = 'B';
            index++;
            output_buffer[index] = '1';
            index++;
            output_buffer[index] = 0;
            index++;
            mb_put_binary_int(true, mb1_size, &output_buffer[index]);
            index += 4;

            mb_put_binary_double(true, ping[i_ping_process].time_d, &output_buffer[index]);
            index += 8;
            mb_put_binary_double(true, ping[i_ping_process].navlat, &output_buffer[index]);
            index += 8;
            mb_put_binary_double(true, ping[i_ping_process].navlon, &output_buffer[index]);
            index += 8;
            mb_put_binary_double(true, ping[i_ping_process].sonardepth, &output_buffer[index]);
            index += 8;
            mb_put_binary_double(true, (double)(DTR * ping[i_ping_process].heading), &output_buffer[index]);
            index += 8;

            mb_put_binary_int(true, ping_number, &output_buffer[index]);
            index += 4;

            mb_put_binary_int(true, n_output, &output_buffer[index]);
            index += 4;

            PMPRINT(MOD_MBTRNPP, MBTRNPP_V1,
                    (stderr,
                     "\nts[%.3lf] beams[%03d] ping[%06u]\nlat[%.4lf] lon[%.4lf] hdg[%6.2lf] sd[%7.2lf]\nv[%+6.2lf] "
                     "p/r/y[%.3lf / %.3lf / %.3lf]\n",
                     ping[i_ping_process].time_d, n_output, ping_number, ping[i_ping_process].navlat,
                     ping[i_ping_process].navlon, (double)(DTR * ping[i_ping_process].heading),
                     ping[i_ping_process].sonardepth, ping[i_ping_process].speed, ping[i_ping_process].pitch,
                     ping[i_ping_process].roll, ping[i_ping_process].heave));

            for (j = 0; j < ping[i_ping_process].beams_bath; j++) {
              if (mb_beam_ok(ping[i_ping_process].beamflag_filter[j])) {

                mb_put_binary_int(true, j, &output_buffer[index]);
                index += 4;
                mb_put_binary_double(true, ping[i_ping_process].bathalongtrack[j], &output_buffer[index]);
                index += 8;
                mb_put_binary_double(true, ping[i_ping_process].bathacrosstrack[j], &output_buffer[index]);
                index += 8;
                //                                mb_put_binary_double(true, ping[i_ping_process].bath[j],
                //                                &output_buffer[index]); index += 8;
                // subtract sonar depth from vehicle bathy; changed 12jul18 cruises
                mb_put_binary_double(true, (ping[i_ping_process].bath[j] - ping[i_ping_process].sonardepth),
                                     &output_buffer[index]);
                index += 8;

                PMPRINT(MOD_MBTRNPP, MBTRNPP_V2,
                        (stderr, "n[%03d] atrk/X[%+10.3lf] ctrk/Y[%+10.3lf] dpth/Z[%+10.3lf]\n", j,
                         ping[i_ping_process].bathalongtrack[j], ping[i_ping_process].bathacrosstrack[j],
                         (ping[i_ping_process].bath[j] - ping[i_ping_process].sonardepth)));
              }
            }

            /* add the checksum */
            checksum = 0;
            unsigned char *cp = (unsigned char *)output_buffer;
            for (j = 0; j < index; j++) {
              //                            checksum += (unsigned int) output_buffer[j];
              checksum += (unsigned int)(*cp++);
            }

            mb_put_binary_int(true, checksum, &output_buffer[index]);
            index += 4;
            PMPRINT(MOD_MBTRNPP, MBTRNPP_V3, (stderr, "mb1 record chk[%08X] idx[%zu] mb1sz[%zu]\n", checksum, index, mb1_size));

            MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_PING_XT], mtime_dtime());

            /* output MB1, TRN data */
            if ( !OUTPUT_FLAGS_ZERO() ) {

                MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_PROC_MB1_XT], mtime_dtime());


                // do MB1 processing/output
                mbtrnpp_process_mb1(output_buffer, mb1_size, trn_cfg);


                MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_PROC_MB1_XT], mtime_dtime());

#ifdef WITH_MBTNAV
                if (mbtrn_cfg->trn_nombgain || (transmit_gain >= transmit_gain_threshold) ){
                    // reinit TRN filter when transmit gains indicate valid input
                    // and clear the flag
                    if ( trn_reinit_flag ){
                        wtnav_reinit_filter(trn_instance,true);
                        trn_reinit_flag=false;
                        mlog_tprintf(mbtrnpp_mlog_id,"mbtrnpp: trn filter reinit gain[%.2lf]\n",transmit_gain);
                        MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_TRN_REINIT]);
                    }

                    MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_PROC_TRN_XT], mtime_dtime());

                    // do TRN processing/output
                    mbtrnpp_trn_process_mb1(trn_instance, (mb1_t *)output_buffer, trn_cfg);

                    MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_PROC_TRN_XT], mtime_dtime());

                }else{
                    // if transmit gain drops below threshold...
                    if(!trn_reinit_flag){
                        // log and count gain lo event (one time)
                        mlog_tprintf(mbtrnpp_mlog_id,"mbtrnpp: transmit gain lo[%.2lf]\n",transmit_gain);
                        MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_GAIN_LO]);
                    }
                    // reset reinit flag so reinit will occur when gain restored
                    trn_reinit_flag=true;
                }
#endif // WITH_MBTNAV

                MBTRNPP_UPDATE_STATS(app_stats, mbtrnpp_mlog_id, mbtrn_cfg->mbtrnpp_stat_flags);

            } // end MBTRNPREPROCESS_OUTPUT_TRN

            /* write the packet to a file */
            if ( OUTPUT_FLAG_SET(OUTPUT_MB1_FILE_EN) ) {

                if(NULL!=output_fp && NULL!=output_buffer){
                    MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_FWRITE_XT], mtime_dtime());

                    size_t obytes=0;
                    if( (obytes=fwrite(output_buffer, mb1_size, 1, output_fp))>0){
                        MST_COUNTER_ADD(app_stats->stats->status[MBTPP_STA_MB_FWRITE_BYTES],mb1_size);
                    }else{
                        MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_EMBLOGWR]);
                    }

                    MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_FWRITE_XT], mtime_dtime());

                }else{
                    fprintf(stderr,"%s:%d - ERR fwrite failed obuf[%p] fp[%p]\n",__FUNCTION__,__LINE__,output_buffer,output_fp);
                }
              // fprintf(stderr, "WRITE SIZE: %zu %zu %zu\n", mb1_size, index, index - mb1_size);
            }
          } // else !stdout
        } // data read (ndata == mbtrn_cfg->n_buffer_max)

        /* move data in buffer */
        if (ndata >= mbtrn_cfg->n_buffer_max) {
          ndata--;
          for (i = 0; i < mbtrn_cfg->n_buffer_max; i++) {
            ping[i].count--;
            if (ping[i].count < 0) {
              idataread = i;
            }
          }
        }
        else {
          idataread++;
          if (idataread >= mbtrn_cfg->n_buffer_max)
            idataread = 0;
        }
      }
      else {

        MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_GETFAIL_XT], mtime_dtime());
        PMPRINT(MOD_MBTRNPP, MBTRNPP_V4,
                (stderr, "mb_get_all failed: status[%d] kind[%d] err[%d]\n", status, kind, error));

        if ((status == MB_FAILURE) && (error == MB_ERROR_EOF) && (mbtrn_cfg->input_mode == INPUT_MODE_SOCKET)) {

          MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_EMBGETALL]);

          fprintf(stderr, "EOF (input socket) - clear status/error\n");
          status = MB_SUCCESS;
          error = MB_ERROR_NO_ERROR;

        }
        MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_GETFAIL_XT], mtime_dtime());
      }

      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_POST_XT], mtime_dtime());

      if (status == MB_FAILURE && error > 0) {
        fprintf(stderr, "mbtrnpp: MB_FAILURE - error>0 : setting done flag\n");
        done = true;
        MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_EMBFAILURE]);
      }
      MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_POST_XT], mtime_dtime());
    } // while(!done) [main loop]

    /* close the files */
    if (mbtrn_cfg->input_mode == INPUT_MODE_SOCKET) {
      fprintf(stderr, "socket input mode - continue (probably shouldn't be here)\n");
      read_data = true;
    }
    else {
      fprintf(stderr, "file input mode - file cleanup\n");
      status = mb_close(mbtrn_cfg->verbose, &imbio_ptr, &error);

      if (logfp != NULL) {
        sprintf(log_message, "Multibeam File <%s> closed", ifile);
      }

      mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
      if (mbtrn_cfg->verbose != 0) {
        fprintf(stderr, "\n%s\n", log_message);
      }

      sprintf(log_message, "MBIO format id: %d", mbtrn_cfg->format);
      if (logfp != NULL) {
        mbtrnpp_postlog(mbtrn_cfg->verbose, logfp, log_message, &error);
      }

      if (mbtrn_cfg->verbose > 0) {
        fprintf(stderr, "%s\n", log_message);
      }

      fflush(logfp);

      /* give the statistics */
      /* figure out whether and what to read next */
      if (read_datalist == true) {
        if ((status = mb_datalist_read(mbtrn_cfg->verbose, datalist, ifile, dfile, &mbtrn_cfg->format, &file_weight, &error)) == MB_SUCCESS) {
          PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "read_datalist status[%d] - continuing\n", status));
          read_data = true;
        }
        else {
          PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "read_datalist status[%d] - done\n", status));
          read_data = false;
        }
      }
      else {
        PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "read_datalist == NO\n"));
        read_data = false;
      }
    }
    /* end loop over files in list */
  }

  fprintf(stderr, "exit loop\n");
  if (read_datalist == true)
    mb_datalist_close(mbtrn_cfg->verbose, &datalist, &error);

  /* close log file */
  gettimeofday(&timeofday, &timezone);
  now_time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
  // fprintf(stderr,"CHECKING AT BOTTOM OF LOOP: logfp:%p log_file_open_time_d:%.6f now_time_d:%.6f\n", logfp,
  // log_file_open_time_d, now_time_d);
  if (logfp != NULL) {
    status = mbtrnpp_logstatistics(mbtrn_cfg->verbose, logfp, n_pings_read, n_soundings_read, n_soundings_valid_read,
                                   n_soundings_flagged_read, n_soundings_null_read, n_soundings_trimmed,
                                   n_soundings_decimated, n_soundings_flagged, n_soundings_written, &error);
    n_tot_pings_read += n_pings_read;
    n_tot_soundings_read += n_soundings_read;
    n_tot_soundings_valid_read += n_soundings_valid_read;
    n_tot_soundings_flagged_read += n_soundings_flagged_read;
    n_tot_soundings_null_read += n_soundings_null_read;
    n_tot_soundings_trimmed += n_soundings_trimmed;
    n_tot_soundings_decimated += n_soundings_decimated;
    n_tot_soundings_flagged += n_soundings_flagged;
    n_tot_soundings_written += n_soundings_written;
    n_pings_read = 0;
    n_soundings_read = 0;
    n_soundings_valid_read = 0;
    n_soundings_flagged_read = 0;
    n_soundings_null_read = 0;
    n_soundings_trimmed = 0;
    n_soundings_decimated = 0;
    n_soundings_flagged = 0;
    n_soundings_written = 0;

    status = mbtrnpp_closelog(mbtrn_cfg->verbose, &logfp, &error);
  }

  /* close output */
  if ( OUTPUT_FLAG_SET(OUTPUT_MB1_FILE_EN) ) {
    fclose(output_fp);
  }

  /* check memory */
  if (mbtrn_cfg->verbose >= 4)
    status = mb_memory_list(mbtrn_cfg->verbose, &error);

  /* give the statistics */
  if (mbtrn_cfg->verbose >= 1) {
  }

  fprintf(stderr, "exit app [%d]\n", error);

  /* end it all */
  exit(error);
}
/*--------------------------------------------------------------------*/

int mbtrnpp_openlog(int verbose, mb_path log_directory, FILE **logfp, int *error) {

  /* local variables */
  int status = MB_SUCCESS;

  /* time, user, host variables */
  struct timeval timeofday;
  struct timezone timezone;
  double time_d;
  int time_i[7];
  char date[32], user[128], *user_ptr;
  mb_path host;
  mb_path log_file;
  mb_path log_message;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:            %d\n", verbose);
    fprintf(stderr, "dbg2       log_directory:      %s\n", log_directory);
    fprintf(stderr, "dbg2       logfp:              %p\n", logfp);
    fprintf(stderr, "dbg2       *logfp:             %p\n", *logfp);
  }

  /* close existing log file */
  if (*logfp != NULL) {
    mbtrnpp_closelog(verbose, logfp, error);
  }

  /* get time and user data */
  gettimeofday(&timeofday, &timezone);
  time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
  status = mb_get_date(verbose, time_d, time_i);
  sprintf(date, "%4.4d%2.2d%2.2d_%2.2d%2.2d%2.2d%6.6d", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5],
          time_i[6]);
  if ((user_ptr = getenv("USER")) == NULL)
    user_ptr = getenv("LOGNAME");
  if (user_ptr != NULL)
    strcpy(user, user_ptr);
  else
    strcpy(user, "unknown");
  gethostname(host, sizeof(mb_path));

  /* open new log file */
  sprintf(log_file, "%s/%s_mbtrnpp_log.txt", log_directory, date);
  *logfp = fopen(log_file, "w");
  if (*logfp != NULL) {
    fprintf(*logfp, "Program %s log file\n-------------------\n", program_name);
    if (verbose > 0) {
      fprintf(stderr, "Program %s log file\n-------------------\n", program_name);
    }
    sprintf(log_message, "Opened by user %s on cpu %s", user, host);
    mbtrnpp_postlog(verbose, *logfp, log_message, error);
  }
  else {
    *error = MB_ERROR_OPEN_FAIL;
    fprintf(stderr, "\nUnable to open %s log file: %s\n", program_name, log_file);
    fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
    exit(*error);
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       logfp:              %p\n", logfp);
    fprintf(stderr, "dbg2       *logfp:             %p\n", *logfp);
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_closelog(int verbose, FILE **logfp, int *error) {

  /* local variables */
  int status = MB_SUCCESS;
  char *log_message = "Closing mbtrnpp log file";

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:            %d\n", verbose);
    fprintf(stderr, "dbg2       logfp:              %p\n", logfp);
    fprintf(stderr, "dbg2       *logfp:             %p\n", *logfp);
  }

  /* close log file */
  if (logfp != NULL) {
    mbtrnpp_postlog(verbose, *logfp, log_message, error);
    fclose(*logfp);
    *logfp = NULL;
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       logfp:              %p\n", logfp);
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_postlog(int verbose, FILE *logfp, char *log_message, int *error) {

  /* local variables */
  int status = MB_SUCCESS;

  /* time, user, host variables */
  struct timeval timeofday;
  struct timezone timezone;
  double time_d;
  int time_i[7];
  char date[32];

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:            %d\n", verbose);
    fprintf(stderr, "dbg2       logfp:              %p\n", logfp);
    fprintf(stderr, "dbg2       log_message:        %s\n", log_message);
  }

  /* get time  */
  gettimeofday(&timeofday, &timezone);
  time_d = timeofday.tv_sec + 0.000001 * timeofday.tv_usec;
  status = mb_get_date(verbose, time_d, time_i);
  sprintf(date, "%4.4d%2.2d%2.2d_%2.2d%2.2d%2.2d%6.6d", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4], time_i[5],
          time_i[6]);

  /* post log_message */
  if (logfp != NULL) {
    fprintf(logfp, "<%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d>: %s\n", time_i[0], time_i[1], time_i[2], time_i[3], time_i[4],
            time_i[5], time_i[6], log_message);
  }
  if (verbose > 0) {
    fprintf(stderr, "<%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d.%6.6d>: %s\n", time_i[0], time_i[1], time_i[2], time_i[3],
            time_i[4], time_i[5], time_i[6], log_message);
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/
int mbtrnpp_logparameters(int verbose, FILE *logfp, char *input, int format, char *output, double swath_width,
                          int n_output_soundings, bool median_filter_en, int median_filter_n_across, int median_filter_n_along,
                          double median_filter_threshold, int n_buffer_max, int *error) {
  /* local variables */
  int status = MB_SUCCESS;
  mb_path log_message;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:                      %d\n", verbose);
    fprintf(stderr, "dbg2       logfp:                        %p\n", logfp);
    fprintf(stderr, "dbg2       input:                        %s\n", input);
    fprintf(stderr, "dbg2       format:                       %d\n", format);
    fprintf(stderr, "dbg2       output:                       %s\n", output);
    fprintf(stderr, "dbg2       swath_width:                  %f\n", swath_width);
    fprintf(stderr, "dbg2       n_output_soundings:           %d\n", n_output_soundings);
    fprintf(stderr, "dbg2       median_filter_en:             %d\n", (median_filter_en?1:0));
    fprintf(stderr, "dbg2       median_filter_n_across:       %d\n", median_filter_n_across);
    fprintf(stderr, "dbg2       median_filter_n_along:        %d\n", median_filter_n_along);
    fprintf(stderr, "dbg2       median_filter_threshold:      %f\n", median_filter_threshold);
    fprintf(stderr, "dbg2       n_buffer_max:                 %d\n", n_buffer_max);
  }

  /* post log_message */
  if (logfp != NULL) {
    sprintf(log_message, "       input:                    %s", input);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       format:                   %d", format);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       output:                   %s", output);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       swath_width:              %f", swath_width);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_output_soundings:       %d", n_output_soundings);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       median_filter_en:         %d", (median_filter_en?1:0));
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       median_filter_n_across:   %d", median_filter_n_across);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       median_filter_n_along:    %d", median_filter_n_along);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       median_filter_threshold:  %f", median_filter_threshold);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_buffer_max:             %d", n_buffer_max);
    mbtrnpp_postlog(verbose, logfp, log_message, error);
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/
int mbtrnpp_logstatistics(int verbose, FILE *logfp, int n_pings_read, int n_soundings_read, int n_soundings_valid_read,
                          int n_soundings_flagged_read, int n_soundings_null_read, int n_soundings_trimmed,
                          int n_soundings_decimated, int n_soundings_flagged, int n_soundings_written, int *error) {
  /* local variables */
  int status = MB_SUCCESS;
  mb_path log_message;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:                      %d\n", verbose);
    fprintf(stderr, "dbg2       logfp:                        %p\n", logfp);
    fprintf(stderr, "dbg2       n_pings_read:                 %d\n", n_pings_read);
    fprintf(stderr, "dbg2       n_soundings_read:             %d\n", n_soundings_read);
    fprintf(stderr, "dbg2       n_soundings_valid_read:       %d\n", n_soundings_valid_read);
    fprintf(stderr, "dbg2       n_soundings_flagged_read:     %d\n", n_soundings_flagged_read);
    fprintf(stderr, "dbg2       n_soundings_null_read:        %d\n", n_soundings_null_read);
    fprintf(stderr, "dbg2       n_soundings_trimmed:          %d\n", n_pings_read);
    fprintf(stderr, "dbg2       n_soundings_decimated:        %d\n", n_soundings_decimated);
    fprintf(stderr, "dbg2       n_soundings_flagged:          %d\n", n_soundings_flagged);
    fprintf(stderr, "dbg2       n_soundings_written:          %d\n", n_soundings_written);
  }

  /* post log_message */
  if (logfp != NULL) {
    sprintf(log_message, "Log File Statistics:");
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_pings_read:                 %d", n_pings_read);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_read:             %d", n_soundings_read);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_valid_read:       %d", n_soundings_valid_read);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_flagged_read:     %d", n_soundings_flagged_read);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_null_read:        %d", n_soundings_null_read);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_trimmed:          %d", n_pings_read);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_decimated:        %d", n_soundings_decimated);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_flagged:          %d", n_soundings_flagged);
    mbtrnpp_postlog(verbose, logfp, log_message, error);

    sprintf(log_message, "       n_soundings_written:          %d", n_soundings_written);
    mbtrnpp_postlog(verbose, logfp, log_message, error);
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_update_stats(mstats_profile_t *stats, mlog_id_t log_id, mstats_flags flags) {

  if (NULL != stats) {
    double stats_now = mtime_etime();

    if (log_clock_res) {
      // log the timing clock resolution (once)
      struct timespec res;
      clock_getres(CLOCK_MONOTONIC, &res);
      mlog_tprintf(mbtrnpp_mlog_id, "%.3lf,i,clkres_mono,s[%ld] ns[%ld]\n", stats_now, res.tv_sec, res.tv_nsec);
      log_clock_res = false;
    }

    // we can only measure the previous stats cycle...
    if (stats->stats->per_stats[MBTPP_CH_MB_CYCLE_XT].n > 0) {
      // get the timing of the last cycle
      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_STATS_XT], stats_prev_start);
      MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_STATS_XT], stats_prev_end);
    }
    else {
      // seed the first cycle
      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_STATS_XT], (stats_now - 0.0001));
      MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_STATS_XT], stats_now);
    }

    // end the cycle timer here
    // [start at the end if this function]
    MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_CYCLE_XT], stats_now);

    // measure dtime execution time (twice), while we're at it
    MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_DTIME_XT], mtime_dtime());
    MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_DTIME_XT], mtime_dtime());
    MST_METRIC_DIV(app_stats->stats->metrics[MBTPP_CH_MB_DTIME_XT], 2.0);

    // update uptime
    stats->uptime = stats_now - stats->session_start;

    PMPRINT(MOD_MBTRNPP, MBTRNPP_V4,
            (stderr, "cycle_xt: stat_now[%.4lf] start[%.4lf] stop[%.4lf] value[%.4lf]\n", stats_now,
             app_stats->stats->metrics[MBTPP_CH_MB_CYCLE_XT].start, app_stats->stats->metrics[MBTPP_CH_MB_CYCLE_XT].stop,
             app_stats->stats->metrics[MBTPP_CH_MB_CYCLE_XT].value));

    // update stats
    mstats_update_stats(stats->stats, MBTPP_CH_COUNT, flags);
    mstats_t *mb1svr_stats = netif_stats(mb1svr);
    mstats_update_stats(mb1svr_stats, NETIF_CH_COUNT, flags);
    mstats_t *trnsvr_stats = netif_stats(trnsvr);
    mstats_update_stats(trnsvr_stats, NETIF_CH_COUNT, flags);
    mstats_t *trnusvr_stats = netif_stats(trnusvr);
    mstats_update_stats(trnusvr_stats, NETIF_CH_COUNT, flags);

    PMPRINT(MOD_MBTRNPP,  MBTRNPP_V4,
            (stderr, "cycle_xt.p: N[%lld] sum[%.3lf] min[%.3lf] max[%.3lf] avg[%.3lf]\n",
             app_stats->stats->per_stats[MBTPP_CH_MB_CYCLE_XT].n, app_stats->stats->per_stats[MBTPP_CH_MB_CYCLE_XT].sum,
             app_stats->stats->per_stats[MBTPP_CH_MB_CYCLE_XT].min, app_stats->stats->per_stats[MBTPP_CH_MB_CYCLE_XT].max,
             app_stats->stats->per_stats[MBTPP_CH_MB_CYCLE_XT].avg));

    PMPRINT(MOD_MBTRNPP, MBTRNPP_V4,
            (stderr, "cycle_xt.a: N[%lld] sum[%.3lf] min[%.3lf] max[%.3lf] avg[%.3lf]\n",
             app_stats->stats->agg_stats[MBTPP_CH_MB_CYCLE_XT].n, app_stats->stats->agg_stats[MBTPP_CH_MB_CYCLE_XT].sum,
             app_stats->stats->agg_stats[MBTPP_CH_MB_CYCLE_XT].min, app_stats->stats->agg_stats[MBTPP_CH_MB_CYCLE_XT].max,
             app_stats->stats->agg_stats[MBTPP_CH_MB_CYCLE_XT].avg));

    if (flags & MSF_READER) {
      mstats_update_stats(reader_stats, R7KR_MET_COUNT, flags);
    }

    //        fprintf(stderr,"stat period sec[%.3lf] start[%.3lf] now[%.3lf] elapsed[%.3lf]\n",
    //                stats->stats->stat_period_sec,
    //                stats->stats->stat_period_start,
    //                stats_now,
    //                (stats_now - stats->stats->stat_period_start)
    //                );
    // check stats periods, process if ready
    if ((stats->stats->stat_period_sec > 0.0) &&
        ((stats_now - stats->stats->stat_period_start) > stats->stats->stat_period_sec)) {

      // start log execution timer
      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_LOG_XT], mtime_dtime());

      mlog_tprintf(mbtrnpp_mlog_id, "%.3lf,i,uptime,%0.3lf\n", stats_now, stats->uptime);
      mstats_log_stats(stats->stats, stats_now, log_id, flags);
      mstats_log_stats(mb1svr_stats, stats_now, netif_log(mb1svr), flags);
      mstats_log_stats(trnsvr_stats, stats_now, netif_log(trnsvr), flags);
      mstats_log_stats(trnusvr_stats, stats_now, netif_log(trnusvr), flags);

      if (flags & MSF_READER) {
        mstats_log_stats(reader_stats, stats_now, log_id, flags);
      }

      // reset period stats
      mstats_reset_pstats(stats->stats, MBTPP_CH_COUNT);
      mstats_reset_pstats(reader_stats, R7KR_MET_COUNT);
      mstats_reset_pstats(mb1svr_stats, NETIF_CH_COUNT);
      mstats_reset_pstats(trnsvr_stats, NETIF_CH_COUNT);
      mstats_reset_pstats(trnusvr_stats, NETIF_CH_COUNT);

      // reset period timer
      stats->stats->stat_period_start = stats_now;

      // stop log execution timer
      MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_LOG_XT], mtime_dtime());
    }

    // start cycle timer
    MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_CYCLE_XT], mtime_dtime());

    // update stats execution time variables
    stats_prev_start = stats_now;
    stats_prev_end = mtime_dtime();
  }
  else {
    fprintf(stderr, "mbtrnpp_update_stats: invalid argument\n");
  }
  return 0;
}

/*--------------------------------------------------------------------*/

int mbtrnpp_init_debug(int verbose) {

  /* Open and initialize the socket based input for reading using function
   * mbtrnpp_input_read(). Allocate an internal, hidden buffer to hold data from
   * full s7k records while waiting to return bytes from those records as
   * requested by the MBIO read functions.
   * Store the relevant pointers and parameters within the
   * mb_io_struct structure *mb_io_ptr. */

  mmd_initialize();
  mconf_init(NULL, NULL);

  fprintf(stderr, "%s:%d >>> MOD_MBTRNPP[id=%d]  en[%08X] verbose[%d]\n", __FUNCTION__, __LINE__, MOD_MBTRNPP,
          mmd_get_enmask(MOD_MBTRNPP, NULL),verbose);

  switch (verbose) {
  case 0:
    mmd_channel_set(MOD_MBTRNPP, MM_NONE);
    mmd_channel_set(MOD_R7K, MM_NONE);
    mmd_channel_set(MOD_R7KR, MM_NONE);
    mmd_channel_set(MOD_MSOCK, MM_NONE);
    mmd_channel_set(MOD_NETIF, MM_NONE);
    break;
  case 1:
    mmd_channel_en(MOD_MBTRNPP, MBTRNPP_V1);
    mmd_channel_en(MOD_R7KR, R7KR_V1);
    break;
  case 2:
    mmd_channel_en(MOD_MBTRNPP, MM_DEBUG);
    mmd_channel_en(MOD_R7KR, MM_DEBUG);
    mmd_channel_en(MOD_R7K, R7K_PARSER);
    break;
  case -1:
    mmd_channel_en(MOD_MBTRNPP, MBTRNPP_V1);
    mmd_channel_en(MOD_R7KR, MM_DEBUG);
    mmd_channel_set(MOD_NETIF, NETIF_V1 | NETIF_V2);
    break;
  case -2:
    mmd_channel_en(MOD_MBTRNPP, MBTRNPP_V1 | MBTRNPP_V2);
    mmd_channel_set(MOD_NETIF, NETIF_V1 | NETIF_V2 | NETIF_V3 );
    break;
  case -3:
    mmd_channel_en(MOD_MBTRNPP, MM_DEBUG | MBTRNPP_V1 | MBTRNPP_V2 | MBTRNPP_V3 );
    mmd_channel_en(MOD_R7KR, MM_DEBUG);
    mmd_channel_en(MOD_R7K, MM_WARN | R7K_PARSER);
    mmd_channel_set(MOD_NETIF, NETIF_V1 | NETIF_V2 | NETIF_V3 | NETIF_V4);
    // this enables messages from msock_recv (e.g. resource temporarily unavailable)
    msock_set_debug(1);
    break;
  case -4:
    mmd_channel_en(MOD_MBTRNPP, MM_DEBUG | MBTRNPP_V1 | MBTRNPP_V2 | MBTRNPP_V3 | MBTRNPP_V4);
    mmd_channel_en(MOD_R7KR, MM_DEBUG);
    mmd_channel_en(MOD_R7K, MM_WARN | R7K_PARSER | R7K_DRFCON);
    mmd_channel_en(MOD_MSOCK, MM_DEBUG);
    mmd_channel_set(MOD_NETIF, MM_DEBUG | NETIF_V1 | NETIF_V2 | NETIF_V3 | NETIF_V4);
    msock_set_debug(1);
    break;
  case -5:
    mmd_channel_en(MOD_MBTRNPP, MM_ALL);
    mmd_channel_en(MOD_R7KR, MM_ALL);
    mmd_channel_en(MOD_R7K, MM_ALL);
    mmd_channel_en(MOD_MSOCK, MM_ALL);
    mmd_channel_en(MOD_NETIF, MM_ALL);
    msock_set_debug(1);
    break;
  default:
    break;
  }
  fprintf(stderr, "%s:%d >>> MOD_MBTRNPP  en[%08X]\n", __FUNCTION__, __LINE__, mmd_get_enmask(MOD_MBTRNPP, NULL));

  // open trn data log
  if ( OUTPUT_FLAG_SET(OUTPUT_MB1_BIN) ) {
    mb1_blog_path = (char *)malloc(512);
    sprintf(mb1_blog_path, "%s//%s-%s%s", mbtrn_cfg->trn_log_dir, MB1_BLOG_NAME, s_mbtrnpp_session_str(NULL,0,RF_NONE), MBTRNPP_LOG_EXT);
    mb1_blog_id = mlog_get_instance(mb1_blog_path, &mb1_blog_conf, MB1_BLOG_NAME);
    mlog_show(mb1_blog_id, true, 5);
    mlog_open(mb1_blog_id, flags, mode);
  }
  // open trn message log
  if (OUTPUT_FLAG_SET(OUTPUT_MBTRNPP_MSG) ) {
    mbtrnpp_mlog_path = (char *)malloc(512);
    sprintf(mbtrnpp_mlog_path, "%s//%s-%s%s", mbtrn_cfg->trn_log_dir, MBTRNPP_MLOG_NAME, s_mbtrnpp_session_str(NULL,0,RF_NONE), MBTRNPP_LOG_EXT);
    mbtrnpp_mlog_id = mlog_get_instance(mbtrnpp_mlog_path, &mbtrnpp_mlog_conf, MBTRNPP_MLOG_NAME);
    mlog_show(mbtrnpp_mlog_id, true, 5);
    mlog_open(mbtrnpp_mlog_id, flags, mode);
    mlog_tprintf(mbtrnpp_mlog_id, "*** mbtrn session start ***\n");
    mlog_tprintf(mbtrnpp_mlog_id, "cmdline [%s]\n", s_mbtrnpp_cmdline_str(NULL, 0, 0, NULL, RF_NONE));
    mlog_tprintf(mbtrnpp_mlog_id, "r7kr v[%s] build[%s]\n", R7KR_VERSION_STR, LIBMFRAME_BUILD);


      trn_ulog_path = (char *)malloc(512);
      sprintf(trn_ulog_path, "%s//%s-%s%s", mbtrn_cfg->trn_log_dir, TRN_ULOG_NAME, s_mbtrnpp_session_str(NULL,0,RF_NONE), MBTRNPP_LOG_EXT);
      trn_ulog_id = mlog_get_instance(trn_ulog_path, &trn_ulog_conf, TRN_ULOG_NAME);
      mlog_show(trn_ulog_id, true, 5);
      mlog_open(trn_ulog_id, flags, mode);
      mlog_tprintf(trn_ulog_id, "*** trn update session start ***\n");
      mlog_tprintf(trn_ulog_id, "cmdline [%s]\n", s_mbtrnpp_cmdline_str(NULL, 0, 0, NULL, RF_NONE));
      mlog_tprintf(trn_ulog_id, "r7kr v[%s] build[%s]\n", R7KR_VERSION_STR, LIBMFRAME_BUILD);

  }
  else {
    fprintf(stderr, "*** mbtrn session start ***\n");
    fprintf(stderr, "cmdline [%s]\n", s_mbtrnpp_cmdline_str(NULL, 0, 0, NULL, RF_NONE));
  }

  app_stats = mstats_profile_new(MBTPP_EV_COUNT, MBTPP_STA_COUNT, MBTPP_CH_COUNT, mbtrnpp_stats_labels, mtime_dtime(),
                                 mbtrn_cfg->trn_status_interval_sec);

  return 0;
}
/*--------------------------------------------------------------------*/


#ifdef WITH_MBTNAV

char *mbtrnpp_trn_updatestr(char *dest, int len, trn_update_t *update, int indent)

{
    if(NULL!=dest && NULL!=update){
        char *cp=dest;
        snprintf(dest,len-1,"%*sMLE: %.2lf,%.4lf,%.4lf,%.4lf\n%*sMSE: %.2lf,%.4lf,%.4lf,%.4lf\n%*sCOV: %.2lf,%.2lf,%.2lf\n%*s RI: %d filter_state: %d success: %d cycle: %d ping: %d mb1_time: %0.3lf update_time: %0.3lf isconv:%hd isvalid:%hd\n",
                 indent,"",
                 update->mle_dat->time,
                 (update->mle_dat->x-update->pt_dat->x),
                 (update->mle_dat->y-update->pt_dat->y),
                 (update->mle_dat->z-update->pt_dat->z),
                 indent,"",
                 update->mse_dat->time,
                 (update->mse_dat->x-update->pt_dat->x),
                 (update->mse_dat->y-update->pt_dat->y),
                 (update->mse_dat->z-update->pt_dat->z),
                 indent,"",
                 sqrt(update->mse_dat->covariance[0]),
                 sqrt(update->mse_dat->covariance[2]),
                 sqrt(update->mse_dat->covariance[5]),
                 indent,"",
                 update->reinit_count,
                 update->filter_state,
                 update->success,
                 update->mb1_cycle,
                 update->ping_number,
                 update->mb1_time,
                 update->update_time,
                 update->is_converged,
                 update->is_valid
                 );
    }
    return dest;
}
/*--------------------------------------------------------------------*/

int mbtrnpp_trn_pub_ostream(trn_update_t *update,
                             FILE *stream)
{
    int retval=-1;


    if(NULL!=update->mse_dat && NULL!=update->pt_dat && NULL!=update->mle_dat){
        char str[256]={0};
        fprintf(stream,"\nTRN Update:\n%s", mbtrnpp_trn_updatestr(str,256,update,0));
        retval=0;
    }


    return retval;
}

int mbtrnpp_trn_pub_odebug(trn_update_t *update)
{
    int retval=-1;


    if(NULL!=update->mse_dat && NULL!=update->pt_dat && NULL!=update->mle_dat){
        char str[256]={0};


        PMPRINT(MOD_MBTRNPP,MM_DEBUG|MBTRNPP_V1,(stderr,"\nTRN Update:\n%s", mbtrnpp_trn_updatestr(str,256,update,0)));
        retval=0;
    }


    return retval;
}

int mbtrnpp_trn_pub_olog(trn_update_t *update,
                          mlog_id_t log_id)
{
    int retval=-1;
    if(NULL!=update){
        if(NULL!=update->pt_dat)
            retval=0;
        mlog_tprintf(log_id,"trn_pt_dat,%lf,%.4lf,%.4lf,%.4lf\n",
                     update->pt_dat->time,
                     update->pt_dat->x,
                     update->pt_dat->y,
                     update->pt_dat->z);

        if(NULL!=update->mle_dat)
            mlog_tprintf(log_id,"trn_mle_dat,%lf,%.4lf,%.4lf,%.4lf\n",
                         update->mle_dat->time,
                         update->mle_dat->x,
                         update->mle_dat->y,
                         update->mle_dat->z);

        if(NULL!=update->mse_dat)
            mlog_tprintf(log_id,"trn_mse_dat,%lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf\n",
                         update->mse_dat->time,
                         update->mse_dat->x,
                         update->mse_dat->y,
                         update->mse_dat->z,
                         update->mse_dat->covariance[0],
                         update->mse_dat->covariance[2],
                         update->mse_dat->covariance[5],
                         update->mse_dat->covariance[1]);

        if(NULL!=update->mse_dat && NULL!=update->pt_dat && NULL!=update->mle_dat)
            mlog_tprintf(log_id,"trn_est,%lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.4lf,%.2lf,%.2lf,%.2lf\n",
                         update->mse_dat->time,
                         (update->mle_dat->x-update->pt_dat->x),
                         (update->mle_dat->y-update->pt_dat->y),
                         (update->mle_dat->z-update->pt_dat->z),
                         (update->mse_dat->x-update->pt_dat->x),
                         (update->mse_dat->y-update->pt_dat->y),
                         (update->mse_dat->z-update->pt_dat->z),
                         sqrt(update->mse_dat->covariance[0]),
                         sqrt(update->mse_dat->covariance[2]),
                         sqrt(update->mse_dat->covariance[5]));
        mlog_tprintf(log_id,"trn_state,reinit_flag,%d,fstate,%d,success,%d,cycle,%d,ping,%d,mb1_time,%0.3lf,update_time,%0.3lf,isconv,%hd,isval,%hd\n",update->reinit_count,update->filter_state,update->success,update->mb1_cycle,update->ping_number,update->mb1_time,update->update_time,update->is_converged,update->is_valid);
    }

    return retval;
}

/*--------------------------------------------------------------------*/



int mbtrnpp_trn_pub_osocket(trn_update_t *update,
                             msock_socket_t *pub_sock)
{
    int retval=-1;

    if(NULL!=update && NULL!=pub_sock){
        retval=0;
        int iobytes=0;

        if(NULL!=update && NULL!=pub_sock){
            // serialize data
            trn_offset_pub_t pub_data={
                TRNW_PUB_SYNC,
                {
                    {update->pt_dat->time,update->pt_dat->x,update->pt_dat->y,update->pt_dat->z,
                        {update->pt_dat->covariance[0],update->pt_dat->covariance[2],update->pt_dat->covariance[5],update->pt_dat->covariance[1]}
                    },
                    {update->mle_dat->time,update->mle_dat->x,update->mle_dat->y,update->mle_dat->z,
                        {update->mle_dat->covariance[0],update->mle_dat->covariance[2],update->mle_dat->covariance[5],update->mle_dat->covariance[1]}
                    },
                    {update->mse_dat->time,update->mse_dat->x,update->mse_dat->y,update->mse_dat->z,
                        {update->mse_dat->covariance[0],update->mse_dat->covariance[2],update->mse_dat->covariance[5],update->mse_dat->covariance[1]}
                    }
                },
                update->reinit_count,
                update->reinit_tlast,
                update->filter_state,
                update->success,
                update->is_converged,
                update->is_valid,
                update->mb1_cycle,
                update->ping_number,
                update->mb1_time,
                update->update_time
            };

            if( (iobytes=netif_pub(trnusvr,(char *)&pub_data, sizeof(pub_data)))>0){
                retval=iobytes;
            }
        }
    }
    return retval;
}

int mbtrnpp_init_trn(wtnav_t **pdest, int verbose, trn_config_t *cfg)
{
    int retval = -1;

    if (NULL != cfg && NULL!=pdest) {
        wtnav_t *instance = wtnav_new(cfg);
        if (NULL!=instance) {
            if (wtnav_initialized(instance)) {
                *pdest = instance;
                retval = 0;
                fprintf(stderr, "%s : TRN intialize - OK\n",__FUNCTION__);
            }
            else {
                fprintf(stderr, "%s : ERR - TRN wtnav intialization failed\n",__FUNCTION__);
                wtnav_destroy(instance);
            }
        }
        else {
            fprintf(stderr, "%s : ERR - TRN new failed\n",__FUNCTION__);
        }
    }
    else {
        fprintf(stderr, "%s : ERR - TRN config NULL\n",__FUNCTION__);
    }

    return retval;
}

int mbtrnpp_init_trnsvr(netif_t **psvr, wtnav_t *trn, char *host, int port, bool verbose)
{
    int retval = -1;

    PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"configuring trn server socket using %s:%d\n",host,port));
    if(NULL!=psvr && NULL!=host){
        netif_t *svr  = netif_new("trnsvr",host,
                          port,
                          ST_TCP,
                          IFM_REQRES,
                          mbtrn_cfg->trnsvr_hbto,
                          trnif_msg_read_ct,
                          trnif_msg_handle_ct,
                          NULL);

        if(NULL!=svr){
            *psvr = svr;
            netif_set_reqres_res(svr,trn);
            netif_show(svr,true,5);
            netif_init_log(svr, "trnsvr", (NULL!=mbtrn_cfg->trn_log_dir?mbtrn_cfg->trn_log_dir:"."), s_mbtrnpp_session_str(NULL,0,RF_NONE));
            mlog_tprintf(svr->mlog_id,"*** trnsvr session start (TEST) ***\n");
            mlog_tprintf(svr->mlog_id,"libnetif v[%s] build[%s]\n",netif_get_version(),netif_get_build());
            retval = netif_connect(svr);
        }else{
            fprintf(stderr,"%s:%d - ERR allocation\n",__FUNCTION__,__LINE__);
        }
    }else{
        fprintf(stderr,"%s:%d - ERR invalid args\n",__FUNCTION__,__LINE__);
    }
    return retval;
}
/*--------------------------------------------------------------------*/
int mbtrnpp_init_mb1svr(netif_t **psvr, char *host, int port, bool verbose)
{
    int retval = -1;
    PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"configuring MB1 server socket using %s:%d\n",host,port));
    fprintf(stderr,"configuring MB1 server socket using %s:%d hbto[%lf]\n",host,port,mbtrn_cfg->mbsvr_hbto);
   if(NULL!=psvr && NULL!=host){
        netif_t *svr = netif_new("mb1svr",host,
                          port,
                          ST_UDP,
                          IFM_REQRES,
                          mbtrn_cfg->mbsvr_hbto,
                          trnif_msg_read_mb,
                          trnif_msg_handle_mb,
                          trnif_msg_pub_mb);

        if(NULL!=svr){
            *psvr = svr;
//            netif_set_reqres_res(svr,trn);
            netif_show(svr,true,5);
            netif_init_log(svr, "mb1svr", (NULL!=mbtrn_cfg->trn_log_dir?mbtrn_cfg->trn_log_dir:"."), s_mbtrnpp_session_str(NULL,0,RF_NONE));
            mlog_tprintf(svr->mlog_id,"*** mb1svr session start (TEST) ***\n");
            mlog_tprintf(svr->mlog_id,"libnetif v[%s] build[%s]\n",netif_get_version(),netif_get_build());
            retval = netif_connect(svr);
        }else{
            fprintf(stderr,"%s:%d - ERR allocation\n",__FUNCTION__,__LINE__);
        }
   }else{
       fprintf(stderr,"%s:%d - ERR invalid args\n",__FUNCTION__,__LINE__);
   }
    return retval;
}
/*--------------------------------------------------------------------*/
int mbtrnpp_init_trnusvr(netif_t **psvr, char *host, int port, bool verbose)
{
    int retval = -1;
    PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"configuring trnu (update) server socket using %s:%d\n",host,port));
    if(NULL!=psvr && NULL!=host){
        netif_t *svr = netif_new("trnusvr",host,
                                 port,
                                 ST_UDP,
                                 IFM_REQRES,
                                 mbtrn_cfg->trnusvr_hbto,
                                 trnif_msg_read_trnu,
                                 trnif_msg_handle_trnu,
                                 trnif_msg_pub_trnu);


        if(NULL!=svr){
            *psvr = svr;
            //            netif_set_reqres_res(svr,trn);
            netif_show(svr,true,5);
            netif_init_log(svr, "trnusvr", (NULL!=mbtrn_cfg->trn_log_dir?mbtrn_cfg->trn_log_dir:"."), s_mbtrnpp_session_str(NULL,0,RF_NONE));
            mlog_tprintf(svr->mlog_id,"*** trnusvr session start (TEST) ***\n");
            mlog_tprintf(svr->mlog_id,"libnetif v[%s] build[%s]\n",netif_get_version(),netif_get_build());
            retval = netif_connect(svr);
        }else{
            fprintf(stderr,"%s:%d - ERR allocation\n",__FUNCTION__,__LINE__);
        }
    }else{
        fprintf(stderr,"%s:%d - ERR invalid args\n",__FUNCTION__,__LINE__);
    }
    return retval;
}

/*--------------------------------------------------------------------*/
int mbtrnpp_trn_get_bias_estimates(wtnav_t *self, wposet_t *pt, trn_update_t *pstate) {
    int retval = -1;
    uint32_t uret = 0;
    bool meas_valid = false;
    wposet_t *mle = wposet_dnew();
    wposet_t *mse = wposet_dnew();

    if ( (NULL != self) && (NULL != pt) && (NULL != pstate)) {

        wtnav_estimate_pose(self, mle, 1);
        wtnav_estimate_pose(self, mse, 2);

        //        fprintf(stderr,"%s:%d MLE,MSE\n",__FUNCTION__,__LINE__);
        //        wposet_show(mle,true,5);
        //        fprintf(stderr,"\n");
        //        wposet_show(mse,true,5);

        if (wtnav_last_meas_successful(self)) {
            wposet_pose_to_cdata(&pstate->pt_dat, pt);
            wposet_pose_to_cdata(&pstate->mle_dat, mle);
            wposet_pose_to_cdata(&pstate->mse_dat, mse);
            pstate->success=1;
            retval = 0;
        }
        else {
            PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "Last Meas Invalid\n"));
            mlog_tprintf(trn_ulog_id,"ERR: last meas invalid\n");
        }
        wposet_destroy(mle);
        wposet_destroy(mse);
    }

    return retval;
}

/*--------------------------------------------------------------------*/
int mbtrnpp_trn_publish(trn_update_t *pstate, trn_config_t *cfg)
{
    int retval = -1;


    if(NULL!=pstate && NULL!=cfg){
        // publish to selected outputs
        if( OUTPUT_FLAG_SET(OUTPUT_TRNU_SVR_EN) ){

            MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_TRNU_PUB_XT], mtime_dtime());

            mbtrnpp_trn_pub_osocket(pstate, trnusvr->socket);

            MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_TRNU_PUB_XT], mtime_dtime());
            MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_TRNU_PUBN]);
        }
        if( OUTPUT_FLAG_SET(OUTPUT_TRNU_ASC) ){
            MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_TRNU_LOG_XT], mtime_dtime());

            mbtrnpp_trn_pub_olog(pstate, trn_ulog_id);

            MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_TRNU_LOG_XT], mtime_dtime());
        }
        if( OUTPUT_FLAG_SET(OUTPUT_TRNU_DEBUG) ){
            mbtrnpp_trn_pub_odebug(pstate);
        }
        if( OUTPUT_FLAG_SET(OUTPUT_TRNU_SOUT) ){
            mbtrnpp_trn_pub_ostream(pstate, stdout);
        }
        if( OUTPUT_FLAG_SET(OUTPUT_TRNU_SERR)){
            mbtrnpp_trn_pub_ostream(pstate, stderr);
        }
        retval=0;
    }

    return retval;
}

/*--------------------------------------------------------------------*/

int mbtrnpp_trn_update(wtnav_t *self, mb1_t *src, wposet_t **pt_out, wmeast_t **mt_out, trn_config_t *cfg) {
  int retval = -1;
  int test = -1;
  uint32_t uret = 0;

  if (NULL != self && NULL != src && NULL != pt_out && NULL != mt_out) {

    if ((test = wmeast_mb1_to_meas(mt_out, src, cfg->utm_zone)) == 0) {

      if ((test = wposet_mb1_to_pose(pt_out, src, cfg->utm_zone)) == 0) {
        // must do motion update first if pt time <= mt time
        wtnav_motion_update(self, *pt_out);
        wtnav_meas_update(self, *mt_out, TRN_SENSOR_MB);
        //                fprintf(stderr,"%s:%d DONE [PT, MT]\n",__FUNCTION__,__LINE__);
        //                wposet_show(*pt_out,true,5);
        //                wmeast_show(*mt_out,true,5);
        retval = 0;
      }
      else {
        PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "wposet_mb1_to_pose failed [%d]\n", test));
          mlog_tprintf(trn_ulog_id,"ERR: mb1_to_pose failed [%d]\n", test);
      }
    }
    else {
      PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "wmeast_mb1_to_meas failed [%d]\n", test));
        mlog_tprintf(trn_ulog_id,"ERR: mb1_to_meas failed [%d]\n", test);
    }
  }

  return retval;
}

/*--------------------------------------------------------------------*/

int mbtrnpp_trn_process_mb1(wtnav_t *tnav, mb1_t *mb1, trn_config_t *cfg)
{
    int retval=-1;

    static int mb1_count=0;
    static int process_count=0;

    mlog_tprintf(trn_ulog_id,"trn_mb1_count,%lf,%d\n",mtime_etime(),++mb1_count);

    // ignore if trn disabled
    if(mbtrn_cfg->trn_enable){
        // check decimation
        bool do_process=false;

        // TODO: arbitrate between time/count decimation
        if(mbtrn_cfg->trn_decn>0){
            if( ((++trn_dec_cycles)%mbtrn_cfg->trn_decn)==0 ){
                do_process=true;
                trn_dec_cycles=0;
            }
        }else if(mbtrn_cfg->trn_decs>0.0){
            double now=mtime_dtime();
            if( ((mtime_dtime()-trn_dec_time)) > mbtrn_cfg->trn_decs){
                do_process=true;
                trn_dec_time=now;
            }
        }else {
            // always process of decimation disabled
            // (trn_decs<=0 && mbtrn_cfg->trn_decn<=0 )
            do_process=true;
        }

        MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_TRNSVR_XT], mtime_dtime());

        // server: update (trn_server) client connections
        netif_update_connections(trnsvr);

        // server: service (trn_server) client requests
        netif_reqres(trnsvr);

        MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_TRNSVR_XT], mtime_dtime());

        MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_TRNUSVR_XT], mtime_dtime());
       // server: update (trnu server) client connections
        netif_update_connections(trnusvr);
        // server: service (trnu server) client requests
        netif_reqres(trnusvr);

        MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_TRNUSVR_XT], mtime_dtime());

        if (do_process) {
            mlog_tprintf(trn_ulog_id,"trn_update_start,%lf,%lf,%d\n",mtime_etime(),mb1->sounding.ts,++process_count);
            MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_TRN_PROCN]);

            if(NULL!=tnav && NULL!=mb1 && NULL!=cfg){
                MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_PROC_XT], mtime_dtime());
                int test=-1;

                wmeast_t *mt = NULL;
                wposet_t *pt = NULL;
                trn_update_t trn_state={NULL,NULL,NULL,0,0,0,0,0.0,0.0},*pstate=&trn_state;

                if(NULL!=tnav && NULL!=mb1 && NULL!=cfg){

                    // get TRN update
                    MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_UPDATE_XT], mtime_dtime());

                    test=mbtrnpp_trn_update(tnav, mb1, &pt, &mt,cfg);

                    MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_UPDATE_XT], mtime_dtime());

                    if( test==0){
                        // get TRN bias estimates
                        MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_BIASEST_XT], mtime_dtime());

                        test=mbtrnpp_trn_get_bias_estimates(tnav, pt, pstate);

                        MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_BIASEST_XT], mtime_dtime());

                        if( test==0){
                            if(NULL!=pstate->pt_dat &&  NULL!= pstate->mle_dat && NULL!=pstate->mse_dat ){

                                // get number of reinits
                                MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_TRN_NREINITS_XT], mtime_dtime());

                                pstate->reinit_count = wtnav_get_num_reinits(tnav);
                                pstate->filter_state = wtnav_get_filter_state(tnav);
                                pstate->is_converged = (wtnav_is_converged(tnav) ? 1 : 0);
                                pstate->is_valid = ( (mb1->sounding.ts > 0. &&
                                                      pstate->mse_dat->covariance[0] <= cfg->max_northing_cov &&
                                                      pstate->mse_dat->covariance[2] <= cfg->max_easting_cov &&
                                                      fabs(pstate->mse_dat->x-pstate->pt_dat->x) <= cfg->max_northing_err &&
                                                      fabs(pstate->mse_dat->y-pstate->pt_dat->y) <= cfg->max_easting_err
                                                    )? 1 : 0);
                                pstate->mb1_cycle=mb1_count;
                                pstate->ping_number=mb1->sounding.ping_number;
                                pstate->mb1_time=mb1->sounding.ts;
                                pstate->update_time=mtime_etime();

                                MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_NREINITS_XT], mtime_dtime());

                                // publish to selected outputs
                                mbtrnpp_trn_publish(pstate, cfg);

                                retval=0;

                            }else{
                                PMPRINT(MOD_MBTRNPP,MM_DEBUG,(stderr,"ERR: pt[%p] pt_dat[%p] mle_dat[%p] mse_dat[%p]\n",pt,pstate->pt_dat,pstate->mle_dat,pstate->mse_dat));
                                mlog_tprintf(trn_ulog_id,"ERR: NULL data pt[%p] pt_dat[%p] mle_dat[%p] mse_dat[%p] ts[%.3lf] beams[%u] ping[%d] lat[%.5lf] lon[%.5lf] hdg[%.2lf] sd[%.1lf]\n",
                                             pt,pstate->pt_dat,pstate->mle_dat,pstate->mse_dat,
                                             mb1->sounding.ts, mb1->sounding.nbeams, mb1->sounding.ping_number,
                                             mb1->sounding.lat, mb1->sounding.lon, mb1->sounding.hdg, mb1->sounding.depth);
                            }
                        }else{
                            mlog_tprintf(trn_ulog_id,"ERR: trncli_get_bias_estimates failed [%d] [%d/%s]\n",test,errno,strerror(errno));

                            PMPRINT(MOD_MBTRNPP,MM_DEBUG|MBTRNPP_V3,(stderr,"ERR: trn_get_bias_estimates failed [%d] [%d/%s]\n",test,errno,strerror(errno)));
                        }
                    }else{
                        mlog_tprintf(trn_ulog_id,"ERR: trncli_send_update failed [%d] [%d/%s]\n",test,errno,strerror(errno));
                        PMPRINT(MOD_MBTRNPP,MM_DEBUG|MBTRNPP_V3,(stderr,"ERR: trn_update failed [%d] [%d/%s]\n",test,errno,strerror(errno)));
                    }
                    wmeast_destroy(mt);
                    wposet_destroy(pt);
                    if(NULL!=pstate->pt_dat)
                    free(pstate->pt_dat);
                    if(NULL!=pstate->mse_dat)
                    free(pstate->mse_dat);
                    if(NULL!=pstate->mle_dat)
                    free(pstate->mle_dat);
                }
                MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_TRN_PROC_XT], mtime_dtime());
            }// if tnav, mb1,cfg != NULL
            mlog_tprintf(trn_ulog_id,"trn_update_end,%lf,%d\n",mtime_etime(),retval);
        }// if do_process
    }// if trn_en

    return retval;
}
#endif // WITH_MBTNAV

int mbtrnpp_process_mb1(char *src, size_t len, trn_config_t *cfg)
{
    int retval=-1;

    if(NULL!=src && NULL!=cfg){

        // log current TRN message
        if ( OUTPUT_FLAG_SET(OUTPUT_MB1_BIN) ) {
            mlog_write(mb1_blog_id, (byte *)src, len);
        }

        if ( OUTPUT_FLAG_SET(OUTPUT_MB1_SVR_EN) ) {
            // server: update (mb1 server) client connections
            netif_update_connections(mb1svr);
            // server: service (mb1 server) client requests
            netif_reqres(mb1svr);
           // publish mb1 sounding to all clients
            netif_pub(mb1svr,(char *)src, len);
            MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_PUBN]);

        }
        MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_CYCLES]);

        //                struct timeval stv={0};
        //                gettimeofday(&stv,NULL);
        //                double stime = (double)stv.tv_sec+((double)stv.tv_usec/1000000.0);
        //                double ptime=ping[i_ping_process].time_d;
        //                fprintf(stderr,"mbtx : ptime[%.3lf] stime[%.3lf]
        //                (s-p)[%+6.3lf]**\n",ptime,stime,(stime-ptime)); fprintf(stderr,"mbtx :
        //                (s-p)[%+6.3lf]**\n",(stime-ptime));

        if (mbtrn_cfg->mbtrnpp_loop_delay_msec > 0) {
            PMPRINT(MOD_MBTRNPP, MBTRNPP_V5, (stderr, "delaying msec[%lld]\n", mbtrn_cfg->mbtrnpp_loop_delay_msec));
            mtime_delay_ms(mbtrn_cfg->mbtrnpp_loop_delay_msec);
        }

        retval=0;
    }
    return retval;
}

/*--------------------------------------------------------------------*/

int mbtrnpp_reson7kr_input_open(int verbose, void *mbio_ptr, char *definition, int *error) {

  /* local variables */
  int status = MB_SUCCESS;
  struct mb_io_struct *mb_io_ptr;

  uint32_t reson_nsubs = 11;
  uint32_t reson_subs[] = {1003, 1006, 1008, 1010, 1012, 1013, 1015, 1016, 7000, 7004, 7027};

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
    fprintf(stderr, "dbg2       mbio_ptr:   %p,%p\n", mbio_ptr, &mbio_ptr);
    fprintf(stderr, "dbg2       hostname:   %s\n", definition);
  }

  /* get pointer to mbio descriptor */
  mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

  /* set initial status */
  status = MB_SUCCESS;

  /* Open and initialize the socket based input for reading using function
   * mbtrnpp_reson7kr_input_read(). Allocate an internal, hidden buffer to hold data from
   * full s7k records while waiting to return bytes from those records as
   * requested by the MBIO read functions.
   * Store the relevant pointers and parameters within the
   * mb_io_struct structure *mb_io_ptr. */

  mb_path hostname;
  int port = 0;
  size_t size = 0;

  // copy def (strtok is destructive)
  char *defcpy = strdup(definition);
  char *addr[2]={NULL,NULL};
  // separate hostname, numeric tokens
  addr[0]=strtok(defcpy,":");
  addr[1]=strtok(NULL,"");

    // parse hostname, port, size
    if(NULL!=addr[0])
    strcpy(hostname, addr[0]);
    if(NULL!=addr[1])
    sscanf(addr[1], "%d:%zu", &port, &size);
    // release definition copy
    free(defcpy);

    if (strlen(hostname) == 0)
    strcpy(hostname, "localhost");
    if (port == 0)
    port = R7K_7KCENTER_PORT;
    if (size <= 0)
    size = SONAR_READER_CAPACITY_DFL;

  PMPRINT(MOD_MBTRNPP, MM_DEBUG, (stderr, "configuring r7kr_reader using %s:%d\n", hostname, port));
  r7kr_reader_t *reader = r7kr_reader_new(hostname, port, size, reson_subs, reson_nsubs);

  if (NULL != mb_io_ptr && NULL != reader) {

    // set r7k_reader
    mb_io_ptr->mbsp = (void *) reader;

    if (reader->state == R7KR_CONNECTED || reader->state == R7KR_SUBSCRIBED) {
      // update application performance profile
      MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_CONN]);
    }

    // get global 7K reader performance profile
    reader_stats = r7kr_reader_get_stats(reader);
    mstats_set_period(reader_stats, app_stats->stats->stat_period_start, app_stats->stats->stat_period_sec);

    // configure reader data log
      if ( OUTPUT_FLAG_SET(OUTPUT_RESON_BIN) ) {
      // open mbr data log
      reson_blog_path = (char *)malloc(512);
      sprintf(reson_blog_path, "%s//%s-%s%s", mbtrn_cfg->trn_log_dir, RESON_BLOG_NAME, s_mbtrnpp_session_str(NULL,0,RF_NONE), MBTRNPP_LOG_EXT);

      reson_blog_id = mlog_get_instance(reson_blog_path, &reson_blog_conf, RESON_BLOG_NAME);

      mlog_show(reson_blog_id, true, 5);
      mlog_open(reson_blog_id, flags, mode);

      r7kr_reader_set_log(reader, reson_blog_id);
    }

    if (verbose >= 1) {
      r7kr_reader_show(reader, true, 5);
    }
  }
  else {
    fprintf(stderr, "ERR - r7kr_reader_new failed (NULL) [%d:%s]\n", errno, strerror(errno));
    status = MB_FAILURE;
    *error = MB_ERROR_INIT_FAIL;
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_reson7kr_input_read(int verbose, void *mbio_ptr, size_t *size, char *buffer, int *error) {

  /* local variables */
  int status = MB_SUCCESS;
  struct mb_io_struct *mb_io_ptr;
  r7kr_reader_t *mbsp;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
    fprintf(stderr, "dbg2       mbio_ptr:   %p\n", mbio_ptr);
    fprintf(stderr, "dbg2       size:       %zu\n", *size);
    fprintf(stderr, "dbg2       buffer:     %p\n", buffer);
  }

  /* get pointer to mbio descriptor */
  mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

  /* set initial status */
  status = MB_SUCCESS;

  /* Read the requested number of bytes (= size) off the input and  place
   * those bytes into the buffer.
   * This requires reading full s7k records off the socket, storing the data
   * in an internal, hidden buffer, and parceling those bytes out as requested.
   * The internal buffer should be allocated in mbtrnpp_reson7kr_input_init() and stored
   * in the mb_io_struct structure *mb_io_ptr. */

  // use the socket reader
  // read and return single frame
  uint32_t sync_bytes=0;
  int64_t rbytes=-1;
  r7kr_reader_t *reader = (r7kr_reader_t *)mb_io_ptr->mbsp;
  if ( (rbytes = r7kr_read_stripped_frame(reader, (byte *) buffer,
                                          R7K_MAX_FRAME_BYTES, R7KR_NET_STREAM,
                                          0.0, R7KR_READ_TMOUT_MSEC,
                                          &sync_bytes)) < 0) {
    status   = MB_FAILURE;
    *error   = MB_ERROR_EOF;
    *size    = (size_t)rbytes;

      MST_METRIC_START(app_stats->stats->metrics[MBTPP_CH_MB_GETFAIL_XT], mtime_dtime());
      PMPRINT(MOD_MBTRNPP,MBTRNPP_V4,(stderr,"r7kr_read_stripped_frame failed: sync_bytes[%d] status[%d] err[%d]\n",sync_bytes,status, *error));
      fprintf(stderr,"r7kr_read_stripped_frame failed: sync_bytes[%d] status[%d] err[%d]\n",sync_bytes,status, *error);

      MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_EMBFRAMERD]);
      MST_COUNTER_ADD(app_stats->stats->events[MBTPP_STA_MB_SYNC_BYTES],sync_bytes);

      fprintf(stderr,"EOF (input socket) - clear status/error\n");
      status = MB_SUCCESS;
      error = MB_ERROR_NO_ERROR;

      // check connection status
      // only reconnect if disconnected
      if ((NULL!=reader && reader->state==R7KR_INITIALIZED) || (me_errno==ME_ESOCK) || (me_errno==ME_EOF)  ) {
          MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_EMBSOCKET]);

          // empty the reader's record frame container
          r7kr_reader_purge(reader);
          fprintf(stderr,"mbtrnpp: input socket disconnected status[%s]\n",r7kr_strstate(reader->state));
          mlog_tprintf(mbtrnpp_mlog_id,"mbtrnpp: input socket disconnected status[%s]\n",r7kr_strstate(reader->state));
          MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_DISN]);
          if (r7kr_reader_connect(reader,true)==0) {
              fprintf(stderr,"mbtrnpp: input socket connected status[%s]\n",r7kr_strstate(reader->state));
              mlog_tprintf(mbtrnpp_mlog_id,"mbtrnpp: input socket connected status[%s]\n",r7kr_strstate(reader->state));
              MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_MB_CONN]);
          }else{
              fprintf(stderr,"mbtrnpp: input socket reconnect failed status[%s]\n",r7kr_strstate(reader->state));
              mlog_tprintf(mbtrnpp_mlog_id,"mbtrnpp: input socket reconnect failed status[%s]\n",r7kr_strstate(reader->state));
              MST_COUNTER_INC(app_stats->stats->events[MBTPP_EV_EMBCON]);


              struct timespec twait={0},trem={0};
              twait.tv_sec=5;
              nanosleep(&twait,&trem);
          }
      }

      MST_METRIC_LAP(app_stats->stats->metrics[MBTPP_CH_MB_GETFAIL_XT], mtime_dtime());

//    if (me_errno==ME_ESOCK) {
//        fprintf(stderr,"r7kr_reader server connection closed.\n");
//    } else if (me_errno==ME_EOF) {
//        fprintf(stderr,"r7kr_reader end of file (server connection closed).\n");
//    } else{
//        fprintf(stderr,"r7kr_read_stripped_frame me_errno %d/%s\n",me_errno,me_strerror(me_errno));
//    }

  } else {
    *error = MB_ERROR_NO_ERROR;
    *size    = (size_t)rbytes;
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_reson7kr_input_close(int verbose, void *mbio_ptr, int *error) {

  /* local variables */
  int status = MB_SUCCESS;
  struct mb_io_struct *mb_io_ptr;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
    fprintf(stderr, "dbg2       mbio_ptr:   %p\n", mbio_ptr);
  }

  /* get pointer to mbio descriptor */
  mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

  /* set initial status */
  status = MB_SUCCESS;

  /* Close the socket based input for reading using function
   * mbtrnpp_reson7kr_input_read(). Deallocate the internal, hidden buffer and any
   * other resources that were allocated by mbtrnpp_reson7kr_input_init(). */
  r7kr_reader_t *reader = (r7kr_reader_t *)mb_io_ptr->mbsp;
  r7kr_reader_destroy(&reader);
  mb_io_ptr->mbsp = NULL;

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_kemkmall_input_open(int verbose, void *mbio_ptr, char *definition, int *error) {

  /* local variables */
  int status = MB_SUCCESS;
  struct mb_io_struct *mb_io_ptr;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
    fprintf(stderr, "dbg2       mbio_ptr:   %p,%p\n", mbio_ptr, &mbio_ptr);
    fprintf(stderr, "dbg2       definition: %s\n", definition);
  }

  /* get pointer to mbio descriptor */
  mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

  /* set initial status */
  status = MB_SUCCESS;

  // Open and initialize the socket based input for reading using function
  // mbtrnpp_kemkmall_input_read().
  // - use mb_io_ptr->mbsp to hold pointer to socket i/o structure
  // - the socket definition = "hostInterface:broadcastGroup:port"
  int port;
  mb_path bcastGrp;
  mb_path hostInterface;
  struct sockaddr_in localSock;
  struct ip_mreq group;
  char *token;
  if ((token = strsep(&definition, ":")) != NULL) {
    strncpy(hostInterface, token, sizeof(mb_path));
  }
  if ((token = strsep(&definition, ":")) != NULL) {
    strncpy(bcastGrp, token, sizeof(mb_path));
  }
  if ((token = strsep(&definition, ":")) != NULL) {
    sscanf(token, "%d", &port);
  }

  //sscanf(definition, "%s:%s:%d", hostInterface, bcastGrp, &port);
  fprintf(stderr, "Attempting to open socket to Kongsberg sonar multicast at:\n");
  fprintf(stderr, "  Definition: %s\n", definition);
  fprintf(stderr, "  hostInterface: %s\n  bcastGrp: %s\n  port: %d\n",
          hostInterface, bcastGrp, port);

  /* Create a datagram socket on which to receive. */
  int sd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sd < 0)
  {
      perror("Opening datagram socket error");
      exit(1);
  }

  /* Enable SO_REUSEADDR to allow multiple instances of this */
  /* application to receive copies of the multicast datagrams. */
  int reuse = 1;
  if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0)
    {
      perror("Setting SO_REUSEADDR error");
      close(sd);
      exit(1);
    }

  /* Bind to the proper port number with the IP address */
  /* specified as INADDR_ANY. */
  memset((char *) &localSock, 0, sizeof(localSock));
  localSock.sin_family = AF_INET;
  localSock.sin_port = htons(port);
  localSock.sin_addr.s_addr = INADDR_ANY;
  if (bind(sd, (struct sockaddr*)&localSock, sizeof(localSock))) {
      perror("Binding datagram socket error");
      close(sd);
      exit(1);
  }

  /* Join the multicast group on the specified */
  /* interface. Note that this IP_ADD_MEMBERSHIP option must be */
  /* called for each local interface over which the multicast */
  /* datagrams are to be received. */
  group.imr_multiaddr.s_addr = inet_addr(bcastGrp);
  group.imr_interface.s_addr = inet_addr(hostInterface);

  if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
    (char *)&group, sizeof(group)) < 0) {
    perror("Adding multicast group error");
    close(sd);
    exit(1);
  }

  // save the socket within the mb_io structure
  int *sd_ptr = NULL;
  status &= mb_mallocd(verbose, __FILE__, __LINE__, sizeof(sd), (void **)&sd_ptr, error);
  *sd_ptr = sd;
  mb_io_ptr->mbsp = (void *) sd_ptr;

  /*initialize buffer for fragmented MWZ and MRC datagrams*/
  memset(mRecordBuf, 0, sizeof(mRecordBuf));

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_kemkmall_rd_hdr(int verbose, char *buffer, void *header_ptr, void *emdgm_type_ptr, int *error) {
  struct mbsys_kmbes_header *header = NULL;
  mbsys_kmbes_emdgm_type *emdgm_type = NULL;
  int index = 0;

  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:        %d\n", verbose);
    fprintf(stderr, "dbg2       buffer:         %p\n", (void *)buffer);
    fprintf(stderr, "dbg2       header_ptr:     %p\n", (void *)header_ptr);
    fprintf(stderr, "dbg2       emdgm_type_ptr: %p\n", (void *)emdgm_type_ptr);
  }

  /* get pointer to header structure */
  header = (struct mbsys_kmbes_header *)header_ptr;
  emdgm_type = (mbsys_kmbes_emdgm_type *)emdgm_type_ptr;

  /* extract the data */
  index = 0;
  mb_get_binary_int(true, &buffer[index], &(header->numBytesDgm));
  index += 4;
  memcpy(&(header->dgmType), &buffer[index], sizeof(header->dgmType));
  index += 4;
  header->dgmVersion = buffer[index];
  index++;
  header->systemID = buffer[index];
  index++;
  mb_get_binary_short(true, &buffer[index], &(header->echoSounderID));
  index += 2;
  mb_get_binary_int(true, &buffer[index], &(header->time_sec));
  index += 4;
  mb_get_binary_int(true, &buffer[index], &(header->time_nanosec));

  /* identify the datagram type */
  if (strncmp((const char *)header->dgmType, MBSYS_KMBES_I_INSTALLATION_PARAM, 4) == 0 ) {
    *emdgm_type = IIP;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_I_OP_RUNTIME, 4) == 0) {
    *emdgm_type = IOP;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_POSITION, 4) == 0) {
    *emdgm_type = SPO;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_KM_BINARY, 4) == 0) {
    *emdgm_type = SKM;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_SOUND_VELOCITY_PROFILE, 4) == 0) {
    *emdgm_type = SVP;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_SOUND_VELOCITY_TRANSDUCER, 4) == 0) {
    *emdgm_type = SVT;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_CLOCK, 4) == 0) {
    *emdgm_type = SCL;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_DEPTH, 4) == 0) {
    *emdgm_type = SDE;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_HEIGHT, 4) == 0) {
    *emdgm_type = SHI;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_S_HEADING, 4) == 0) {
    *emdgm_type = SHA;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_M_RANGE_AND_DEPTH, 4) == 0) {
    *emdgm_type = MRZ;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_M_WATER_COLUMN, 4) == 0) {
    *emdgm_type = MWC;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_C_POSITION, 4) == 0) {
    *emdgm_type = CPO;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_C_HEAVE, 4) == 0) {
    *emdgm_type = CHE;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_X_MBSYSTEM, 4) == 0) {
    *emdgm_type = XMB;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_X_COMMENT, 4) == 0) {
    *emdgm_type = XMC;
  }
  else if (strncmp((const char *)header->dgmType, MBSYS_KMBES_X_PSEUDOSIDESCAN, 4) == 0) {
    *emdgm_type = XMS;
  }
  else {
    *emdgm_type = UNKNOWN;
  }

  if (verbose >= 5) {
    fprintf(stderr, "\ndbg5  Values read in MBIO function <%s>\n", __func__);
    fprintf(stderr, "dbg5       numBytesDgm:    %u\n", header->numBytesDgm);
    fprintf(stderr, "dbg5       dgmType:        %.4s\n", header->dgmType);
    fprintf(stderr, "dbg5       dgmVersion:     %u\n", header->dgmVersion);
    fprintf(stderr, "dbg5       systemID:       %u\n", header->systemID);
    fprintf(stderr, "dbg5       echoSounderID:  %u\n", header->echoSounderID);
    fprintf(stderr, "dbg5       time_sec:       %u\n", header->time_sec);
    fprintf(stderr, "dbg5       time_nanosec:   %u\n", header->time_nanosec);
  }

  int status = MB_SUCCESS;

  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       dgmType:    %.4s\n", header->dgmType);
    fprintf(stderr, "dbg2       emdgm_type: %d\n", *emdgm_type);
    fprintf(stderr, "dbg2       error:      %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:  %d\n", status);
  }

  /* return status */
  return (status);
};

/*--------------------------------------------------------------------*/

int mbtrnpp_kemkmall_input_read(int verbose, void *mbio_ptr, size_t *size,
                                char *buffer, int *error) {

  /* local variables */
  int status = MB_SUCCESS;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
    fprintf(stderr, "dbg2       mbio_ptr:   %p\n", mbio_ptr);
    fprintf(stderr, "dbg2       size:       %zu\n", *size);
    fprintf(stderr, "dbg2       buffer:     %p\n", buffer);
  }

  /* get pointer to mbio descriptor */
  struct mb_io_struct *mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

  /* set initial status */
  status = MB_SUCCESS;

  // Read from the socket.
  int *sd_ptr = (int *)mb_io_ptr->mbsp;
  struct mbsys_kmbes_header header;
  unsigned int num_bytes_dgm_end;
  mbsys_kmbes_emdgm_type emdgm_type;
  memset(buffer, 0, *size);
  int readlen = read(*sd_ptr, buffer, *size);
  if (readlen <= 0) {
    status = MB_FAILURE;
    *error = MB_ERROR_EOF;
  }

  if (status == MB_SUCCESS) {
    status = mbtrnpp_kemkmall_rd_hdr(verbose, buffer, (void *)&header, (void *)&emdgm_type, error);

    if (status == MB_SUCCESS && emdgm_type != UNKNOWN && header.numBytesDgm <= *size) {
      mb_get_binary_int(true, &buffer[header.numBytesDgm-4], &num_bytes_dgm_end);
      if (num_bytes_dgm_end != header.numBytesDgm) {
        status = MB_FAILURE;
        *error = MB_ERROR_UNINTELLIGIBLE;
      }
    } else {
        status = MB_FAILURE;
        *error = MB_ERROR_UNINTELLIGIBLE;
    }
  }

  if (status == MB_SUCCESS) {
    *size = header.numBytesDgm;
  }
  else {
    *size = 0;
  }

  /*handle multi-packet MRZ and MWC records*/
  static int totalDgms, dgmsReceived=0;
  static unsigned int pingSecs, pingNanoSecs;
  unsigned short numOfDgms;
  unsigned short dgmNum;
  int totalSize;
  if (emdgm_type == MRZ || emdgm_type == MWC) {
    mb_get_binary_short(true, &buffer[MBSYS_KMBES_HEADER_SIZE], &numOfDgms);
    mb_get_binary_short(true, &buffer[MBSYS_KMBES_HEADER_SIZE+2], &dgmNum);
    if (numOfDgms > 1) {

      /* if we get a M record of a multi-packet sequence, and its numOfDgms
          or ping time don't match the ping we are looking for, flush the
          current read and start over with this packet */
      if (header.time_sec != pingSecs
          || header.time_nanosec != pingNanoSecs
          || numOfDgms != totalDgms) {
        dgmsReceived = 0;
      }

      if (!dgmsReceived){
        pingSecs = header.time_sec;
        pingNanoSecs = header.time_nanosec;
        totalDgms = numOfDgms;
        dgmsReceived = 1;
      }
      else {
        dgmsReceived++;
      }

      memcpy(mRecordBuf[dgmNum-1], buffer, header.numBytesDgm);

      if (dgmsReceived == totalDgms) {
fprintf(stderr, "%s:%4.4d Handling %d datagrams\n", __FILE__, __LINE__, totalDgms);
        totalSize = sizeof(struct mbsys_kmbes_m_partition)
                    + sizeof(struct mbsys_kmbes_header) + 4;
        int rsize = 0;
        for (int dgm = 0; dgm < totalDgms; dgm++) {
          mb_get_binary_int(true, mRecordBuf[dgm], &rsize);
          totalSize += rsize - sizeof(struct mbsys_kmbes_m_partition)
                      - sizeof(struct mbsys_kmbes_header) - 4;
        }

        /*copy data into new buffer*/
        if (status == MB_SUCCESS) {
          int index = 0;
          status = mbtrnpp_kemkmall_rd_hdr(verbose, mRecordBuf[0], (void *)&header, (void *)&emdgm_type, error);
          memcpy(buffer, mRecordBuf[0], header.numBytesDgm);
          index = header.numBytesDgm - 4;
          void *ptr;
          for (int dgm=1; dgm < totalDgms; dgm++) {
            status = mbtrnpp_kemkmall_rd_hdr(verbose, mRecordBuf[dgm], (void *)&header, (void *)&emdgm_type, error);
            int copy_len = header.numBytesDgm - sizeof(struct mbsys_kmbes_m_partition)
                                  - sizeof(struct mbsys_kmbes_header) - 4;
            ptr = (void *)(mRecordBuf[dgm]+
                     sizeof(struct mbsys_kmbes_m_partition)+
                     sizeof(struct mbsys_kmbes_header));
            memcpy(&buffer[index], ptr, copy_len);
            index += copy_len;
          }
          mb_put_binary_int(true, totalSize, &buffer[0]);
          mb_put_binary_short(true, 1, &buffer[sizeof(struct mbsys_kmbes_header)]);
          mb_put_binary_short(true, 1, &buffer[sizeof(struct mbsys_kmbes_header)+2]);
          mb_put_binary_int(true, totalSize, &buffer[index]);
            dgmsReceived = 0; /*reset received counter back to 0*/
        }
      }
    }
  }

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}

/*--------------------------------------------------------------------*/

int mbtrnpp_kemkmall_input_close(int verbose, void *mbio_ptr, int *error) {

  /* local variables */
  int status = MB_SUCCESS;
  struct mb_io_struct *mb_io_ptr;

  /* print input debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> called\n", __func__);
    fprintf(stderr, "dbg2  Input arguments:\n");
    fprintf(stderr, "dbg2       verbose:    %d\n", verbose);
    fprintf(stderr, "dbg2       mbio_ptr:   %p\n", mbio_ptr);
  }

  /* get pointer to mbio descriptor */
  mb_io_ptr = (struct mb_io_struct *)mbio_ptr;

  /* set initial status */
  status = MB_SUCCESS;

  // Close the socket based input
  int *sd_ptr = (int *)mb_io_ptr->mbsp;
  close(*sd_ptr);
  status &= mb_freed(verbose, __FILE__, __LINE__, (void **)&sd_ptr, error);

  /* print output debug statements */
  if (verbose >= 2) {
    fprintf(stderr, "\ndbg2  MBIO function <%s> completed\n", __func__);
    fprintf(stderr, "dbg2  Return values:\n");
    fprintf(stderr, "dbg2       error:              %d\n", *error);
    fprintf(stderr, "dbg2  Return status:\n");
    fprintf(stderr, "dbg2       status:             %d\n", status);
  }

  /* return */
  return (status);
}
/*--------------------------------------------------------------------*/

