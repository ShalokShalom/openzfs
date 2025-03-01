
use_clang()

wdk_add_library(zfskern
  KMDF 1.9
  abd.c
  aggsum.c
  arc.c
  ../avl/avl.c
  blkptr.c
  bplist.c
  bpobj.c
  bptree.c
  bqueue.c
  btree.c
  dataset_kstats.c
  dbuf.c
  dbuf_stats.c
  ddt.c
  ddt_zap.c
  dmu.c
  dmu_diff.c
  dmu_object.c
  dmu_objset.c
  dmu_recv.c
  dmu_redact.c
  dmu_send.c
  dmu_traverse.c
  dmu_tx.c
  dmu_zfetch.c
  dnode.c
  dnode_sync.c
  dsl_bookmark.c
  dsl_crypt.c
  dsl_dataset.c
  dsl_deadlist.c
  dsl_deleg.c
  dsl_destroy.c
  dsl_dir.c
  dsl_pool.c
  dsl_prop.c
  dsl_scan.c
  dsl_synctask.c
  dsl_userhold.c
  edonr_zfs.c
  fm.c
  gzip.c
  hkdf.c
  lz4.c
  lzjb.c
  metaslab.c
  mmp.c
  multilist.c
  objlist.c
  pathname.c
  range_tree.c
  refcount.c
  rrwlock.c
  sa.c
  sha256.c
  skein_zfs.c
  spa.c
  spa_boot.c
  spa_checkpoint.c
  spa_config.c
  spa_errlog.c
  spa_history.c
  spa_log_spacemap.c
  spa_misc.c
  spa_stats.c
  space_map.c
  space_reftree.c
  txg.c
  uberblock.c
  unique.c
  vdev.c
  vdev_cache.c
  vdev_draid.c
  vdev_draid_rand.c
  vdev_indirect.c
  vdev_indirect_births.c
  vdev_indirect_mapping.c
  vdev_initialize.c
  vdev_label.c
  vdev_mirror.c
  vdev_missing.c
  vdev_queue.c
  vdev_raidz.c
  vdev_raidz_math.c
  vdev_raidz_math_scalar.c
  vdev_rebuild.c
  vdev_removal.c
  vdev_root.c
  vdev_trim.c
  zap.c
  zap_leaf.c
  zap_micro.c
  zcp.c
  zcp_get.c
  zcp_global.c
  zcp_iter.c
  zcp_set.c
  zcp_synctask.c
  zfeature.c
  zfs_byteswap.c
  zfs_fm.c
  zfs_fuid.c
  zfs_ioctl.c
  zfs_log.c
  zfs_onexit.c
  zfs_quota.c
  zfs_ratelimit.c
  zfs_replay.c
  zfs_rlock.c
  zfs_sa.c
  zfs_vnops.c
  zil.c
  zio.c
  zio_checksum.c
  zio_compress.c
  zio_inject.c
  zle.c
  zrlock.c
  zthr.c
  zvol.c
)

target_link_libraries(zfskern PRIVATE splkern icpkern zstdkern)

file(GLOB CSOURCES CONFIGURE_DEPENDS "*.c")

list(REMOVE_ITEM CSOURCES "${CMAKE_CURRENT_SOURCE_DIR}/txg.c")

function(add_macro_property)
        foreach (I ${CSOURCES})
                get_filename_component(OUTPUT_FILE_WE ${I} NAME_WE)
                set_source_files_properties(${I} PROPERTIES COMPILE_FLAGS -DWPPFILE=${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh)
                message(STATUS "FILES_IN ======: ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh") 
        endforeach()
endfunction()

function(tracewpp OUTPUT_DIR SOURCE)
        # [mlr] list of .tmh files to be generated -> TMH
        set(WPP_DIR "${WDK_ROOT}/bin/${WDK_VERSION}")

        get_filename_component(FILEN ${SOURCE} NAME)
        set(TMH_FILEN ${FILEN}.tmh)
        set(TMH ${OUTPUT_DIR}/${TMH_FILEN})
        set(EXTENSIONS ".c")

        # [mlr] cmake only converts the command name to the native path format. the
        # path names to be used in arguments must be converted manually.

        file(TO_NATIVE_PATH ${SOURCE} NATIVE_SOURCE)
        file(TO_NATIVE_PATH ${WPP_DIR} NATIVE_WPP_DIR)
        file(TO_NATIVE_PATH ${OUTPUT_DIR} NATIVE_OUTPUT_DIR)

        # [mlr] note that if -preserveext: occurs after the source file specification, it has
        # no effect.

        set(TRACE "TraceEvent{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...)")
        set(DPRINT "dprintf{FLAGS=MYDRIVER_ALL_INFO, LEVEL=TRACE_INFO}(MSG, ...)")
        set(CFGDIR "${WPP_DIR}/wppconfig/rev1")
        set(SCAN "${WDK_ROOT}/Include/wdf/kmdf/1.9/WdfTraceEnums.h")
        set(WPP_MACRO "WPP_INLINE __inline")

        execute_process(COMMAND "${NATIVE_WPP_DIR}/${WDK_PLATFORM}/tracewpp.exe"
 -scan:${SCAN} /D${WPP_MACRO}
 -cfgdir:${CFGDIR} -I${CMAKE_CURRENT_BINARY_DIR} -odir:${NATIVE_OUTPUT_DIR} -km -func:${TRACE}
 -func:${DPRINT} -gen:{km-default.tpl}*.tmh ${NATIVE_SOURCE})

endfunction()


function(wpp OUTPUT_DIR)

        add_macro_property()
        # [mlr] invoke tracewpp() for each source file, adding the resulting file to a list
        # named TMH.
        message(STATUS "OUTPUT_DIR ======: ${OUTPUT_DIR}")

        foreach ( I ${CSOURCES} )
                tracewpp(${OUTPUT_DIR} ${I})
        endforeach()

endfunction()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    wpp("${CMAKE_SOURCE_DIR}/out/build")
endif()
