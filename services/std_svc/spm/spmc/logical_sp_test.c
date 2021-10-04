#include <common/debug.h>
#include <smccc_helpers.h>
#include <services/ffa_svc.h>
#include <services/logical_sp.h>

#define LP_PARTITION_ID 0xC000
#define LP_UUID {0x0, 0x0, 0x0, 0x12}

static int64_t sp_init(void) {
        // TODO: Do some initialisation.
        INFO("LSP: Init function called.\n");
        return 0;
}

static uint64_t handle_ffa_direct_request(uint32_t smc_fid,  bool secure_origin, uint64_t x1, uint64_t x2,
                                uint64_t x3, uint64_t x4, void *cookie, void *handle, uint64_t flags) {
        uint64_t ret;

        /* Determine if we have a 64 or 32 direct request. */
        if (smc_fid == FFA_MSG_SEND_DIRECT_REQ_SMC32) {
                ret = FFA_MSG_SEND_DIRECT_RESP_SMC32;
        } else if (smc_fid == FFA_MSG_SEND_DIRECT_REQ_SMC64) {
                ret = FFA_MSG_SEND_DIRECT_RESP_SMC64;
        }
        else {
                panic(); // Unknown SMC
        }
        /* TODO: Do something with the request. - Echo only*/
	INFO("Logical Partition: Received Direct Request from %s world!\n", secure_origin ? "Secure" : "Normal");

        /* SP's must always respond to their calls so we can populate our response directly. */
        SMC_RET8(handle, ret, 0, 0, x4, 0, 0, 0, 0);
}

/* Register logical partition  */
DECLARE_LOGICAL_PARTITION(
        my_logical_partition,
        sp_init,                   // Init Function
        LP_PARTITION_ID,           // FFA Partition ID
        LP_UUID,                   // UUID
        0x1,                       // Partition Properties. Can only receive direct messages.
        handle_ffa_direct_request  // Callback for direct requests.
);
