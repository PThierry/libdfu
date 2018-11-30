#include "autoconf.h"
#include "api/types.h"
#include "api/syscall.h"
#include "api/print.h"
#include "api/dfu.h"
#include "usb.h"
#include "dfu_desc.h"
#if 0
#include "dummy_crypt.h"
#include "dummy_flash.h"
#include "stm32f4xx.h"
#include "stm32f4xx_nvic.h"
#include "cortex_m4_systick.h"
#endif
#include "usb_control.h"
#include "queue.h"

//#define TIMER 0
/* FIXME should be replaced by another way... */
#if TIMER
#include "stm32f4xx_timer.h"
#include "stm32f4xx_timer_regs.h"
#endif

/* FIXME: should be get back from USB driver */
#define MAX_TIME_DETACH     4000

/* FIXME dummy */
uint8_t read_firmware_data_cmd = 0;
uint8_t read_firmware_data_done = 0;

dfu_functional_descriptor_t dfu_fct_desc = {
	.bLength = 0x09,
	.bDescriptorType = 0x21,
	.bmAttributes.bitWillDetach = 0,
	.bmAttributes.bitManifestationTolerant = 1,
#ifdef DEBUG_LVL
	.bmAttributes.bitCanUpload = 1,
#else
    .bmAttributes.bitCanUpload = 0,
#endif
	.bmAttributes.bitCanDnload = 1,
	.wDetachTimeOut = MAX_TIME_DETACH,
	.wTransferSize = MAX_TRANSFERT_SIZE,
	.bcdDFUVersion = 0x0101
};

//#define NOT_TIMER5

uint8_t data_out_buffer[MAX_TRANSFERT_SIZE];
uint8_t data_in_buffer[MAX_TRANSFERT_SIZE];

dfu_context_t dfu_context = {0};
void dfu_init_context(void){
        dfu_context.block_in_progress = 0;
        dfu_context.status = OK;
        dfu_context.state = DFUIDLE;
        dfu_context.data_out_buffer = (uint8_t**)&data_out_buffer;
        dfu_context.data_out_current_block_nb = 0;
        dfu_context.data_out_nb_blocks = 0;
        dfu_context.data_out_length = 0;
        dfu_context.data_in_buffer = (uint8_t**)&data_in_buffer;
        dfu_context.data_in_nb_blocks = 0;
        dfu_context.data_in_current_block_nb = 0;
        dfu_context.data_in_length = 0;
        dfu_context.flash_address = 0x80000000;
        dfu_context.detach_timeout_ms = MAX_TIME_DETACH;
        dfu_context.detach_timeout_start = 0;
        dfu_context.poll_timeout_ms = MAX_POLL_TIMEOUT;
        dfu_context.poll_start = 0;
        dfu_context.block_size = MAX_TRANSFERT_SIZE;
        dfu_context.firmware_size = 0;
}

dfu_context_t * dfu_ctx = &dfu_context;

struct queue *dfu_data_in_queue = NULL;
struct queue *dfu_data_out_queue = NULL;


typedef struct {
    uint16_t id;
	uint32_t size;
	uint8_t * data;
} dfu_data_block_t;

static void dfu_release_block(dfu_data_block_t *b){
	if(b != NULL){
        if(b->data != NULL){
#ifdef CONFIG_STD_MALLOC_LIGHT
		    wfree((void**)&b->data);
#else
		    free(b->data);
#endif
        }
#ifdef CONFIG_STD_MALLOC_LIGHT
		wfree((void**)&b);
#else
		free(b);
#endif
	}
	b = NULL;
}


static inline void dfu_set_state(const dfu_state_enum_t new_state) {
    printf("%x => %x\n", dfu_ctx->state, new_state);
    dfu_ctx->state = new_state;
}


static void dfu_prepare_stall(void){
    usb_driver_stall_out(0);
}

