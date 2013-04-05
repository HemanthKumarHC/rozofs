/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#ifndef _EPPROTO_H_RPCGEN
#define _EPPROTO_H_RPCGEN

#include <rpc/rpc.h>


#ifdef __cplusplus
extern "C" {
#endif

#include <rozofs/rozofs.h>

enum epp_status_t {
	EPP_SUCCESS = 0,
	EPP_FAILURE = 1,
};
typedef enum epp_status_t epp_status_t;

struct epp_status_ret_t {
	epp_status_t status;
	union {
		int error;
	} epp_status_ret_t_u;
};
typedef struct epp_status_ret_t epp_status_ret_t;

struct epp_estat_t {
	uint32_t eid;
	uint32_t vid;
	uint16_t bsize;
	uint64_t blocks;
	uint64_t bfree;
	uint64_t files;
	uint64_t ffree;
};
typedef struct epp_estat_t epp_estat_t;

struct epp_sstat_t {
	uint16_t sid;
	uint8_t status;
	uint64_t size;
	uint64_t free;
};
typedef struct epp_sstat_t epp_sstat_t;

struct epp_vstat_t {
	uint16_t vid;
	uint16_t bsize;
	uint64_t bfree;
	uint32_t nb_storages;
	epp_sstat_t sstats[2048];
};
typedef struct epp_vstat_t epp_vstat_t;

struct epp_profiler_t {
	uint64_t uptime;
	uint64_t now;
	uint8_t vers[20];
	uint32_t nb_volumes;
	epp_vstat_t vstats[16];
	uint32_t nb_exports;
	epp_estat_t estats[2048];
	uint64_t ep_mount[2];
	uint64_t ep_umount[2];
	uint64_t ep_statfs[2];
	uint64_t ep_lookup[2];
	uint64_t ep_getattr[2];
	uint64_t ep_setattr[2];
	uint64_t ep_readlink[2];
	uint64_t ep_mknod[2];
	uint64_t ep_mkdir[2];
	uint64_t ep_unlink[2];
	uint64_t ep_rmdir[2];
	uint64_t ep_symlink[2];
	uint64_t ep_rename[2];
	uint64_t ep_readdir[2];
	uint64_t ep_read_block[3];
	uint64_t ep_write_block[3];
	uint64_t ep_link[2];
	uint64_t ep_setxattr[2];
	uint64_t ep_getxattr[2];
	uint64_t ep_removexattr[2];
	uint64_t ep_listxattr[2];
	uint64_t export_lv1_resolve_entry[2];
	uint64_t export_lv2_resolve_path[2];
	uint64_t export_lookup_fid[2];
	uint64_t export_update_files[2];
	uint64_t export_update_blocks[2];
	uint64_t export_stat[2];
	uint64_t export_lookup[2];
	uint64_t export_getattr[2];
	uint64_t export_setattr[2];
	uint64_t export_link[2];
	uint64_t export_mknod[2];
	uint64_t export_mkdir[2];
	uint64_t export_unlink[2];
	uint64_t export_rmdir[2];
	uint64_t export_symlink[2];
	uint64_t export_readlink[2];
	uint64_t export_rename[2];
	uint64_t export_read[3];
	uint64_t export_read_block[2];
	uint64_t export_write_block[2];
	uint64_t export_setxattr[2];
	uint64_t export_getxattr[2];
	uint64_t export_removexattr[2];
	uint64_t export_listxattr[2];
	uint64_t export_readdir[2];
	uint64_t lv2_cache_put[2];
	uint64_t lv2_cache_get[2];
	uint64_t lv2_cache_del[2];
	uint64_t volume_balance[2];
	uint64_t volume_distribute[2];
	uint64_t volume_stat[2];
	uint64_t mdir_open[2];
	uint64_t mdir_close[2];
	uint64_t mdir_read_attributes[2];
	uint64_t mdir_write_attributes[2];
	uint64_t mreg_open[2];
	uint64_t mreg_close[2];
	uint64_t mreg_read_attributes[2];
	uint64_t mreg_write_attributes[2];
	uint64_t mreg_read_dist[2];
	uint64_t mreg_write_dist[2];
	uint64_t mslnk_open[2];
	uint64_t mslnk_close[2];
	uint64_t mslnk_read_attributes[2];
	uint64_t mslnk_write_attributes[2];
	uint64_t mslnk_read_link[2];
	uint64_t mslnk_write_link[2];
	uint64_t get_mdirentry[2];
	uint64_t put_mdirentry[2];
	uint64_t del_mdirentry[2];
	uint64_t list_mdirentries[2];
};
typedef struct epp_profiler_t epp_profiler_t;

struct epp_profiler_ret_t {
	epp_status_t status;
	union {
		epp_profiler_t profiler;
		int error;
	} epp_profiler_ret_t_u;
};
typedef struct epp_profiler_ret_t epp_profiler_ret_t;

#define EXPORTD_PROFILE_PROGRAM 0x20000005
#define EXPORTD_PROFILE_VERSION 1

#if defined(__STDC__) || defined(__cplusplus)
#define EPP_NULL 0
extern  void * epp_null_1(void *, CLIENT *);
extern  void * epp_null_1_svc(void *, struct svc_req *);
#define EPP_GET_PROFILER 1
extern  epp_profiler_ret_t * epp_get_profiler_1(void *, CLIENT *);
extern  epp_profiler_ret_t * epp_get_profiler_1_svc(void *, struct svc_req *);
#define EPP_CLEAR 2
extern  epp_status_ret_t * epp_clear_1(void *, CLIENT *);
extern  epp_status_ret_t * epp_clear_1_svc(void *, struct svc_req *);
extern int exportd_profile_program_1_freeresult (SVCXPRT *, xdrproc_t, caddr_t);

#else /* K&R C */
#define EPP_NULL 0
extern  void * epp_null_1();
extern  void * epp_null_1_svc();
#define EPP_GET_PROFILER 1
extern  epp_profiler_ret_t * epp_get_profiler_1();
extern  epp_profiler_ret_t * epp_get_profiler_1_svc();
#define EPP_CLEAR 2
extern  epp_status_ret_t * epp_clear_1();
extern  epp_status_ret_t * epp_clear_1_svc();
extern int exportd_profile_program_1_freeresult ();
#endif /* K&R C */

/* the xdr functions */

#if defined(__STDC__) || defined(__cplusplus)
extern  bool_t xdr_epp_status_t (XDR *, epp_status_t*);
extern  bool_t xdr_epp_status_ret_t (XDR *, epp_status_ret_t*);
extern  bool_t xdr_epp_estat_t (XDR *, epp_estat_t*);
extern  bool_t xdr_epp_sstat_t (XDR *, epp_sstat_t*);
extern  bool_t xdr_epp_vstat_t (XDR *, epp_vstat_t*);
extern  bool_t xdr_epp_profiler_t (XDR *, epp_profiler_t*);
extern  bool_t xdr_epp_profiler_ret_t (XDR *, epp_profiler_ret_t*);

#else /* K&R C */
extern bool_t xdr_epp_status_t ();
extern bool_t xdr_epp_status_ret_t ();
extern bool_t xdr_epp_estat_t ();
extern bool_t xdr_epp_sstat_t ();
extern bool_t xdr_epp_vstat_t ();
extern bool_t xdr_epp_profiler_t ();
extern bool_t xdr_epp_profiler_ret_t ();

#endif /* K&R C */

#ifdef __cplusplus
}
#endif

#endif /* !_EPPROTO_H_RPCGEN */
