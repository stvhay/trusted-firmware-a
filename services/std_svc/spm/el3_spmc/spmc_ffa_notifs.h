/*
 * Copyright (C) 2021 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef __SPMC_FFA_NOTIFS_H
#define __SPMC_FFA_NOTIFS_H

#include <stdint.h>


int spmc_ffa_notifications_init_per_pe(void);
uintptr_t spmc_ffa_notification_bitmap_create(uint32_t w1, uint32_t w2,
                                              void *ns_ctx);
uintptr_t spmc_ffa_notification_bitmap_destroy(uint32_t w1, void *ns_ctx);
uintptr_t spmc_ffa_notification_bind(uint32_t w1, uint32_t w2,
                                     uint32_t w3, uint32_t w4,
                                     void *ns_ctx);
uintptr_t spmc_ffa_notification_unbind(uint32_t w1, uint32_t w3, uint32_t w4,
                                       void *ns_ctx);
uintptr_t spmc_ffa_notification_set(uint32_t w1, uint32_t w2,
                                    uint32_t w3, uint32_t w4,
                                    void *ns_ctx);
uintptr_t spmc_ffa_notification_get(uint32_t w1, uint32_t w2, void *ns_ctx);
uintptr_t spmc_ffa_notification_info_get(void *ns_ctx);
uintptr_t spmc_ffa_features_schedule_receiver_int(void *ns_ctx);

#endif  /* __SPMC_FFA_NOTIFS_H */