static void dfu_functional_desc_request_handler(uint16_t wLength){

    if ( wLength <= 0 ){
        usb_driver_setup_send_status(0);
        usb_driver_setup_read_status();
        return;
    }

    if ( wLength >  dfu_fct_desc.bLength) {
        usb_driver_setup_send((uint8_t *)&dfu_fct_desc, dfu_fct_desc.bLength,0);
    }else{
        usb_driver_setup_send((uint8_t *)&dfu_fct_desc, wLength,0);
    }

    usb_driver_setup_read_status();
}


static uint8_t dfu_validate_suffix(dfu_suffix_t * dfu_suffix){
    return 1;
}


static uint8_t dfu_validate_sec_suffix(dfu_sec_metadata_hdr_t * dfu_sec_metadata_hdr){
    return 1;
}


static uint8_t dfu_validate_memory_policy(uint32_t addr, uint32_t length){
    //printf("\n");
    return 1;
}

#define PROD_CLOCK_APB1 42000000
void dfu_set_poll_timeout(uint32_t t){

    uint64_t ms;
    /*
     *
     * TIM2_CLK = PROD_CLOCK_APB1 * 2 (because APB1_PRESQ != 1)
     * TIM2_INC_FREQ = TIM2_CLK / TIM2_PSC
     *
     * We want max resolution = 1s
     *
     * => PSC_MAX = 65535 = 1s
     * psc = TIM2_CLK / 65535
     * psc = 1267
     *
     * counter = (TIME_IN_MS * TIM2_CLK) / (1000 * psc)
     *
     */

    dfu_ctx->poll_timeout_ms = t;
    sys_get_systick(&ms, PREC_MILLI);
    dfu_ctx->poll_start = ms;

#ifdef TIMER
    uint32_t psc = (PROD_CLOCK_APB1 * 2) / 0xFFFF;
    uint32_t counter = (t * (PROD_CLOCK_APB1 * 2)) / (1000 * psc);

    timer2_set_counter(counter);
    timer2_set_prescaler(psc);
    timer2_set_autoreload(counter);


#if 0
    // for bare metal....
    set_reg_bits(r_CORTEX_M_NVIC_IPR6,(0xF<<28)); /* See PM0214 rev 5 page 214
                                                   * TIM2_IRQn = 28
                                                   * Priority = 0xF
                                                   *
                                                   */

    NVIC_EnableIRQ(TIM2_IRQn);
#endif
    timer2_enable_interrupt();
    timer2_enable();
#endif


    }


uint32_t dfu_get_poll_timeout(void){
    return dfu_ctx->poll_timeout_ms;
}


static inline uint8_t dfu_get_state() {
    return dfu_ctx->state;
}


static inline void dfu_set_status(const dfu_status_enum_t new_status) {
    dfu_ctx->status = new_status;
}

#ifdef TIMER
volatile uint32_t tim5_it = 0;
void TIM2_IRQHandler(uint8_t irq, uint32_t cr, uint32_t sr){
    tim5_it +=1;
    timer2_disable();
    timer2_disable_interrupt();
    write_reg_value(r_CORTEX_M_TIM2CNT,read_reg_value(r_CORTEX_M_TIM2ARR));
    write_reg_value(r_CORTEX_M_TIM2SR,0);
    if (dfu_get_state() == DFUDNBUSY){
        dfu_set_state(DFUDNLOAD_SYNC);
    }
}
#endif


static inline uint8_t dfu_get_status() {
    //printf("\n");
    return dfu_ctx->status;
}

uint8_t dfu_get_status_string_id() {
    return 0;
}

static inline void dfu_error(const dfu_status_enum_t new_status) {
    if(new_status == ERRSTALLEDPKT){
        dfu_prepare_stall();
    }
    if( (dfu_get_state() != APPIDLE) && (dfu_get_state() != APPDETACH) ) {
         dfu_set_state(DFUERROR);
    }
    dfu_set_status(new_status);
}


void dfu_request_detach(struct usb_setup_packet *setup_packet) {
    if( (setup_packet->wLength == 0) && (setup_packet->wValue <= dfu_ctx->detach_timeout_ms) ) {
        /* The Detach request is not meaningful in our case.
         * The DFU mode is started by after system reset depending on
         * the boot mode configuration settings, which means that no other
         * application is running at that time.
         */
        dfu_set_state(APPDETACH);
    } else {
        dfu_error(ERRUNKNOWN);
    }
}


