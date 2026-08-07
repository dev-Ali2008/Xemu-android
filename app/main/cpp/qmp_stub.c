#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-cxl.h"
#include "qapi/qapi-commands-dump.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-machine-s390x.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-commands-misc-arm.h"
#include "qapi/qapi-commands-misc-i386.h"
#include "qapi/qapi-commands-rocker.h"
#include "qapi/qapi-commands-trace.h"
#include "qapi/qapi-commands-virtio.h"
#include "qapi/qapi-types-cxl.h"
#include "qapi/qapi-types-dump.h"
#include "qapi/qapi-types-machine.h"
#include "qapi/qapi-types-machine-s390x.h"
#include "qapi/qapi-types-misc-arm.h"
#include "qapi/qapi-types-misc-i386.h"
#include "qapi/qapi-types-rocker.h"
#include "qapi/qapi-types-trace.h"
#include "qapi/qapi-types-virtio.h"
#include "block/nbd.h"

static void set_not_supported(Error **errp, const char *name)
{
    error_setg(errp, "%s not supported on Android build", name);
}

SevInfo *qmp_query_sev(Error **errp)
{
    set_not_supported(errp, "query-sev");
    return NULL;
}

SevLaunchMeasureInfo *qmp_query_sev_launch_measure(Error **errp)
{
    set_not_supported(errp, "query-sev-launch-measure");
    return NULL;
}

SevCapability *qmp_query_sev_capabilities(Error **errp)
{
    set_not_supported(errp, "query-sev-capabilities");
    return NULL;
}

void qmp_sev_inject_launch_secret(const char *packet_header, const char *secret,
                                  bool has_gpa, uint64_t gpa, Error **errp)
{
    (void)packet_header;
    (void)secret;
    (void)has_gpa;
    (void)gpa;
    set_not_supported(errp, "sev-inject-launch-secret");
}

SevAttestationReport *qmp_query_sev_attestation_report(const char *mnonce,
                                                       Error **errp)
{
    (void)mnonce;
    set_not_supported(errp, "query-sev-attestation-report");
    return NULL;
}

EvtchnInfoList *qmp_xen_event_list(Error **errp)
{
    set_not_supported(errp, "xen-event-list");
    return NULL;
}

void qmp_xen_event_inject(uint32_t port, Error **errp)
{
    (void)port;
    set_not_supported(errp, "xen-event-inject");
}

VirtioInfoList *qmp_x_query_virtio(Error **errp)
{
    set_not_supported(errp, "x-query-virtio");
    return NULL;
}

VirtioStatus *qmp_x_query_virtio_status(const char *path, Error **errp)
{
    (void)path;
    set_not_supported(errp, "x-query-virtio-status");
    return NULL;
}

VirtQueueStatus *qmp_x_query_virtio_queue_status(const char *path, uint16_t queue,
                                                 Error **errp)
{
    (void)path;
    (void)queue;
    set_not_supported(errp, "x-query-virtio-queue-status");
    return NULL;
}

VirtVhostQueueStatus *qmp_x_query_virtio_vhost_queue_status(const char *path,
                                                            uint16_t queue,
                                                            Error **errp)
{
    (void)path;
    (void)queue;
    set_not_supported(errp, "x-query-virtio-vhost-queue-status");
    return NULL;
}

void qmp_cxl_inject_general_media_event(const char *path, CxlEventLog log,
                                        uint8_t flags, uint64_t dpa,
                                        uint8_t descriptor, uint8_t type,
                                        uint8_t transaction_type,
                                        bool has_channel, uint8_t channel,
                                        bool has_rank, uint8_t rank,
                                        bool has_device, uint32_t device,
                                        const char *component_id,
                                        Error **errp)
{
    (void)path;
    (void)log;
    (void)flags;
    (void)dpa;
    (void)descriptor;
    (void)type;
    (void)transaction_type;
    (void)has_channel;
    (void)channel;
    (void)has_rank;
    (void)rank;
    (void)has_device;
    (void)device;
    (void)component_id;
    set_not_supported(errp, "cxl-inject-general-media-event");
}

