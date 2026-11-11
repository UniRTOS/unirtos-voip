# UniRTOS external library package. The SDK registers this target for firmware linking.
set(target unirtos-voip)
if(DEFINED UNIRTOS_EXTERNAL_LIBRARY_NAME AND
   NOT "${UNIRTOS_EXTERNAL_LIBRARY_NAME}" STREQUAL "unirtos-voip")
    message(FATAL_ERROR "This repository must be built as the unirtos-voip library")
endif()

# v1.0.1 preserves the EG800Z QuecDuino implementation verified in the source project.
if(NOT "${build_project}" STREQUAL "EG800ZCN_LA")
    message(FATAL_ERROR "unirtos-voip v1.0.1 supports EG800ZCN_LA only")
endif()
if(NOT DEFINED UNIRTOS_VOIP_AUDIO_ABI OR NOT "${UNIRTOS_VOIP_AUDIO_ABI}" STREQUAL "1")
    message(FATAL_ERROR
        "unirtos-voip requires a matching SDK with UNIRTOS_VOIP_AUDIO_ABI=1 "
        "(local validation SDK 1.0.4-voip.1). Update the audio headers, libraries "
        "and platform base together; the stock v1.0.4 audio ABI is incompatible.")
endif()

if(NOT TARGET qcm_audio OR NOT TARGET socket_adapter)
    message(FATAL_ERROR
        "The SDK must configure qcm_audio and socket_adapter targets before unirtos-voip")
endif()