void dfu_request_dnload(struct usb_setup_packet *setup_packet) {
    switch(dfu_get_state()){
        case DFUDNLOAD_IDLE:
            /* fallthrough */
#if GCC_VERSION >= 7
            __attribute__((fallthrough));
#endif
        case DFUIDLE:
            if (dfu_fct_desc.bmAttributes.bitCanDnload != 1 ){
                dfu_error(ERRUNKNOWN);
            }

            if( setup_packet->wLength == 0 ) { /* No data => end of transfert */
                dfu_ctx->data_out_nb_blocks = 0;
                dfu_ctx->data_out_length = 0;
                dfu_ctx->data_out_current_block_nb = 0;
                dfu_ctx->block_in_progress = 0;
                dfu_set_state(DFUMANIFEST_SYNC);
                usb_driver_setup_send_status(0);
            }else if (setup_packet->wLength > MAX_TRANSFERT_SIZE) {
                dfu_ctx->block_in_progress = 0;
                dfu_error(ERRSTALLEDPKT);
            }else{
                dfu_ctx->data_out_current_block_nb;
                dfu_ctx->data_out_nb_blocks = setup_packet->wValue;
                dfu_ctx->data_out_length = setup_packet->wLength;
                dfu_set_state(DFUDNLOAD_SYNC); /* We have data to dl */

                dfu_ctx->block_in_progress = 1;
                usb_driver_setup_send_status(0);
                usb_driver_setup_read(dfu_ctx->data_out_buffer, dfu_ctx->block_size,0);

            }
            break;
        default:
            dfu_error(ERRSTALLEDPKT);
    }

}


static void dfu_data_out_handler(uint32_t size) {

    switch(dfu_get_state()){
        case DFUDNBUSY:
            dfu_error(ERRSTALLEDPKT);
            break;
        case DFUDNLOAD_SYNC: {
            dfu_data_block_t *dfu_current_out_block = NULL;
#ifdef CONFIG_STD_MALLOC_LIGHT
	        wmalloc((void**)&dfu_current_out_block, sizeof(dfu_data_block_t), ALLOC_NORMAL);
#else
	        dfu_current_out_block  = (dfu_data_block_t*)malloc(sizeof(dfu_data_block_t), ALLOC_NORMAL);
#endif
#ifdef CONFIG_STD_MALLOC_LIGHT
	        wmalloc((void**)&dfu_current_out_block->data, MAX_TRANSFERT_SIZE, ALLOC_NORMAL);
#else
	        dfu_current_out_block->data  = (uint8_t*)malloc(MAX_TRANSFERT_SIZE);
#endif
            dfu_current_out_block->id = dfu_ctx->data_out_current_block_nb;
            dfu_current_out_block->size = size;
            memcpy(dfu_current_out_block->data, dfu_ctx->data_out_buffer, size);
            queue_enqueue(dfu_data_out_queue, dfu_current_out_block);
            dfu_ctx->data_out_current_block_nb += 1;
            dfu_ctx->block_in_progress = 0;
            dfu_ctx->poll_start = 0;
            break;
        }
        default:
            dfu_error(ERRSTALLEDPKT);
    }
}

void dfu_request_upload(struct usb_setup_packet *setup_packet) {
    switch(dfu_get_state()){
        case DFUUPLOAD_IDLE:
            /* fallthrough */
#if GCC_VERSION >= 7
            __attribute__((fallthrough));
#endif
        case DFUIDLE:
            if (dfu_fct_desc.bmAttributes.bitCanUpload != 1 ){
                dfu_error(ERRUNKNOWN);
            }

            if( setup_packet->wLength == 0 ) { /* No data => end of transfert */
                dfu_ctx->data_in_nb_blocks = 0;
                dfu_ctx->data_in_length = 0;
                dfu_ctx->data_in_current_block_nb = 0;
                dfu_ctx->block_in_progress = 0;
                dfu_set_state(DFUIDLE);
                usb_driver_setup_send_status(0);
            }else if (setup_packet->wLength > MAX_TRANSFERT_SIZE) {
                dfu_error(ERRSTALLEDPKT);
            }else{

                //FIXME
                read_firmware_data_cmd = 1;
            }
            break;
        default:
            dfu_error(ERRSTALLEDPKT);
    }
}

