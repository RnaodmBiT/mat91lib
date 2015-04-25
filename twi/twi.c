/** @file   twi.c
    @author M. P. Hayes
    @date   25 April 2015
    @brief  TWI routines for AT91 processors
*/


#include "twi.h"
#include "pio.h"
#include "mcu.h"
#include "bits.h"
#include "delay.h"


#define TWI_DEVICES_NUM 2

static twi_dev_t twi_devices[TWI_DEVICES_NUM];


twi_t 
twi_init (const twi_cfg_t *cfg)
{
    twi_t twi;
    
    twi = &twi_devices[cfg->channel];
    
    switch (cfg->channel)
    {
    case TWI_CHANNEL_0:
        twi->base = TWI0;
        break;

    case TWI_CHANNEL_1:
        twi->base = TWI1;
        break;

    default:
        return 0;
    }

    /* Enable TWIx peripheral clock.  */
    mcu_pmc_enable (ID_TWI0 + cfg->channel);
    
    twi = &twi_devices[cfg->channel];

    twi->base->TWI_CWGR = cfg->period;

    return twi;
}


static twi_ret_t
twi_write_wait_ack (twi_t twi)
{
    uint32_t status;
    uint32_t retries = 100;

    while (retries)
    {
        status = twi->base->TWI_SR;

        if (status & TWI_SR_NACK)
            return TWI_ERROR_NO_ACK;

        if (status & TWI_SR_TXRDY)
            return TWI_OK;

        DELAY_US (1);
    }
    return TWI_ERROR_TIMEOUT;
}


static twi_ret_t
twi_read_wait_ack (twi_t twi)
{
    uint32_t status;
    uint32_t retries = 100;

    while (retries)
    {
        status = twi->base->TWI_SR;

        if (status & TWI_SR_NACK)
            return TWI_ERROR_NO_ACK;

        if (status & TWI_SR_RXRDY)
            return TWI_OK;

        DELAY_US (1);
    }
    return TWI_ERROR_TIMEOUT;
}


twi_ret_t
twi_master_write (twi_t twi, twi_id_t slave_addr,
                  twi_id_t addr, uint8_t addr_size,
                  void *buffer, uint8_t size)
{
    uint8_t i;
    uint8_t *data = buffer;
    twi_ret_t ret;

    twi->base->TWI_MMR = TWI_MMR_DADR (slave_addr)
        | BITS (addr_size, 8, 9);

    twi->base->TWI_IADR = addr;

    /* Switch to master mode.  */
    twi->base->TWI_CR = TWI_CR_MSDIS | TWI_CR_MSEN;

    twi->base->TWI_CR = TWI_CR_START;

    /* A START command is sent followed by the slave address and the
       optional internal address.  This initiated by writing to THR.
       Each of the sent bytes needs to be acknowledged by the slave.
       There are two error scenarios 1) another master transmits at
       the same time with a higher priority 2) no slave responds to
       the desired address
    */

    for (i = 0; i < size; i++)
    {
    
        twi->base->TWI_THR = *data++;
        if (i == size - 1)
            twi->base->TWI_CR = TWI_CR_STOP;
            
        ret = twi_write_wait_ack (twi);
        if (ret < 0)
        {
            twi->base->TWI_CR = TWI_CR_STOP;
            return ret;
        }
    }

    return i;
}


twi_ret_t
twi_master_read (twi_t twi, twi_id_t slave_addr,
                 twi_id_t addr, uint8_t addr_size,
                 void *buffer, uint8_t size)
{
    uint8_t i;
    uint8_t *data = buffer;
    twi_ret_t ret;

    twi->base->TWI_MMR = TWI_MMR_DADR (slave_addr)
        | BITS (addr_size, 8, 9) | TWI_MMR_MREAD;

    twi->base->TWI_IADR = addr;

    if (size == 1)
        twi->base->TWI_CR = TWI_CR_START;
    else
        twi->base->TWI_CR = TWI_CR_START | TWI_CR_STOP;

    /* The slave address and optional internal address is sent. 
       Each sent byte should be acknowledged.  */

    for (i = 0; i < size; i++)
    {
        ret = twi_read_wait_ack (twi);
        if (ret < 0)
        {
            twi->base->TWI_CR = TWI_CR_STOP;
            return ret;
        }

        *data++ = twi->base->TWI_RHR;

        /* Need to set STOP prior to reading last byte.  */
        if ((i != 0) && (i == size - 1))
            twi->base->TWI_CR = TWI_CR_STOP;
    }

    return i;
}
