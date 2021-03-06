/*
 *  Hardware entropy collector for M487 RNG
 *
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "NuMicro.h"

/*
 * Get Random number generator.
 */
 #define PRNG_KEY_SIZE  (0x20UL)
 
static volatile int  g_PRNG_done;
volatile int  g_AES_done;


#define SNUM        32       /* recorded number of lastest samples */


static uint32_t   adc_val[SNUM];
static uint32_t   val_sum;
static int        oldest;

#ifdef __ICCARM__
#define __inline   inline
#endif

static __inline uint32_t  get_adc_bg_val()
{
    EADC->SWTRG = (1 << 16);      //Trigger Band-gap
    while ((EADC->STATUS1 & 1) != 1);
    return (EADC->DAT[16] & 0xFFF);
}

int  adc_trng_gen_bit()
{
    uint32_t   new_val, average;
    int        ret_val;
    int ref_bg_ub = ((1.2/3.3)*4095) + 50;
    int ref_bg_lb = ((1.2/3.3)*4095) - 50;
    
    do{
      new_val = get_adc_bg_val();
    } while( (new_val > ref_bg_ub) || (new_val < ref_bg_lb) );

    average = (val_sum / SNUM);   /* sum divided by 32 */

    if (average >= new_val)
        ret_val = 1;
    else
        ret_val = 0;

    //printf("%d - sum = 0x%x, avg = 0x%x, new = 0x%x\n", oldest, val_sum, average, new_val);

    /* kick-off the oldest one and insert the new one */
    val_sum -= adc_val[oldest];
    val_sum += new_val;
    adc_val[oldest] = new_val;
    oldest = (oldest + 1) % SNUM;

    return ret_val;
}


uint32_t  adc_trng_gen_rnd()
{
    int       i;
    uint32_t  val32;

    val32 = 0;
    for (i = 31; i >= 0; i--)
        val32 |= (adc_trng_gen_bit() << i);
    //printf("### %s: 0x%x \n", __FUNCTION__, val32);

    return val32;
}

void  init_adc_init()
{
    static uint8_t init_flag = FALSE;
    int    i;
    
    if( init_flag ) return;
  
    /* ADC refernce external 1.2V */
    /* Unlock protected registers */
    //SYS_UnlockReg();    
    //SYS->VREFCTL = SYS_VREFCTL_VREF_PIN;
    /* Lock protected registers */
    //SYS_LockReg();
  
    /* Enable EADC clock */
    CLK->APBCLK0 |= CLK_APBCLK0_EADCCKEN_Msk;

    /* Set EADC clock divider */
    CLK->CLKDIV0 &= ~CLK_CLKDIV0_EADCDIV_Msk;
    CLK->CLKDIV0 |= (5 << CLK_CLKDIV0_EADCDIV_Pos);

    EADC->CTL = (0x3 << EADC_CTL_RESSEL_Pos) | EADC_CTL_ADCEN_Msk;        /* A/D Converter Enable, select 12-bit ADC result  */

    while (!(EADC->PWRM & EADC_PWRM_PWUPRDY_Msk));

    EADC->SCTL[16] = (0x70 << EADC_SCTL_EXTSMPT_Pos)  /* ADC Sampling Time Extend          */
                     | (0x0 << EADC_SCTL_TRGSEL_Pos);      /* A/D SAMPLE Start of Conversion Trigger Source Selection */

    val_sum = 0;
    for (i = 0; i < SNUM; i++)
    {
        adc_val[i] = get_adc_bg_val();
        val_sum += adc_val[i];
    }
    oldest = 0;
    init_flag = TRUE;
    adc_trng_gen_rnd();    // drop the first 32-bits
}

void CRYPTO_IRQHandler()
{
    if (PRNG_GET_INT_FLAG(CRPT)) {
        g_PRNG_done = 1;
        PRNG_CLR_INT_FLAG(CRPT);
    } else	if (AES_GET_INT_FLAG(CRPT)) {
        g_AES_done = 1;
        AES_CLR_INT_FLAG(CRPT);
    }

} 

static void trng_get(unsigned char *pConversionData)
{
	uint32_t *p32ConversionData;
    uint32_t u32val;
  
	p32ConversionData = (uint32_t *)pConversionData;
	
    /* Unlock protected registers */
    SYS_UnlockReg();	
    /* Enable IP clock */
    CLK_EnableModuleClock(CRPT_MODULE);
	
    /* Lock protected registers */
    SYS_LockReg();	
	
    NVIC_EnableIRQ(CRPT_IRQn);
    PRNG_ENABLE_INT(CRPT);
	
    u32val = adc_trng_gen_rnd();
    //printf("=== %s: 0x%x \n", __FUNCTION__, u32val);
    PRNG_Open(CRPT, PRNG_KEY_SIZE_256, 1, u32val); //adc_trng_gen_rnd());

    PRNG_Start(CRPT);
    while (!g_PRNG_done);

    PRNG_Read(CRPT, p32ConversionData);

//    printf("    0x%08x  0x%08x  0x%08x  0x%08x\n\r", *p32ConversionData, *(p32ConversionData+1), *(p32ConversionData+2), *(p32ConversionData+3));
//    printf("    0x%08x  0x%08x  0x%08x  0x%08x\n\r", *(p32ConversionData+4), *(p32ConversionData+5), *(p32ConversionData+6), *(p32ConversionData+7));

    PRNG_DISABLE_INT(CRPT);
///    NVIC_DisableIRQ(CRPT_IRQn);
 //    CLK_DisableModuleClock(CRPT_MODULE);
		
}


/*
 * Get len bytes of entropy from the hardware RNG.
 */
 
int mbedtls_hardware_poll( void *data,
                    unsigned char *output, size_t len, size_t *olen )
{
#if 0
    unsigned long timer = xTaskGetTickCount();
	  ((void) data);
    *olen = 0;
 
    if( len < sizeof(unsigned long) )
        return( 0 );
 
    memcpy( output, &timer, sizeof(unsigned long) );
    *olen = sizeof(unsigned long);
#else
    unsigned char tmpBuff[PRNG_KEY_SIZE];
    size_t cur_length = 0;
    ((void) data);

    init_adc_init();
    while (len >= sizeof(tmpBuff)) {
        trng_get(output);
        output += sizeof(tmpBuff);
        cur_length += sizeof(tmpBuff);
        len -= sizeof(tmpBuff);
    }
    if (len > 0) {
        trng_get(tmpBuff);
        memcpy(output, tmpBuff, len);
        cur_length += len;
    }
    *olen = cur_length;
#endif
	
    return( 0 );
}
 
#if 1
uint32_t numaker_ulRand( void )
{
	unsigned char tmpBuff[PRNG_KEY_SIZE];
    init_adc_init();
	trng_get(tmpBuff);
	return *((uint32_t*)tmpBuff);
}
#endif