void dfu_request_getstatus(struct usb_setup_packet *setup_packet) {
    if( (setup_packet->wValue == 0) && (setup_packet->wLength == 6) ) {
        switch( dfu_get_state() ) {
	        case DFUDNLOAD_SYNC:
                if ((dfu_ctx->block_in_progress == 1) || (queue_available_space(dfu_data_out_queue) == 0)){
                    dfu_set_poll_timeout(MAX_POLL_TIMEOUT);
                    dfu_set_state(DFUDNBUSY);      /* Block transfert still in progress */
                }else{
                    dfu_set_state(DFUDNLOAD_IDLE); /* Block transgert complete */
                }
                break;

            case DFUMANIFEST_SYNC:
                dfu_set_state(DFUIDLE);
                break;

            default:
                break;
        }

        char data[6];
        uint32_t poll_timeout = dfu_get_poll_timeout();
        data[0] = dfu_get_status();
        data[1] = (poll_timeout >>  0) & 0xFF;
        data[2] = (poll_timeout >>  8) & 0xFF;
        data[3] = (poll_timeout >> 16) & 0xFF;
        data[4] = dfu_get_state();
        data[5] = dfu_get_status_string_id();
#if 0
        /* FIXME REMOVE ME When timeout done */
	    if(dfu_get_state()==DFUDNBUSY){
	        dfu_set_state(DFUDNLOAD_SYNC);
        }
	    /* END FIXME */
#endif
        usb_driver_setup_send(data, sizeof(data),0);
        usb_driver_setup_read_status();

    } else {
        dfu_error(ERRUNKNOWN);
    }
}

void dfu_request_clrstatus(struct usb_setup_packet *setup_packet) {
    if( (setup_packet->wValue == 0) && (setup_packet->wLength == 0) ) {
        if( dfu_get_state() == DFUERROR ) {
            dfu_init_context();
            usb_driver_setup_send_status(0);

        } else {
           dfu_error(ERRUNKNOWN);
        }

    } else {
        dfu_error(ERRUNKNOWN);
    }
}

void dfu_request_getstate(struct usb_setup_packet *setup_packet) {
    if( (setup_packet->wValue == 0) && (setup_packet->wLength == 1) ) {
        char data;
        /* DFUDNLOAD_SYNC */
        data = dfu_get_state();
        usb_driver_setup_send((const void*)&data, sizeof(data),0);
        usb_driver_setup_read_status();

    } else {
        dfu_error(ERRUNKNOWN);
    }
}

void dfu_request_abort(struct usb_setup_packet *setup_packet) {
    if( (setup_packet->wValue == 0) && (setup_packet->wLength == 0) ) {
        if( dfu_get_state() != DFUERROR ) {
            dfu_set_state(DFUIDLE);
            usb_driver_setup_send_status(0);
        } else {
            dfu_error(ERRUNKNOWN);
        }

    } else {
        dfu_error(ERRUNKNOWN);
    }
}