add_library(${target} STATIC)
target_include_directories(${target} PRIVATE
    ${SOURCE_TOP_DIR}
    ${SOURCE_SYS_DEBUG_DIR}
    ${SOURCE_SYS_OS_DIR}
    ${SOURCE_COMPONENTS_PUBLIC_INC}
    ${UNIRTOS_COMPONENTS_DIR}/include
)
target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/app
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ortp/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/ortp/mpc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/osip/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/osip/include/osip2
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/osip/include/osipparser2
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/osip/exosip/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/osip/exosip/include/eXosip2
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/osip/exosip/src
)
target_compile_definitions(${target} PRIVATE CONFIG_QUEC_VOIP_SUPPORT=1 VOIP_MEDIA_VERBOSE=0 OSIP_MEM_TRACE=0 HAVE_CLOCK_GETTIME_MONOTONIC=1 HAVE_CTYPE_H=1 HAVE_SYS_TYPES_H=1 HAVE_TIME_H=1 HAVE_STDARG_H=1)
# External libraries are configured separately from the application target.
# Keep the verified Cortex-M3 Thumb ABI and per-function/data sections explicit.
target_compile_options(${target} PRIVATE
    -mcpu=cortex-m3 -mthumb -mapcs-frame
    -std=gnu99 -ffunction-sections -fdata-sections -Wno-error
)
target_sources(${target} PRIVATE
    qcm_voip.c
    port/qcm_voip_port.c
    port/qcm_voip_pcm.c
    port/qcm_voip_resample.c
    port/qcm_voip_sdp.c
    vendor/app/qosa_voip.c
    vendor/ortp/src/avprofile.c
    vendor/ortp/src/b64.c
    vendor/ortp/src/congestiondetector.c
    vendor/ortp/src/event.c
    vendor/ortp/src/extremum.c
    vendor/ortp/src/jitterctl.c
    vendor/ortp/src/kalmanrls.c
    vendor/ortp/src/logging.c
    vendor/ortp/src/nack.c
    vendor/ortp/src/netsim.c
    vendor/ortp/src/ortp.c
    vendor/ortp/src/payloadtype.c
    vendor/ortp/src/port.c
    vendor/ortp/src/posixtimer.c
    vendor/ortp/src/rtcp.c
    vendor/ortp/src/rtcp_fb.c
    vendor/ortp/src/rtcp_xr.c
    vendor/ortp/src/rtcpparse.c
    vendor/ortp/src/rtpparse.c
    vendor/ortp/src/rtpprofile.c
    vendor/ortp/src/rtpsession.c
    vendor/ortp/src/rtpsession_inet.c
    vendor/ortp/src/rtpsignaltable.c
    vendor/ortp/src/rtptimer.c
    vendor/ortp/src/scheduler.c
    vendor/ortp/src/sessionset.c
    vendor/ortp/src/str_utils.c
    vendor/ortp/src/telephonyevents.c
    vendor/ortp/src/videobandwidthestimator.c
    vendor/ortp/src/utils.c
    vendor/ortp/mpc/ortp_list.c
    vendor/ortp/mpc/ortp_port.c
    vendor/ortp/mpc/rtp_g711.c
    vendor/osip/osip2/fsm_misc.c
    vendor/osip/osip2/ict.c
    vendor/osip/osip2/ict_fsm.c
    vendor/osip/osip2/ist.c
    vendor/osip/osip2/ist_fsm.c
    vendor/osip/osip2/nict.c
    vendor/osip/osip2/nict_fsm.c
    vendor/osip/osip2/nist.c
    vendor/osip/osip2/nist_fsm.c
    vendor/osip/osip2/osip.c
    vendor/osip/osip2/osip_dialog.c
    vendor/osip/osip2/osip_event.c
    vendor/osip/osip2/osip_time.c
    vendor/osip/osip2/osip_transaction.c
    vendor/osip/osip2/port_condv.c
    vendor/osip/osip2/port_fifo.c
    vendor/osip/osip2/port_sema.c
    vendor/osip/osip2/port_thread.c
    vendor/osip/osipparser2/osip_accept.c
    vendor/osip/osipparser2/osip_accept_encoding.c
    vendor/osip/osipparser2/osip_accept_language.c
    vendor/osip/osipparser2/osip_alert_info.c
    vendor/osip/osipparser2/osip_allow.c
    vendor/osip/osipparser2/osip_authentication_info.c
    vendor/osip/osipparser2/osip_authorization.c
    vendor/osip/osipparser2/osip_body.c
    vendor/osip/osipparser2/osip_call_id.c
    vendor/osip/osipparser2/osip_call_info.c
    vendor/osip/osipparser2/osip_contact.c
    vendor/osip/osipparser2/osip_content_disposition.c
    vendor/osip/osipparser2/osip_content_encoding.c
    vendor/osip/osipparser2/osip_content_length.c
    vendor/osip/osipparser2/osip_content_type.c
    vendor/osip/osipparser2/osip_cseq.c
    vendor/osip/osipparser2/osip_error_info.c
    vendor/osip/osipparser2/osip_from.c
    vendor/osip/osipparser2/osip_header.c
    vendor/osip/osipparser2/osip_list.c
    vendor/osip/osipparser2/osip_md5c.c
    vendor/osip/osipparser2/osip_message.c
    vendor/osip/osipparser2/osip_message_parse.c
    vendor/osip/osipparser2/osip_message_to_str.c
    vendor/osip/osipparser2/osip_mime_version.c
    vendor/osip/osipparser2/osip_parser_cfg.c
    vendor/osip/osipparser2/osip_port.c
    vendor/osip/osipparser2/osip_proxy_authenticate.c
    vendor/osip/osipparser2/osip_proxy_authentication_info.c
    vendor/osip/osipparser2/osip_proxy_authorization.c
    vendor/osip/osipparser2/osip_record_route.c
    vendor/osip/osipparser2/osip_route.c
    vendor/osip/osipparser2/osip_to.c
    vendor/osip/osipparser2/osip_uri.c
    vendor/osip/osipparser2/osip_via.c
    vendor/osip/osipparser2/osip_www_authenticate.c
    vendor/osip/osipparser2/sdp_accessor.c
    vendor/osip/osipparser2/sdp_message.c
    vendor/osip/exosip/src/eXcall_api.c
    vendor/osip/exosip/src/eXconf.c
    vendor/osip/exosip/src/eXinsubscription_api.c
    vendor/osip/exosip/src/eXmessage_api.c
    vendor/osip/exosip/src/eXoptions_api.c
    vendor/osip/exosip/src/eXosip.c
    vendor/osip/exosip/src/eXpublish_api.c
    vendor/osip/exosip/src/eXregister_api.c
    vendor/osip/exosip/src/eXsubscription_api.c
    vendor/osip/exosip/src/eXtl_dtls.c
    vendor/osip/exosip/src/eXtl_tcp.c
    vendor/osip/exosip/src/eXtl_tls.c
    vendor/osip/exosip/src/eXtl_udp.c
    vendor/osip/exosip/src/eXtransport.c
    vendor/osip/exosip/src/eXutils.c
    vendor/osip/exosip/src/inet_ntop.c
    vendor/osip/exosip/src/jauth.c
    vendor/osip/exosip/src/jcall.c
    vendor/osip/exosip/src/jcallback.c
    vendor/osip/exosip/src/jdialog.c
    vendor/osip/exosip/src/jevents.c
    vendor/osip/exosip/src/jnotify.c
    vendor/osip/exosip/src/jpipe.c
    vendor/osip/exosip/src/jpublish.c
    vendor/osip/exosip/src/jreg.c
    vendor/osip/exosip/src/jrequest.c
    vendor/osip/exosip/src/jresponse.c
    vendor/osip/exosip/src/jsubscribe.c
    vendor/osip/exosip/src/milenage.c
    vendor/osip/exosip/src/misc.c
    vendor/osip/exosip/src/rijndael.c
    vendor/osip/exosip/src/sdp_offans.c
    vendor/osip/exosip/src/udp.c
)
target_link_libraries(${target} PRIVATE qcm_audio socket_adapter)

# Legacy vendor warnings stay visible; new UniRTOS boundary code keeps -Werror.
set_source_files_properties(qcm_voip.c port/qcm_voip_port.c port/qcm_voip_pcm.c PROPERTIES COMPILE_OPTIONS "-Werror")
set_source_files_properties(port/qcm_voip_resample.c port/qcm_voip_sdp.c PROPERTIES COMPILE_OPTIONS "-Werror")