void qmp_cxl_inject_dram_event(const char *path, CxlEventLog log, uint8_t flags,
                               uint64_t dpa, uint8_t descriptor, uint8_t type,
                               uint8_t transaction_type, bool has_channel,
                               uint8_t channel, bool has_rank, uint8_t rank,
                               bool has_nibble_mask, uint32_t nibble_mask,
                               bool has_bank_group, uint8_t bank_group,
                               bool has_bank, uint8_t bank, bool has_row,
                               uint32_t row, bool has_column, uint16_t column,
                               bool has_correction_mask,
                               uint64List *correction_mask, Error **errp)
{
    (void)path;
    (void)log;
    (void)flags;
    (void)dpa;
    (void)descriptor;
    (void)type;
    (void)transaction_type;
    (void)has_channel;
    (void)channel;
    (void)has_rank;
    (void)rank;
    (void)has_nibble_mask;
    (void)nibble_mask;
    (void)has_bank_group;
    (void)bank_group;
    (void)has_bank;
    (void)bank;
    (void)has_row;
    (void)row;
    (void)has_column;
    (void)column;
    (void)has_correction_mask;
    (void)correction_mask;
    set_not_supported(errp, "cxl-inject-dram-event");
}

void qmp_cxl_inject_memory_module_event(const char *path, CxlEventLog log,
                                        uint8_t flags, uint8_t type,
                                        uint8_t health_status,
                                        uint8_t media_status,
                                        uint8_t additional_status,
                                        uint8_t life_used, int16_t temperature,
                                        uint32_t dirty_shutdown_count,
                                        uint32_t corrected_volatile_error_count,
                                        uint32_t corrected_persistent_error_count,
                                        Error **errp)
{
    (void)path;
    (void)log;
    (void)flags;
    (void)type;
    (void)health_status;
    (void)media_status;
    (void)additional_status;
    (void)life_used;
    (void)temperature;
    (void)dirty_shutdown_count;
    (void)corrected_volatile_error_count;
    (void)corrected_persistent_error_count;
    set_not_supported(errp, "cxl-inject-memory-module-event");
}

void qmp_cxl_inject_poison(const char *path, uint64_t start, uint64_t length,
                           Error **errp)
{
    (void)path;
    (void)start;
    (void)length;
    set_not_supported(errp, "cxl-inject-poison");
}

void qmp_cxl_inject_uncorrectable_errors(const char *path,
                                         CXLUncorErrorRecordList *errors,
                                         Error **errp)
{
    (void)path;
    (void)errors;
    set_not_supported(errp, "cxl-inject-uncorrectable-errors");
}

void qmp_cxl_inject_correctable_error(const char *path, CxlCorErrorType type,
                                      Error **errp)
{
    (void)path;
    (void)type;
    set_not_supported(errp, "cxl-inject-correctable-error");
}

void qmp_cxl_add_dynamic_capacity(const char *path, uint16_t host_id,
                                  CxlExtentSelectionPolicy selection_policy,
                                  uint8_t region, const char *tag,
                                  CxlDynamicCapacityExtentList *extents,
                                  Error **errp)
{
    (void)path;
    (void)host_id;
    (void)selection_policy;
    (void)region;
    (void)tag;
    (void)extents;
    set_not_supported(errp, "cxl-add-dynamic-capacity");
}

void qmp_cxl_release_dynamic_capacity(const char *path, uint16_t host_id,
                                      CxlExtentRemovalPolicy removal_policy,
                                      bool has_forced_removal,
                                      bool forced_removal,
                                      bool has_sanitize_on_release,
                                      bool sanitize_on_release, uint8_t region,
                                      const char *tag,
                                      CxlDynamicCapacityExtentList *extents,
                                      Error **errp)
{
    (void)path;
    (void)host_id;
    (void)removal_policy;
    (void)has_forced_removal;
    (void)forced_removal;
    (void)has_sanitize_on_release;
    (void)sanitize_on_release;
    (void)region;
    (void)tag;
    (void)extents;
    set_not_supported(errp, "cxl-release-dynamic-capacity");
}

int coroutine_fn nbd_co_do_establish_connection(BlockDriverState *bs,
                                                bool blocking, Error **errp)
{
    (void)bs;
    (void)blocking;
    set_not_supported(errp, "nbd-do-establish-connection");
    return -ENOTSUP;
}

VirtioQueueElement *qmp_x_query_virtio_queue_element(const char *path,
                                                     uint16_t queue,
                                                     bool has_index,
                                                     uint16_t index,
                                                     Error **errp)
{
    (void)path;
    (void)queue;
    (void)has_index;
    (void)index;
    set_not_supported(errp, "x-query-virtio-queue-element");
    return NULL;
}

GICCapabilityList *qmp_query_gic_capabilities(Error **errp)
{
    set_not_supported(errp, "query-gic-capabilities");
    return NULL;
}