static void dfu_class_request_handler(struct usb_setup_packet *setup_packet){
    unsigned long long now;
    sys_get_systick(&now, PREC_MILLI);
    if (dfu_get_state() == DFUDNBUSY){

        if (((now - dfu_ctx->poll_start ) >= dfu_ctx->poll_timeout_ms)){
        //printf("DNLOAD TIMEOUT block_in_progress: %d, tick: %lld dfu_ctx->poll_start+tick: %lld => Stalling\n",dfu_ctx->block_in_progress, now, dfu_ctx->poll_start + now);
            dfu_set_state(DFUDNLOAD_SYNC);
        }else{
            dfu_error(ERRSTALLEDPKT);
            return;
        }
    }
    //printf("tick: %lld dfu_ctx->poll_start+tick: %lld\n", now, dfu_ctx->poll_start + now);
    switch( setup_packet->bRequest ) {
        case USB_RQST_DFU_DETACH:
            dfu_request_detach(setup_packet);
            break;

        case USB_RQST_DFU_DNLOAD:
            dfu_request_dnload(setup_packet);
            break;

        case USB_RQST_DFU_UPLOAD:
            dfu_request_upload(setup_packet);
            break;

        case USB_RQST_DFU_GET_STATUS:
            dfu_request_getstatus(setup_packet);
            break;

        case USB_RQST_DFU_CLEAR_STATUS:
            dfu_request_clrstatus(setup_packet);
            break;

        case USB_RQST_DFU_GET_STATE:
            dfu_request_getstate(setup_packet);
            break;

        case USB_RQST_DFU_ABORT:
            dfu_request_abort(setup_packet);
            break;

#ifdef DEBUG_LVL // FIXME We should implement these features
        case USB_RQST_DFU_DEBUG_CHKSIGN:
        case USB_RQST_DFU_DEBUG_DECRYPT:
        case USB_RQST_DFU_DEBUG_CRYPT:

        case USB_RQST_DFU_DEBUG_GETADDR:
        case USB_RQST_DFU_DEBUG_SETSIZE:
        case USB_RQST_DFU_DEBUG_GETSIZE:
#endif
        default:
            dfu_error(ERRUNKNOWN);
    }
}



static void dfu_data_in_handler(void) {
    if (dfu_ctx->block_in_progress == 1){
        dfu_ctx->block_in_progress = 0;
    }
}

void dfu_early_init(void)
{
	usb_driver_early_init(dfu_data_out_handler, dfu_data_in_handler);
#if TIMER
    timer2_early_init();
#endif
}


void dfu_init(void)
{

    usb_driver_map();
#ifdef CONFIG_STD_MALLOC_LIGHT
    wmalloc_init();
	//wmalloc(DFU_DATA_QUEUE_MAX_SIZE*sizeof(uint8_t), ALLOC_NORMAL);
//	wmalloc_init(heap_base, DFU_DATA_QUEUE_MAX_SIZE);
#endif
    dfu_data_in_queue = queue_create(DFU_DATA_QUEUE_MAX_SIZE);
    dfu_data_out_queue = queue_create(DFU_DATA_QUEUE_MAX_SIZE);

	printf("Initializing DFU Layer\n");
    usb_ctrl_callbacks_t dfu_usb_ctrl_callbacks = { // FIXME Replace handler pointers
        .class_rqst_handler             = dfu_class_request_handler,
        .vendor_rqst_handler            = NULL,
        .set_configuration_rqst_handler = NULL,
        .set_interface_rqst_handler     = NULL,
        .functional_rqst_handler        = dfu_functional_desc_request_handler,
        .mft_string_rqst_handler        = NULL,
    };
    usb_ctrl_init(dfu_usb_ctrl_callbacks, dfu_device_desc, dfu_configuration_desc);
	usb_driver_init();
    dfu_init_context();
#ifdef TIMER
    timer2_init_master(TIMER25_PRESCALER_2_TCKINT,
                       TIMER25_AUTO_RELOAD_ENABLE,
                       TIMER25_CMS_MODE_1,
                       TIMER25_DIR_DOWN,
                       TIMER25_ONEPULSE,
                       TIMER25_URS_ONLY_ON_OVERFLOW,
                       TIMER25_UPDATE_ENABLE);

#endif
}


int j = 0;


