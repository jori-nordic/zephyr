/*
 * Copyright (c) 2023 Pawel Osypiuk <pawelosyp@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_BLUETOOTH_HCI_NRF53_SUPPORT_H_
#define ZEPHYR_DRIVERS_BLUETOOTH_HCI_NRF53_SUPPORT_H_

/**
 * @brief Forces off the cpunet.
 */
void cpunet_forceoff(void);
/**
 * @brief Forces on the cpunet.
 */
void cpunet_forceon(void);

#endif /* ZEPHYR_DRIVERS_BLUETOOTH_HCI_NRF53_SUPPORT_H_ */
