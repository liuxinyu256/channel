/********************************** (C) COPYRIGHT *******************************
 * File Name          : phy_uart1.h
 * Description        : CH579 UART1 RS485 物理层驱动
 *******************************************************************************/

#ifndef PHY_UART1_H
#define PHY_UART1_H

#include "phy.h"

struct bus_controller;
phy_driver_t *phy_uart1_create(struct bus_controller *bus);

#endif