void dfu_loop(void)
{

    /* FIXME MOCKUP */
    if (read_firmware_data_cmd == 1){
        printf("Reading flash data\n");
        if(j < 20){
            if (queue_available_space(dfu_data_out_queue) > 0){

                dfu_data_block_t *dfu_new_tmp_in_block = NULL;
#ifdef CONFIG_STD_MALLOC_LIGHT
    	        wmalloc((void**)& dfu_new_tmp_in_block, sizeof(dfu_data_block_t), ALLOC_NORMAL);
#else
	            wmalloc((void**)& dfu_new_tmp_in_block, sizeof(dfu_data_block_t), ALLOC_NORMAL);
#endif

#ifdef CONFIG_STD_MALLOC_LIGHT
	            wmalloc((void**)& dfu_new_tmp_in_block->data, MAX_TRANSFERT_SIZE, ALLOC_NORMAL);
#else
    	        dfu_new_tmp_in_block->data  = (uint8_t *)malloc(MAX_TRANSFERT_SIZE);
                printf("data: %x\n",dfu_new_tmp_in_block->data);
#endif
                memset((void *)dfu_new_tmp_in_block->data, j, MAX_TRANSFERT_SIZE);

                dfu_new_tmp_in_block->id = j;
                dfu_new_tmp_in_block->size=MAX_TRANSFERT_SIZE;
                printf("Enqueuing block id: %d, size:%d\n",j, dfu_new_tmp_in_block->size);
                queue_enqueue(dfu_data_in_queue, dfu_new_tmp_in_block);
                j+=1;

            }else{
                printf("dfu_data_in_queue is full, waiting for space ...\n");
            }
        }else{
            j = 0;
            read_firmware_data_cmd = 0;
            printf("Reading flash data: DONE\n");
        }
    }
    /* FIXME END */

	if(!queue_is_empty(dfu_data_out_queue)){
        // TODO check if bwPollTimeout is eslaped
        dfu_data_block_t *dfu_current_out_block = NULL;
        dfu_current_out_block = queue_dequeue(dfu_data_out_queue);

        // TODO check the DFU suffix
        // if( !dfu_validate_suffix(dfu_suffix_t * dfu_suffix)){dfu_error(ERRSTALLEDPKT);}

        // TODO check the Sec DFU suffix
        // if( ! dfu_validate_sec_suffix(dfu_sec_metadata_hdr_t * dfu_sec_metadata_hdr)){dfu_error(ERRSTALLEDPKT);}

        // TODO write data to the flash

        if( dfu_validate_memory_policy(dfu_ctx->flash_address, dfu_current_out_block->size) ) {
//            printf("dfu_validate_memory_policy => OK\n");
//            printf("Writing block: #%d, size %d\n", dfu_current_out_block->id, dfu_current_out_block->size);

            // write simulation...
//            printf("Writing block: OK\n");
            dfu_ctx->flash_address += dfu_current_out_block->size;
            dfu_release_block(dfu_current_out_block);
#if 0
            FLASH WRITE HERE...
            if( flash_write(dfu_ctx->flash_address, dfu_current_out_block->data, dfu_current_out_block->size) ) {
                printf("Writing block: OK\n");
                dfu_ctx->flash_address += dfu_current_out_block->size;
                dfu_release_block(dfu_current_out_block);
            } else {
                printf("Writing block: KO\n");
                dfu_release_block(dfu_current_out_block);
                dfu_error(ERRPROG);
            }
#endif
        } else {
            printf("dfu_validate_memory_policy => KO\n");
            dfu_release_block(dfu_current_out_block);
            dfu_error(ERRWRITE);
        }
    }

    if(!queue_is_empty(dfu_data_in_queue)){
        dfu_data_block_t *dfu_tmp_in_block = NULL;
        dfu_tmp_in_block = queue_dequeue(dfu_data_in_queue);
        printf("Sending block: #%d\n", dfu_tmp_in_block->id);
        usb_driver_setup_send(dfu_tmp_in_block->data, dfu_tmp_in_block->size,0);
        usb_driver_setup_read_status();
        dfu_release_block(dfu_tmp_in_block);
    }

    // FIXME
    if (read_firmware_data_done == 1){
        read_firmware_data_done = 0;
        // FIXME: no zlp API ??
        //usb_fs_driver_prepare_send_zlp(0);
        usb_driver_setup_read_status();
    }

}