void qmp_set_cpu_topology(uint16_t core_id,
                          bool has_socket_id, uint16_t socket_id,
                          bool has_book_id, uint16_t book_id,
                          bool has_drawer_id, uint16_t drawer_id,
                          bool has_entitlement, S390CpuEntitlement entitlement,
                          bool has_dedicated, bool dedicated,
                          Error **errp)
{
    (void)core_id;
    (void)has_socket_id;
    (void)socket_id;
    (void)has_book_id;
    (void)book_id;
    (void)has_drawer_id;
    (void)drawer_id;
    (void)has_entitlement;
    (void)entitlement;
    (void)has_dedicated;
    (void)dedicated;
    set_not_supported(errp, "set-cpu-topology");
}

CpuPolarizationInfo *qmp_query_s390x_cpu_polarization(Error **errp)
{
    set_not_supported(errp, "query-s390x-cpu-polarization");
    return NULL;
}

HvBalloonInfo *qmp_query_hv_balloon_status_report(Error **errp)
{
    set_not_supported(errp, "query-hv-balloon-status-report");
    return NULL;
}

FirmwareLog *qmp_query_firmware_log(bool has_max_size, uint64_t max_size,
                                    Error **errp)
{
    (void)has_max_size;
    (void)max_size;
    set_not_supported(errp, "query-firmware-log");
    return NULL;
}

CpuModelCompareInfo *qmp_query_cpu_model_comparison(CpuModelInfo *modela,
                                                    CpuModelInfo *modelb,
                                                    Error **errp)
{
    (void)modela;
    (void)modelb;
    set_not_supported(errp, "query-cpu-model-comparison");
    return NULL;
}

CpuModelBaselineInfo *qmp_query_cpu_model_baseline(CpuModelInfo *modela,
                                                   CpuModelInfo *modelb,
                                                   Error **errp)
{
    (void)modela;
    (void)modelb;
    set_not_supported(errp, "query-cpu-model-baseline");
    return NULL;
}

TraceEventInfoList *qmp_trace_event_get_state(const char *name, Error **errp)
{
    (void)name;
    set_not_supported(errp, "trace-event-get-state");
    return NULL;
}

void qmp_trace_event_set_state(const char *name, bool enable,
                               bool has_ignore_unavailable,
                               bool ignore_unavailable, Error **errp)
{
    (void)name;
    (void)enable;
    (void)has_ignore_unavailable;
    (void)ignore_unavailable;
    set_not_supported(errp, "trace-event-set-state");
}

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
    (void)enable;
    set_not_supported(errp, "xen-set-global-dirty-log");
}

RockerSwitch *qmp_query_rocker(const char *name, Error **errp)
{
    (void)name;
    set_not_supported(errp, "query-rocker");
    return NULL;
}

RockerPortList *qmp_query_rocker_ports(const char *name, Error **errp)
{
    (void)name;
    set_not_supported(errp, "query-rocker-ports");
    return NULL;
}

RockerOfDpaFlowList *qmp_query_rocker_of_dpa_flows(const char *name,
                                                   bool has_tbl_id,
                                                   uint32_t tbl_id,
                                                   Error **errp)
{
    (void)name;
    (void)has_tbl_id;
    (void)tbl_id;
    set_not_supported(errp, "query-rocker-of-dpa-flows");
    return NULL;
}

RockerOfDpaGroupList *qmp_query_rocker_of_dpa_groups(const char *name,
                                                     bool has_type,
                                                     uint8_t type,
                                                     Error **errp)
{
    (void)name;
    (void)has_type;
    (void)type;
    set_not_supported(errp, "query-rocker-of-dpa-groups");
    return NULL;
}

void qmp_dump_guest_memory(bool paging, const char *protocol,
                           bool has_detach, bool detach,
                           bool has_begin, int64_t begin,
                           bool has_length, int64_t length,
                           bool has_format, DumpGuestMemoryFormat format,
                           Error **errp)
{
    (void)paging;
    (void)protocol;
    (void)has_detach;
    (void)detach;
    (void)has_begin;
    (void)begin;
    (void)has_length;
    (void)length;
    (void)has_format;
    (void)format;
    set_not_supported(errp, "dump-guest-memory");
}

DumpQueryResult *qmp_query_dump(Error **errp)
{
    set_not_supported(errp, "query-dump");
    return NULL;
}

DumpGuestMemoryCapability *qmp_query_dump_guest_memory_capability(Error **errp)
{
    set_not_supported(errp, "query-dump-guest-memory-capability");
    return NULL;
}
