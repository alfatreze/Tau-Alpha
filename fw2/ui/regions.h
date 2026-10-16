/* The Helios regions of Player 2 (ids are the order views_init() registers them; each view's `invalidates` mask is built from these). Rows are framebuffer rows. */
#ifndef P2_REGIONS_H
#define P2_REGIONS_H
enum { R_NP_HEAD = 0, R_NP_METER, R_NP_FOOT, R_LIST, R_MENU, R_INFO, R_HAL, R_FS_FIG, R_FS_FOOT, R_COUNT };
#define RM(r) (1u << (r))
#define NP_REGIONS (RM(R_NP_HEAD) | RM(R_NP_METER) | RM(R_NP_FOOT))
#define NP_HEAD_Y0 0u
#define NP_HEAD_Y1 210u
#define NP_METER_X 20u
#define NP_METER_W 360u
#define NP_METER_Y0 212u
#define NP_METER_Y1 292u
#define NP_FOOT_Y0 296u
#define NP_FOOT_Y1 360u
#endif
